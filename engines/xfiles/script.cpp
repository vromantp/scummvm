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

/** OnIntInvClick (XFiles.exe 0x467fb0): the PDA's Inventory_iPDA value, bDisablePDA and PDAalert.amv. */
static const int32 kInventoryPDA = 14;
/** The camera (docs/subsystems/camera.md): photos it holds, its shutter assets (RegCameraAss), viewfinder width, the view band. */
static const int32 kCameraPhotos = 24;
static const uint32 kCameraShutter = 34747;
static const uint32 kCameraShutterFull = 57104;
static const int kCameraWidth = 320;
static const int kBandLeft = 20;
static const int kBandTop = 90;
static const int kBandWidth = 600;
static const int kBandHeight = 240;
static const uint32 kDisablePDAVariable = 5277;
static const uint32 kPDAAlertSound = 75688;

/** PageTurnSound runs trigger 92463's list (XFiles.exe 0x4b3930): one of six random page sounds. */
static const uint32 kPageTurnSoundActions = 92468;

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

Script::Script(const VCData &data) : _data(data), _hooks(nullptr), _phone(*this, data), _computer(*this), _pda(*this, data), _prefs(*this), _texts(nullptr), _timerSerial(0), _random(1), _soundEnds(false), _fixedSeeds(false), _seedBase(0), _box(0), _restoreMode(-1), _disc(1), _photosOnCamera(0), _cameraEnabled(false), _evidenceRowShown(false), _hintList(0), _hintTrigger(0), _hintGlow(0), _tint(0), _restored(false), _restorePending(false), _focus(0), _held(0), _currNodeVariable(0), _boxShown(false), _ideaTopic(0), _viewSpecialVariable(0), _clickOpensBox(false), _heldVariable(0), _heldNone(0), _heldCursorItem(0), _planning(false), _nextVideoEnd(0), _clipsActivated(false), _clipQueued(false), _fireSerial(0), _inEvent(false), _viewSerial(0), _videoNextPending(false), _fireDepth(0), _travelPending(false), _viewHeld(false), _viewActive(false), _viewAssetHeld(false), _prefArtificialIntuition(0), _uberParanoia(0), _node(0), _location(0), _viewPoint(0), _view(0) {
	memset(_prefVariables, 0, sizeof(_prefVariables));
	_jumpCut.first = _jumpCut.later = false;
	memset(_flashlight, 0, sizeof(_flashlight));
	memset(_tintColor, 0, sizeof(_tintColor));
	addHooks();
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
		if (_data.getTrigger(triggers[i], t) && t.event == event)
			runActions(t.actions, event, fields);
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
		if (a.path[1] != _node || a.path[2] != _location || a.path[3] != _viewPoint || a.path[0] != _view)
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

void Script::addHooks() {
	addCoreHooks();
	_phone.addHooks(_hookTable);
	_computer.addHooks(_hookTable);
	_pda.addHooks(_hookTable);
	_prefs.addHooks(_hookTable);
}

void Script::addCoreHooks() {
	HookTable<Script> &t = _hookTable;
	t.add("SwapDisc", [](Script &s, const HookCall &c) {
		if (!c.has(4))
			return false;
		// The last call in an event wins (0x4cfc70 replaces the pending travel)
		for (int i = 0; i < 4; i++)
			s._travelTo[i] = c.args[i];
		s._travelPending = true;
		return true;
	});
	t.add("NukeAllIcons", [](Script &s, const HookCall &) {
		// 0x49f3c0: deactivates the icons of kinds 14, 12, 15 (and others the engine does not model); the
		// emotion and action ones lose their enable flags
		s.spendIcons(kClassEmotionIcon);
		s.spendIcons(kClassActionIcon);
		return true;
	});
	t.add("EnableKitIcon", [](Script &s, const HookCall &c) {
		if (!c.has(1))
			return false;
		s.enableKitIcon(c.args[0]);
		return true;
	});
	t.add("RandGen", [](Script &s, const HookCall &c) {
		if (!c.has(2) || !c.isVariable(0))
			return false;
		s.randGen(c.action.args[0].value, c.args[1]);
		return true;
	});
	t.add("IsIItemHeld", [](Script &s, const HookCall &c) {
		if (!c.has(2) || !c.isVariable(1))
			return false;
		s.isIItemHeld(c.action.args[1].value, c.args[0]);
		return true;
	});
	t.add("RegCurrNode", [](Script &s, const HookCall &c) {
		if (!c.isVariable(0))
			return false;
		s._currNodeVariable = c.action.args[0].value; // 0x4926a0
		return true;
	});
	t.add("RegNode2Vars", [](Script &s, const HookCall &c) {
		return s.regNodeVars(0, 3, c);
	});
	t.add("RegNode3Vars", [](Script &s, const HookCall &c) {
		return s.regNodeVars(1, 4, c);
	});
	t.add("RegNode4Vars", [](Script &s, const HookCall &c) {
		return s.regNodeVars(2, 2, c);
	});
	t.add("RegNode5Vars", [](Script &s, const HookCall &c) {
		return s.regNodeVars(3, 3, c);
	});
	t.add("RegNode6Vars", [](Script &s, const HookCall &c) {
		return s.regNodeVars(4, 2, c);
	});
	t.add("RegLink2Vars", [](Script &s, const HookCall &c) {
		return s.regNodeVars(5, 2, c);
	});
	t.add("RegLink3Vars", [](Script &s, const HookCall &c) {
		return s.regNodeVars(6, 2, c);
	});
	t.add("SetIsNode2Done", [](Script &s, const HookCall &) {
		return s.setNodeDone(0);
	});
	t.add("SetIsNode3Done", [](Script &s, const HookCall &) {
		return s.setNodeDone(1);
	});
	t.add("SetIsNode4Done", [](Script &s, const HookCall &) {
		return s.setNodeDone(2);
	});
	t.add("SetIsNode5Done", [](Script &s, const HookCall &) {
		return s.setNodeDone(3);
	});
	t.add("SetIsNode6Done", [](Script &s, const HookCall &) {
		return s.setNodeDone(4);
	});
	t.add("SetIsLink2Done", [](Script &s, const HookCall &) {
		return s.setNodeDone(5);
	});
	t.add("SetIsLink3Done", [](Script &s, const HookCall &) {
		return s.setNodeDone(6);
	});
	t.add("WillmoreSleeps", [](Script &, const HookCall &) {
		return true; // 0x4df9f0 is empty
	});
	t.add("PreloadEmIcon", [](Script &, const HookCall &) {
		return true; // 0x49c1f0 only builds the icon's object, hidden: nothing to draw
	});
	t.add("RegShanksNode2", [](Script &s, const HookCall &c) {
		return s.regShanksNode(0, 20, c);
	});
	t.add("RegShanksNode3", [](Script &s, const HookCall &c) {
		return s.regShanksNode(1, 18, c);
	});
	t.add("RegShanksNode6", [](Script &s, const HookCall &c) {
		return s.regShanksNode(2, 5, c);
	});
	t.add("SetShanksVar", [](Script &s, const HookCall &c) {
		if (!c.isVariable(0))
			return false;
		s.setShanksVar(c.action.args[0].value);
		return true;
	});
	t.add("RegViewSpecial", [](Script &s, const HookCall &c) {
		if (!c.isVariable(0))
			return false;
		s._viewSpecialVariable = c.action.args[0].value; // 0x481140
		return true;
	});
	t.add("OnBinocClick", [](Script &s, const HookCall &c) {
		if (!c.has(1))
			return false;
		// 0x4e1b30: the item goes into the variable if it is not in hand; then, unless the view has a view change of the
		// binoculars' own (not ported), a character reacts
		if (s._heldVariable && s.getVariable(s._heldVariable) != c.args[0]) {
			s._variables[s._heldVariable] = c.args[0];
			if (s.isSpecialView())
				s.log("  (binoculars' view change of this view not ported)");
			else
				s.toolReaction(c.args[0]);
		}
		return true;
	});
	t.add("OnFLightClick", [](Script &s, const HookCall &c) {
		if (!c.has(1))
			return false;
		// 0x4e1d80
		if (s._heldVariable && s.getVariable(s._heldVariable) != c.args[0]) {
			s._variables[s._heldVariable] = c.args[0];
			s.toolReaction(c.args[0]);
		}
		return true;
	});
	t.add("OnNVisionClick", [](Script &s, const HookCall &c) {
		if (!c.has(1))
			return false;
		// 0x4e1f80
		if (s._heldVariable && s.getVariable(s._heldVariable) != c.args[0]) {
			s._variables[s._heldVariable] = c.args[0];
			s.toolReaction(c.args[0]);
		}
		return true;
	});
	t.add("OnCameraClick", [](Script &s, const HookCall &c) {
		if (!c.has(1))
			return false;
		// 0x4e1c50: no character reaction for the camera (it passes 3 to Tool_CharReaction). The camera object is
		// enabled unless the view is a special one (iCurrViewSpecial 1)
		if (s._heldVariable && s.getVariable(s._heldVariable) != c.args[0]) {
			s._variables[s._heldVariable] = c.args[0];
			s._cameraEnabled = !s._viewSpecialVariable || s.getVariable(s._viewSpecialVariable) != 1;
		}
		return true;
	});
	t.add("RegCurrInvVar", [](Script &s, const HookCall &c) {
		if (!c.has(2) || !c.isVariable(0))
			return false;
		// 0x458e60: the variable starts as "nothing in hand"
		s._heldVariable = c.action.args[0].value;
		s._heldNone = c.args[1];
		s._variables[s._heldVariable] = s._heldNone;
		return true;
	});
	t.add("OnInvClick", [](Script &s, const HookCall &c) {
		if (!c.has(1))
			return false;
		// Inv_Select 0x49b410: the item's value goes into the variable
		if (s._heldVariable)
			s._variables[s._heldVariable] = c.args[0];
		return true;
	});
	t.add("SelectInv", [](Script &s, const HookCall &c) {
		if (!c.has(1))
			return false;
		// 0x50a390: if the item has an icon, put back what is in hand and
		// take the item, as a click on its icon would
		if (s.hasInventoryIcon(c.args[0]))
			s.selectInventory(c.args[0]);
		return true;
	});
	t.add("DeselectInv", [](Script &s, const HookCall &c) {
		if (!c.has(1))
			return false;
		// 0x50a570: 0 puts back any item, else only that one
		if (c.args[0] == 0 || s._heldCursorItem == (uint32)c.args[0])
			s.putBack();
		return true;
	});
	t.add("OnIntInvClick", [](Script &s, const HookCall &c) {
		// 0x467fb0: the PDA and phone icons never set iCurrInventory; this
		// clears the held cursor (Inv_SetHeldCursor(0)). For the PDA, while a
		// sound object plays an asset with a picture (a video, a clip), it sets
		// bDisablePDA, which the icon's trigger tests before it shows the PDA,
		// and plays the alert; else the timers pause until no layout is shown
		if (c.has(1) && c.args[0] == kInventoryPDA) {
			if (!s._videoQueue.empty() || !s._clips.empty()) {
				s._variables[kDisablePDAVariable] = 1;
				s.log(Common::String::format("      PDA refused: bDisablePDA = 1, alert #%u", kPDAAlertSound));
				if (s._hooks)
					s._hooks->playAsset(kPDAAlertSound, kModePlayOnce, false, 0, 0);
			} else if (s._hooks) {
				s._hooks->pauseTimers();
			}
		}
		s._heldCursorItem = 0;
		if (s._hooks)
			s._hooks->heldItemChanged(0);
		return true;
	});
	t.add("StopEnvSounds", [](Script &s, const HookCall &c) {
		s.stopEnvSounds();
		return true;
	});
	t.add("RegPreferences", [](Script &s, const HookCall &c) {
		s.regPreferences(c.action);
		return true;
	});
	t.add("SetLanguage", [](Script &s, const HookCall &c) {
		// (text, var): 0x48b680 reads the text asset (0x45f690: a file asset's file first) and writes it
		// into the string variable only if it differs (0x41bcb0)
		if (!c.has(2))
			return false;
		Common::U32String text;
		if (!s.readTextFile(c.args[0], text) && !s.getTextAsset(c.args[0], text))
			return false;
		Common::String value = text.encode(Common::kWindows1252);
		uint32 variable = c.action.args[1].value;
		if (s.getString(variable) != value)
			s.setString(variable, value);
		return true;
	});
	t.add("SetIItemText", [](Script &s, const HookCall &c) {
		// (item, text, colour, font, size, shadow, align): 0x47de90 -> 0x4be1e0, laid out at twice
		// the item's width (+0x194 is 0), as the PDA texts
		if (!c.has(7))
			return false;
		Common::U32String text;
		if (!s.readTextFile(c.args[1], text) && !s.getTextAsset(c.args[1], text))
			return false;
		uint32 color = 0x000000;
		if (c.args[2] == 'w')
			color = 0xffffff;
		else if (c.args[2] == 'x')
			color = 0x2e6b8c;
		Script::TextAlign align = Script::kAlignLeft;
		if (c.args[6] == 'c')
			align = Script::kAlignCenter;
		else if (c.args[6] == 'r')
			align = Script::kAlignRight;
		VCInterfaceItem item;
		int width = 0;
		if (s._data.getInterfaceItem(c.args[0], item))
			width = (item.rect.right - item.rect.left) * 2;
		s.setItemText(c.args[0], text, c.args[4], false, color, align, c.args[5] == 'y', width, c.args[3] == 'H');
		return true;
	});
	t.add("SetIItemFocus", [](Script &s, const HookCall &c) {
		if (!c.has(1))
			return false;
		s._focus = c.args[0];
		return true;
	});
	t.add("RegViewState", [](Script &s, const HookCall &c) {
		if (!c.isVariable(0))
			return false;
		s._variables[c.action.args[0].value] = 0; // 0x47e9d0 clears the variable it registers
		return true;
	});
	t.add("PDAEscape", [](Script &s, const HookCall &c) {
		if (!c.has(1))
			return false;
		s.runActionList(c.args[0] == 1 ? kPDAEscapeOnActions : kPDAEscapeOffActions, c.event, c.fields);
		return true;
	});
	t.add("PageTurnSound", [](Script &s, const HookCall &c) {
		s.runActionList(kPageTurnSoundActions, c.event, c.fields);
		return true;
	});

	// The hooks whose arguments a save keeps (GameState_Save 0x4d9450): recorded, but the engine draws none of
	// them yet, so they are not handled
	t.add("SetAIList", [](Script &s, const HookCall &c) {
		// SetAIList(trigger, list)
		if (c.has(2)) {
			s._hintTrigger = c.args[0];
			s._hintList = c.args[1];
		}
		return false;
	});
	t.add("ClearAIList", [](Script &s, const HookCall &c) {
		s._hintTrigger = s._hintList = 0;
		return false;
	});
	t.add("SetAIGlow", [](Script &s, const HookCall &c) {
		// A level above 1 is kept
		if (c.has(1))
			s._hintGlow = c.args[0] > 1 ? c.args[0] : 0;
		return false;
	});
	t.add("SetFLightProps", [](Script &s, const HookCall &c) {
		// 0x44bda0 turns the arguments into the three values the save keeps:
		// 2 -> 1 (else 0), 'O' -> 1 (else 0), 'B' -> 0, 'D' -> 2, else 1
		if (c.has(3)) {
			s._flashlight[0] = c.args[0] == 2;
			s._flashlight[1] = c.args[1] == 'O';
			s._flashlight[2] = 1;
			if (c.args[2] == 'B')
				s._flashlight[2] = 0;
			else if (c.args[2] == 'D')
				s._flashlight[2] = 2;
		}
		return false;
	});
	t.add("SetLBoxTint", [](Script &s, const HookCall &c) {
		if (c.has(3)) {
			s._tint = 1;
			s._tintColor[0] = c.args[2];
			s._tintColor[1] = c.args[1];
			s._tintColor[2] = c.args[0];
		}
		return true; // the band's background, drawn by XFilesEngine::clearScreen
	});
	t.add("ClearLBoxTint", [](Script &s, const HookCall &c) {
		s._tint = 0;
		return true;
	});
	t.add("DispEvIcons", [](Script &s, const HookCall &c) {
		// The engine shows or hides the row (XFilesEngine)
		if (c.has(1))
			s._evidenceRowShown = c.args[0] != 0;
		return false;
	});

	// Hooks only logged on purpose: the menu's Save and Load (B-063)
	static const char *const kLoggedMenuHooks[] = {"SaveGameState", "SaveKeyPress",
		"SaveLoadComit", "SaveLoadInit", "SaveSlotSelect"};
	for (uint i = 0; i < ARRAYSIZE(kLoggedMenuHooks); i++)
		t.addLogged(kLoggedMenuHooks[i]);
}

void Script::logHook(const Common::String &name, HookStatus status) {
	static const char *const kStatus[] = {"unknown", "logged", "handled"};
	// Level 2 and not log(): vcdump's sim has no hooks, and its logs stay as they are (tools/hook_coverage.py)
	debugC(2, kDebugScript, "hook %s %s", name.c_str(), kStatus[status]);
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

void Script::isIItemHeld(uint32 variableId, int32 item) {
	// 0x4b3850: 1 when the object the last mouse-down captured is the interface item
	_variables[variableId] = _held && _held == (uint32)item ? 1 : 0;
	Common::U32String name;
	_data.getVariableName(variableId, name);
	log(Common::String::format("      %s = %d", name.encode().c_str(), _variables[variableId]));
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
	// Action_DoAsset 0x47a4e0 drops a play (once or loop) of an asset that still has a sound object (0x491380),
	// until a stop or the next main-loop pass after its end removes it. Only where sound ends are known
	if (_soundEnds && (mode == kModePlayOnce || mode == kModePlayLoop) && isPlaying(id))
		return;
	// Without known sound ends nothing drops the play, so the stopped sound ends before it restarts
	if (!_soundEnds && (mode == kModePlayOnce || mode == kModePlayLoop) && Common::find(_pendingStops.begin(), _pendingStops.end(), id) != _pendingStops.end()) {
		_pendingStops.erase(Common::find(_pendingStops.begin(), _pendingStops.end(), id));
		endSound(id, "stopped");
		endClip(id, "stopped");
	}
	bool video = false;
	for (uint i = 0; i < a.files.size(); i++)
		video |= a.files[i].file.hasPrefix("XV:") || a.files[i].file.hasPrefix("XN:") || a.files[i].file.hasSuffixIgnoreCase(".nmv");
	Common::String file = a.files.empty() ? Common::String() : formatFile(a.files[0]);

	// XFiles.exe makes a sound object for every asset it plays (Action_DoAsset
	// 0x47a4e0) and treats one whose asset is an .xmv or .nmv (types 0x17 and
	// 0x16, 0x444d00) the same whatever its mode or range: its movie starts in
	// 0x488240, which raises the hold on views and on other sound objects'
	// play events (0x44f860), so these clips play one after another, and its
	// end or stop deactivates the sound and its asset object (0x43a970).
	// The .nmv clips (ranges of a view's navigation movie) are queued like
	// the .xmv ones: the media layer plays the file entry's time range
	if (video && (mode == kModePlayOnce || mode == kModePlayLoop)) {
		queueVideo(id, 0, mode == kModePlayLoop);
	} else if (mode == kModePlayOnce || mode == kModePlayLoop) {
		Common::String where = sound3D ? Common::String::format(" at (%d, %d)", sound3D->x, sound3D->y) : Common::String();
		log(Common::String::format("      %s #%u ", kModeNames[mode], id) + file + where + "  \"" + nameOrNone(_data, a.name) + "\"");
		// A sound (.amv, .mus, .wav) ends on its own; its end or a stop
		// deactivates its asset object too (0x43a970): see endSound()
		if (!a.files.empty() && a.files[0].file.hasPrefix("XS:"))
			_sounds[id] = true;
		if (!_clipsActivated)
			fire(id, kEventObjectActivation);
		// The sound object's event is held while a video plays (0x433000,
		// raised by 0x44f860), so the sound starts after the videos, in
		// order; its asset object is active at once (media2.log: 30399
		// 'asset.start' 42996, 'movie.start' 49706, after the video's end)
		HeldSound held = { id, mode, sound3D != nullptr, sound3D ? sound3D->x : 0, sound3D ? sound3D->y : 0 };
		if (!_videoQueue.empty())
			_heldSounds.push_back(held);
		else if (_hooks)
			_hooks->playAsset(held.id, held.mode, held.positional, held.x, held.y);
	} else if (mode == kModeStop) {
		log(Common::String::format("      stop #%u ", id) + file);
		for (uint i = _heldSounds.size(); i-- > 0; ) {
			if (_heldSounds[i].id == id)
				_heldSounds.remove_at(i);
		}
		// A sound object stays (isPlaying) until a later pass; its deactivation follows the event (flushStops).
		// Without one, the stop runs a shown view asset to its end, with no deactivation (0x504c90)
		bool hasObject = isPlaying(id);
		if (_hooks) {
			_hooks->playAsset(id, mode, false, 0, 0);
			if (!hasObject)
				_hooks->stopViewAsset(id);
		}
		if ((_sounds.contains(id) || _clips.contains(id)) && Common::find(_pendingStops.begin(), _pendingStops.end(), id) == _pendingStops.end())
			_pendingStops.push_back(id);
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

void Script::releaseHeldSounds() {
	Common::Array<HeldSound> held;
	held.swap(_heldSounds);
	for (uint i = 0; i < held.size(); i++) {
		if (_hooks)
			_hooks->playAsset(held[i].id, held[i].mode, held[i].positional, held[i].x, held[i].y);
	}
}

void Script::endSound(uint32 id, const char *how) {
	if (!_sounds.contains(id))
		return;
	_sounds.erase(id);
	log(Common::String::format("  << sound #%u %s", id, how));
	fire(id, kEventObjectDeactivation);
}

bool Script::isPlaying(uint32 id) const {
	return _sounds.contains(id) || _clips.contains(id) || Common::find(_videoQueue.begin(), _videoQueue.end(), id) != _videoQueue.end();
}

void Script::playAssetAt(uint32 asset, int x, int y) {
	// 0x491de0: starts the asset as a Do Asset action would, unless it is already playing (0x491380).
	if (isPlaying(asset))
		return;
	// An XG clip is drawn once with its top-left at (x, y) over the screen, without a hold; its end fires
	// the asset's Object Deactivation (ing-search run, B-071). Other assets play as a Do Asset action does
	VCAsset a;
	if (_data.getAsset(asset, a) && !a.files.empty() && a.files[0].file.hasPrefix("XG:"))
		startClip(a, x, y);
	else
		playAsset(asset, kModePlayOnce, nullptr);
}

void Script::startClip(const VCAsset &a, int x, int y) {
	log(Common::String::format("      %s #%u ", kModeNames[kModePlayOnce], a.id) + formatFile(a.files[0]) + "  \"" + nameOrNone(_data, a.name) + "\"");
	_clips[a.id] = true;
	fire(a.id, kEventObjectActivation);
	if (_hooks)
		_hooks->playClip(a.id, x, y);
}

void Script::endClip(uint32 id, const char *how) {
	if (!_clips.contains(id))
		return;
	_clips.erase(id);
	log(Common::String::format("  << clip #%u %s", id, how));
	fire(id, kEventObjectDeactivation);
}

void Script::stopEnvSounds() {
	// 0x4cc9c0 queues every playing asset without a picture for deactivation:
	// its Object Deactivation fires as at a stop (0x43a970). _sounds holds
	// the audio-only assets (XS); clips with a picture keep playing. The
	// players stop first, so a sound a trigger starts is not cut
	if (_hooks)
		_hooks->stopAllSounds();
	_heldSounds.clear();
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
	if (a.args.size() > kPrefArtificialIntuition && a.args[kPrefArtificialIntuition].kind == kOperandVariable)
		_prefArtificialIntuition = a.args[kPrefArtificialIntuition].value;
	for (uint index = 0; index < a.args.size() && index < kPrefCount; index++) {
		if (a.args[index].kind != kOperandVariable)
			continue;
		_prefVariables[index] = a.args[index].value;
		// What a Preferences screen hook stored (PrefHooks) or setPreference() set is the registry's value
		int32 value = 0;
		if (_preferences.contains(index))
			_variables[a.args[index].value] = _preferences[index];
		else if (_hooks && _hooks->queryPreference(index, value))
			_variables[a.args[index].value] = value;
	}
}

int32 Script::getPreferenceValue(uint index) const {
	if (_preferences.contains(index))
		return _preferences[index];
	return _prefVariables[index] ? getVariable(_prefVariables[index]) : 0;
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
				_hooks->callFunction(HookCall(a, args, -1, EventFields()));
			}
			return;
		}
	}
}

void Script::queueVideo(uint32 id, uint32 limit, bool loop) {
	VideoPlay play = {limit, _nextVideoEnd, loop, _fireSerial, false, _clipsActivated};
	_videoQueue.push_back(id);
	_videoPlays.push_back(play);
	if (_videoQueue.size() == 1) {
		startVideo();
	} else if (!play.activated) {
		// The exe activates a clip's asset object when it is queued, not when it starts
		_videoPlays.back().activated = true;
		fire(id, kEventObjectActivation);
	}
}

void Script::startVideo() {
	uint32 id = _videoQueue[0];
	VCAsset a;
	_data.getAsset(id, a);
	Common::String limit = _videoPlays[0].limit ? Common::String::format(" for %u ms", _videoPlays[0].limit) : Common::String();
	if (_videoPlays[0].loop)
		limit += " looped";
	log(Common::String::format("  >> video #%u ", id) + a.files[0].file + limit + "  \"" + nameOrNone(_data, a.name) + "\"");
	if (!_videoPlays[0].activated) {
		_videoPlays[0].activated = true;
		fire(id, kEventObjectActivation);
	}
}

void Script::endVideo(const char *how) {
	if (_videoQueue.empty()) {
		log("  (nothing playing)");
		return;
	}
	uint32 id = _videoQueue.remove_at(0);
	bool viewVideo = _videoPlays[0].serial == _viewSerial || _videoPlays[0].viewWait;
	_videoPlays.remove_at(0);
	log(Common::String::format("  << video #%u %s", id, how));
	bool next = !_videoQueue.empty();
	bool nextVideo = next && (_videoPlays[0].serial == _viewSerial || _videoPlays[0].viewWait);
	bool nextWait = next && _videoPlays[0].viewWait;
	// A held view comes at the stop of the last video it waits for, before
	// that video's Object Deactivation (its asset object is deactivated
	// after the view's activation); a video the view queues starts at once
	// if the queue was empty, otherwise after the one already queued. A video queued before the view was entered
	// that has not started yet comes first (its show event is older than the view's activation; box-click-kinds:
	// 33401 and 20361 of a topic, then a view entered when the clip before them ended)
	if (_viewHeld && !nextWait && (!nextVideo || !viewVideo))
		activateView();
	else if (_viewAssetHeld)
		activateViewAsset();
	_videoNextPending = next;
	fire(id, kEventObjectDeactivation);
	_videoNextPending = false;
	if (next)
		startVideo();
	else if (_videoQueue.empty()) {
		releaseHeldSounds();
		showView(); // Back to the view, with whatever the video's triggers changed
		// The box comes back: its idea icons get their objects again, before an idea topic's deactivation
		if (_box && !_boxShown) {
			_boxShown = true;
			activateIdeaIcons();
		}
		endIdeaTopic();
	}
}

void Script::endIdeaTopic() {
	// The clips of an idea's topic (PlayItemTopic) have ended and the box is shown again (0x436230): the topic's
	// object is deactivated, firing its Object Deactivation triggers (evidence1-to-skinnergone: 41642, which counts
	// iAllQuestionsAskedSkinner up).
	if (!_ideaTopic || !_videoQueue.empty() || !_clips.empty())
		return;
	uint32 topic = _ideaTopic;
	_ideaTopic = 0;
	fire(topic, kEventObjectDeactivation);
}

// -- Navigation ---------------------------------------------------------------

void Script::travel() {
	// 0x5068b0, 0x493090: the emotion and action icons are deactivated first (0x44d850; clears their enable
	// flags, idea icons keep theirs), then all four old objects, whatever the new path shares, then the new
	// four are activated. The exe also hides the inventory and AI icons and clears the letterbox tint; the
	// engine does not model those yet
	_travelPending = false;
	spendIcons(kClassEmotionIcon);
	spendIcons(kClassActionIcon);
	// A SwapDisc in the triggers of the travel waits for it to finish
	_fireDepth++;
	enter(_travelTo[0], _travelTo[1], _travelTo[2], _travelTo[3], true);
	_fireDepth--;
}

void Script::stopAllForTravel() {
	// 0x493090 deactivates every active sound object before it travels (script-flow.md "Every sound stops
	// before the travel", read from the exe only: no log has a sound still active at a SwapDisc). Each one
	// stops as at a Stop action, with its Object Deactivation, after the old path's and before the new
	// node's activation. It is no end: a playlist gets no "child ended", so queued topic clips are dropped
	// without a topic end or a box re-show (SoundObj_OnDeactivate 0x43a970)
	if (_hooks)
		_hooks->stopAllSounds();
	Common::Array<uint32> clips;
	for (Common::HashMap<uint32, bool>::const_iterator it = _clips.begin(); it != _clips.end(); ++it)
		clips.push_back(it->_key);
	Common::sort(clips.begin(), clips.end());
	for (uint i = 0; i < clips.size() && _hooks; i++)
		_hooks->playAsset(clips[i], kModeStop, false, 0, 0);
	Common::Array<uint32> sounds;
	for (Common::HashMap<uint32, bool>::const_iterator it = _sounds.begin(); it != _sounds.end(); ++it)
		sounds.push_back(it->_key);
	Common::sort(sounds.begin(), sounds.end());
	Common::Array<uint32> videos;
	videos.swap(_videoQueue);
	_videoPlays.clear();
	_ideaTopic = 0;
	_heldSounds.clear();
	_pendingStops.clear();
	for (uint i = 0; i < sounds.size(); i++)
		endSound(sounds[i], "stopped");
	for (uint i = 0; i < clips.size(); i++)
		endClip(clips[i], "stopped");
	for (uint i = 0; i < videos.size(); i++) {
		log(Common::String::format("  << video #%u stopped", videos[i]));
		fire(videos[i], kEventObjectDeactivation);
	}
}

void Script::enter(uint32 node, uint32 location, uint32 viewPoint, uint32 view, bool deactivateAll) {
	const uint32 newPath[4] = { node, location, viewPoint, view };
	const uint32 oldPath[4] = { _node, _location, _viewPoint, _view };
	// Only a nav click passes a view mode (0x46ae70); every other way into a view cuts
	_transition = _navTransition;
	_transitionCentre = _navTransitionCentre;
	_transitionEmpty = _navTransitionEmpty;
	_navTransition = kTransitionCut;
	int first = 0;
	while (!deactivateAll && first < 4 && oldPath[first] == newPath[first])
		first++;
	for (int i = 3; i >= first; i--) {
		// A view still held (below) was never activated: entering another
		// one drops it, as Action_DoSetView (0x46a950) cancels its activation
		// The shown asset's deactivation runs before the view's own triggers (0x458e10, B-112)
		if (i == 3) {
			deactivateViewAsset();
			// The view is being left: an asset enabled by the deactivations below is not started (0x46f210)
			_viewActive = false;
		}
		if (oldPath[i] && !(i == 3 && _viewHeld))
			fire(oldPath[i], kEventObjectDeactivation);
		if (i == 3)
			deactivateViewObjects();
	}
	if (first < 4) {
		_viewHeld = false;
		_viewActive = false;
		_viewAssetHeld = false;
	}
	_node = node;
	_location = location;
	_viewPoint = viewPoint;
	_view = view;
	_box = 0;
	log("  == enter " + getObjectLabel(_data, node) + " / " + getObjectLabel(_data, location) + " / " +
		getObjectLabel(_data, viewPoint) + " / " + getObjectLabel(_data, view));
	if (deactivateAll)
		stopAllForTravel();
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
			// It waits for the videos queued in this event, those queued
			// after it too (comityinn-to-warehouse: 20061 and 19937), but
			// not for one a later event queues (newgame-to-comityinn: 19802
			// and 19806 behind 19796) nor those queued before it that
			// already play
			_viewSerial = _fireSerial;
			for (uint i = 0; i < _videoPlays.size(); i++)
				_videoPlays[i].viewWait = (i > 0 || _videoNextPending) && _videoPlays[i].serial != _viewSerial;
			log("      view held until the video ends");
		}
	}
	showView();
}

