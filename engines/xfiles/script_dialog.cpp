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
#include "common/textconsole.h"

#include "xfiles/detection.h"
#include "xfiles/script.h"
#include "xfiles/script_internal.h"
#include "xfiles/vcdump.h"

namespace XFiles {

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
		} else if (!value && it != _inventory.end())
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

bool Script::hasEnabledConversation(uint32 charViewId) const {
	// 0x435a90: some conversation of the view's list is enabled (0x417eb0 with 6)
	Common::Array<uint32> topics;
	getBoxTopics(charViewId, topics);
	for (uint i = 0; i < topics.size(); i++) {
		if (isEnabled(topics[i]))
			return true;
	}
	return false;
}

bool Script::hasTopicOrIdea(uint32 charViewId) const {
	// What CharView_ShowCursor (0x418520) and CharView_OnMouseClick (0x436080) test: an enabled topic or idea icon
	return hasEnabledConversation(charViewId) || hasEnabledIdea(charViewId);
}

uint32 Script::getTeleportTarget(uint32 charViewId) const {
	// 0x407cd0 (from CharView_ShowCursor): the object's CharTeleport action gives the target view (argument 5); the
	// same lookup as the click (0x4c61b0) finds the character view there, kept only with a topic or idea (0x435a90)
	VCCharView here;
	if (!_data.getCharView(charViewId, here))
		return 0;
	Common::Array<uint32> triggers;
	_data.getMembers(getTriggers(charViewId), triggers);
	for (uint i = 0; i < triggers.size(); i++) {
		VCTrigger t;
		if (!_data.getTrigger(triggers[i], t))
			continue;
		for (uint j = 0; j < t.actions.size(); j++) {
			VCAction a;
			VCView target;
			if (!_data.getAction(t.actions[j], a) || a.type != kActionFunction || a.function != "CharTeleport" ||
				a.args.size() < 6 || !_data.getView(a.args[5].value, target))
				continue;
			Common::Array<uint32> ids;
			_data.getMembers(target.characters, ids);
			for (uint k = 0; k < ids.size(); k++) {
				VCCharView cv;
				if (_data.getCharView(ids[k], cv) && isEnabled(ids[k]) && cv.character == here.character &&
					hasTopicOrIdea(ids[k]))
					return ids[k];
			}
		}
	}
	return 0;
}

void Script::idea(uint32 iconId, uint32 charView) {
	// IdeaIcon_DragEnd 0x4d15f0 -> PlayItemTopic 0x4c5500: asked (History) or count + 1, the topic's Object
	// Activation and its playlist with +0x144 clear; no Mouse Click and no Drop triggers. The box comes back
	// after the clips, so it stays open here.
	uint32 conversation;
	if (!_box || !findIdeaTopic(charView ? charView : _box, iconId, conversation) || !conversation)
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
	// (a piece of the view's loop is the view's own asset: none). Only a clip that is queued or a sound whose end
	// deactivates it counts as a use of the asset object; any other fires directly (B-141, B-162)
	for (uint i = 0; i < list.size(); i++) {
		VCAsset a;
		if (list[i].limit)
			continue;
		if (_data.getAsset(list[i].id, a) && !a.files.empty() && (a.files[0].file.hasPrefix("XS:") || isVideoAsset(a)))
			activateAsset(list[i].id);
		else
			fire(list[i].id, kEventObjectActivation);
	}
	_clipsActivated = true;
	for (uint i = 0; i < list.size(); i++) {
		const TopicClip &s = list[i];
		_nextVideoEnd = s.end;
		if (s.limit)
			queueVideo(s.id, s.limit, false, true);
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
	// Counted by recordTopic(); 0 for a topic that is never recorded.
	// count < 2 is Topic_Play's repeat flag clear: a box click on a topic
	// not yet in History, or a dropped item's topic asked once (0x4e9440
	// checks count < 2 again for a character with an Atoms topic)
	uint count = _asked.getValOrDefault(conversationId, 0);
	playList(c.question);
	if (count < 2) {
		beforeAnswer();
		playList(c.answer);
		return;
	}
	// 0x4e9440 returns 0 without a character (0x4e9e50): Topic_Play
	// plays the answer alone
	VCCharView charView;
	if (!findTopicCharView(conversationId, inBox, charView)) {
		beforeAnswer();
		playList(c.answer);
		return;
	}
	uint32 stock[kStockAtomCount];
	if (!getCharViewAtoms(charView, stock)) {
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
	queueVideo(id, 750, false, true);
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

bool Script::findTopicCharView(uint32 conversationId, bool inBox, VCCharView &charView) const {
	// 0x4e9e50: the view's only character view, else the
	// one with the topic, looked up in its conversations for a box click
	// and in its idea maps (any icon, enabled or not) for a dropped item
	VCView view;
	Common::Array<uint32> charViews;
	if (!_data.getView(_view, view) || !_data.getMembers(view.characters, charViews))
		return false;
	for (uint i = 0; i < charViews.size(); i++) {
		if (!_data.getCharView(charViews[i], charView))
			continue;
		bool found = charViews.size() == 1;
		Common::Array<uint32> members;
		if (!found && inBox && _data.getMembers(charView.conversations, members))
			found = Common::find(members.begin(), members.end(), conversationId) != members.end();
		else if (!found && !inBox && charView.ideaMaps && _data.getMembers(charView.ideaMaps, members)) {
			for (uint j = 0; j < members.size() && !found; j++) {
				VCIdeaMap map;
				found = _data.getIdeaMap(members[j], map) && map.conversation == conversationId;
			}
		}
		if (found)
			return true;
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
			Dice dice = newDice(0x490de2);
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

} // End of namespace XFiles
