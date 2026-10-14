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

enum ConditionOp {
	kCondEqual = 0,
	kCondNotEqual = 1,
	kCondGreater = 2,
	kCondLess = 3,
	kCondGreaterEqual = 4,
	kCondLessEqual = 5,
	kCondAnd = 6,
	kCondOr = 7
};

enum StatementOp {
	kStatAssign = 0,
	kStatIncrement = 1,
	kStatDecrement = 2,
	kStatAdd = 3,
	kStatSubtract = 4,
	kStatMultiply = 5,
	kStatDivide = 6,
	kStatModulo = 7
};

static const char *const kModeNames[] = { "preload", "play_once", "play_loop", "stop", "unload" };

/** PDAEscape's action lists (XFiles.exe 0x47d090): escape on (argument 1) or off. */
static const uint32 kPDAEscapeOnActions = 75533;
static const uint32 kPDAEscapeOffActions = 75530;

/** Offset of the VCTriggerList field in each class that owns triggers (VCAssetRef: see VCAsset::triggers). */
static uint32 getTriggerOffset(uint32 classId) {
	switch (classId) {
	case kClassTitle:
		return 64;
	case kClassNode:
		return 36;
	case kClassLocation:
	case kClassViewPoint:
		return 40;
	case kClassView:
	case kClassActionIcon:
	case kClassEmotionIcon:
	case kClassEvidenceIcon:
	case kClassIdeaIcon:
	case kClassInventory:
		return 26;
	case kClassNav:
		return 14;
	case kClassCharView:
		return 34;
	case kClassConversation:
	case kClassExplorable:
		return 18;
	case kClassInterfaceItem:
		return 22;
	default:
		return 0;
	}
}

static Common::String formatRect(const VCRect &r) {
	return Common::String::format("(%d, %d, %d, %d)", r.left, r.top, r.right, r.bottom);
}

/** First media file of an asset as the log shows it: "XV:19812.xmv", with " @7s" if it starts later. */
static Common::String formatFile(const VCMediaFile &f) {
	Common::String text = f.file;
	if (f.start)
		text += Common::String::format(" @%gs", f.start / 1000.0);
	return text;
}

Script::Script(const VCData &data) : _data(data), _hooks(nullptr), _texts(nullptr), _timerSerial(0), _random(1), _fixedSeeds(false), _seedBase(0), _box(0), _restoreMode(-1), _disc(1), _evidenceRowShown(false), _restored(false), _focus(0), _held(0), _currNodeVariable(0), _heldVariable(0), _heldNone(0), _heldCursorItem(0), _clipQueued(false), _fireDepth(0), _travelPending(false), _viewHeld(false), _viewActive(false), _inboxTop(0), _pdaFontSize(12), _pdaLayout(false), _scrollItem(0), _addressIndex(0), _noteDay(0), _noteDays(0), _node(0), _location(0), _viewPoint(0), _view(0) {
	_jumpCut.first = _jumpCut.later = false;
	_phone.bookSize = _phone.bookIndex = 0;
	_phone.showsName = false;
	_phone.tone = 0;
	Common::Array<NeoEntry> entries;
	data.getGam().getObjects(kClassVariable, entries);
	for (uint i = 0; i < entries.size(); i++) {
		VCVariable v;
		if (data.getVariable(entries[i].key, v))
			_variables[v.id] = (int32)v.value;
	}
	entries.clear();
	data.getGam().getObjects(kClassEnabled, entries);
	for (uint i = 0; i < entries.size(); i++) {
		uint32 target;
		bool enabled;
		if (data.getEnabled(entries[i].key, target, enabled))
			_enabled[target] = enabled;
	}
}

void Script::log(const Common::String &line) {
	debugC(1, kDebugScript, "%s", line.c_str());
}

bool Script::isEnabled(uint32 id) const {
	return !_enabled.contains(id) || _enabled[id];
}

int32 Script::getVariable(uint32 id) const {
	return _variables.contains(id) ? _variables[id] : 0;
}

Common::String Script::getString(uint32 variableId) const {
	uint32 stringId = getVariable(variableId);
	Common::String text;
	if (_strings.contains(stringId))
		return _strings[stringId];
	_data.getString(stringId, text);
	return text;
}

void Script::setString(uint32 variableId, const Common::String &value) {
	uint32 stringId = getVariable(variableId);
	if (stringId)
		_strings[stringId] = value;
}

const Script::ItemText *Script::getItemText(uint32 itemId) const {
	return _itemTexts.contains(itemId) ? &_itemTexts[itemId] : nullptr;
}

void Script::keyPress(uint vk) {
	// The focused item's Key Press triggers; the event's field 0 is the key
	if (!_focus)
		return;
	EventFields fields;
	fields.push_back(vk);
	fire(_focus, kEventKeyPress, fields);
}

void Script::takePressedItems(Common::Array<uint32> &items) {
	items.push_back(_pressed);
	_pressed.clear();
}

void Script::releaseItem(uint32 itemId) {
	// The Enable action PhoneButton queues (0x4c8870, 0x4083d0)
	log(Common::String::format("  (button #%u up)", itemId));
	_enabled[itemId] = false;
	updateLayoutItem(itemId, false);
}

// -- Triggers and actions -----------------------------------------------------

uint32 Script::getTriggers(uint32 id) const {
	uint32 classId, mark;
	if (!_data.lookup(id, classId, mark))
		return 0;
	if (classId == kClassAssetRef) {
		VCAsset asset;
		return _data.getAsset(id, asset) ? asset.triggers : 0;
	}
	uint32 offset = getTriggerOffset(classId);
	return offset ? _data.getHdb().readUint32(mark + offset) & 0xffffff : 0;
}

bool Script::firesOnRestore(uint32 id) const {
	// Obj_OnActivate while a restore runs (docs/formats/saves.md): mode 1
	// fires none, mode 2 only objects of kind 3 (viewpoint), 4 (view), 5 (nav),
	// 6 (character view) and 20 (explorable), other modes all
	if (_restoreMode == 1)
		return false;
	if (_restoreMode != 2)
		return true;
	uint32 classId = _data.getClassOf(id);
	return classId == kClassViewPoint || classId == kClassView || classId == kClassNav || classId == kClassCharView ||
		classId == kClassExplorable;
}

void Script::fire(uint32 id, int event, const EventFields &fields) {
	if (_restoreMode >= 0 && event == kEventObjectActivation && !firesOnRestore(id))
		return;
	_fireDepth++;
	fireTriggers(id, event, fields);
	_fireDepth--;
	// XFiles.exe carries out a pending travel (0x493090) once the event queue
	// is empty: after the action list and the triggers it set off
	while (_fireDepth == 0 && _travelPending)
		travel();
}

void Script::fireTriggers(uint32 id, int event, const EventFields &fields) {
	Common::Array<uint32> triggers;
	_data.getMembers(getTriggers(id), triggers);
	for (uint i = 0; i < triggers.size(); i++) {
		VCTrigger t;
		if (_data.getTrigger(triggers[i], t) && t.event == event)
			runActions(t.actions, event, fields);
	}
}

bool Script::isTestedOnClick(uint32 id, uint32 variable) const {
	// 0x4d2ee0 looks at the conditional actions of each Mouse Click action
	// list for one with the variable as an operand
	Common::Array<uint32> triggers;
	_data.getMembers(getTriggers(id), triggers);
	for (uint i = 0; i < triggers.size(); i++) {
		VCTrigger t;
		if (!_data.getTrigger(triggers[i], t) || t.event != kEventMouseClick)
			continue;
		for (uint j = 0; j < t.actions.size(); j++) {
			VCAction a;
			if (!_data.getAction(t.actions[j], a) || !a.conditional)
				continue;
			const VCExpression &c = a.condition;
			bool tested = (c.lhs.kind == kOperandVariable && c.lhs.value == variable) ||
				(c.rhs.kind == kOperandVariable && c.rhs.value == variable);
			if (tested && test(c, EventFields(), LocalVariables()))
				return true;
		}
	}
	return false;
}