void Script::activateView() {
	// The mark of the view's own activation: the exe logs it after the Activations of its node, location and viewpoint
	log("  == view activated " + getObjectLabel(_data, _view));
	_viewHeld = false;
	_viewActive = true;
	// The exe activates the view's asset before the view's own actions run (B-151)
	activateViewAsset();
	activateViewObjects(true);
	fire(_view, kEventObjectActivation);
	activateViewObjects(false);
}

void Script::activateViewObjects(bool before) {
	// XFiles.exe activates the asset it shows (activateViewAsset) and its
	// enabled character views (before = true) ahead of the view's own
	// Activation actions, then (after the transition) its enabled navs and
	// explorables (reference run, 2026-10-12); the view's activation
	// creates them (0x4402e0). B-151
	VCView v;
	if (!_data.getView(_view, v))
		return;
	Common::Array<uint32> ids;
	const uint32 lists[3] = { v.characters, v.navs, v.explorables };
	for (int i = before ? 0 : 1; i < (before ? 1 : 3); i++) {
		ids.clear();
		_data.getMembers(lists[i], ids);
		for (uint j = 0; j < ids.size(); j++) {
			if (isEnabled(ids[j]))
				activateViewObject(ids[j]);
		}
	}
}

void Script::deactivateViewAsset() {
	// Leaving a view queues the shown asset's Object Deactivation (0x458e10, from the view's removal event) after
	// the view's own Deactivation trigger is queued but before its actions run: the asset's comes first (reference
	// run manual1, 2026-10-13: view 1214's background 38994 disables the explorables its activation enabled)
	Common::Array<uint32> ids = _viewObjects;
	for (uint j = 0; j < ids.size(); j++) {
		if (_data.getClassOf(ids[j]) != kClassAssetRef)
			continue;
		Common::Array<uint32>::iterator it = Common::find(_viewObjects.begin(), _viewObjects.end(), ids[j]);
		if (it != _viewObjects.end())
			_viewObjects.erase(it);
		fire(ids[j], kEventObjectDeactivation);
	}
}

