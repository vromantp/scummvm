/* ScummVM - Graphic Adventure Engine
 *
 * ScummVM is the legal property of its developers, whose names
 * are too numerous to list here. Please refer to the COPYRIGHT
 * file distributed with this source distribution.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 */

#include "common/algorithm.h"
#include "common/debug.h"
#include "common/textconsole.h"

#include "xfiles/detection.h"
#include "xfiles/script.h"
#include "xfiles/vcdump.h"

namespace XFiles {

void Script::start() {
	VCTitle title;
	if (!_data.getTitle(title)) {
		warning("Script: no VCTitle");
		return;
	}
	log("== start: " + nameOrNone(_data, title.name));
	// Game_Start (0x48cd70) writes the preferences before the title activates
	shootoutReset(); // 0x48ccd0
	_gunValue = 0;
	loadPreferences();
	fire(title.id, kEventObjectActivation);
}

void Script::restore(const VCSave &save) {
	// A load is Game_Start (0x48cd70) like a new game: VCGameState is read on
	// the load screen, so the saved state is in memory before the title
	// activates; the title's Reg* calls then run and SetStartView takes the
	// save's position instead of its arguments (restoreView, below)
	// (docs/formats/saves.md, "How the game restores a save")
	_restored = true;
	_restorePending = true;
	_restoredSave = save;
	// The travel gets the save's disc (+42) the way SwapDisc's does
	if (save.disc >= 1)
		_disc = save.disc;
	// A save's history is a set (an older engine save may repeat a topic:
	// each repeat still counts)
	_history.clear();
	_asked.clear();
	_assetActive.clear();
	for (uint i = 0; i < save.history.size(); i++)
		recordTopic(save.history[i]);
	_pda.restore(save);
	_photos = save.photoList;
	_photosOnCamera = save.photos;
	_inventory = save.inventory;
	_evidence = save.evidence;
	_kit = save.kit;
	_hintList = save.hintList;
	_hintTrigger = save.hintTrigger;
	_hintCursor = 0;
	_hintGlow = save.hintGlow;
	_hintGlowOn = _hintGlow != 0; // AI_RestoreGlow (0x508c00)
	memcpy(_flashlight, save.flashlight, sizeof(_flashlight));
	_tint = save.tint;
	memcpy(_tintColor, save.tintColor, sizeof(_tintColor));
	log("== restore");
	log(Common::String::format("  disc %d", _disc));
	start();
	_restorePending = false;
}

void Script::restoreView() {
	// SetStartView (0x486a10) takes the save's position and Game_BuildObjects
	// (0x477940) rebuilds the objects from VCGameState, in the order below.
	// The position mode decides which Object Activation fires while the list
	// is activated.
	const VCSave &save = _restoredSave;
	_restorePending = false;
	_restoreMode = save.positionMode;
	enter(save.position[0], save.position[1], save.position[2], save.position[3]);
	if (save.clip)
		log(Common::String::format("  clip #%u at time %d in the view (the view plays from there)", save.clip, save.clipTime));

	// 1. The full-screen layout (Layout_SetAsView)
	if (save.layout)
		showLayout(save.layout);

	// 2. Sounds playing are kind 27 sound objects: no Object Activation. The
	// 2nd value is the object's loop flag (+0x30, set by a Play loop asset
	// action); the time (3rd value, ms in the file) is the sound's in point:
	// it starts there and a loop goes back to it (0x4a88d0, 0x48825f)
	for (uint i = 0; i + 3 <= save.sounds.size(); i += 3) {
		uint32 id = save.sounds[i];
		VCAsset a;
		if (!_data.getAsset(id, a))
			continue;
		_sounds[id] = true;
		_assetActive[id]++; // No Object Activation (kind 27), but its end deactivates the asset object
		log(Common::String::format("  restored sound #%u at %u%s", id, save.sounds[i + 2], save.sounds[i + 1] ? ", looped" : ""));
		if (_hooks)
			_hooks->restoreSound(id, save.sounds[i + 1] != 0, save.sounds[i + 2]);
	}

	// 3. Every timer is a Timer action Start (0x4a1250) with the saved number,
	// time and periodic bit: a one-shot timer restarts with its remaining time
	for (uint i = 0; i + 3 <= save.timers.size(); i += 3) {
		Timer timer = { save.timers[i + 1], ++_timerSerial, save.timers[i + 2] != 0 };
		_timers[save.timers[i]] = timer;
		log(Common::String::format("      timer %u restored (%u ms%s)", save.timers[i], timer.ms, timer.periodic ? ", periodic" : ""));
	}

	// 7. Evidence: EvIcons_SetRowShown(0), then each icon outside the kit; a
	// shown one is shown and sets the row shown. Kit icons are not shown.
	_evidenceRowShown = false;
	for (uint i = 0; i + 2 <= save.evidence.size(); i += 2) {
		if (save.evidence[i + 1]) {
			_evidenceRowShown = true;
			log(Common::String::format("  evidence icon #%u shown", save.evidence[i]));
		}
	}

	// 8. Not modelled by the engine: kept in the restored save and logged
	if (save.hintList || save.hintTrigger || save.hintGlow)
		log(Common::String::format("  hint list #%u trigger #%u glow %d (not applied)", save.hintList, save.hintTrigger, save.hintGlow));
	if (save.credits != -1)
		log(Common::String::format("  credits at %d", save.credits));
	log(Common::String::format("  flashlight %u %u %u, letterbox tint %u (%u %u %u) (not applied)", save.flashlight[0], save.flashlight[1],
		save.flashlight[2], save.tint, save.tintColor[0], save.tintColor[1], save.tintColor[2]));
	if (save.dialogBox)
		log(Common::String::format("  dialog box of #%u", save.dialogBox));
	_restoreMode = -1;
}

void Script::addEvidence(uint32 id) {
	for (uint i = 0; i + 2 <= _evidence.size(); i += 2) {
		if (_evidence[i] == id)
			return;
	}
	_evidence.push_back(id);
	_evidence.push_back(0);
}

void Script::removeEvidence(uint32 id) {
	for (uint i = 0; i + 2 <= _evidence.size(); i += 2) {
		if (_evidence[i] == id) {
			_evidence.remove_at(i + 1);
			_evidence.remove_at(i);
			return;
		}
	}
}

void Script::snapshot(VCSave &save) const {
	// GameState_Save (0x4d9450). The position is always the current view
	// (mode 1); modes 0 and 2 only come from the original's saves.
	save.position[0] = _node;
	save.position[1] = _location;
	save.position[2] = _viewPoint;
	save.position[3] = _view;
	save.positionMode = 1;
	save.history = _history;
	_pda.snapshot(save);
	save.disc = _disc;
	save.photos = _photosOnCamera;
	save.photoList = _photos;
	save.hintList = _hintList;
	save.hintTrigger = _hintTrigger;
	save.hintGlow = _hintGlow;
	memcpy(save.flashlight, _flashlight, sizeof(_flashlight));
	save.tint = _tint;
	memcpy(save.tintColor, _tintColor, sizeof(_tintColor));
	save.inventory = _inventory;
	// The group 0 icons are shown together (DispEvIcons)
	save.evidence = _evidence;
	for (uint i = 0; i + 2 <= save.evidence.size(); i += 2)
		save.evidence[i + 1] = _evidenceRowShown;
	save.kit = _kit;
	save.dialogBox = _box;
	// Only five full-screen layouts are kept, the last one shown
	static const uint32 kKeptLayouts[] = { 57082, 84328, 84370, 72504, 58553 };
	save.layout = 0;
	for (uint i = 0; i < _layouts.size(); i++) {
		for (uint j = 0; j < ARRAYSIZE(kKeptLayouts); j++) {
			if (_layouts[i].id == kKeptLayouts[j])
				save.layout = kKeptLayouts[j];
		}
	}
}

void Script::getSaved(SavedValues &values) const {
	values = SavedValues();
	values.nextId = _nextId;
	Common::Array<NeoEntry> entries;
	_data.getGam().getObjects(kClassVariable, entries);
	for (uint i = 0; i < entries.size(); i++) {
		values.variables.push_back(entries[i].key);
		values.variables.push_back((uint32)getVariable(entries[i].key));
	}
	entries.clear();
	_data.getGam().getObjects(kClassEnabled, entries);
	for (uint i = 0; i < entries.size(); i++) {
		uint32 target;
		bool enabled;
		if (_data.getEnabled(entries[i].key, target, enabled)) {
			values.enabled.push_back(entries[i].key);
			values.enabled.push_back(isEnabled(target));
		}
	}
	// In the order of the IDs, so the same game gives the same bytes
	Common::Array<uint32> ids;
	for (Common::HashMap<uint32, Common::String>::const_iterator it = _strings.begin(); it != _strings.end(); ++it)
		ids.push_back(it->_key);
	Common::sort(ids.begin(), ids.end());
	for (uint i = 0; i < ids.size(); i++) {
		values.stringIds.push_back(ids[i]);
		values.strings.push_back(_strings.getVal(ids[i]));
	}
}

void Script::applySaved(const SavedValues &values) {
	for (uint i = 0; i + 2 <= values.variables.size(); i += 2) {
		VCVariable v;
		if (_data.getVariable(values.variables[i], v))
			_variables[v.id] = (int32)values.variables[i + 1];
	}
	for (uint i = 0; i + 2 <= values.enabled.size(); i += 2) {
		uint32 target;
		bool enabled;
		if (_data.getEnabled(values.enabled[i], target, enabled))
			_enabled[target] = values.enabled[i + 1] != 0;
	}
	for (uint i = 0; i < values.stringIds.size() && i < values.strings.size(); i++)
		_strings[values.stringIds[i]] = values.strings[i];
	if (values.nextId)
		_nextId = values.nextId;
}

} // End of namespace XFiles