void Script::runActions(const Common::Array<uint32> &actions, int event, const EventFields &fields) {
	LocalVariables locals; // Local variables live for one action list
	for (uint i = 0; i < actions.size(); i++) {
		VCAction a;
		if (!_data.getAction(actions[i], a)) {
			warning("Script: bad action #%u", actions[i]);
			continue;
		}
		if (a.conditional && !test(a.condition, fields, locals))
			continue;
		execute(a, event, fields, locals);
	}
}

int32 Script::getValue(const VCOperand &operand, const EventFields &fields, const LocalVariables &locals) const {
	switch (operand.kind) {
	case kOperandVariable:
		return getVariable(operand.value);
	case kOperandLocal:
		return locals.contains(operand.value) ? locals[operand.value] : 0;
	case kOperandEvent:
		return operand.value < fields.size() ? fields[operand.value] : 0;
	default:
		return (int32)operand.value;
	}
}

bool Script::test(const VCExpression &c, const EventFields &fields, const LocalVariables &locals) const {
	int32 a = getValue(c.lhs, fields, locals);
	int32 b = getValue(c.rhs, fields, locals);
	switch (c.op) {
	case kCondEqual:
		return a == b;
	case kCondNotEqual:
		return a != b;
	case kCondGreater:
		return a > b;
	case kCondLess:
		return a < b;
	case kCondGreaterEqual:
		return a >= b;
	case kCondLessEqual:
		return a <= b;
	case kCondAnd:
		return a && b;
	case kCondOr:
		return a || b;
	default:
		warning("Script: unknown condition operator %u", c.op);
		return false;
	}
}

void Script::execute(const VCAction &a, int event, const EventFields &fields, LocalVariables &locals) {
	switch (a.type) {
	case kActionStatement: {
		const VCExpression &s = a.statement;
		int32 rhs = getValue(s.rhs, fields, locals);
		bool local = s.lhs.kind == kOperandLocal;
		int32 old = local ? (locals.contains(s.lhs.value) ? locals[s.lhs.value] : 0) : getVariable(s.lhs.value);
		int32 value;
		switch (s.op) {
		case kStatAssign:
			value = rhs;
			break;
		case kStatIncrement:
			value = old + 1;
			break;
		case kStatDecrement:
			value = old - 1;
			break;
		case kStatAdd:
			value = old + rhs;
			break;
		case kStatSubtract:
			value = old - rhs;
			break;
		case kStatMultiply:
			value = old * rhs;
			break;
		case kStatDivide:
			value = rhs ? old / rhs : old;
			break;
		case kStatModulo:
			value = rhs ? old % rhs : old;
			break;
		default:
			warning("Script: unknown statement operator %u", s.op);
			return;
		}
		if (local)
			locals[s.lhs.value] = value;
		else
			_variables[s.lhs.value] = value;
		break;
	}
	case kActionEnable: {
		uint32 target;
		bool enabled;
		if (_data.getEnabled(a.target, target, enabled) && target) {
			bool was = isEnabled(target);
			_enabled[target] = a.value != 0;
			if (!a.value)
				_spent.erase(target);
			// The held item disabled goes back (0x436c20)
			if (!a.value && _heldCursorItem && _heldCursorItem == target)
				putBack();
			// An evidence icon gets its object when it is enabled (0x4201a0),
			// firing its Object Activation; disabling removes its kit icon too
			// (0x4a1940)
			if (_data.getClassOf(target) == kClassEvidenceIcon) {
				Common::Array<uint32>::iterator it = Common::find(_kit.begin(), _kit.end(), target);
				if (a.value && !was)
					fire(target, kEventObjectActivation);
				else if (!a.value && it != _kit.end())
					_kit.erase(it);
			}
			// A topic enabled while its box is open joins the box (0x476340)
			if (a.value && _box && isBoxTopic(_box, target))
				activateTopic(target);
			// A character view, nav or explorable of the view comes and goes
			// with its flag (0x492170, 0x46de10, 0x46efc0; 0x47ff40)
			if (a.value && !was)
				activateViewObject(target);
			else if (!a.value && was)
				deactivateViewObject(target);
			updateLayoutItem(target, a.value != 0);
		}
		break;
	}
	case kActionAsset:
	case kActionSound3D: {
		// XFiles.exe Action_DoAsset 0x47a4e0: play once and play loop play
		// only the list's first enabled asset (0x41fe60); a stop stops every
		// asset of the list, enabled or not
		const VCAction *sound3D = a.type == kActionSound3D ? &a : nullptr;
		if (a.mode == kModeStop) {
			Common::Array<uint32> assets;
			_data.getMembers(a.assetList, assets);
			for (uint i = 0; i < assets.size(); i++)
				playAsset(assets[i], a.mode, sound3D);
		} else if (uint32 asset = firstEnabled(a.assetList)) {
			playAsset(asset, a.mode, sound3D);
		}
		break;
	}
	case kActionTimer:
		if (a.command == 0) {
			Timer &timer = _timers[a.timer];
			timer.ms = a.ms;
			timer.serial = ++_timerSerial;
			timer.periodic = a.periodic;
			log(Common::String::format("      timer %u started (%u ms%s)", a.timer, a.ms, a.periodic ? ", periodic" : ""));
		} else {
			_timers.erase(a.timer);
		}
		break;
	case kActionSetView:
		enter(a.path[1], a.path[2], a.path[3], a.path[0]);
		break;
	case kActionInterface:
		log(Common::String(a.mode ? "      hide" : "      show") + " interface " + getObjectLabel(_data, a.layout));
		if (a.mode)
			hideLayout(a.layout);
		else
			showLayout(a.layout);
		break;
	case kActionFunction:
		callFunction(a, event, fields, locals);
		break;
	default:
		warning("Script: unknown action type %u in #%u", a.type, a.id);
		break;
	}
}