void Script::deactivateViewObjects() {
	// Then the view deactivates its navs, character views and explorables (0x438770)
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
	// A character view, once its Object Activation has run, plays the first
	// enabled asset of its asset list as a Do Asset action would (sound
	// object, then the clip): reference run newgame-to-comityinn, 21993
	// plays 19746 after trigger 56118, 22631 plays 19983 (and not 20031,
	// enabled after it). Those clips' Deactivation disables them, so each
	// plays once (B-092)
	VCCharView cv;
	// While a restore activates its list the character view's activation
	// (0x436f80) makes no player: GameState_Restoring is set, so the asset
	// stays enabled and unplayed
	if (_restoreMode >= 0 || !_data.getCharView(id, cv))
		return;
	Common::Array<uint32> assets;
	_data.getMembers(cv.assets, assets);
	for (uint i = 0; i < assets.size(); i++) {
		VCAsset a;
		if (!isEnabled(assets[i]) || !_data.getAsset(assets[i], a) || a.files.empty())
			continue;
		playAsset(assets[i], kModePlayOnce, nullptr);
		break;
	}
}

void Script::deactivateViewObject(uint32 id) {
	Common::Array<uint32>::iterator it = Common::find(_viewObjects.begin(), _viewObjects.end(), id);
	if (it == _viewObjects.end() || !isInView(id))
		return;
	_viewObjects.erase(it);
	fire(id, kEventObjectDeactivation);
}

