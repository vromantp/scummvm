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

Script::Script(const VCData &data) : _data(data), _hooks(nullptr), _timerSerial(0), _random(1), _box(0), _node(0), _location(0), _viewPoint(0), _view(0) {
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
			_enabled[target] = a.value != 0;
			// A topic enabled while its box is open joins the box (0x476340)
			if (a.value && _box && isBoxTopic(_box, target))
				fire(target, kEventObjectActivation);
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
			log(Common::String::format("      timer %u started (%u ms%s)", a.timer, a.ms, a.periodic ? ", periodic" : ""));
		} else {
			_timers.erase(a.timer);
		}
		break;
	case kActionSetView:
		enter(a.path[1], a.path[2], a.path[3], a.path[0]);
		break;
	case kActionInterface:
		log(Common::String(a.mode ? "      show" : "      hide") + " interface " + getObjectLabel(_data, a.layout));
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
	} else if (a.function == "DoActionList" && args.size() >= 2) {
		Common::Array<uint32> actions;
		_data.getMembers(args[1], actions);
		runActions(actions, event, fields);
	} else {
		log("      call " + describeAction(_data, a.id, event, false));
		if (_hooks)
			_hooks->callFunction(a, args);
	}
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

	if (mode == kModePlayOnce && video && !a.hasRange) {
		_videoQueue.push_back(id);
		if (_videoQueue.size() == 1)
			startVideo();
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
	}
}

void Script::startVideo() {
	uint32 id = _videoQueue[0];
	VCAsset a;
	_data.getAsset(id, a);
	log(Common::String::format("  >> video #%u ", id) + a.files[0].file + "  \"" + nameOrNone(_data, a.name) + "\"");
	fire(id, kEventObjectActivation);
}

void Script::finishVideo() {
	if (_videoQueue.empty()) {
		log("  (nothing playing)");
		return;
	}
	uint32 id = _videoQueue.remove_at(0);
	log(Common::String::format("  << video #%u ended", id));
	fire(id, kEventObjectDeactivation);
	if (!_videoQueue.empty())
		startVideo();
	else
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
		if (oldPath[i])
			fire(oldPath[i], kEventObjectDeactivation);
	}
	_node = node;
	_location = location;
	_viewPoint = viewPoint;
	_view = view;
	_box = 0;
	log("  == enter " + getObjectLabel(_data, node) + " / " + getObjectLabel(_data, location) + " / " +
		getObjectLabel(_data, viewPoint) + " / " + getObjectLabel(_data, view));
	for (int i = first; i < 4; i++)
		fire(newPath[i], kEventObjectActivation);
	showView();
}

void Script::getViewAssets(Common::Array<uint32> &assets, uint32 &background) const {
	assets.clear();
	background = 0;
	VCView v;
	Common::Array<uint32> ids;
	if (!_data.getView(_view, v))
		return;
	_data.getMembers(v.assets, ids);
	for (uint i = 0; i < ids.size(); i++) {
		VCAsset asset;
		if (!_data.getAsset(ids[i], asset) || !isEnabled(ids[i]) || asset.files.empty())
			continue;
		if (asset.flags & kAssetStill) {
			// The first enabled still is the background, later ones are fallbacks
			if (background)
				continue;
			background = ids[i];
		}
		assets.push_back(ids[i]);
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
	if (!isEnabled(id))
		log("  " + getObjectLabel(_data, id) + " is disabled");
	else
		fire(id, kEventMouseClick);
}

void Script::expireTimer(uint n) {
	if (!_timers.contains(n))
		return;
	_timers.erase(n);
	VCTitle title;
	_data.getTitle(title);
	const uint32 ids[5] = { _view, _viewPoint, _location, _node, title.id };
	EventFields fields;
	fields.push_back(n);
	for (int i = 0; i < 5; i++)
		fire(ids[i], kEventTimerExpiration, fields);
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
			fire(topics[i], kEventObjectActivation);
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
	VCConversation c;
	if (!_data.getConversation(conversationId, c)) {
		warning("Script: #%u is not a conversation", conversationId);
		return;
	}
	uint count = ++_asked[conversationId];
	playList(c.question);
	if (count < 2) {
		playList(c.answer);
		return;
	}
	uint32 stock[kStockAtomCount];
	if (!getStockAtoms(conversationId, stock)) {
		playList(c.atom);
		playList(c.answer);
		return;
	}
	// 0x4e9880: from the box S0, sometimes followed by "What's the matter
	// with you?"; otherwise that alone
	if (count >= 4 && stock[0]) {
		int limit = count == 4 ? 6 : count == 5 ? 7 : count == 6 ? 8 : 9;
		if (roll() < limit) {
			if (inBox) {
				playStock(stock[0]);
				if (stock[kStockWhatsTheMatter] && roll() < 4)
					playStock(stock[kStockWhatsTheMatter]);
				return;
			}
			if (stock[kStockWhatsTheMatter]) {
				playStock(stock[kStockWhatsTheMatter]);
				return;
			}
		}
	}
	if (c.atom && roll() < 9) {
		playList(c.atom);
		if (c.answer && roll() < 4)
			playList(c.answer);
		return;
	}
	// 0x495820: from the box S0 or S1, otherwise S1
	if ((stock[0] || stock[1]) && roll() < 4) {
		if (!inBox)
			playStock(stock[1]);
		else if (stock[0] && stock[1])
			playStock(roll() < 6 ? stock[0] : stock[1]);
		else
			playStock(stock[0] ? stock[0] : stock[1]);
		if (stock[kStockWhatsTheMatter] && count > 2 && roll() < 4)
			playStock(stock[kStockWhatsTheMatter]);
	}
	playList(c.answer);
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

int Script::roll() {
	// XFiles.exe 0x468df0; the exe seeds it from the clock
	_random = _random * 0x41C64E6D + 0x3039;
	return _random % 10 + 1;
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
	if (kind == "wait")
		finishVideo();
	else if (kind == "click")
		click(id);
	else if (kind == "timer")
		expireTimer(id);
	else if (kind == "ask")
		ask(id);
	else if (kind == "goto")
		goTo(id);
	else if (kind == "nav")
		followNav(id);
}

} // End of namespace XFiles