void Script::callFunction(const VCAction &a, int event, const EventFields &fields, const LocalVariables &locals) {
	Common::Array<int32> args;
	for (uint i = 0; i < a.args.size(); i++)
		args.push_back(getValue(a.args[i], fields, locals));

	if (a.function == "SetStartView" && args.size() >= 4) {
		enter(args[0], args[1], args[2], args[3]);
		// The exe also builds the game objects there (0x486a10 calls
		// Game_BuildObjects 0x477940)
		if (_hooks)
			_hooks->callFunction(a, args);
	} else if ((a.function == "DoActionList" || a.function == "DoActionListID") && args.size() >= 2) {
		// DoActionListID takes the two IDs from variables (0x50a200)
		if (a.function == "DoActionListID")
			log(Common::String::format("      call %s(%d, %d)", a.function.c_str(), args[0], args[1]));
		runActionList(args[1], event, fields);
	} else {
		// The values of its variable arguments follow, as a reference run
		// logs them
		Common::String values;
		for (uint i = 0; i < a.args.size(); i++) {
			if (a.args[i].kind == kOperandVariable)
				values += Common::String::format("%s%d", values.empty() ? "" : ", ", args[i]);
		}
		log("      call " + describeAction(_data, a.id, event, false) + (values.empty() ? "" : " [" + values + "]"));
		if (a.function == "SwapDisc" && args.size() >= 4) {
			// The last call in an event wins (0x4cfc70 replaces the pending travel)
			for (int i = 0; i < 4; i++)
				_travelTo[i] = args[i];
			_travelPending = true;
		} else if (a.function == "EnableKitIcon" && !args.empty())
			enableKitIcon(args[0]);
		else if (a.function == "RandGen" && args.size() >= 2 && a.args[0].kind == kOperandVariable)
			randGen(a.args[0].value, args[1]);
		else if (a.function == "IsIItemHeld" && args.size() >= 2 && a.args[1].kind == kOperandVariable)
			isIItemHeld(a.args[1].value, args[0]);
		else if (a.function == "RegCurrNode" && !a.args.empty() && a.args[0].kind == kOperandVariable)
			_currNodeVariable = a.args[0].value; // 0x4926a0
		else if (a.function == "RegCurrInvVar" && args.size() >= 2 && a.args[0].kind == kOperandVariable) {
			// 0x458e60: the variable starts as "nothing in hand"
			_heldVariable = a.args[0].value;
			_heldNone = args[1];
			_variables[_heldVariable] = _heldNone;
		} else if (a.function == "OnInvClick" && !args.empty()) {
			// Inv_Select 0x49b410: the item's value goes into the variable
			if (_heldVariable)
				_variables[_heldVariable] = args[0];
		} else if (a.function == "SelectInv" && !args.empty()) {
			// 0x50a390: if the item has an icon, put back what is in hand and
			// take the item, as a click on its icon would
			if (hasInventoryIcon(args[0]))
				selectInventory(args[0]);
		} else if (a.function == "DeselectInv" && !args.empty()) {
			// 0x50a570: 0 puts back any item, else only that one
			if (args[0] == 0 || _heldCursorItem == (uint32)args[0])
				putBack();
		} else if (a.function == "OnIntInvClick") {
			// 0x467fb0: the PDA and phone icons never set iCurrInventory;
			// this only clears the held cursor (Inv_SetHeldCursor(0))
			_heldCursorItem = 0;
			if (_hooks)
				_hooks->heldItemChanged(0);
		} else if (a.function == "StopEnvSounds")
			stopEnvSounds();
		else if (a.function == "RegPreferences")
			regPreferences(a);
		else if (a.function == "SetIItemFocus" && !args.empty())
			_focus = args[0];
		else if (a.function == "RegViewState" && !a.args.empty() && a.args[0].kind == kOperandVariable)
			_variables[a.args[0].value] = 0; // 0x47e9d0 clears the variable it registers
		else if (a.function == "PDAEscape" && !args.empty())
			runActionList(args[0] == 1 ? kPDAEscapeOnActions : kPDAEscapeOffActions, event, fields);
		else if (a.function.hasPrefix("Phone"))
			callPhone(a, args, event, fields);
		else if (a.function.contains("Email") || a.function == "DisplayInBox" || a.function == "InitEmailRead" ||
				a.function == "MainScroll" || a.function == "ScrollTextKill" || a.function.hasPrefix("PDAInitAddr") ||
				a.function == "PDAFlipAddr" || a.function == "NotePadInit" || a.function == "NoteFlipDay" ||
				a.function == "EnablePDANote" || a.function == "EnableAllNotes" || a.function == "ScrollTextList" ||
				a.function == "ScrollTextInit")
			callEmail(a, args, event, fields);
		if (_hooks)
			_hooks->callFunction(a, args);
	}
}

bool Script::isHolding() const {
	return _heldVariable && getVariable(_heldVariable) != _heldNone;
}

void Script::clearHeldItem() {
	if (_heldVariable)
		_variables[_heldVariable] = _heldNone;
}

void Script::putBack() {
	// The held-item object exists only while something is in hand; its
	// deactivation (0x437c20) resets the variable
	if (!isHolding())
		return;
	debugC(1, kDebugScript, "-- #%u put back", _heldCursorItem);
	clearHeldItem();
	_heldCursorItem = 0; // Inv_SetHeldCursor(0)
	if (_hooks)
		_hooks->heldItemChanged(0);
}

bool Script::hasInventoryIcon(uint32 id) const {
	bool has;
	if (_hooks && _hooks->queryInventoryIcon(id, has))
		return has;
	return _data.getClassOf(id) == kClassInventory && isEnabled(id);
}

void Script::selectInventory(uint32 id) {
	if (_heldCursorItem == id)
		return;
	putBack();
	_heldCursorItem = id; // Inv_SetHeldCursor, before the Mouse Click triggers
	if (_hooks)
		_hooks->heldItemChanged(id);
	debugC(1, kDebugScript, "-- click:%u (inventory)", id);
	click(id);
}

void Script::runActionList(uint32 actionList, int event, const EventFields &fields) {
	Common::Array<uint32> actions;
	_data.getMembers(actionList, actions);
	runActions(actions, event, fields);
}

void Script::randGen(uint32 variableId, int32 n) {
	// 0x49f6d0: a generator from 0 to n, rolled once
	Dice dice = newDice(0x49f6f9);
	int32 value = roll(dice, 0, n);
	_variables[variableId] = value;
	Common::U32String name;
	_data.getVariableName(variableId, name);
	log(Common::String::format("      %s = %d", name.encode().c_str(), value));
}

void Script::enableKitIcon(uint32 id) {
	// XFiles.exe 0x4effd0: enable the evidence icon and give it an object in
	// the kit, whose activation fires its Object Activation
	if (_data.getClassOf(id) != kClassEvidenceIcon)
		return;
	_enabled[id] = true;
	if (Common::find(_kit.begin(), _kit.end(), id) == _kit.end())
		_kit.push_back(id);
	fire(id, kEventObjectActivation);
}

// -- Media --------------------------------------------------------------------

void Script::playAsset(uint32 id, byte mode, const VCAction *sound3D) {
	VCAsset a;
	if (!_data.getAsset(id, a)) {
		warning("Script: #%u is not an asset", id);
		return;
	}
	bool video = false;
	for (uint i = 0; i < a.files.size(); i++)
		video |= a.files[i].file.hasPrefix("XV:") || a.files[i].file.hasPrefix("XN:");
	Common::String file = a.files.empty() ? Common::String() : formatFile(a.files[0]);

	// XFiles.exe makes a sound object for every asset it plays (Action_DoAsset
	// 0x47a4e0) and treats one whose asset is an .xmv or .nmv (types 0x17 and
	// 0x16, 0x444d00) the same whatever its mode or range: its movie starts in
	// 0x488240, which raises the hold on views and on other sound objects'
	// play events (0x44f860), so these clips play one after another, and its
	// end or stop deactivates the sound and its asset object (0x43a970).
	// Only the .xmv clips (XV, XN) are queued here: the ranged .nmv clips
	// (frames of a view's navigation movie) are not played as videos yet
	if (video && (mode == kModePlayOnce || mode == kModePlayLoop)) {
		queueVideo(id, 0, mode == kModePlayLoop);
	} else if (mode == kModePlayOnce || mode == kModePlayLoop) {
		Common::String where = sound3D ? Common::String::format(" at (%d, %d)", sound3D->x, sound3D->y) : Common::String();
		log(Common::String::format("      %s #%u ", kModeNames[mode], id) + file + where + "  \"" + nameOrNone(_data, a.name) + "\"");
		// A sound (.amv, .mus, .wav) ends on its own; its end or a stop
		// deactivates its asset object too (0x43a970): see endSound()
		if (!a.files.empty() && a.files[0].file.hasPrefix("XS:"))
			_sounds[id] = true;
		fire(id, kEventObjectActivation);
		if (_hooks)
			_hooks->playAsset(id, mode, sound3D != nullptr, sound3D ? sound3D->x : 0, sound3D ? sound3D->y : 0);
	} else if (mode == kModeStop) {
		log(Common::String::format("      stop #%u ", id) + file);
		if (_hooks)
			_hooks->playAsset(id, mode, false, 0, 0);
		endSound(id, "stopped");
		// A video stopped while it plays ends; one still waiting never starts
		for (uint i = _videoQueue.size(); i-- > 0; ) {
			if (_videoQueue[i] != id) {
				continue;
			} else if (i == 0) {
				endVideo("stopped");
			} else {
				_videoQueue.remove_at(i);
				_videoPlays.remove_at(i);
			}
		}
	}
}