void Script::updateViewAsset(uint32 id) {
	// 0x4494d0 (disable), 0x47a260 (enable): for an asset of the current
	// view's list, unless a view change is pending (0x46f210), the view
	// object is made again: its old asset is deactivated at once (event kind
	// 12), the first enabled one activated by kind 11, which a video holds
	// (exe-map.md "Held events"). Crime Lab view 10788: Amis's greeting 71783
	// disables the clip 33974, whose Deactivation stops timer 0 (B-052).
	// Only a change of the shown asset is modelled
	VCView v;
	if (!_viewActive || _viewHeld || _travelPending || !_data.getView(_view, v))
		return;
	Common::Array<uint32> ids;
	_data.getMembers(v.assets, ids);
	if (Common::find(ids.begin(), ids.end(), id) == ids.end())
		return;
	uint32 background;
	getViewAssets(ids, background);
	uint32 shown = ids.empty() ? 0 : ids[0];
	for (uint i = 0; i < _viewObjects.size(); i++) {
		if (_data.getClassOf(_viewObjects[i]) != kClassAssetRef)
			continue;
		uint32 old = _viewObjects[i];
		if (old == shown)
			return;
		_viewObjects.remove_at(i);
		fire(old, kEventObjectDeactivation);
		break;
	}
	_viewAssetHeld = !_videoQueue.empty();
	if (!_viewAssetHeld)
		activateViewAsset();
}

