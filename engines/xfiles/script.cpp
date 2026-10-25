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

Script::Script(const VCData &data) : _data(data), _hooks(nullptr), _phone(*this, data), _computer(*this), _pda(*this, data), _prefs(*this), _texts(nullptr), _timerSerial(0), _random(1), _soundEnds(false), _fixedSeeds(false), _seedBase(0), _box(0), _restoreMode(-1), _disc(1), _photosOnCamera(0), _cameraEnabled(false), _evidenceRowShown(false), _hintList(0), _hintTrigger(0), _hintCursor(0), _hintGlow(0), _hintGlowOn(false), _aiSoundEndPending(false), _aiSuspended(false), _tint(0), _restored(false), _restorePending(false), _focus(0), _held(0), _currNodeVariable(0), _boxShown(false), _ideaTopic(0), _viewSpecialVariable(0), _clickOpensBox(false), _heldVariable(0), _heldNone(0), _gunValue(0), _heldCursorItem(0), _binocularsMode(0), _binocularsHeld(false), _planning(false), _nextVideoEnd(0), _clipsActivated(false), _clipQueued(false), _fireSerial(0), _inEvent(false), _viewSerial(0), _videoNextPending(false), _fireDepth(0), _nextId(0), _navViewQueued(false), _travelPending(false), _viewHeld(false), _viewActive(false), _viewGone(false), _enterDeactivating(false), _travelDropped(false), _viewLeaving(false), _leavePending(false), _viewStateVariable(0), _viewAssetHeld(false), _endGamePending(false), _endGameVar(0), _endGameValue(0), _prefArtificialIntuition(0), _cookEmailCount(0), _cookEmailFlag(0), _uberParanoia(0), _node(0), _location(0), _viewPoint(0), _view(0) {
	memset(_prefVariables, 0, sizeof(_prefVariables));
	memset(_shootVars, 0, sizeof(_shootVars));
	memset(_shootIdx, 0, sizeof(_shootIdx));
	memset(_shootDead, 0, sizeof(_shootDead));
	memset(_shootPerm, 0, sizeof(_shootPerm));
	_shootCur = 0;
	_fbiShot = true;
	_jumpCut.first = _jumpCut.later = false;
	memset(_flashlight, 0, sizeof(_flashlight));
	memset(_tintColor, 0, sizeof(_tintColor));
	addHooks();
	// The next free object ID: a save has the game state's six objects in its header count, a new game takes them
	// at its start (5323-5328; phone.md "Original behaviour")
	Common::Array<NeoEntry> states;
	data.getGam().getObjects(kClassGameState, states);
	_nextId = data.getGam()._nextId + (states.empty() ? 6 : 0);
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

void Script::playPhoneTone(uint32 id) {
	if (id)
		log(Common::String::format("      phone: tone #%u", id));
	if (_hooks)
		_hooks->playPhoneTone(id);
}

void Script::gunClick() {
	// 0x4cb520: the location picks a remark (docs/exe/tool-objects.md "The gun's own click"); the warehouse ones
	// need iCurrNode 12, or 11 with cViewState 'd'
	const uint32 kGunShot = 34743;
	uint32 remark = 0;
	switch (_location) {
	case 303:
	case 4535:
	case 6523:
	case 10611:
	case 10785:
	case 23807:
		remark = 19654;
		break;
	case 3825:
		remark = 19656;
		break;
	case 1713:
	case 2572:
	case 2735:
	case 3059: {
		int32 node = getCurrentNode();
		if (node == 12 || (node == 11 && _viewStateVariable && getVariable(_viewStateVariable) == 'd'))
			remark = 43070;
		break;
	}
	default:
		break;
	}
	log(Common::String::format("      gun: miss #%u, shot #%u, remark #%u", _location, kGunShot, remark));
	if (_hooks)
		_hooks->playGunShot(kGunShot, remark);
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
	// 0x5ce4e0: a changed text is a new VCString with the database's next free ID, which replaces the old one in the
	// variable; the same text changes nothing (the callers test it, 0x41bcb0)
	uint32 oldId = getVariable(variableId);
	if (getString(variableId) == value)
		return;
	_strings.erase(oldId);
	uint32 newId = _nextId++;
	_strings[newId] = value;
	_variables[variableId] = (int32)newId;
}

const Script::ItemText *Script::getItemText(uint32 itemId) const {
	return _itemTexts.contains(itemId) ? &_itemTexts[itemId] : nullptr;
}

void Script::keyPress(uint key) {
	// The focused item's Key Press triggers; the event's field 0 is the key
	if (!_focus)
		return;
	EventFields fields;
	fields.push_back(key);
	fire(_focus, kEventKeyPress, fields);
}

bool Script::getItemOrigin(uint32 itemId, Common::Point &origin) const {
	Common::HashMap<uint32, Common::Point>::const_iterator it = _itemOrigins.find(itemId);
	if (it == _itemOrigins.end())
		return false;
	origin = it->_value;
	return true;
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
	if (event == kEventObjectActivation)
		cover('O', id);
	if (_fireDepth == 0 && !_inEvent)
		_fireSerial++;
	_fireDepth++;
	fireTriggers(id, event, fields);
	_fireDepth--;
	// XFiles.exe carries out a pending travel (0x493090) once the event queue
	// is empty: after the action list and the triggers it set off
	// The Object Deactivation of a sound a Stop action stopped comes on a later pass (0x4336e0, B-112)
	// Not between a click's playlist and its triggers (one event, see _inEvent): ask() flushes after them
	while (_fireDepth == 0 && (_travelPending || (!_inEvent && !_pendingStops.empty()))) {
		if (_travelPending)
			travel();
		else
			flushStops();
	}
}

void Script::flushStops() {
	Common::Array<uint32> ids;
	ids.swap(_pendingStops);
	for (uint i = 0; i < ids.size(); i++) {
		endSound(ids[i], "stopped");
		endClip(ids[i], "stopped");
	}
}

void Script::fireTriggers(uint32 id, int event, const EventFields &fields) {
	Common::Array<uint32> triggers;
	_data.getMembers(getTriggers(id), triggers);
	for (uint i = 0; i < triggers.size(); i++) {
		VCTrigger t;
		if (_data.getTrigger(triggers[i], t) && t.event == event) {
			cover('T', t.id);
			runActions(t.actions, event, fields);
		}
	}
}

bool Script::isTestedOnClick(uint32 id, uint32 variable, int event) const {
	// 0x4d2ee0 looks at the conditional actions of each Mouse Click action
	// list for one with the variable as an operand
	Common::Array<uint32> triggers;
	_data.getMembers(getTriggers(id), triggers);
	for (uint i = 0; i < triggers.size(); i++) {
		VCTrigger t;
		if (!_data.getTrigger(triggers[i], t) || t.event != event)
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
	_actionFrames.push_back(ActionFrame(&actions, 0));
	uint frame = _actionFrames.size() - 1;
	for (uint i = 0; i < actions.size(); i++) {
		_actionFrames[frame].next = i + 1;
		VCAction a;
		if (!_data.getAction(actions[i], a)) {
			warning("Script: bad action #%u", actions[i]);
			continue;
		}
		if (a.conditional && !test(a.condition, fields, locals)) {
			cover('F', a.id);
			continue;
		}
		cover('A', a.id);
		execute(a, event, fields, locals);
	}
	_actionFrames.pop_back();
}

bool Script::isViewChangeQueued() const {
	// 0x46f210: the exe's event queue holds a Set View action (condition not evaluated) or a nav's view change.
	// A trigger queues its whole action list at once, so the later actions of every running list count
	if (_navViewQueued)
		return true;
	for (uint f = 0; f < _actionFrames.size(); f++) {
		const Common::Array<uint32> &list = *_actionFrames[f].actions;
		for (uint i = _actionFrames[f].next; i < list.size(); i++) {
			VCAction a;
			if (_data.getAction(list[i], a) && a.type == kActionSetView)
				return true;
		}
	}
	return false;
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
		return testEqual(c, a, b);
	case kCondNotEqual:
		return !testEqual(c, a, b);
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

bool Script::getStringOperand(const VCOperand &operand, Common::String &text) const {
	VCVariable variable;
	if (operand.kind != kOperandVariable || !_data.getVariable(operand.value, variable) || (variable.flags & 0x7f) != 3)
		return false;
	text = getString(operand.value);
	return true;
}

bool Script::testEqual(const VCExpression &c, int32 a, int32 b) const {
	// A string variable's number is its first character; it equals a number only while it is one
	// character long (0x5cf870, 0x5cfc20). Locals and event fields against a string are not checked.
	Common::String lhs, rhs;
	bool lhsString = getStringOperand(c.lhs, lhs);
	bool rhsString = getStringOperand(c.rhs, rhs);
	if (lhsString && rhsString)
		return lhs == rhs;
	if (lhsString)
		return lhs.size() == 1 && (int32)(int8)lhs[0] == b;
	if (rhsString && c.lhs.kind == kOperandVariable)
		return rhs.size() == 1 && (int32)(int8)rhs[0] == a;
	if (rhsString && (c.lhs.kind == kOperandInt || c.lhs.kind == kOperandChar))
		return rhs.size() < 2 && (int32)(int8)(rhs.empty() ? 0 : rhs[0]) == a;
	return a == b;
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
		if (_data.getEnabled(a.target, target, enabled) && target)
			enableTarget(target, a.value != 0);
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
		// The exe activates and draws nothing for the view already shown
		// (newgame-to-comityinn, action 61115); a held view counts as shown
		if (_viewLeaving) {
			// The Set View of the leaving view's Deactivation (0x46a950 queues it): the last one wins, and it
			// is entered once the view is gone (view 45822 sets 45857; entering at once would leave it again)
			_leavePending = true;
			for (int i = 0; i < 4; i++)
				_leavePath[i] = a.path[i];
		} else if (a.path[1] != _node || a.path[2] != _location || a.path[3] != _viewPoint || a.path[0] != _view)
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
	HookCall call(a, args, event, fields);
	// The values of its variable arguments follow the call, as a reference run logs them
	Common::String values;
	for (uint i = 0; i < a.args.size(); i++) {
		if (a.args[i].kind == kOperandVariable)
			values += Common::String::format("%s%d", values.empty() ? "" : ", ", args[i]);
	}
	Common::String callLine = "      call " + describeAction(_data, a.id, event, false) + (values.empty() ? "" : " [" + values + "]");

	if (a.function == "SetStartView" && args.size() >= 4) {
		// Game_BuildObjects (0x477940) makes an object for every enabled
		// VCInventory in class order; a restore uses the save's list
		if (!_restorePending) {
			Common::Array<NeoEntry> icons;
			_inventory.clear();
			_data.getHdb().getObjects(kClassInventory, icons);
			for (uint i = 0; i < icons.size(); i++) {
				if (isEnabled(icons[i].key))
					_inventory.push_back(icons[i].key);
			}
		}
		if (_restorePending)
			restoreView();
		else
			enter(args[0], args[1], args[2], args[3]);
		// The exe also builds the game objects there (0x486a10 calls
		// Game_BuildObjects 0x477940)
		if (_hooks)
			_hooks->callFunction(call);
		logHook(a.function, kHookHandled);
	} else if ((a.function == "DoActionList" || a.function == "DoActionListID") && args.size() >= 2) {
		// DoActionListID takes the two IDs from variables (0x50a200)
		if (a.function == "DoActionListID")
			log(callLine);
		runActionList(args[1], event, fields);
		logHook(a.function, kHookHandled);
	} else {
		log(callLine);
		// The Script's own hooks first (this file and the area files), then the engine's
		HookStatus status = _hookTable.run(*this, call);
		if (_hooks && _hooks->callFunction(call))
			status = kHookHandled;
		logHook(a.function, status);
	}
}

bool Script::regShanksNode(int node, uint count, const HookCall &c) {
	// 0x4c42f0, 0x4c4510, 0x48e8d0: each argument variable is kept in its own global, in argument order
	if (c.action.args.size() < count)
		return false;
	_shanksVars[node].clear();
	for (uint i = 0; i < count; i++) {
		if (!c.isVariable(i))
			return false;
		_shanksVars[node].push_back(c.action.args[i].value);
	}
	return true;
}

bool Script::regNodeVars(int slot, uint count, const HookCall &c) {
	// 0x467350, 0x432440, 0x452360, 0x4556d0, 0x467450, 0x457790, 0x47b720: each argument variable is kept in its own
	// global, in argument order, only when every argument is a variable (docs/exe/hooks/nodes.md)
	if (c.action.args.size() < count)
		return false;
	Common::Array<uint32> vars;
	for (uint i = 0; i < count; i++) {
		if (!c.isVariable(i))
			return false;
		vars.push_back(c.action.args[i].value);
	}
	_nodeVars[slot] = vars;
	return true;
}

bool Script::setNodeDone(int slot) {
	// 0x4ad400..0x4ad760 (docs/exe/hooks/nodes.md): the "complete" flag, the slot's last variable, is written on every
	// call, 1 when the rule holds, else 0; nothing is written without a registered node or slot
	const Common::Array<uint32> &v = _nodeVars[slot];
	if (!_currNodeVariable || v.empty())
		return true;
	int32 node = getCurrentNode();
	int32 value = 0;
	switch (slot) {
	case 0: // 0x4ad400: v = 4335, 1991, 2045
		value = node == 2 && getVariable(v[0]) == 1 && getVariable(v[1]) > 2;
		break;
	case 1: { // 0x4ad480: v = 2047, 2046, 1956, 2048; 5122 bN3DidFieldOfficeCookThing is found by ID
		const uint32 kDidFieldOfficeCookThing = 5122;
		if (!_variables.contains(kDidFieldOfficeCookThing))
			return true;
		int32 translated = getVariable(v[2]);
		int32 processed = getVariable(v[1]);
		value = node == 3 && getVariable(kDidFieldOfficeCookThing) == 1 && getVariable(v[0]) == 1 &&
				((translated == 1 && processed > 2) || (translated == 0 && processed > 1));
		break;
	}
	case 2: // 0x4ad5c0: v = 2049, 2051
		value = node == 4 && getVariable(v[0]) == 1;
		break;
	case 3: // 0x4ad620: v = 1992, 2050, 2052
		value = node == 5 && getVariable(v[1]) == 1 && getVariable(v[0]) == 1;
		break;
	case 4: // 0x4ad6a0: v = 1994, 2053
		value = node == 6 && getVariable(v[0]) == 1;
		break;
	case 5: // 0x4ad700: v = 2575, 2055
		value = node == 12 && getVariable(v[0]) == 1;
		break;
	case 6: // 0x4ad760: v = 2574, 2056
		value = node == 13 && getVariable(v[0]) == 1;
		break;
	}
	setVariable(v.back(), value);
	return true;
}

void Script::setShanksVar(uint32 var) {
	// SetShanksVar 0x4c4710 (docs/exe/hooks/shanks.md): the argument is a "tell will" flag; it is set to 1 when its
	// rule holds, else 0, and the call ends with the warehouse e-mail check
	// Positions in the RegShanksNode2, 3 and 6 argument lists
	enum {
		kGotLapTop = 0,
		kGotPhone,
		kGotLicense,
		kBeenToWarehouse,
		kGotBlackPowder,
		kGotBlood,
		kGotBullet,
		kPowderToLab,
		kBloodToLab,
		kBulletToLab,
		kTalkedToWong,
		kDoneWithWong,
		kWongBackgroundChecked,
		kTellGoBackToComity,
		kTellGetLicense,
		kTellGoBackToWarehouse,
		kTellTalkToWong,
		kTellRunBackgroundCheck,
		kTellTakeEvidenceToCrimeLab,
		kTellLooksCompleteToMe,
		kN2Count
	};
	enum {
		kAllWongBoatEvidence = 0,
		kHarbormasterQuestioned,
		kAllQuestionsHarborMaster,
		kVisitedTarakan,
		kAllTarakanEvidence,
		kTalkedToCoroner,
		kGotWongBullet,
		kKnowsMSDidAutopsies,
		kKnowsBodiesAreMissing,
		kLogBookToLab,
		kLeadSphereToLab,
		kWongBulletToLab,
		kTellTakeYourEvidence,
		kTellGoBackToWong,
		kTellMissedAtTarakan,
		kTellTalkToCoroner,
		kTellTalkToCoronerAgain,
		kTellTalkToHarbormaster,
		kN3Count
	};
	enum { kBeenToSmolWarehouse = 0,
		   kGotSmolGun,
		   kSmolGunToLab,
		   kTellTieSmol,
		   kTellTakeEvidence6,
		   kN6Count };
	// Variables the exe finds by ID
	static const uint32 kCalledComputerCrimes = 5054, kTellCallComputerCrimes = 5053, kHadEncounterWithNSA = 4553,
						kArrestedSmolnikoff = 5155, kSkinnerIsInSeattle = 1990, kFoundWarehouseInING = 5051,
						kSentWarehouseEmail = 5052;
	static const uint32 kWarehouseEmail = 88440;

	const Common::Array<uint32> &n2 = _shanksVars[0], &n3 = _shanksVars[1], &n6 = _shanksVars[2];
	if (n2.size() != kN2Count || n3.size() != kN3Count || n6.size() != kN6Count)
		return;
	// The variables found by ID must exist (never missing in this data), else the call ends before the tail
	static const uint32 kFound[] = {kCalledComputerCrimes, kTellCallComputerCrimes, kHadEncounterWithNSA,
									kArrestedSmolnikoff, kSkinnerIsInSeattle, kFoundWarehouseInING, kSentWarehouseEmail};
	for (uint i = 0; i < ARRAYSIZE(kFound); i++) {
		if (!_variables.contains(kFound[i]))
			return;
	}

	int32 node = getCurrentNode();
	int32 value = 0;
	uint32 flag = 0;
	if (var == n2[kTellGoBackToComity]) {
		flag = var;
		value = node == 2 && (!getVariable(n2[kGotPhone]) || !getVariable(n2[kGotLapTop]));
	} else if (var == n2[kTellGetLicense]) {
		flag = var;
		value = node == 2 && !getVariable(n2[kGotLicense]);
	} else if (var == kTellCallComputerCrimes) {
		flag = var;
		value = node == 2 && getVariable(n2[kGotLapTop]) == 1 && getVariable(kCalledComputerCrimes) == 0;
	} else if (var == n2[kTellGoBackToWarehouse]) {
		flag = var;
		value = node == 2 && getVariable(n2[kBeenToWarehouse]) == 1 && getVariable(kHadEncounterWithNSA) == 0;
	} else if (var == n2[kTellTalkToWong]) {
		flag = var;
		value = node == 2 && getVariable(n2[kTalkedToWong]) == 1 && !getVariable(n2[kDoneWithWong]);
	} else if (var == n2[kTellTakeEvidenceToCrimeLab]) {
		flag = var;
		value = node == 2 && getVariable(n2[kBeenToWarehouse]) == 1 && getVariable(n2[kGotBlackPowder]) == 1 &&
				getVariable(n2[kGotBlood]) == 1 && getVariable(n2[kGotBullet]) == 1 &&
				(!getVariable(n2[kPowderToLab]) || !getVariable(n2[kBloodToLab]) || !getVariable(n2[kBulletToLab]));
	} else if (var == n2[kTellRunBackgroundCheck]) {
		flag = var;
		value = node == 2 && getVariable(n2[kDoneWithWong]) == 1 && getVariable(kSkinnerIsInSeattle) == 0 &&
				!getVariable(n2[kWongBackgroundChecked]);
	} else if (var == n2[kTellLooksCompleteToMe]) {
		flag = var;
		value = 1;
	} else if (var == n3[kTellGoBackToWong]) {
		flag = var;
		value = node == 3 && !getVariable(n3[kAllWongBoatEvidence]);
	} else if (var == n3[kTellTalkToHarbormaster]) {
		flag = var;
		value = node == 3 && getVariable(n3[kHarbormasterQuestioned]) == 1 && !getVariable(n3[kAllQuestionsHarborMaster]);
	} else if (var == n3[kTellMissedAtTarakan]) {
		flag = var;
		value = node == 3 && getVariable(n3[kAllQuestionsHarborMaster]) == 1 && !getVariable(n3[kAllTarakanEvidence]);
	} else if (var == n3[kTellTalkToCoroner]) {
		flag = var;
		value = node == 3 && getVariable(n3[kAllTarakanEvidence]) == 1 && !getVariable(n3[kTalkedToCoroner]);
	} else if (var == n3[kTellTalkToCoronerAgain]) {
		flag = var;
		value = node == 3 && getVariable(n3[kTalkedToCoroner]) == 1 &&
				(!getVariable(n3[kGotWongBullet]) || !getVariable(n3[kKnowsMSDidAutopsies]) ||
				 !getVariable(n3[kKnowsBodiesAreMissing]));
	} else if (var == n3[kTellTakeYourEvidence]) {
		flag = var;
		value = node == 3 && getVariable(n3[kGotWongBullet]) == 1 &&
				(!getVariable(n3[kLogBookToLab]) || !getVariable(n3[kLeadSphereToLab]) || !getVariable(n3[kWongBulletToLab]));
	} else if (var == n6[kTellTieSmol]) {
		flag = var;
		value = node == 6 && getVariable(n6[kBeenToSmolWarehouse]) == 1 && getVariable(kArrestedSmolnikoff) == 0;
	} else if (var == n6[kTellTakeEvidence6]) {
		flag = var;
		value = node == 6 && getVariable(n6[kGotSmolGun]) == 1 && !getVariable(n6[kSmolGunToLab]);
	}
	if (flag)
		_variables[flag] = value;

	// The tail, after every call: the first review after the laptop and phone are found sends the warehouse e-mail
	if (node == 2 && !getVariable(n2[kTellGoBackToComity]) && !getVariable(kFoundWarehouseInING) &&
		!getVariable(kSentWarehouseEmail)) {
		_pda.addEmail(kWarehouseEmail);
		_variables[kSentWarehouseEmail] = 1;
	}
}

void Script::click(uint32 id, const Common::Point *at) {
	// A click on an inventory item or the evidence kit closes the box before the item's triggers (dbox.close, dbox.show on=0
	// in box-click-kinds and newgame-to-comityinn); one on an interface item, an emotion icon or an action icon leaves it open
	// (the next topic click is still a box click, the box's topics are not activated again)
	uint32 clickClass = _data.getClassOf(id);
	_clickOpensBox = !isHolding();
	if (clickClass != kClassInterfaceItem && clickClass != kClassEmotionIcon && clickClass != kClassActionIcon)
		_box = 0;
	if (!isEnabled(id)) {
		log("  " + getObjectLabel(_data, id) + " is disabled");
		return;
	}
	VCCharView clicked;
	if (clickClass == kClassCharView && _data.getCharView(id, clicked))
		_clickPoint = at ? *at : Common::Point((clicked.rect.left + clicked.rect.right) / 2, (clicked.rect.top + clicked.rect.bottom) / 2);
	fire(id, kEventMouseClick);
	// A character view with an item in hand that no trigger took (0x436080): the slot's clip plays as a sound, with
	// no trigger; none (slot 10 or empty) opens the box as with nothing in hand. With the gun the exe first plays
	// the held item's animation once (blocking; not ported)
	_clickOpensBox = !isHolding();
	if (clickClass == kClassCharView && isHolding() && !isTestedOnClick(id, _heldVariable)) {
		if (uint32 asset = getHeldReaction(id))
			playStock(asset);
		else
			_clickOpensBox = true;
	}
	// A click on an emotion or action icon removes all of them (0x49bfe0, 0x4c0660); each one's deactivation
	// (0x485020, 0x4c0610) also clears its enable flag (0x40f050)
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
	// Removing an icon deactivates it, and the deactivation of an emotion or action icon sets its enable
	// flag to 0 (0x485020, 0x4c0610 -> 0x40f050); the saves of newgame-to-comityinn have them at 0
	Common::Array<NeoEntry> entries;
	_data.getHdb().getObjects(classId, entries);
	for (uint i = 0; i < entries.size(); i++) {
		if (isEnabled(entries[i].key))
			_enabled[entries[i].key] = false;
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
	if (!isEnabled(id))
		return;
	fire(id, into ? kEventRollInto : kEventRollOutOf);
	// CharView_OnRoll 0x443010: rolling into a character with an Atoms topic with the gun, handcuffs or stiletto in
	// hand that its triggers do not take: S5 "Don't do that"
	if (into && isHolding() && _data.getClassOf(id) == kClassCharView && !isTestedOnClick(id, _heldVariable, kEventRollInto)) {
		int32 item = getVariable(_heldVariable);
		uint32 slots[kReactionSlots];
		if ((item == 9 || item == 10 || item == 23) && getReactionSlots(id, slots))
			playStock(slots[5]);
	}
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
					if (isEnabled(entries[k].key))
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

} // End of namespace XFiles