void Script::endSound(uint32 id, const char *how) {
	if (!_sounds.contains(id))
		return;
	_sounds.erase(id);
	log(Common::String::format("  << sound #%u %s", id, how));
	fire(id, kEventObjectDeactivation);
}

void Script::stopEnvSounds() {
	// 0x4cc9c0 queues every playing asset without a picture for deactivation:
	// its Object Deactivation fires as at a stop (0x43a970). _sounds holds
	// the audio-only assets (XS); clips with a picture keep playing. The
	// players stop first, so a sound a trigger starts is not cut
	if (_hooks)
		_hooks->stopAllSounds();
	Common::Array<uint32> ids;
	for (Common::HashMap<uint32, bool>::const_iterator it = _sounds.begin(); it != _sounds.end(); ++it)
		ids.push_back(it->_key);
	Common::sort(ids.begin(), ids.end());
	for (uint i = 0; i < ids.size(); i++)
		endSound(ids[i], "stopped");
}

void Script::regPreferences(const VCAction &a) {
	// 0x4ccda0 (mode 5) writes the registry's values into the 24 preference
	// variables. The registry is the player's settings or setPreference();
	// a preference neither knows keeps its .gam value, which is what the exe
	// stores on its first start (no registry key: 0x4ccda0 mode 0)
	for (uint index = 0; index < a.args.size() && index < kPrefCount; index++) {
		if (a.args[index].kind != kOperandVariable)
			continue;
		int32 value = 0;
		if (_hooks && _hooks->queryPreference(index, value))
			_variables[a.args[index].value] = value;
		else if (_preferences.contains(index))
			_variables[a.args[index].value] = _preferences[index];
	}
}

void Script::loadPreferences() {
	// 0x4310f0: walk the title's triggers and run the first C++ Function
	// action named RegPreferences, whatever its event or condition. The
	// reference log shows it as "cpp from=0043137e"; compare.py leaves it out
	VCTitle title;
	if (!_data.getTitle(title))
		return;
	Common::Array<uint32> triggers;
	_data.getMembers(title.triggers, triggers);
	for (uint i = 0; i < triggers.size(); i++) {
		VCTrigger t;
		if (!_data.getTrigger(triggers[i], t))
			continue;
		for (uint j = 0; j < t.actions.size(); j++) {
			VCAction a;
			if (!_data.getAction(t.actions[j], a) || a.type != kActionFunction || a.function != "RegPreferences")
				continue;
			regPreferences(a);
			if (_hooks) {
				Common::Array<int32> args;
				for (uint k = 0; k < a.args.size(); k++)
					args.push_back(getValue(a.args[k], EventFields(), LocalVariables()));
				_hooks->callFunction(a, args);
			}
			return;
		}
	}
}

void Script::queueVideo(uint32 id, uint32 limit, bool loop) {
	VideoPlay play = { limit, loop };
	_videoQueue.push_back(id);
	_videoPlays.push_back(play);
	if (_videoQueue.size() == 1)
		startVideo();
}

void Script::startVideo() {
	uint32 id = _videoQueue[0];
	VCAsset a;
	_data.getAsset(id, a);
	Common::String limit = _videoPlays[0].limit ? Common::String::format(" for %u ms", _videoPlays[0].limit) : Common::String();
	if (_videoPlays[0].loop)
		limit += " looped";
	log(Common::String::format("  >> video #%u ", id) + a.files[0].file + limit + "  \"" + nameOrNone(_data, a.name) + "\"");
	fire(id, kEventObjectActivation);
}

void Script::endVideo(const char *how) {
	if (_videoQueue.empty()) {
		log("  (nothing playing)");
		return;
	}
	uint32 id = _videoQueue.remove_at(0);
	_videoPlays.remove_at(0);
	log(Common::String::format("  << video #%u %s", id, how));
	bool next = !_videoQueue.empty();
	// A held view comes first, before the video's Object Deactivation (its
	// asset object is deactivated after the view's activation); a video the
	// view queues starts at once if the queue was empty, otherwise after
	// the one already queued
	if (_viewHeld)
		activateView();
	fire(id, kEventObjectDeactivation);
	if (next)
		startVideo();
	else if (_videoQueue.empty())
		showView(); // Back to the view, with whatever the video's triggers changed
}

// -- Navigation ---------------------------------------------------------------

void Script::travel() {
	// 0x5068b0, 0x493090: all four old objects are deactivated, whatever the
	// new path shares, then the new four are activated. The exe also hides the
	// inventory, AI, emotion, idea and action icons and clears the letterbox
	// tint; the engine does not model those yet
	_travelPending = false;
	// A SwapDisc in the triggers of the travel waits for it to finish
	_fireDepth++;
	enter(_travelTo[0], _travelTo[1], _travelTo[2], _travelTo[3], true);
	_fireDepth--;
}

void Script::enter(uint32 node, uint32 location, uint32 viewPoint, uint32 view, bool deactivateAll) {
	const uint32 newPath[4] = { node, location, viewPoint, view };
	const uint32 oldPath[4] = { _node, _location, _viewPoint, _view };
	int first = 0;
	while (!deactivateAll && first < 4 && oldPath[first] == newPath[first])
		first++;
	for (int i = 3; i >= first; i--) {
		// A view still held (below) was never activated: entering another
		// one drops it, as Action_DoSetView (0x46a950) cancels its activation
		if (oldPath[i] && !(i == 3 && _viewHeld))
			fire(oldPath[i], kEventObjectDeactivation);
		if (i == 3)
			deactivateViewObjects();
	}
	if (first < 4) {
		_viewHeld = false;
		_viewActive = false;
	}
	_node = node;
	_location = location;
	_viewPoint = viewPoint;
	_view = view;
	_box = 0;
	log("  == enter " + getObjectLabel(_data, node) + " / " + getObjectLabel(_data, location) + " / " +
		getObjectLabel(_data, viewPoint) + " / " + getObjectLabel(_data, view));
	for (int i = first; i < 3; i++)
		fire(newPath[i], kEventObjectActivation);
	if (first < 4) {
		// While a video plays, XFiles.exe's event loop holds the activation
		// of views (0x433000, the table 0x6bbff8 that 0x44f860 raises when a
		// video's movie starts and lowers when it ends): the view, its
		// triggers and its objects come after the video
		if (_videoQueue.empty()) {
			activateView();
		} else {
			_viewHeld = true;
			log("      view held until the video ends");
		}
	}
	showView();
}

void Script::activateView() {
	if (_viewHeld) {
		_viewHeld = false;
		log("  == view activated " + getObjectLabel(_data, _view));
	}
	_viewActive = true;
	fire(_view, kEventObjectActivation);
	activateViewObjects();
}

void Script::activateViewObjects() {
	// XFiles.exe activates, after the view, the asset it shows and its
	// enabled character views, then (after the transition) its enabled navs
	// and explorables (reference run, 2026-10-12); the view's activation
	// creates them (0x4402e0)
	VCView v;
	if (!_data.getView(_view, v))
		return;
	Common::Array<uint32> ids;
	uint32 background;
	getViewAssets(ids, background);
	if (!ids.empty()) {
		_viewObjects.push_back(ids[0]);
		fire(ids[0], kEventObjectActivation);
	}
	const uint32 lists[3] = { v.characters, v.navs, v.explorables };
	for (int i = 0; i < 3; i++) {
		ids.clear();
		_data.getMembers(lists[i], ids);
		for (uint j = 0; j < ids.size(); j++) {
			if (isEnabled(ids[j]))
				activateViewObject(ids[j]);
		}
	}
}