void Script::activateViewAsset() {
	_viewAssetHeld = false;
	Common::Array<uint32> ids;
	uint32 background;
	getViewAssets(ids, background);
	if (ids.empty() || Common::find(_viewObjects.begin(), _viewObjects.end(), ids[0]) != _viewObjects.end())
		return;
	_viewObjects.push_back(ids[0]);
	logViewAsset(ids[0], background);
	fire(ids[0], kEventObjectActivation);
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

void Script::logViewAssets() {
	Common::Array<uint32> ids;
	uint32 background;
	getViewAssets(ids, background);
	for (uint i = 0; i < ids.size(); i++)
		logViewAsset(ids[i], background);
}

void Script::logViewAsset(uint32 id, uint32 background) {
	VCAsset asset;
	_data.getAsset(id, asset);
	log(Common::String::format("      %s #%u ", id == background ? "background" : "overlay", id) +
		formatFile(asset.files[0]) + "  \"" + nameOrNone(_data, asset.name) + "\"");
}

void Script::showView() {
	VCView v;
	if (!_data.getView(_view, v))
		return;

	logViewAssets();

	Common::Array<uint32> ids;
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
	_hintGlow = save.hintGlow;
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
}

void Script::click(uint32 id) {
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
	_boxShown = true;
	Common::Array<uint32> topics;
	getBoxTopics(charViewId, topics);
	for (uint i = 0; i < topics.size(); i++) {
		if (isEnabled(topics[i]))
			activateTopic(topics[i]);
	}
	activateIdeaIcons();
}

bool Script::getBoxIdeaMaps(Common::Array<uint32> &maps) const {
	VCCharView charView;
	return _box && _data.getCharView(_box, charView) && charView.ideaMaps && _data.getMembers(charView.ideaMaps, maps);
}

void Script::activateIdeaIcons() {
	// 0x435d90 -> 0x460360: at each show of the box, a new object for every enabled idea icon of the character
	// view in map order, whose Object Activation fires (21975's 90677 sets bN2WongIdeaIconActive). The hide only
	// deactivates them, and no idea icon has an Object Deactivation trigger
	Common::Array<uint32> maps;
	if (!getBoxIdeaMaps(maps))
		return;
	for (uint i = 0; i < maps.size(); i++) {
		VCIdeaMap map;
		if (_data.getIdeaMap(maps[i], map) && isEnabled(map.icon))
			fire(map.icon, kEventObjectActivation);
	}
}

void Script::getBoxIdeaIcons(Common::Array<uint32> &icons) const {
	// The icons of the shown box's character are drawn as any icon in pane 0 (0x4d17f0, 0x459730, 0x50ec90)
	icons.clear();
	Common::Array<uint32> maps;
	if (!_boxShown || !getBoxIdeaMaps(maps))
		return;
	for (uint i = 0; i < maps.size(); i++) {
		VCIdeaMap map;
		if (_data.getIdeaMap(maps[i], map) && isEnabled(map.icon))
			icons.push_back(map.icon);
	}
}

bool Script::isIdeaIconOfBox(uint32 iconId) const {
	Common::Array<uint32> maps;
	if (!getBoxIdeaMaps(maps))
		return false;
	for (uint i = 0; i < maps.size(); i++) {
		VCIdeaMap map;
		if (_data.getIdeaMap(maps[i], map) && map.icon == iconId)
			return true;
	}
	return false;
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
	_layouts.push_back(shown);

	// The exe appends the objects in layout order to a list kept sorted by
	// layer (low byte of the layer word): an item goes to the end if its layer
	// is not below the last one's, else before the first item of an equal or
	// higher layer. That reproduces all 15 layout shows of the runs
	// pda-usage-run1 and manual1 (layer 0 ties, layout 34010, the save screen).
	// The list also gives the order of the unshow's Object Deactivation.
	for (uint i = 0; i < layout.items.size(); i++) {
		if (isEnabled(layout.items[i]))
			insertLayoutItem(_layouts.back().items, layout.items[i]);
	}
	Common::Array<uint32> order = _layouts.back().items;
	for (uint i = 0; i < order.size(); i++)
		fire(order[i], kEventObjectActivation);
}

void Script::insertLayoutItem(Common::Array<uint32> &items, uint32 itemId) {
	// The layer-sorted insertion rule of showLayout
	VCInterfaceItem item;
	byte layer = _data.getInterfaceItem(itemId, item) ? item.layer & 0xff : 0;
	uint at = items.size();
	VCInterfaceItem other;
	if (at && _data.getInterfaceItem(items[at - 1], other) && (other.layer & 0xff) > layer) {
		at = 0;
		while (at < items.size() && _data.getInterfaceItem(items[at], other) && (other.layer & 0xff) < layer)
			at++;
	}
	items.insert_at(at, itemId);
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
			_itemOrigins.erase(items[j]);
			if (items[j] == _focus)
				_focus = 0;
		}
		// Code run when some layouts close (0x41f8a5)
		if (layoutId == kPhoneLayout)
			_phone.closed();
		return;
	}
}

