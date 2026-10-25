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
#include "xfiles/script_internal.h"
#include "xfiles/vcdump.h"

namespace XFiles {

/** The camera (docs/subsystems/camera.md): photos it holds, its shutter assets (RegCameraAss), viewfinder width, the view band. */
static const int32 kCameraPhotos = 24;
static const uint32 kCameraShutter = 34747;
static const uint32 kCameraShutterFull = 57104;
static const int kCameraWidth = 320;
static const int kBandLeft = 20;
static const int kBandTop = 90;
static const int kBandWidth = 600;
static const int kBandHeight = 240;

static Common::String formatRect(const VCRect &r) {
	return Common::String::format("(%d, %d, %d, %d)", r.left, r.top, r.right, r.bottom);
}

// -- Navigation ---------------------------------------------------------------

void Script::answerEndGame(bool asked) {
	if (asked)
		setVariable(_endGameVar, 0); // 0x4fbd60 writes the changed value back, after the answer
	_endGamePending = false;
}

void Script::travel() {
	if (_hooks)
		_hooks->checkpoint(); // 0x5068b0 snapshots the game before it travels
	// 0x5068b0, 0x493090: the emotion and action icons are deactivated first (0x44d850; clears their enable
	// flags, idea icons keep theirs), then all four old objects, whatever the new path shares, then the new
	// four are activated. The exe also hides the inventory icon and clears the letterbox tint; the engine does
	// not model those yet
	_travelPending = false;
	spendIcons(kClassEmotionIcon);
	spendIcons(kClassActionIcon);
	// The AI icon is hidden at 0x5068b0 and shown again here (0x493090) before the new node: its show handler
	// applies the glow from the level alone. The engine draws no hidden frame in between
	if (_hooks)
		_hooks->reshowAIIcon();
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
	Common::Array<VideoPlay> plays;
	plays.swap(_videoPlays);
	_ideaTopic = 0;
	_heldSounds.clear();
	_pendingStops.clear();
	for (uint i = 0; i < sounds.size(); i++)
		endSound(sounds[i], "stopped");
	for (uint i = 0; i < clips.size(); i++)
		endClip(clips[i], "stopped");
	for (uint i = 0; i < videos.size(); i++) {
		log(Common::String::format("  << video #%u stopped", videos[i]));
		if (!plays[i].viewLoop)
			deactivateAsset(videos[i]);
	}
}

void Script::deactivateView() {
	// The shown asset's deactivation runs before the view's own triggers (0x458e10, B-112)
	// The view is being left: an asset enabled by the deactivations (the shown asset's included) is not started
	// (0x46f210)
	_viewActive = false;
	const bool wasLeaving = _viewLeaving;
	_viewLeaving = true;
	deactivateViewAsset();
	if (_view && !_viewHeld)
		fire(_view, kEventObjectDeactivation);
	deactivateViewObjects();
	_viewLeaving = wasLeaving;
}

void Script::enterLeavePath() {
	if (!_leavePending)
		return;
	_leavePending = false;
	enter(_leavePath[1], _leavePath[2], _leavePath[3], _leavePath[0]);
}

int Script::hotActionFor(uint32 kind, bool &gunAnimation) const {
	// HotSpotObj_OnMouseClick (0x4b4470): kind -> action number of 0x4b4a20 (docs/formats/hot.md, the census table;
	// read from the exe, only 221-223 are confirmed by a run). "Gun" is the gun in hand, "stiletto" inventory value 23
	const bool gun = isGunHeld();
	const bool stiletto = getHeldValue() == 23;
	gunAnimation = false;
	switch (kind) {
	case 100:
		gunAnimation = true;
		return gun ? 27 : -1;
	case 101:
	case 103:
		return gun ? 1 : -1;
	case 102:
	case 104:
		return gun ? 2 : -1;
	case 111:
	case 113:
	case 121:
	case 131:
	case 141:
		return gun ? 4 : -1;
	case 112:
	case 114:
	case 122:
	case 132:
	case 142:
		return gun ? 5 : -1;
	case 201:
		return gun ? 9 : -1;
	case 211:
		return gun ? 10 : -1;
	case 221:
	case 222:
	case 223:
		return gun ? (int)(kind - 221 + 6) : -1;
	case 401:
		return gun ? 11 : -1;
	case 411:
		return gun ? 12 : -1;
	case 421:
		return gun ? 13 : -1;
	case 431:
		return gun ? 14 : -1;
	case 432:
		return gun ? 15 : -1;
	case 434:
		return gun ? 23 : -1;
	case 433:
		if (stiletto)
			return 17;
		gunAnimation = true;
		return gun ? 16 : -1;
	case 441:
		return 18;
	case 442:
		return gun ? 19 : -1;
	case 451:
		return 20;
	case 461:
		gunAnimation = gun;
		return gun || stiletto ? 21 : -1;
	case 462:
		return stiletto ? 22 : (gun ? 24 : -1);
	case 463:
		return 25;
	case 470:
		gunAnimation = gun;
		return gun || stiletto ? 26 : -1;
	case 500:
		gunAnimation = true;
		return gun ? 27 : -1;
	case 600:
		gunAnimation = true;
		return gun ? 28 : -1;
	case 666:
		gunAnimation = true;
		return gun ? 3 : -1;
	default:
		return -1; // 2400, 6000, 8000 and the kinds without a case: a click that does nothing
	}
}

void Script::hotClick(uint32 kind) {
	bool animation;
	int action = hotActionFor(kind, animation);
	if (action < 0) {
		if (!isGunHeld())
			log("  (no gun in hand)");
		return;
	}
	// The held item's animation before 16, 21, 26-28 and 3 (0x4cb440: it plays once to its end, blocking) is not
	// ported (B-031): the fork has no gun animation to run
	hotAction(action);
}

void Script::setViewState(char letter) {
	// ViewState_Set (0x51ce80): the variable RegViewState registered
	if (_viewStateVariable)
		_variables[_viewStateVariable] = (unsigned char)letter;
}

void Script::clickAIIcon() {
	if (_hintGlow > 0)
		setViewByExe(_hintGlow);
}

void Script::clickAIHint() {
	// AIIconObj's click without a glow (0x5097e0): walk the hint list from the cursor, which persists between
	// clicks. An action whose condition holds (the hint trigger as context) runs; the cursor advances and wraps.
	// The walk ends after the first run action that is not a Statement, or when the cursor is back at its start
	Common::Array<uint32> actions;
	if (!_hintList || !_data.getMembers(_hintList, actions) || actions.empty()) {
		debugC(1, kDebugScript, "-- click: AI icon, no hint list");
		return;
	}
	uint n = actions.size();
	uint start = _hintCursor < n ? _hintCursor : 0;
	uint i = start;
	do {
		VCAction a;
		bool run = _data.getAction(actions[i], a);
		LocalVariables locals;
		EventFields fields;
		if (run && a.conditional && !test(a.condition, fields, locals))
			run = false;
		i = (i + 1) % n;
		if (run) {
			// The exe closes the open dialog box first for a Play Once of a kind 1 or 2 asset (0x447340; which
			// media kinds is not read: every Play Once closes it here)
			if (a.type == kActionAsset && a.mode == kModePlayOnce)
				closeBox();
			execute(a, kEventMouseClick, fields, locals);
			if (a.type != kActionStatement)
				break;
		}
	} while (i != start);
	_hintCursor = i;
}

bool Script::isHintAvailable() {
	// 0x508c40: the glow flag, else the first hint-list action whose condition holds (the walk of clickAIHint, running nothing)
	if (_hintGlowOn)
		return true;
	Common::Array<uint32> actions;
	if (!_hintList || !_data.getMembers(_hintList, actions))
		return false;
	for (uint i = 0; i < actions.size(); i++) {
		VCAction a;
		if (!_data.getAction(actions[i], a))
			continue;
		LocalVariables locals;
		EventFields fields;
		if (!a.conditional || test(a.condition, fields, locals))
			return true;
	}
	return false;
}

void Script::setViewByExe(uint32 view) {
	// The exe's own Set View (0x4b3bc0, which also drops the pending .HOT entries: nothing to drop here)
	VCAction a;
	a.id = 0;
	a.type = kActionSetView;
	if (!findViewPath(view, a.path)) {
		warning("Script: .HOT click: no owner for view %u", view);
		return;
	}
	LocalVariables locals;
	EventFields fields;
	execute(a, kEventMouseClick, fields, locals);
}

void Script::hotAction(int n) {
	// HotSpot_ClickAction (0x4b4a20), docs/formats/hot.md "Click actions". Only 6-8 are confirmed by a run (B-031);
	// the rest is read from the exe
	struct Action {
		char state;      // the view state letter, 0 for none
		bool cursor;     // VCCursor 73520 after the click
		uint32 asset;    // played once, 0 for none
		bool reshow;     // the view is entered again
		uint32 setView;  // the Set View that follows, 0 for none
		bool stopSounds; // every sound object is deactivated first
	};
	static const Action kActions[] = {
		{0, true, 21672, true, 8138, false},     // 0 (the asset-enable rewrite of 0x4b5680 is not ported for 0)
		{'t', true, 21664, true, 8138, false},   // 1
		{'d', true, 21662, true, 8138, false},   // 2
		{'m', true, 0, true, 0, true},           // 3
		{'t', true, 21718, true, 0, false},      // 4
		{'d', true, 21716, true, 0, false},      // 5
		{0, true, 21230, false, 0, false},       // 6
		{0, true, 21232, false, 0, false},       // 7
		{0, true, 21234, false, 0, false},       // 8
		{'r', true, 28456, true, 0, false},      // 9
		{'r', true, 21238, false, 5501, false},  // 10
		{'r', true, 21812, true, 0, false},      // 11
		{'r', true, 21818, true, 0, false},      // 12
		{'r', true, 21808, true, 0, false},      // 13
		{'r', true, 44483, false, 44283, false}, // 14
		{'r', true, 49014, false, 14519, false}, // 15
		{'z', false, 80090, false, 0, true},     // 16
		{'z', false, 49013, false, 0, true},     // 17
		{'z', false, 21782, false, 0, false},    // 18
		{'r', true, 21784, false, 0, false},     // 19
		{'t', false, 0, false, 0, false},        // 20
		{0, false, 80090, false, 0, false},      // 21
		{0, false, 23434, false, 0, false},      // 22
		{'w', true, 49022, false, 49374, false}, // 23
		{'d', true, 49022, false, 49374, false}, // 24
		{0, false, 23430, false, 0, false},      // 25
		{'b', false, 0, false, 0, false},        // 26
		{'S', false, 0, false, 0, false},        // 27
		{0, false, 19656, false, 0, false},      // 28
	};
	if (n < 0 || n >= (int)ARRAYSIZE(kActions))
		return;
	Action act = kActions[n];
	log(Common::String::format("  == .HOT click action %d", n));

	// Actions 1, 2, 4, 5 first rewrite the enable flags of the view's assets (0x4b5680, 0x4b59f0); "success" is
	// that no asset is left enabled. Action 0 does the same in the exe with a rule the doc does not give: left out
	// (B-031), as is the view state `a` of 0x4b5680 on success
	bool success = false;
	if (n == 1 || n == 2 || n == 4 || n == 5) {
		VCView v;
		Common::Array<uint32> assets;
		if (_data.getView(_view, v))
			_data.getMembers(v.assets, assets);
		uint count = assets.size();
		uint chosen = count;
		if (!assets.empty() && isEnabled(assets[0]))
			chosen = (n == 1 || n == 4) ? 2 : 1;
		for (uint i = 0; i < count; i++)
			_enabled[assets[i]] = i == chosen;
		success = chosen >= count;
	}

	// The view state is written before the view's deactivation runs: the exe only queues it (0x42b230), and the
	// view's Object Deactivation tests the state (view 5487: 'r' kills the stairs goon and enables its exits)
	if (act.state)
		setViewState(act.state);
	// The current view's deactivation is queued
	deactivateView();
	_viewGone = true;
	// A Set View in the view's Deactivation (45822 sets 45857): entered below unless the action has its own
	const bool leaveSet = _leavePending;
	uint32 leavePath[4] = {_leavePath[0], _leavePath[1], _leavePath[2], _leavePath[3]};
	_leavePending = false;

	if (act.stopSounds)
		stopAllForTravel(); // 0x44d850(scene, 27): approximated by the travel sweep of the sounds
	if (n == 27 || n == 28) {
		// Stop the shown videos (approximated as above), fade to black, Scene_SuspendAll(scene, 1)
		stopAllForTravel();
		if (_hooks) {
			_hooks->fadeToBlack();
			_hooks->suspendScene();
		}
		if (n == 27) {
			// Suicide Note White or Yellow (table 0x6549a0); no cursor, asset or re-show
			static const uint32 kLayouts[2] = {62847, 62849};
			Dice dice = newDice(0x4b4a20);
			showLayout(kLayouts[roll(dice, 0, 1)]);
			return;
		}
	}
	if (n == 3) {
		// Easter candy: Mel shot 1-5 (table 0x6549a8)
		static const uint32 kLayouts[5] = {84686, 84695, 84704, 84713, 84722};
		Dice dice = newDice(0x4b4a20);
		uint32 layout = kLayouts[roll(dice, 0, 4)];
		if (_hooks)
			_hooks->showCursorAnimation(73520);
		showLayout(layout);
		_viewGone = false;
		activateOrHoldView();
		return;
	}
	if (act.cursor && _hooks)
		_hooks->showCursorAnimation(73520);
	if (act.asset)
		playAsset(act.asset, kModePlayOnce, nullptr);
	if (success && (n == 4 || n == 5))
		playAsset(38448, kModePlayOnce, nullptr);
	if (act.reshow && !success) {
		// 0x46a370: a new view object for the same view data
		_viewGone = false;
		activateOrHoldView();
	}
	if (success && (n == 1 || n == 2))
		setViewByExe(8138);
	else if (n > 2 && act.setView)
		setViewByExe(act.setView);
	else if (leaveSet && !(act.reshow && !success))
		enter(leavePath[1], leavePath[2], leavePath[3], leavePath[0]);
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
	const bool wasDeactivating = _enterDeactivating;
	_enterDeactivating = true;
	for (int i = 3; i >= first; i--) {
		// A view still held (below) was never activated: entering another
		// one drops it, as Action_DoSetView (0x46a950) cancels its activation
		// The shown asset's deactivation runs before the view's own triggers (0x458e10, B-112)
		if (i == 3) {
			// After a gun click the scene has no view left to drop (hotClick)
			if (!_viewGone)
				deactivateView();
			_viewGone = false;
		} else if (oldPath[i])
			fire(oldPath[i], kEventObjectDeactivation);
	}
	_enterDeactivating = wasDeactivating;
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
	if (_travelDropped) {
		// AlienBlast ran in the deactivations: the queued activations are dropped (0x41a770), the old picture stays
		_travelDropped = false;
		return;
	}
	for (int i = first; i < 3; i++)
		fire(newPath[i], kEventObjectActivation);
	if (first < 4)
		activateOrHoldView();
	showView();
	enterLeavePath();
}

void Script::activateOrHoldView() {
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

void Script::activateView() {
	// The mark of the view's own activation: the exe logs it after the Activations of its node, location and viewpoint
	log("  == view activated " + getObjectLabel(_data, _view));
	_viewHeld = false;
	_viewActive = true;
	// The exe activates the view's asset before the view's own actions run (B-151),
	// its character views after them (tarakan-to-evidence2: view 11705's
	// Activation actions run before character view 24182 is activated, B-167)
	activateViewAsset();
	fire(_view, kEventObjectActivation);
	activateViewObjects(true);
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
		deactivateAsset(ids[j]);
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
	if (!_viewActive || _viewHeld || _travelPending || isViewChangeQueued() || !_data.getView(_view, v))
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
		deactivateAsset(old);
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
	activateAsset(ids[0]);
}

void Script::activateAsset(uint32 id) {
	// XFiles.exe 0x446580 shares the asset object of a second user; its Object Activation fires on the first
	// use only (0x45a330, B-162)
	uint &count = _assetActive[id];
	if (count++ == 0)
		fire(id, kEventObjectActivation);
}

void Script::deactivateAsset(uint32 id, bool fireEvent) {
	if (!_assetActive.contains(id))
		return;
	uint &count = _assetActive[id];
	if (--count > 0)
		return;
	_assetActive.erase(id);
	if (fireEvent)
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

bool Script::binocularsView(bool on) {
	// Tool_SpecialView 0x51bc70: the view change of the binoculars, by iCurrViewSpecial (conversation-reactions.md)
	struct Change {
		int32 value;
		uint32 take;
		uint32 putBack;
	};
	static const Change kChanges[] = {
		{7879, 7878, 7879},
		{7878, 7878, 7879},
		{19688, 70710, 19688},
		{70710, 70710, 19688},
		{19684, 70712, 19684},
		{70712, 70712, 19684},
		{19692, 71914, 19692},
		{71914, 71914, 19692},
	};
	if (!isSpecialView())
		return false;
	int32 value = getVariable(_viewSpecialVariable);
	for (uint i = 0; i < ARRAYSIZE(kChanges); i++) {
		if (kChanges[i].value != value)
			continue;
		// 0x51cf10: disable the shown asset, then enable the new one; the asset list then swaps them
		uint32 from = 0;
		for (uint j = 0; j < _viewObjects.size() && !from; j++) {
			if (_data.getClassOf(_viewObjects[j]) == kClassAssetRef)
				from = _viewObjects[j];
		}
		uint32 to = on ? kChanges[i].take : kChanges[i].putBack;
		if (!from)
			return true;
		log(Common::String::format("  binoculars' view change: #%u -> #%u", from, to));
		enableTarget(from, false); // 0x4083d0
		enableTarget(to, true);
		return true;
	}
	if (value == 23486 || value == 70706)
		log("  (binoculars' view change of the video pair 23486 / 70706 not ported)");
	return true;
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
	// The nav's view change is queued after its Mouse Click runners (0x46ae70), so the click's triggers see it
	_navViewQueued = nav.target != _view;
	fire(navId, kEventMouseClick);
	_navViewQueued = false;
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
	// literals at 0x6b2aac); any other cursor cuts. The wipe opens from the middle of the hotspot's rectangle, from
	// the click point when the rectangle is empty
	_navTransition = kTransitionCut;
	_navTransitionEmpty = true;
	_navTransitionCentre = _clickPoint;
	VCHotSpot hotSpot;
	if (!_data.getHotSpot(nav.hotSpot, hotSpot))
		return;
	if (hotSpot.cursor >= 284 && hotSpot.cursor <= 286)
		_navTransition = kTransitionLeft + (hotSpot.cursor - 284);
	_navTransitionEmpty = hotSpot.rect.left >= hotSpot.rect.right || hotSpot.rect.top >= hotSpot.rect.bottom;
	if (!_navTransitionEmpty)
		_navTransitionCentre = Common::Point((hotSpot.rect.left + hotSpot.rect.right) / 2, (hotSpot.rect.top + hotSpot.rect.bottom) / 2);
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
	else if (kind == "aiclick") {
		// Engine only: a click on the AI icon (a glowing one goes to its view, else the next hint); the
		// frame's sound-end check has run by then
		aiSoundEndCheck();
		if (_aiSuspended)
			log("   suspended: no hit");
		else if (_hintGlowOn)
			clickAIIcon();
		else
			clickAIHint();
	} else if (kind == "click" && _data.getClassOf(id) == kClassInventory)
		selectInventory(id); // an inventory icon takes its cursor (0x49c180)
	else if (kind == "click") {
		click(id);
		// A character view clicked with nothing in hand opens its box, as
		// XFilesEngine::openDialogBox does: the topics activate now, not
		// at the first ask
		VCCharView charView;
		if (_clickOpensBox && _data.getCharView(id, charView) && hasTopicOrIdea(id))
			openBox(id);
	} else if (kind == "answer") {
		answerEndGame(true); // the question of EndGame answered (the engine reads yes or no from the step)
	} else if (kind == "hot") {
		hotClick(id);
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
	else if (kind == "idea") {
		// idea:ICON[:CHARVIEW]: the icon dropped on that character view (the exe plays the topic of the one under the
		// drop; tarakan-to-evidence2, B-167)
		size_t second = arg.findFirstOf(':');
		idea(id, second == Common::String::npos ? 0 : (uint32)atoi(arg.c_str() + second + 1));
	} else if (kind == "goto")
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
	else if (kind == "menu") {
		// menu:new|load|previous|quit: a click in the start-up menu after a game end (steps.py, B-203). The restart is
		// the caller's (vcdump, the engine's runMenu); nothing happens in the interpreter
	} else if (kind == "media")
		_soundEnds = true; // the step list has the sound ends (steps.py `end:`): later plays of a playing asset are dropped
}

} // End of namespace XFiles