void Script::deactivateViewObjects() {
	// After its own triggers, the view stops the asset it showed, which fires
	// that asset's Object Deactivation (reference run manual1, 2026-10-13:
	// view 1214's background 38994 disables the explorables its activation
	// enabled), then deactivates its navs, character views and explorables
	// (0x438770)
	Common::Array<uint32> ids = _viewObjects;
	_viewObjects.clear();
	const uint32 kinds[4] = { kClassAssetRef, kClassNav, kClassCharView, kClassExplorable };
	for (int i = 0; i < 4; i++) {
		for (uint j = 0; j < ids.size(); j++) {
			if (_data.getClassOf(ids[j]) == kinds[i])
				fire(ids[j], kEventObjectDeactivation);
		}
	}
}

bool Script::isInView(uint32 id) const {
	VCView v;
	if (!_data.getView(_view, v))
		return false;
	uint32 list;
	switch (_data.getClassOf(id)) {
	case kClassCharView:
		list = v.characters;
		break;
	case kClassNav:
		list = v.navs;
		break;
	case kClassExplorable:
		list = v.explorables;
		break;
	default:
		return false;
	}
	Common::Array<uint32> ids;
	_data.getMembers(list, ids);
	return Common::find(ids.begin(), ids.end(), id) != ids.end();
}

void Script::activateViewObject(uint32 id) {
	if (!isInView(id) || Common::find(_viewObjects.begin(), _viewObjects.end(), id) != _viewObjects.end())
		return;
	_viewObjects.push_back(id);
	fire(id, kEventObjectActivation);
}

void Script::deactivateViewObject(uint32 id) {
	Common::Array<uint32>::iterator it = Common::find(_viewObjects.begin(), _viewObjects.end(), id);
	if (it == _viewObjects.end() || !isInView(id))
		return;
	_viewObjects.erase(it);
	fire(id, kEventObjectDeactivation);
}

void Script::getViewAssets(Common::Array<uint32> &assets, uint32 &background) const {
	assets.clear();
	background = 0;
	VCView v;
	Common::Array<uint32> ids;
	if (!_data.getView(_view, v))
		return;
	_data.getMembers(v.assets, ids);
	// The view's player (view object +0x148) holds one asset: the first
	// enabled one of the list, a still or a looping clip. Later ones are
	// fallbacks (reference run, 2026-10-12: view 889 shows its loop 38812,
	// never the still 26360 after it)
	for (uint i = 0; i < ids.size(); i++) {
		VCAsset asset;
		if (!_data.getAsset(ids[i], asset) || !isEnabled(ids[i]) || asset.files.empty())
			continue;
		if (asset.flags & kAssetStill)
			background = ids[i];
		assets.push_back(ids[i]);
		break;
	}
}

void Script::showView() {
	VCView v;
	if (!_data.getView(_view, v))
		return;

	Common::Array<uint32> ids;
	uint32 background;
	getViewAssets(ids, background);
	for (uint i = 0; i < ids.size(); i++) {
		VCAsset asset;
		_data.getAsset(ids[i], asset);
		log(Common::String::format("      %s #%u ", ids[i] == background ? "background" : "overlay", ids[i]) +
			formatFile(asset.files[0]) + "  \"" + nameOrNone(_data, asset.name) + "\"");
	}

	ids.clear();
	_data.getMembers(v.characters, ids);
	for (uint i = 0; i < ids.size(); i++) {
		VCCharView cv;
		if (!isEnabled(ids[i]) || !_data.getCharView(ids[i], cv))
			continue;
		VCCharacter character;
		uint32 characterName = _data.getCharacter(cv.character, character) ? character.name : 0;
		Common::String line = Common::String::format("      character view #%u ", ids[i]) + nameOrNone(_data, characterName) +
			" at " + formatRect(cv.rect);
		Common::Array<uint32> topics;
		_data.getMembers(cv.conversations, topics);
		Common::String list;
		for (uint j = 0; j < topics.size(); j++) {
			VCConversation c;
			if (!isEnabled(topics[j]) || !_data.getConversation(topics[j], c))
				continue;
			Common::U32String name;
			list += Common::String::format("%s%u:", list.empty() ? "" : ", ", topics[j]) +
				(_data.getName(c.name, name) ? pythonRepr(name) : Common::String("None"));
		}
		if (!list.empty())
			line += "; topics " + list;
		log(line);
	}

	ids.clear();
	_data.getMembers(v.explorables, ids);
	for (uint i = 0; i < ids.size(); i++) {
		VCExplorable e;
		if (isEnabled(ids[i]) && _data.getExplorable(ids[i], e))
			log(Common::String::format("      explorable #%u at ", ids[i]) + formatRect(e.rect));
	}

	ids.clear();
	_data.getMembers(v.navs, ids);
	Common::String exits;
	for (uint i = 0; i < ids.size(); i++) {
		VCNav nav;
		if (!isEnabled(ids[i]) || !_data.getNav(ids[i], nav))
			continue;
		VCHotSpot hotSpot;
		VCRect rect = { 0, 0, 0, 0 };
		if (_data.getHotSpot(nav.hotSpot, hotSpot))
			rect = hotSpot.rect;
		exits += Common::String::format("%snav:%u->", exits.empty() ? "" : "; ", ids[i]) + getObjectLabel(_data, nav.target) +
			" " + formatRect(rect);
	}
	if (!exits.empty())
		log("      exits: " + exits);
}

void Script::start() {
	VCTitle title;
	if (!_data.getTitle(title)) {
		warning("Script: no VCTitle");
		return;
	}
	log("== start: " + nameOrNone(_data, title.name));
	// Game_Start (0x48cd70) writes the preferences before the title activates
	loadPreferences();
	fire(title.id, kEventObjectActivation);
}