void Script::enableTarget(uint32 target, bool value) {
	// An Enable action (kActionEnable), or code that does the same (0x4083d0)
	bool was = isEnabled(target);
	_enabled[target] = value;
	// The held item disabled goes back (0x436c20)
	if (!value && _heldCursorItem && _heldCursorItem == target)
		putBack();
	// An evidence icon gets its object when it is enabled (0x4201a0),
	// firing its Object Activation; disabling removes its kit icon too
	// (0x4a1940)
	if (_data.getClassOf(target) == kClassEvidenceIcon) {
		Common::Array<uint32>::iterator it = Common::find(_kit.begin(), _kit.end(), target);
		if (value && !was)
			fire(target, kEventObjectActivation);
		else if (!value && it != _kit.end())
			_kit.erase(it);
		// The group 0 object joins the evidence list, or goes
		if (value)
			addEvidence(target);
		else
			removeEvidence(target);
	}
	// An inventory icon's object joins the bar at the end, or goes
	// (0x4201a0); the bar's order is what a save keeps. With the bar
	// hidden it joins later (0x4a1fb0), when the engine says
	if (_data.getClassOf(target) == kClassInventory) {
		Common::Array<uint32>::iterator it = Common::find(_inventory.begin(), _inventory.end(), target);
		if (value && it == _inventory.end() && !(_hooks && _hooks->deferInventoryIcon(target))) {
			_inventory.push_back(target);
			// The icon object's activation runs the item's Object Activation (0x440f80)
			fire(target, kEventObjectActivation);
		}
		else if (!value && it != _inventory.end())
			_inventory.erase(it);
	}
	// An idea icon enabled while the box is shown, with the icon in its character's map, gets an object (0x4a1420)
	if (value && !was && _boxShown && _data.getClassOf(target) == kClassIdeaIcon && isIdeaIconOfBox(target))
		fire(target, kEventObjectActivation);
	// A topic enabled while its box is open joins the box (0x476340)
	if (value && _box && isBoxTopic(_box, target))
		activateTopic(target);
	// A character view, nav or explorable of the view comes and goes
	// with its flag (0x492170, 0x46de10, 0x46efc0; 0x47ff40)
	if (value && !was)
		activateViewObject(target);
	else if (!value && was)
		deactivateViewObject(target);
	if ((value) != was && _data.getClassOf(target) == kClassAssetRef)
		updateViewAsset(target);
	updateLayoutItem(target, value);
}

void Script::addInventoryIcon(uint32 id) {
	// The icon object activates and is appended to the bar's list
	// (List_Append_459070): the save's order is the order of these
	if (!isEnabled(id) || _data.getClassOf(id) != kClassInventory)
		return;
	Common::Array<uint32>::iterator it = Common::find(_inventory.begin(), _inventory.end(), id);
	bool joins = it == _inventory.end();
	if (!joins)
		_inventory.erase(it);
	_inventory.push_back(id);
	// Only a new icon activates its item (0x440f80)
	if (joins)
		fire(id, kEventObjectActivation);
}

void Script::addUberParanoia(int32 n) {
	if (_uberParanoia)
		_variables[_uberParanoia] = getVariable(_uberParanoia) + n;
}

void Script::setItemEnabled(uint32 item, bool enabled) {
	_enabled[item] = enabled;
	updateLayoutItem(item, enabled);
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
			// Into the layer-sorted list, as at the show (fbi-login-wrong: 67487 before 67511)
			insertLayoutItem(items, itemId);
			fire(itemId, kEventObjectActivation);
		} else if (!enabled && it != items.end()) {
			items.erase(it);
			fire(itemId, kEventObjectDeactivation);
		}
	}
}

