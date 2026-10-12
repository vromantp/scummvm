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

Script::Script(const VCData &data) : _data(data), _hooks(nullptr), _timerSerial(0), _random(1), _fixedSeeds(false), _seedBase(0), _box(0), _clipQueued(false), _viewHeld(false), _viewActive(false), _node(0), _location(0), _viewPoint(0), _view(0) {
	_jumpCut.first = _jumpCut.later = false;
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

void Script::fire(uint32 id, int event, const EventFields &fields) {
	Common::Array<uint32> triggers;
	_data.getMembers(getTriggers(id), triggers);
	for (uint i = 0; i < triggers.size(); i++) {
		VCTrigger t;
		if (_data.getTrigger(triggers[i], t) && t.event == event)
			runActions(t.actions, event, fields);
	}
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
		Common::Array<uint32> assets;
		_data.getMembers(a.assetList, assets);
		for (uint i = 0; i < assets.size(); i++) {
			if (isEnabled(assets[i]))
				playAsset(assets[i], a.mode, a.type == kActionSound3D ? &a : nullptr);
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
	} else if (a.function == "DoActionList" && args.size() >= 2) {
		Common::Array<uint32> actions;
		_data.getMembers(args[1], actions);
		runActions(actions, event, fields);
	} else {
		// The values of its variable arguments follow, as a reference run
		// logs them
		Common::String values;
		for (uint i = 0; i < a.args.size(); i++) {
			if (a.args[i].kind == kOperandVariable)
				values += Common::String::format("%s%d", values.empty() ? "" : ", ", args[i]);
		}
		log("      call " + describeAction(_data, a.id, event, false) + (values.empty() ? "" : " [" + values + "]"));
		if (a.function == "EnableKitIcon" && !args.empty())
			enableKitIcon(args[0]);
		if (_hooks)
			_hooks->callFunction(a, args);
	}
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
		if (_hooks)
			_hooks->playAsset(id, mode, sound3D != nullptr, sound3D ? sound3D->x : 0, sound3D ? sound3D->y : 0);
		fire(id, kEventObjectActivation);
	} else if (mode == kModeStop) {
		log(Common::String::format("      stop #%u ", id) + file);
		if (_hooks)
			_hooks->playAsset(id, mode, false, 0, 0);
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

void Script::enter(uint32 node, uint32 location, uint32 viewPoint, uint32 view) {
	const uint32 newPath[4] = { node, location, viewPoint, view };
	const uint32 oldPath[4] = { _node, _location, _viewPoint, _view };
	int first = 0;
	while (first < 4 && oldPath[first] == newPath[first])
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
	// After its own triggers, the view deactivates its navs, character views
	// and explorables (0x438770); the asset it showed is left alone
	Common::Array<uint32> ids = _viewObjects;
	_viewObjects.clear();
	const uint32 kinds[3] = { kClassNav, kClassCharView, kClassExplorable };
	for (int i = 0; i < 3; i++) {
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
	fire(title.id, kEventObjectActivation);
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

void Script::expireTimer(uint n) {
	// 0x42fa20: a periodic timer keeps running, a one-shot timer goes away.
	// The event reaches every active object in the order of their kinds
	// (0x41d3b0); one fires its triggers only if it holds the timer (0x41d430).
	if (!_timers.contains(n))
		return;
	if (!_timers[n].periodic)
		_timers.erase(n);
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
	for (uint i = 0; i < shown.items.size(); i++)
		fire(shown.items[i], kEventObjectActivation);
}

void Script::hideLayout(uint32 layoutId) {
	// The items' objects go away, firing their Object Deactivation (0x425200)
	for (uint i = 0; i < _layouts.size(); i++) {
		if (_layouts[i].id != layoutId)
			continue;
		Common::Array<uint32> items = _layouts[i].items;
		_layouts.remove_at(i);
		for (uint j = 0; j < items.size(); j++)
			fire(items[j], kEventObjectDeactivation);
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

void Script::playList(uint32 listId) {
	Common::Array<uint32> assets;
	if (!listId || !_data.getMembers(listId, assets))
		return;
	for (uint i = 0; i < assets.size(); i++) {
		if (isEnabled(assets[i]))
			playAsset(assets[i], kModePlayOnce, nullptr);
	}
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
	if (kind == "wait" && isVideoLooped())
		log(Common::String::format("  (video #%u loops)", _videoQueue[0]));
	else if (kind == "wait")
		finishVideo();
	else if (kind == "click")
		click(id);
	else if (kind == "dclick")
		doubleClick(id);
	else if (kind == "rollin" || kind == "rollout")
		roll(id, kind == "rollin");
	else if (kind == "timer")
		expireTimer(id);
	else if (kind == "ask")
		ask(id);
	else if (kind == "goto")
		goTo(id);
	else if (kind == "nav")
		followNav(id);
	else if (kind == "seed")
		setFixedSeeds(id);
}

} // End of namespace XFiles