void Script::restore(const VCSave &save) {
	// SetStartView (0x486a10) takes the save's position and Game_BuildObjects
	// (0x477940) rebuilds the objects from VCGameState, in the order below
	// (docs/formats/saves.md, "How the game restores a save"). The position
	// mode decides which Object Activation fires while the list is activated.
	_restoreMode = save.positionMode;
	_restored = true;
	_restoredSave = save;
	log("== restore");
	// The travel gets the save's disc (+42) the way SwapDisc's does
	if (save.disc >= 1)
		_disc = save.disc;
	log(Common::String::format("  disc %d", _disc));
	// Game_Start (0x48cd70) also writes the preferences over a save's values
	loadPreferences();
	enter(save.position[0], save.position[1], save.position[2], save.position[3]);
	if (save.clip)
		log(Common::String::format("  clip #%u at time %d in the view (the view plays from there)", save.clip, save.clipTime));

	_history = save.history;
	_asked.clear();
	for (uint i = 0; i < save.history.size(); i++)
		_asked[save.history[i]]++;
	_emails = save.emails;
	_emailsRead = save.emailsRead;
	_emailPending = save.emailsPending;
	_notes = save.notes;
	_inventory = save.inventory;
	_evidence = save.evidence;
	_kit = save.kit;

	// 1. The full-screen layout (Layout_SetAsView)
	if (save.layout)
		showLayout(save.layout);

	// 2. Sounds playing are kind 27 sound objects: no Object Activation. The
	// time (3rd value, ms in the file) is the sound's in point: it starts
	// there and a loop goes back to it (0x4a88d0, 0x48825f)
	for (uint i = 0; i + 3 <= save.sounds.size(); i += 3) {
		uint32 id = save.sounds[i];
		VCAsset a;
		if (!_data.getAsset(id, a))
			continue;
		_sounds[id] = true;
		log(Common::String::format("  restored sound #%u at %u", id, save.sounds[i + 2]));
		if (_hooks)
			_hooks->restoreSound(id, (a.flags & kAssetLoop) != 0, save.sounds[i + 2]);
	}

	// 3. Every timer is a Timer action Start (0x4a1250) with the saved number,
	// time and periodic bit: a one-shot timer restarts with its remaining time
	for (uint i = 0; i + 3 <= save.timers.size(); i += 3) {
		Timer timer = { save.timers[i + 1], ++_timerSerial, save.timers[i + 2] != 0 };
		_timers[save.timers[i]] = timer;
		log(Common::String::format("      timer %u started (%u ms%s)", save.timers[i], timer.ms, timer.periodic ? ", periodic" : ""));
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

void Script::click(uint32 id) {
	_box = 0; // a click outside the box closes it
	if (!isEnabled(id)) {
		log("  " + getObjectLabel(_data, id) + " is disabled");
		return;
	}
	fire(id, kEventMouseClick);
	// A click on an emotion or action icon removes all of them (0x49bfe0, 0x4c0660)
	uint32 classId = _data.getClassOf(id);
	if (classId == kClassEmotionIcon || classId == kClassActionIcon)
		spendIcons(classId);
}

void Script::press(uint32 id) {
	// A mouse-down on an interface item captures it until the button goes up (0x441480 clears it)
	_held = _data.getClassOf(id) == kClassInterfaceItem ? id : 0;
	click(id);
}

void Script::release() {
	_held = 0;
}

void Script::spendIcons(uint32 classId) {
	Common::Array<NeoEntry> entries;
	_data.getHdb().getObjects(classId, entries);
	for (uint i = 0; i < entries.size(); i++) {
		if (isEnabled(entries[i].key))
			_spent[entries[i].key] = true;
	}
}

void Script::doubleClick(uint32 id) {
	_box = 0;
	if (!isEnabled(id))
		log("  " + getObjectLabel(_data, id) + " is disabled");
	else
		fire(id, kEventMouseDoubleClick);
}

void Script::roll(uint32 id, bool into) {
	if (isEnabled(id))
		fire(id, into ? kEventRollInto : kEventRollOutOf);
}

bool Script::holdsTimer(uint32 id, uint n) const {
	// 0x416620, 0x480e70: an object holds the timer IDs that the conditions
	// of its Timer Expiration actions compare an event field with
	Common::Array<uint32> triggers;
	_data.getMembers(getTriggers(id), triggers);
	for (uint i = 0; i < triggers.size(); i++) {
		VCTrigger t;
		if (!_data.getTrigger(triggers[i], t) || t.event != kEventTimerExpiration)
			continue;
		for (uint j = 0; j < t.actions.size(); j++) {
			VCAction a;
			if (!_data.getAction(t.actions[j], a) || !a.conditional)
				continue;
			const VCOperand &lhs = a.condition.lhs, &rhs = a.condition.rhs;
			const VCOperand *other = lhs.kind == kOperandEvent ? &rhs : rhs.kind == kOperandEvent ? &lhs : nullptr;
			if (other && (other->kind == kOperandInt || other->kind == kOperandChar) && other->value == n)
				return true;
		}
	}
	return false;
}

void Script::expireTimer(uint n, bool stale) {
	// 0x42fa20: a periodic timer keeps running, a one-shot timer goes away.
	// The event reaches every active object in the order of their kinds
	// (0x41d3b0); one fires its triggers only if it holds the timer (0x41d430).
	if (_timers.contains(n)) {
		if (!_timers[n].periodic)
			_timers.erase(n);
	} else if (!stale) {
		return;
	}
	VCTitle title;
	_data.getTitle(title);
	const uint32 path[5] = { title.id, _node, _location, _viewPoint, _view };
	Common::Array<uint32> ids(path, 5);
	const uint32 kinds[3] = { kClassNav, kClassCharView, kClassExplorable };
	for (int i = 0; i < 3; i++) {
		for (uint j = 0; j < _viewObjects.size(); j++) {
			if (_data.getClassOf(_viewObjects[j]) == kinds[i])
				ids.push_back(_viewObjects[j]);
		}
		if (kinds[i] == kClassCharView) {
			// Then the icons on screen: action icons (kind 14), emotion icons (15)
			const uint32 icons[2] = { kClassActionIcon, kClassEmotionIcon };
			for (int j = 0; j < 2; j++) {
				Common::Array<NeoEntry> entries;
				_data.getHdb().getObjects(icons[j], entries);
				for (uint k = 0; k < entries.size(); k++) {
					if (isEnabled(entries[k].key) && !isSpent(entries[k].key))
						ids.push_back(entries[k].key);
				}
			}
		}
	}
	for (uint i = 0; i < _layouts.size(); i++)
		ids.push_back(_layouts[i].items);
	for (uint i = 0; i < _viewObjects.size(); i++) {
		if (_data.getClassOf(_viewObjects[i]) == kClassAssetRef)
			ids.push_back(_viewObjects[i]);
	}
	if (!_videoQueue.empty())
		ids.push_back(_videoQueue[0]);

	EventFields fields;
	fields.push_back(n);
	for (uint i = 0; i < ids.size(); i++) {
		if (ids[i] && holdsTimer(ids[i], n))
			fire(ids[i], kEventTimerExpiration, fields);
	}
}

void Script::getBoxTopics(uint32 charViewId, Common::Array<uint32> &topics) const {
	VCCharView charView;
	if (_data.getCharView(charViewId, charView) && charView.conversations)
		_data.getMembers(charView.conversations, topics);
}

bool Script::isBoxTopic(uint32 charViewId, uint32 conversationId) const {
	Common::Array<uint32> topics;
	getBoxTopics(charViewId, topics);
	return Common::find(topics.begin(), topics.end(), conversationId) != topics.end();
}

void Script::openBox(uint32 charViewId) {
	// XFiles.exe 0x4767a0: each enabled topic gets a runtime object listed
	// in the box, whose activation (0x47d010) fires its Object Activation
	// triggers and plays nothing.
	_box = charViewId;
	Common::Array<uint32> topics;
	getBoxTopics(charViewId, topics);
	for (uint i = 0; i < topics.size(); i++) {
		if (isEnabled(topics[i]))
			activateTopic(topics[i]);
	}
}

void Script::activateTopic(uint32 conversationId) {
	// 0x47d010: the topic's Object Activation, then HandleFixJumpCut
	// (0x47ca30), which reads FixJumpCut(first, later) among the actions of
	// those triggers into +0x148 and +0x14c of the topic's object
	fire(conversationId, kEventObjectActivation);
	_jumpCuts.erase(conversationId);
	Common::Array<uint32> triggers;
	_data.getMembers(getTriggers(conversationId), triggers);
	for (uint i = 0; i < triggers.size(); i++) {
		VCTrigger t;
		if (!_data.getTrigger(triggers[i], t) || t.event != kEventObjectActivation)
			continue;
		for (uint j = 0; j < t.actions.size(); j++) {
			VCAction a;
			if (!_data.getAction(t.actions[j], a) || a.type != kActionFunction || a.function != "FixJumpCut" ||
					a.args.size() < 2)
				continue;
			JumpCut cut;
			cut.first = getValue(a.args[0], EventFields(), LocalVariables()) != 0;
			cut.later = getValue(a.args[1], EventFields(), LocalVariables()) != 0;
			_jumpCuts[conversationId] = cut;
			return;
		}
	}
}

void Script::showLayout(uint32 layoutId) {
	// XFiles.exe 0x44ec80, 0x42b250: every enabled item gets a runtime
	// object, whose activation fires its Object Activation triggers. The
	// layout itself owns no triggers.
	for (uint i = 0; i < _layouts.size(); i++) {
		if (_layouts[i].id == layoutId)
			return;
	}
	VCLayout layout;
	if (!_data.getLayout(layoutId, layout)) {
		warning("Script: #%u is not a layout", layoutId);
		return;
	}
	Layout shown;
	shown.id = layoutId;
	for (uint i = 0; i < layout.items.size(); i++) {
		if (isEnabled(layout.items[i]))
			shown.items.push_back(layout.items[i]);
	}
	_layouts.push_back(shown);

	// The exe appends the objects in layout order to a list kept sorted by
	// layer (low byte of the layer word): an item goes to the end if its layer
	// is not below the last one's, else before the first item of an equal or
	// higher layer. That reproduces all 15 layout shows of the runs
	// pda-usage-run1 and manual1 (layer 0 ties, layout 34010, the save screen)
	Common::Array<uint32> order;
	Common::Array<byte> layers;
	for (uint i = 0; i < shown.items.size(); i++) {
		VCInterfaceItem item;
		byte layer = _data.getInterfaceItem(shown.items[i], item) ? item.layer & 0xff : 0;
		uint at = layers.size();
		if (at && layers[at - 1] > layer) {
			at = 0;
			while (layers[at] < layer)
				at++;
		}
		order.insert_at(at, shown.items[i]);
		layers.insert_at(at, layer);
	}
	for (uint i = 0; i < order.size(); i++)
		fire(order[i], kEventObjectActivation);
}

void Script::hideLayout(uint32 layoutId) {
	// The items' objects go away, firing their Object Deactivation (0x425200)
	for (uint i = 0; i < _layouts.size(); i++) {
		if (_layouts[i].id != layoutId)
			continue;
		Common::Array<uint32> items = _layouts[i].items;
		_layouts.remove_at(i);
		for (uint j = 0; j < items.size(); j++) {
			fire(items[j], kEventObjectDeactivation);
			_itemTexts.erase(items[j]);
			if (items[j] == _focus)
				_focus = 0;
		}
		// Code run when some layouts close (0x41f8a5)
		if (layoutId == kPhoneLayout)
			phoneClosed();
		return;
	}
}

void Script::updateLayoutItem(uint32 itemId, bool enabled) {
	// An item of a shown layout comes and goes with its flag (0x41ae20,
	// 0x446790), with its Object Activation or Deactivation
	for (uint i = 0; i < _layouts.size(); i++) {
		VCLayout layout;
		if (!_data.getLayout(_layouts[i].id, layout) ||
				Common::find(layout.items.begin(), layout.items.end(), itemId) == layout.items.end())
			continue;
		Common::Array<uint32> &items = _layouts[i].items;
		Common::Array<uint32>::iterator it = Common::find(items.begin(), items.end(), itemId);
		if (enabled && it == items.end()) {
			items.push_back(itemId);
			fire(itemId, kEventObjectActivation);
		} else if (!enabled && it != items.end()) {
			items.erase(it);
			fire(itemId, kEventObjectDeactivation);
		}
	}
}

void Script::ask(uint32 conversationId) {
	// XFiles.exe 0x46d0d0: a click on a topic in the box queues its
	// playlist (0x4e9080), then fires its Mouse Click triggers.
	if (!_box || !isBoxTopic(_box, conversationId)) {
		VCView view;
		Common::Array<uint32> charViews;
		if (_data.getView(_view, view))
			_data.getMembers(view.characters, charViews);
		for (uint i = 0; i < charViews.size(); i++) {
			if (isBoxTopic(charViews[i], conversationId)) {
				openBox(charViews[i]);
				break;
			}
		}
	}
	playTopic(conversationId, true);
	fire(conversationId, kEventMouseClick);
}

void Script::playTopic(uint32 conversationId, bool inBox) {
	// 0x4e9080, 0x4e9440: Willmore asks the question, the character
	// answers. Asked again, a character with stock replies may give the
	// topic's atom, a stock reply before the answer, or (after four times)
	// "What's the matter with you?" instead; one without plays atom and
	// answer. inBox is +0x144 of the topic's runtime object: set for a
	// click in the box, clear for an item dropped on the character.
	//
	// The view's loop may go between clips to hide the cut: before the
	// first clip after the question or before the answer, as the topic's
	// FixJumpCut asks (beforeLead, beforeAnswer), and always before "What's
	// the matter with you?" after another stock reply.
	VCConversation c;
	if (!_data.getConversation(conversationId, c)) {
		warning("Script: #%u is not a conversation", conversationId);
		return;
	}
	_jumpCut = _jumpCuts.getValOrDefault(conversationId, JumpCut());
	_clipQueued = false;
	uint count = ++_asked[conversationId];
	playList(c.question);
	if (count < 2) {
		beforeAnswer();
		playList(c.answer);
		return;
	}
	uint32 stock[kStockAtomCount];
	if (!getStockAtoms(conversationId, stock)) {
		if (c.atom) {
			beforeLead();
			playList(c.atom);
			_clipQueued = true;
		}
		beforeAnswer();
		playList(c.answer);
		return;
	}
	// 0x4e9880: from the box S0, sometimes followed by "What's the matter
	// with you?"; otherwise that alone
	// (Each step builds its own generator where XFiles.exe does, rolled or
	// not, so that fixed seeds match a reference run's.)
	if (count >= 4) {
		Dice dice = newDice(0x4e98fe);
		int limit = count == 4 ? 6 : count == 5 ? 7 : count == 6 ? 8 : 9;
		if (stock[0] && roll(dice) < limit) {
			if (inBox) {
				beforeLead();
				playStock(stock[0]);
				_clipQueued = true;
				if (stock[kStockWhatsTheMatter] && roll(dice) < 4) {
					queueViewLoop();
					playStock(stock[kStockWhatsTheMatter]);
				}
				return;
			}
			if (stock[kStockWhatsTheMatter]) {
				beforeLead();
				playStock(stock[kStockWhatsTheMatter]);
				return;
			}
		}
	}
	// 0x4e9440, then 0x4ea400
	Dice dice = newDice(0x4e97c7);
	if (c.atom && roll(dice) < 9) {
		beforeLead();
		playList(c.atom);
		_clipQueued = true;
		if (c.answer && roll(dice) < 4) {
			beforeAnswer();
			playList(c.answer);
		}
		return;
	}
	// 0x495820: from the box S0 or S1, otherwise S1
	dice = newDice(0x495864);
	if ((stock[0] || stock[1]) && roll(dice) < 4) {
		uint32 reply;
		if (!inBox)
			reply = stock[1];
		else if (stock[0] && stock[1])
			reply = roll(dice) < 6 ? stock[0] : stock[1];
		else
			reply = stock[0] ? stock[0] : stock[1];
		if (reply) {
			beforeLead();
			playStock(reply);
			_clipQueued = true;
		}
		if (stock[kStockWhatsTheMatter] && count > 2 && roll(dice) < 4) {
			queueViewLoop();
			beforeLead();
			playStock(stock[kStockWhatsTheMatter]);
			_clipQueued = true;
		}
	}
	beforeAnswer();
	playList(c.answer);
}

void Script::beforeLead() {
	if (_jumpCut.first && !_clipQueued)
		queueViewLoop();
}

void Script::beforeAnswer() {
	if (_jumpCut.later && _clipQueued)
		queueViewLoop();
}

void Script::queueViewLoop() {
	// 0x4ea320: the clip playing in the view, if it loops; 0x5049c0 sets
	// its segment from the clip's start for 750 ms, or less if the clip is
	// shorter (Movie plays from the start of the asset's range and stops at
	// its end)
	uint32 id = getViewLoop();
	if (!id)
		return;
	queueVideo(id, 750, false);
}

uint32 Script::getViewLoop() const {
	// The view's player (view object +0x148) holds its background or its
	// looping clip; only a looping asset (flag 8, 0x4966d0) is used
	Common::Array<uint32> assets;
	uint32 background;
	getViewAssets(assets, background);
	for (uint i = 0; i < assets.size(); i++) {
		VCAsset asset;
		if (assets[i] != background && _data.getAsset(assets[i], asset) && (asset.flags & kAssetLoop))
			return assets[i];
	}
	return 0;
}

uint32 Script::firstEnabled(uint32 listId) const {
	// XFiles.exe 0x41fe60: the first member that is enabled (or has no
	// enabled state), 0 if there is none
	Common::Array<uint32> assets;
	if (!listId || !_data.getMembers(listId, assets))
		return 0;
	for (uint i = 0; i < assets.size(); i++) {
		if (isEnabled(assets[i]))
			return assets[i];
	}
	return 0;
}

void Script::playList(uint32 listId) {
	// A topic's question, answer and atom lists each play one asset
	// (Topic_Play 0x4e9080 through 0x41fe60)
	if (uint32 asset = firstEnabled(listId))
		playAsset(asset, kModePlayOnce, nullptr);
}

void Script::playStock(uint32 assetId) {
	if (assetId && isEnabled(assetId))
		playAsset(assetId, kModePlayOnce, nullptr);
}

void Script::setFixedSeeds(uint32 base) {
	_fixedSeeds = true;
	_seedBase = base;
	_diceBuilt.clear();
}

static uint32 mixBits(uint32 h) {
	// MurmurHash3's finalizer
	h ^= h >> 16;
	h *= 0x85EBCA6B;
	h ^= h >> 13;
	h *= 0xC2B2AE35;
	h ^= h >> 16;
	return h;
}

Script::Dice Script::newDice(uint32 site) {
	// XFiles.exe seeds each generator from the clock; xfref.dll (the
	// reference runs' logging DLL) replaces that with this hash
	Dice dice;
	dice.own = _fixedSeeds;
	dice.state = 0;
	if (_fixedSeeds)
		dice.state = mixBits(mixBits(_seedBase ^ site) + _diceBuilt[site]++);
	return dice;
}

int Script::roll(Dice &dice, int min, int max) {
	// XFiles.exe 0x468df0 (a negative min is not used)
	uint32 &x = dice.own ? dice.state : _random;
	x = x * 0x41C64E6D + 0x3039;
	return x % (uint32)(max - min + 1) + min;
}

bool Script::getStockAtoms(uint32 conversationId, uint32 (&atoms)[kStockAtomCount]) const {
	// 0x43d3d0: the atoms of the first topic of the character asked, if it
	// has no label. Text assets (a placeholder) leave their slot empty.
	VCView view;
	Common::Array<uint32> charViews;
	if (!_data.getView(_view, view) || !_data.getMembers(view.characters, charViews))
		return false;
	for (uint i = 0; i < charViews.size(); i++) {
		VCCharView charView;
		Common::Array<uint32> topics;
		if (!_data.getCharView(charViews[i], charView) || !_data.getMembers(charView.conversations, topics) ||
				Common::find(topics.begin(), topics.end(), conversationId) == topics.end())
			continue;
		VCConversation first;
		Common::Array<uint32> assets;
		if (!_data.getConversation(topics[0], first) || first.label || !_data.getMembers(first.atom, assets))
			return false;
		for (uint j = 0; j < kStockAtomCount; j++) {
			VCAsset asset;
			atoms[j] = j < assets.size() && _data.getAsset(assets[j], asset) && !asset.files.empty() ? assets[j] : 0;
		}
		return true;
	}
	return false;
}

void Script::followNav(uint32 navId) {
	VCNav nav;
	if (!_data.getNav(navId, nav)) {
		warning("Script: #%u is not a nav", navId);
		return;
	}
	fire(navId, kEventMouseClick);
	VCLocation location;
	if (!_data.getLocation(_location, location))
		return;
	for (uint i = 0; i < location.viewPoints.size(); i++) {
		VCViewPoint vp;
		if (!_data.getViewPoint(location.viewPoints[i], vp))
			continue;
		for (uint j = 0; j < vp.views.size(); j++) {
			if (vp.views[j] == nav.target) {
				enter(_node, _location, vp.id, nav.target);
				return;
			}
		}
	}
	warning("Script: nav #%u leads out of the location", navId);
}

void Script::goTo(uint32 target) {
	// Breadth-first search over enabled navs to views
	struct Step {
		uint32 from, nav;
	};
	Common::HashMap<uint32, Step> prev;
	Common::Array<uint32> todo;
	Step root = { 0, 0 };
	prev[_view] = root;
	todo.push_back(_view);
	for (uint next = 0; next < todo.size() && !prev.contains(target); next++) {
		VCView v;
		Common::Array<uint32> navs;
		if (_data.getView(todo[next], v))
			_data.getMembers(v.navs, navs);
		for (uint i = 0; i < navs.size(); i++) {
			VCNav nav;
			if (!_data.getNav(navs[i], nav))
				continue;
			if (isEnabled(navs[i]) && !prev.contains(nav.target) && _data.getClassOf(nav.target) == kClassView) {
				Step s = { todo[next], navs[i] };
				prev[nav.target] = s;
				todo.push_back(nav.target);
			}
		}
	}
	if (!prev.contains(target)) {
		log("  no enabled path to " + getObjectLabel(_data, target));
		return;
	}
	Common::Array<uint32> path;
	for (uint32 v = target; prev[v].nav; v = prev[v].from)
		path.push_back(prev[v].nav);
	for (int i = path.size() - 1; i >= 0; i--)
		step(Common::String::format("nav:%u", path[i]));
}

void Script::step(const Common::String &s) {
	log("-- " + s);
	Common::String kind = s, arg;
	size_t colon = s.findFirstOf(':');
	if (colon != Common::String::npos) {
		kind = s.substr(0, colon);
		arg = s.substr(colon + 1);
	}
	uint32 id = (uint32)atoi(arg.c_str());
	// The phone's buttons pressed by the last step go up (100 ms later in the exe)
	for (uint i = 0; i < _pressed.size(); i++)
		releaseItem(_pressed[i]);
	_pressed.clear();
	if (kind == "wait" && isVideoLooped())
		log(Common::String::format("  (video #%u loops)", _videoQueue[0]));
	else if (kind == "wait")
		finishVideo();
	else if (kind == "click" && _data.getClassOf(id) == kClassInventory)
		selectInventory(id); // an inventory icon takes its cursor (0x49c180)
	else if (kind == "click")
		click(id);
	else if (kind == "press")
		press(id);
	else if (kind == "release")
		release();
	else if (kind == "dclick")
		doubleClick(id);
	else if (kind == "rollin" || kind == "rollout")
		roll(id, kind == "rollin");
	else if (kind == "timer")
		expireTimer(id, true); // pda-usage-run1: expiries queued before a stop still run
	else if (kind == "ask")
		ask(id);
	else if (kind == "goto")
		goTo(id);
	else if (kind == "nav")
		followNav(id);
	else if (kind == "seed")
		setFixedSeeds(id);
	else if (kind == "key")
		keyPress(id);
	else if (kind == "end")
		finishSound(id);
}

} // End of namespace XFiles