void Script::queueShowItem(uint32 itemId, bool shown) {
	// Queue_Show on an item's object: it leaves or joins the shown layouts' lists, but its VCEnabled flag stays and no
	// Object Activation or Deactivation runs (NoteFlipDay 0x4e6a70)
	for (uint i = 0; i < _layouts.size(); i++) {
		VCLayout layout;
		if (!_data.getLayout(_layouts[i].id, layout) ||
				Common::find(layout.items.begin(), layout.items.end(), itemId) == layout.items.end())
			continue;
		Common::Array<uint32> &items = _layouts[i].items;
		Common::Array<uint32>::iterator it = Common::find(items.begin(), items.end(), itemId);
		if (shown && it == items.end() && isEnabled(itemId))
			insertLayoutItem(items, itemId);
		else if (!shown && it != items.end())
			items.erase(it);
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
	// Only a topic with +0x40 set is recorded; one without stays on the Talk
	// tab and plays as a first ask every time
	VCConversation c;
	if (_data.getConversation(conversationId, c) && c.recorded)
		recordTopic(conversationId);
	// The playlist and the triggers are one event (see _viewSerial)
	_boxShown = false; // the box is hidden while the topic plays
	_fireSerial++;
	_inEvent = true;
	playTopic(conversationId, true);
	fire(conversationId, kEventMouseClick);
	_inEvent = false;
	flushStops();
}

bool Script::findIdeaTopic(uint32 charViewId, uint32 iconId, uint32 &conversation) const {
	// CharView_HasIdea 0x49bce0 / CharView_FindIdeaTopic 0x4c5870: the maps of the view's VCIdeaMapList whose icon
	// is enabled; iconId 0 takes any
	VCCharView charView;
	Common::Array<uint32> maps;
	if (!_data.getCharView(charViewId, charView) || !charView.ideaMaps || !_data.getMembers(charView.ideaMaps, maps))
		return false;
	for (uint i = 0; i < maps.size(); i++) {
		VCIdeaMap map;
		if (!_data.getIdeaMap(maps[i], map) || (iconId && map.icon != iconId) || !isEnabled(map.icon))
			continue;
		conversation = map.conversation;
		return true;
	}
	return false;
}

bool Script::hasEnabledIdea(uint32 charViewId) const {
	uint32 conversation;
	return findIdeaTopic(charViewId, 0, conversation);
}

void Script::idea(uint32 iconId) {
	// IdeaIcon_DragEnd 0x4d15f0 -> PlayItemTopic 0x4c5500: asked (History) or count + 1, the topic's Object
	// Activation and its playlist with +0x144 clear; no Mouse Click and no Drop triggers. The box comes back
	// after the clips, so it stays open here.
	uint32 conversation;
	if (!_box || !findIdeaTopic(_box, iconId, conversation) || !conversation)
		return;
	// Recorded whatever +0x40 holds (PlayItemTopic does not test it)
	recordTopic(conversation);
	activateTopic(conversation);
	_boxShown = false; // 0x49c06f hid the box
	playTopic(conversation, false);
	// The box comes back when the clips end
	_ideaTopic = conversation;
	if (_videoQueue.empty() && _clips.empty()) {
		_boxShown = true;
		activateIdeaIcons();
	}
	endIdeaTopic();
}

void Script::recordTopic(uint32 conversationId) {
	// Not asked yet: into the game state's history (0x41aa80), a set the
	// save writes sorted by ID; else its count + 1 (0x4667a0)
	if (_asked[conversationId]++)
		return;
	uint i = 0;
	while (i < _history.size() && _history[i] < conversationId)
		i++;
	_history.insert_at(i, conversationId);
}

void Script::playTopic(uint32 conversationId, bool inBox) {
	// The topic's playlist merges adjacent ranges of one file before any of
	// its sounds is activated (Playlist_MergeRanges 0x4f1930): collect the
	// clips, merge, then play
	_planning = true;
	_topicClips.clear();
	playTopicClips(conversationId, inBox);
	_planning = false;
	mergeTopicClips();
}

void Script::mergeTopicClips() {
	Common::Array<TopicClip> clips;
	clips.swap(_topicClips);
	Common::Array<TopicClip> list;
	for (uint i = 0; i < clips.size(); i++) {
		TopicClip &s = clips[i];
		VCAsset a;
		if (!s.limit && i + 1 < clips.size() && !clips[i + 1].limit && _data.getAsset(s.id, a) && a.hasRange && !a.files.empty()) {
			VCAsset b;
			if (_data.getAsset(clips[i + 1].id, b) && b.hasRange && !b.files.empty() && a.files[0].file == b.files[0].file &&
				b.rangeIn > a.rangeOut && b.rangeIn - a.rangeOut < 5) {
				s.end = b.files[0].end;
				i++; // The merged sound is dropped and stepped over
			}
		}
		list.push_back(s);
	}
	// The exe activates every clip's asset object (Object Activation triggers and their actions) before the first
	// clip plays; the clips whose asset has no PC file are included
	for (uint i = 0; i < list.size(); i++)
		fire(list[i].id, kEventObjectActivation);
	_clipsActivated = true;
	for (uint i = 0; i < list.size(); i++) {
		const TopicClip &s = list[i];
		_nextVideoEnd = s.end;
		if (s.limit)
			queueVideo(s.id, s.limit, false);
		else
			playAsset(s.id, kModePlayOnce, nullptr);
		_nextVideoEnd = 0;
	}
	_clipsActivated = false;
}

void Script::playTopicClips(uint32 conversationId, bool inBox) {
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
	// Counted by recordTopic(); 0 for a topic that is never recorded
	uint count = _asked.getValOrDefault(conversationId, 0);
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
	if (_planning) {
		TopicClip clip = { id, 750, 0 };
		_topicClips.push_back(clip);
		return;
	}
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
		playClip(asset);
}

void Script::playClip(uint32 assetId) {
	if (_planning) {
		TopicClip clip = { assetId, 0, 0 };
		_topicClips.push_back(clip);
		return;
	}
	playAsset(assetId, kModePlayOnce, nullptr);
}

void Script::playStock(uint32 assetId) {
	if (assetId && isEnabled(assetId))
		playClip(assetId);
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
		return getCharViewAtoms(charView, atoms);
	}
	return false;
}

bool Script::getCharViewAtoms(const VCCharView &charView, uint32 (&atoms)[kStockAtomCount]) const {
	Common::Array<uint32> topics;
	VCConversation first;
	Common::Array<uint32> assets;
	if (!_data.getMembers(charView.conversations, topics) || topics.empty() || !_data.getConversation(topics[0], first) ||
			first.label || !_data.getMembers(first.atom, assets))
		return false;
	for (uint j = 0; j < kStockAtomCount; j++) {
		VCAsset asset;
		atoms[j] = j < assets.size() && _data.getAsset(assets[j], asset) && !asset.files.empty() ? assets[j] : 0;
	}
	return true;
}

bool Script::getReactionSlots(uint32 charViewId, uint32 (&slots)[kReactionSlots]) const {
	for (uint i = 0; i < kReactionSlots; i++)
		slots[i] = 0;
	VCCharView charView;
	uint32 atoms[kStockAtomCount];
	if (!_data.getCharView(charViewId, charView) || !getCharViewAtoms(charView, atoms))
		return false;
	for (uint i = 0; i < kStockAtomCount; i++)
		slots[i] = atoms[i];
	slots[7] = 19656; // prisonA.mov, an end game
	slots[8] = 19654; // Out of FBIA.mov, an end game
	return true;
}

uint32 Script::getHeldReaction(uint32 charViewId) {
	uint32 slots[kReactionSlots];
	if (!isHolding() || !getReactionSlots(charViewId, slots))
		return 0;
	// 0x490d50
	int slot = kReactionNone;
	switch (getVariable(_heldVariable)) {
	case 1:
	case 4:
	case 6:
	case 7:
	case 11:
	case 12:
	case 21:
	case 22:
	case 25:
	case 26:
	case 30:
	case 31:
		slot = 4;
		break;
	case 5: // the cattle prod
		if (slots[3] && slots[5]) {
			Dice dice = newDice(0x490d50);
			slot = roll(dice, 1, 2) == 1 ? 3 : 5;
		} else {
			slot = slots[5] ? 5 : 3;
		}
		break;
	case 16:
	case 17:
	case 18:
	case 19:
	case 20:
		slot = 6;
		break;
	case 9:
	case 23:
		slot = 7;
		break;
	case 10:
		slot = 8;
		break;
	default:
		break;
	}
	return slot == kReactionNone ? 0 : slots[slot];
}

bool Script::hasHeldReactionSlot(uint32 charViewId) const {
	uint32 slots[kReactionSlots];
	if (!isHolding() || !getReactionSlots(charViewId, slots))
		return false;
	// The items whose slot at 0x490d50 is not 10 (none)
	int32 item = getVariable(_heldVariable);
	switch (item) {
	case 1:
	case 4:
	case 5:
	case 6:
	case 7:
	case 9:
	case 10:
	case 11:
	case 12:
	case 16:
	case 17:
	case 18:
	case 19:
	case 20:
	case 21:
	case 22:
	case 23:
	case 25:
	case 26:
	case 30:
	case 31:
		return true;
	default:
		return false;
	}
}

bool Script::isSpecialView() const {
	static const int32 kViews[] = { 1, 1228, 1236, 7878, 7879, 19684, 19688, 19692, 23486, 70706, 70710, 70712, 71914, 84307 };
	if (!_viewSpecialVariable)
		return false;
	int32 value = getVariable(_viewSpecialVariable);
	for (uint i = 0; i < ARRAYSIZE(kViews); i++) {
		if (kViews[i] == value)
			return true;
	}
	return false;
}

void Script::toolReaction(int32 item) {
	// 0x4e2160: S2 for the binoculars and the flashlight, S3 for night vision
	uint slot = 0;
	if (item == 2 || item == 8)
		slot = 2;
	else if (item == 13)
		slot = 3;
	VCView view;
	Common::Array<uint32> charViews;
	if (!slot || !_data.getView(_view, view) || !_data.getMembers(view.characters, charViews))
		return;
	for (uint i = 0; i < charViews.size(); i++) {
		uint32 slots[kReactionSlots];
		if (!isEnabled(charViews[i]) || !getReactionSlots(charViews[i], slots) || !slots[slot])
			continue;
		// A one-shot delay of 2000 ms (0x483f90); without an engine the sim plays it at once
		if (!_hooks || !_hooks->delayPlay(slots[slot], 2000))
			playStock(slots[slot]);
		return;
	}
}

bool Script::runCameraActions(uint32 id) {
	bool ran = false;
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
			bool tested = c.lhs.kind == kOperandVariable && c.lhs.value == _heldVariable;
			tested = tested || (c.rhs.kind == kOperandVariable && c.rhs.value == _heldVariable);
			LocalVariables locals;
			if (tested && test(c, EventFields(), locals)) {
				execute(a, kEventMouseClick, EventFields(), locals);
				ran = true;
			}
		}
	}
	return ran;
}

void Script::cameraClick(const Common::Point &pos) {
	if (!isCameraEnabled())
		return;
	// 0x4f3090: the shutter, as a movie of its own (no sound object, no play line)
	bool full = _photosOnCamera >= kCameraPhotos;
	uint32 shutter = full ? kCameraShutterFull : kCameraShutter;
	log(Common::String::format("      camera: shutter #%u", shutter));
	if (_hooks)
		_hooks->playShutter(shutter);

	// Camera_TakePhoto (0x4f31e0): the video playing in the band, else the view's asset; the count goes up
	// whether or not a photo was made
	if (!full) {
		uint32 asset = 0;
		if (!_videoQueue.empty()) {
			asset = _videoQueue[0];
		} else {
			Common::Array<uint32> assets;
			uint32 background;
			getViewAssets(assets, background);
			if (!assets.empty())
				asset = assets[0];
		}
		VCAsset data;
		if (asset && _data.getAsset(asset, data) && !data.files.empty()) {
			VCPhoto photo;
			photo.asset = asset;
			photo.clipTime = _hooks ? _hooks->getPhotoTime() : 0;
			// The viewfinder, 320 wide, moved back into the 600 wide band; top 0, bottom 240
			int left = pos.x - kCameraWidth / 2;
			if (left < 0)
				left = 0;
			if (left + kCameraWidth > kBandWidth)
				left = kBandWidth - kCameraWidth;
			photo.crop[0] = left;
			photo.crop[1] = 0;
			photo.crop[2] = left + kCameraWidth;
			photo.crop[3] = kBandHeight;
			photo.disc = _disc;
			_photos.push_back(photo);
		}
		_photosOnCamera++;
		log(Common::String::format("      camera: photos %d", _photosOnCamera));
	}

	// The objects under the viewfinder (320 wide at the click, in the band): the character views of the view, then
	// the explorables; the first of each kind that had a camera action to run ends its loop
	int vLeft = MAX<int>(pos.x - kCameraWidth / 2, kBandLeft);
	int vRight = MIN<int>(pos.x + kCameraWidth / 2, kBandLeft + kBandWidth);
	VCView view;
	if (!_data.getView(_view, view))
		return;
	for (int kind = 0; kind < 2; kind++) {
		Common::Array<uint32> ids;
		_data.getMembers(kind == 0 ? view.characters : view.explorables, ids);
		for (uint i = 0; i < ids.size(); i++) {
			VCRect r;
			if (!isEnabled(ids[i]))
				continue;
			if (kind == 0) {
				VCCharView cv;
				if (!_data.getCharView(ids[i], cv))
					continue;
				r = cv.rect;
			} else {
				VCExplorable e;
				if (!_data.getExplorable(ids[i], e))
					continue;
				r = e.rect;
			}
			if (r.left >= vRight || r.right <= vLeft || r.top >= kBandTop + kBandHeight || r.bottom <= kBandTop)
				continue;
			if (runCameraActions(ids[i]))
				break;
		}
	}

	// The first character view with a reaction clip in slot 2 not already playing speaks at once
	Common::Array<uint32> charViews;
	_data.getMembers(view.characters, charViews);
	for (uint i = 0; i < charViews.size(); i++) {
		uint32 slots[kReactionSlots];
		if (!isEnabled(charViews[i]) || !getReactionSlots(charViews[i], slots) || !slots[2])
			continue;
		if (_sounds.contains(slots[2]) && _sounds[slots[2]])
			continue;
		playStock(slots[2]);
		break;
	}
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
	setNavTransition(nav);
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

void Script::setNavTransition(const VCNav &nav) {
	// 0x46ae70: the view mode comes from the cursor of the nav's hotspot, 284 Left, 285 Right, 286 Forward (the
	// VCDefaultHotSpots' cursors); any other cursor cuts. The wipe opens from the middle of the hotspot's rectangle
	_navTransition = kTransitionCut;
	_navTransitionEmpty = true;
	VCHotSpot hotSpot;
	uint32 defaults[3];
	if (!_data.getHotSpot(nav.hotSpot, hotSpot) || !_data.getDefaultHotSpots(defaults))
		return;
	for (int i = 0; i < 3; i++) {
		VCHotSpot standard;
		if (_data.getHotSpot(defaults[i], standard) && standard.cursor == hotSpot.cursor) {
			_navTransition = kTransitionLeft + i;
			break;
		}
	}
	_navTransitionCentre = Common::Point((hotSpot.rect.left + hotSpot.rect.right) / 2, (hotSpot.rect.top + hotSpot.rect.bottom) / 2);
	_navTransitionEmpty = hotSpot.rect.left >= hotSpot.rect.right || hotSpot.rect.top >= hotSpot.rect.bottom;
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
	else if (kind == "click") {
		click(id);
		// A character view clicked with nothing in hand opens its box, as
		// XFilesEngine::openDialogBox does: the topics activate now, not
		// at the first ask
		VCCharView charView;
		if (_clickOpensBox && _data.getCharView(id, charView) && (charView.conversations || hasEnabledIdea(id)))
			openBox(id);
	} else if (kind == "photo") {
		// the camera tool clicked in the band at x,y
		size_t comma = arg.findFirstOf(',');
		cameraClick(Common::Point(id, comma == Common::String::npos ? 0 : atoi(arg.substr(comma + 1).c_str())));
	} else if (kind == "press")
		press(id);
	else if (kind == "release")
		release();
	else if (kind == "putback")
		putBack(); // the mouse moved into the bar while holding (InvBar_OnRoll 0x468330)
	else if (kind == "dclick")
		doubleClick(id);
	else if (kind == "rollin" || kind == "rollout")
		roll(id, kind == "rollin");
	else if (kind == "timer")
		expireTimer(id, true); // pda-usage-run1: expiries queued before a stop still run
	else if (kind == "ask")
		ask(id);
	else if (kind == "idea")
		idea(id);
	else if (kind == "goto")
		goTo(id);
	else if (kind == "nav")
		followNav(id);
	else if (kind == "seed")
		setFixedSeeds(id);
	else if (kind == "enable" || kind == "disable")
		enableTarget(id, kind == "enable"); // an Enable or Disable action on the object, as if a trigger ran it
	else if (kind == "do") {
		// one action by ID, run alone as a trigger's action list would (the condition applies)
		Common::Array<uint32> actions;
		actions.push_back(id);
		runActions(actions, kEventMouseClick, EventFields());
	} else if (kind == "key")
		keyPress(id);
	else if (kind == "layout")
		showLayout(id); // shown from exe code, not by a Show Layout action
	else if (kind == "end")
		finishSound(id);
	else if (kind == "icon")
		addInventoryIcon(id); // the exe log's late activation of the icon (the hidden bar's 500 ms, 0x4a1fb0)
	else if (kind == "media")
		_soundEnds = true; // the step list has the sound ends (steps.py `end:`): later plays of a playing asset are dropped
}

} // End of namespace XFiles
