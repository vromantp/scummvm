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

/** PDAEscape's action lists (XFiles.exe 0x47d090): escape on (argument 1) or off. */
static const uint32 kPDAEscapeOnActions = 75533;
static const uint32 kPDAEscapeOffActions = 75530;

/** OnIntInvClick (XFiles.exe 0x467fb0): the PDA's Inventory_iPDA value, bDisablePDA and PDAalert.amv. */
static const int32 kInventoryPDA = 14;

static const uint32 kDisablePDAVariable = 5277;
static const uint32 kPDAAlertSound = 75688;

/** PageTurnSound runs trigger 92463's list (XFiles.exe 0x4b3930): one of six random page sounds. */
static const uint32 kPageTurnSoundActions = 92468;

void Script::addHooks() {
	addCoreHooks();
	_phone.addHooks(_hookTable);
	_computer.addHooks(_hookTable);
	_pda.addHooks(_hookTable);
	_prefs.addHooks(_hookTable);
}

void Script::addCoreHooks() {
	addGameHooks();
	addNodeHooks();
	addToolHooks();
	addInterfaceHooks();
	addStateHooks();
}

void Script::addGameHooks() {
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
	t.add("InitSmShootout", [](Script &s, const HookCall &c) {
		if (!c.isVariable(0) || !c.isVariable(1) || !c.isVariable(2))
			return false;
		for (int i = 0; i < 3; i++)
			s._shootVars[i] = c.action.args[i].value;
		s.shootoutReset(); // its three rolls are the reset's (0x4f9630)
		return true;
	});
	t.add("DoSmolShootout", [](Script &s, const HookCall &c) {
		s.doSmolShootout(c);
		return true;
	});
	t.add("SmIsWillShot1", [](Script &s, const HookCall &c) {
		if (!c.has(1))
			return false;
		s.smIsWillShot(c.args[0]);
		return true;
	});
	t.add("SmolGoonShot1", [](Script &s, const HookCall &) {
		s.smolGoonShot();
		return true;
	});
	t.add("GetPlayFBIShot", [](Script &s, const HookCall &c) {
		if (!c.isVariable(0))
			return false;
		s.setVariable(c.action.args[0].value, s._fbiShot); // 0x4f9ef0
		s._fbiShot = false;
		return true;
	});
	t.add("EndGame", [](Script &s, const HookCall &c) {
		if (!c.isVariable(0))
			return false;
		// 0x4cc870 closes the dialog box; the question and the restart are the engine's (answerEndGame)
		s.closeBox();
		s._endGameVar = c.action.args[0].value;
		s._endGameValue = s.getVariable(s._endGameVar);
		s._endGamePending = true;
		return true;
	});
	t.add("IsIItemHeld", [](Script &s, const HookCall &c) {
		if (!c.has(2) || !c.isVariable(1))
			return false;
		s.isIItemHeld(c.action.args[1].value, c.args[0]);
		return true;
	});
}

void Script::addNodeHooks() {
	HookTable<Script> &t = _hookTable;
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
}

void Script::addToolHooks() {
	HookTable<Script> &t = _hookTable;
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
			s._binocularsHeld = s.isHolding(); // stands for the held object 0x51baf0
			if (!s.binocularsView(true))
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
	t.add("OnGunClick", [](Script &s, const HookCall &c) {
		if (!c.has(1))
			return false;
		// 0x4e1e80: the gun is taken like the camera, without a character reaction (no Tool_CharReaction)
		if (s._heldVariable && s.getVariable(s._heldVariable) != c.args[0])
			s._variables[s._heldVariable] = c.args[0];
		s._gunValue = c.args[0];
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
		s._binocularsHeld = false;
		if (s._hooks)
			s._hooks->heldItemChanged(0);
		return true;
	});
}

void Script::addInterfaceHooks() {
	HookTable<Script> &t = _hookTable;
	t.add("AlienBlast", [](Script &s, const HookCall &) {
		// 0x4b6220: drops the queued activations of nodes, locations, viewpoints and views (0x41a770), then flashes
		// the screen (B-199). Only a travel queued behind the deactivation that ran it exists (B-204)
		if (s._enterDeactivating) {
			s._travelDropped = true;
			s.log("      -- AlienBlast: queued travel dropped");
		}
		return true;
	});
	t.add("IsoChamberStat", [](Script &s, const HookCall &c) {
		// 0x4fc970 -> 0x4b6120 (docs/data/addresses.tsv): the 11 arguments are variables, 4-11 are outputs
		if (c.action.args.size() < 11)
			return false;
		for (int i = 0; i < 11; i++) {
			if (!c.isVariable(i))
				return false;
		}
		const bool east = s.getVariable(c.action.args[0].value) != 0;
		const bool west = s.getVariable(c.action.args[1].value) != 0;
		int32 out[8] = {0, 0, 0, 0, 0, 0, 0, 0}; // arguments 4-11
		if (s.getVariable(c.action.args[2].value) == 1) {
			if (east && west) {
				out[2] = 1;
				out[4] = 1;
			} else if (east) {
				out[1] = 1;
				out[6] = 1;
			} else if (west) {
				out[2] = 1;
				out[5] = 1;
			} else {
				out[1] = 1;
				out[7] = 1;
			}
		} else {
			out[0] = 1;
			out[3] = 1;
		}
		for (int i = 0; i < 8; i++) {
			uint32 var = c.action.args[3 + i].value;
			if (s.getVariable(var) != out[i])
				s.setVariable(var, out[i]);
		}
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
	t.add("SuicideNote", [](Script &s, const HookCall &c) {
		// (Event::1) 0x4f9fc0: one of three note texts (table 0x659a00) on the activated item. The face is
		// ZonkersHand (Jrn.ttr), which the fonts do not have: the sans face stands in. 14 high, black, left,
		// wrapped at the item's own width (+0x194 is 1)
		if (!c.has(1) || !c.args[0])
			return false;
		static const uint32 kNotes[3] = {62868, 62870, 62872};
		Script::Dice dice = s.newDice(0x4fa005);
		uint32 asset = kNotes[s.roll(dice, 0, 2)];
		Common::U32String text;
		if (!s.readTextFile(asset, text) && !s.getTextAsset(asset, text))
			return false;
		VCInterfaceItem item;
		int width = 0;
		if (s._data.getInterfaceItem(c.args[0], item))
			width = item.rect.right - item.rect.left;
		s.setItemText(c.args[0], text, 14, false, 0x000000, Script::kAlignLeft, false, width, true);
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
		s._viewStateVariable = c.action.args[0].value; // and keeps it as the view-state variable (0x6c1450)
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
}

void Script::addStateHooks() {
	HookTable<Script> &t = _hookTable;
	// The hooks whose arguments a save keeps (GameState_Save 0x4d9450): recorded, but the engine draws none of
	// them yet, so they are not handled
	t.add("SetAIList", [](Script &s, const HookCall &c) {
		// SetAIList(trigger, list)
		if (c.has(2)) {
			s._hintTrigger = c.args[0];
			s._hintList = c.args[1];
			s._hintCursor = 0;
			s.recheckAIIcon();
		}
		return false;
	});
	t.add("ClearAIList", [](Script &s, const HookCall &c) {
		s._hintTrigger = s._hintList = 0;
		s._hintCursor = 0;
		s.recheckAIIcon();
		return false;
	});
	t.add("SetAIGlow", [](Script &s, const HookCall &c) {
		// AI_SetGlow (0x480670): 0 clears the flag and the level, 1 sets the flag with level 0, n > 1 flag and level n
		if (c.has(1)) {
			s._hintGlow = c.args[0] > 1 ? c.args[0] : 0;
			s._hintGlowOn = c.args[0] != 0;
		}
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
		// FLight_SetProps (0x4d7d20) also re-selects the held item when it is the flashlight, so the held light
		// takes the new look. That belongs to the flashlight tool mode, which is not ported
		return true;
	});
	t.add("FixJumpCut", [](Script &s, const HookCall &c) {
		// 0x48aa20 reads both variables and calls 0x48aa10, a bare ret 0xc: no effect in this build
		return true;
	});
	t.add("CharTeleport", [](Script &s, const HookCall &c) {
		// Hook_CharTeleport 0x4fb570 -> 0x4c56a0: Event::0/1 are the click. The character view under it (the current
		// view's) gives the character; the target view must have an enabled character view of the same one, else
		// nothing happens. Only within the same node, the player is cut to the target (0x4c5cd0)
		if (!c.has(6))
			return false;
		VCView view, target;
		if (!s._data.getView(s._view, view) || !s._data.getView(c.args[5], target))
			return true;
		const Common::Point click = s._clickPoint;
		uint32 character = 0;
		Common::Array<uint32> ids;
		s._data.getMembers(view.characters, ids);
		for (uint i = 0; i < ids.size() && !character; i++) {
			VCCharView cv;
			if (s._data.getCharView(ids[i], cv) && s.isEnabled(ids[i]) && click.x >= cv.rect.left && click.x < cv.rect.right &&
				click.y >= cv.rect.top && click.y < cv.rect.bottom)
				character = cv.character;
		}
		if (!character)
			return true;
		bool found = false;
		ids.clear();
		s._data.getMembers(target.characters, ids);
		for (uint i = 0; i < ids.size() && !found; i++) {
			VCCharView cv;
			found = s._data.getCharView(ids[i], cv) && s.isEnabled(ids[i]) && cv.character == character;
		}
		if (found && s._node == (uint32)c.args[2])
			s.enter(c.args[2], c.args[3], c.args[4], c.args[5]);
		return true;
	});
	t.add("RegDayNight", [](Script &s, const HookCall &c) {
		// 0x49b5b0 is a bare ret 0xc: the handle is not kept; the scripts read iDayNight themselves
		return true;
	});
	t.add("RegCookEmail", [](Script &s, const HookCall &c) {
		// 0x48e830 keeps the two handles. The only reader is the notepad's send-to-address code (0x4e6b90, called
		// by AddrSelPDA), which is not ported: sent to address 45290 it adds 1 to the count and, on notepad day 1,
		// sets the flag
		if (!c.isVariable(0) || !c.isVariable(1))
			return false;
		s._cookEmailCount = c.variableArg(0);
		s._cookEmailFlag = c.variableArg(1);
		return true;
	});
	t.add("StopAllTimers", [](Script &s, const HookCall &c) {
		// 0x49fde0 queues a Timer Stop (type 2, command 1) for every timer of the list, paused ones included;
		// they run right after the hook
		Common::Array<uint> ids;
		for (Common::HashMap<uint, Timer>::const_iterator it = s._timers.begin(); it != s._timers.end(); ++it)
			ids.push_back(it->_key);
		Common::sort(ids.begin(), ids.end());
		for (uint i = 0; i < ids.size(); i++) {
			s._timers.erase(ids[i]);
			s.log(Common::String::format("      timer %u stopped (StopAllTimers)", ids[i]));
		}
		return true;
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
	t.add("DisplayDBox", [](Script &s, const HookCall &c) {
		// 0x451d90 -> 0x481660: opens the box of a character view with topics or an enabled idea, replacing the
		// open one; 0 closes it (0x4bdc30). The engine hook opens the widget
		if (!c.has(1))
			return false;
		VCCharView charView;
		if (c.args[0] == 0)
			s.closeBox();
		else if (s._data.getCharView(c.args[0], charView) && s.hasTopicOrIdea(charView.id))
			s.openBox(charView.id);
		return false;
	});
	t.add("DispEvIcons", [](Script &s, const HookCall &c) {
		// 0x4efb20: every group 0 icon is shown or hidden; the interface draws the row from the pairs
		if (c.has(1)) {
			s._evidenceRowShown = c.args[0] != 0;
			for (uint i = 0; i + 2 <= s._evidence.size(); i += 2)
				s._evidence[i + 1] = s._evidenceRowShown;
		}
		return false;
	});

	// Hooks only logged on purpose: the menu's Save and Load (B-063)
	static const char *const kLoggedMenuHooks[] = {"SaveKeyPress", "SaveLoadComit", "SaveLoadInit", "SaveSlotSelect"};
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
	// The binoculars' held object deactivates (0x51c160): the base first, then the view change back
	if (_binocularsHeld) {
		_binocularsHeld = false;
		binocularsView(false);
	}
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

void Script::shootoutReset() {
	// 0x48ccd0 (Game_Start) and InitSmShootout 0x4f9630: forget the shots, clear the variables, roll three permutations
	for (int i = 0; i < 3; i++) {
		_shootIdx[i] = 0;
		_shootDead[i] = false;
		if (_shootVars[i])
			setVariable(_shootVars[i], 0);
	}
	_shootCur = 0;
	static const int perms[6][3] = {{1, 2, 3}, {1, 3, 2}, {2, 1, 3}, {2, 3, 1}, {3, 1, 2}, {3, 2, 1}};
	for (int i = 0; i < 3; i++) {
		// Nothing reads the permutations; without fixed seeds the dice would draw from (and shift) the shared sequence
		if (!_fixedSeeds)
			break;
		Dice dice = newDice(0x4f9658);
		const int *perm = perms[roll(dice, 1, 6) - 1];
		for (int k = 0; k < 3; k++)
			_shootPerm[i][k] = perm[k];
	}
}

bool Script::findViewPath(uint32 view, uint32 (&path)[4]) const {
	// The exe finds the view's owners in the database; the current viewpoint is the likely one
	path[0] = view;
	VCViewPoint viewPoint;
	if (_data.getViewPoint(_viewPoint, viewPoint) && Common::find(viewPoint.views.begin(), viewPoint.views.end(), view) != viewPoint.views.end()) {
		path[1] = _node;
		path[2] = _location;
		path[3] = _viewPoint;
		return true;
	}
	Common::Array<NeoEntry> nodes;
	_data.getHdb().getObjects(kClassNode, nodes);
	for (uint n = 0; n < nodes.size(); n++) {
		VCNode node;
		if (!_data.getNode(nodes[n].key, node))
			continue;
		for (uint l = 0; l < node.locations.size(); l++) {
			VCLocation location;
			if (!_data.getLocation(node.locations[l], location))
				continue;
			for (uint v = 0; v < location.viewPoints.size(); v++) {
				if (_data.getViewPoint(location.viewPoints[v], viewPoint) &&
					Common::find(viewPoint.views.begin(), viewPoint.views.end(), view) != viewPoint.views.end()) {
					path[1] = node.id;
					path[2] = location.id;
					path[3] = viewPoint.id;
					return true;
				}
			}
		}
	}
	return false;
}

void Script::doSmolShootout(const HookCall &call) {
	// 0x4f9a40: the next goon to appear, then a Set View the exe makes (0x4b3bc0, no VCAction in the data)
	static const uint32 views[3][3] = {{62256, 62258, 62260}, {62262, 62264, 62266}, {62268, 62270, 62272}};
	if (_shootDead[0] && _shootDead[1] && _shootDead[2]) {
		_shootCur = 0;
	} else {
		Dice dice = newDice(0x4f9a59);
		int r = roll(dice, 1, 3);
		int goon = r - 1;
		if (_shootDead[goon])
			goon = (goon + 1) % 3;
		if (_shootDead[goon])
			goon = (goon + 1) % 3;
		_shootCur = goon + 1;
	}
	uint32 view = 5208;
	if (_shootCur) {
		int &idx = _shootIdx[_shootCur - 1];
		view = views[_shootCur - 1][idx];
		if (idx < 2)
			idx++;
	}
	// The exe also drops the pending .HOT entries first (nothing to drop until B-175)
	VCAction a;
	a.id = 0;
	a.type = kActionSetView;
	if (!findViewPath(view, a.path)) {
		warning("Script: DoSmolShootout: no owner for view %u", view);
		return;
	}
	LocalVariables locals;
	execute(a, call.event, call.fields, locals);
}

void Script::smIsWillShot(uint32 timer) {
	// 0x4f9bf0
	if (!_shootCur)
		return;
	int n = _shootIdx[_shootCur - 1];
	Dice dice = newDice(0x4f9c07);
	int r = roll(dice, 0, 100);
	int32 difficulty = getPreferenceValue(kPrefActionSceneDifficulty);
	bool easy = difficulty == 'E', hard = difficulty == 'H';
	int limit1 = easy ? 20 : (hard ? 60 : 40);
	int limit2 = easy ? 50 : (hard ? 80 : 65);
	bool shot = n >= 3 || (n == 1 && r < limit1) || (n == 2 && r < limit2);
	if (shot) {
		// No reference run takes this branch (docs/subsystems/smolnikoff-shootout.md): a Timer Stop, then asset 21168
		_timers.erase(timer);
		playAsset(21168, kModePlayOnce, nullptr); // the SoundObj play of 0x491de0: a plain play of the death clip
	}
}

void Script::smolGoonShot() {
	// 0x4f9d20: the goon on screen is dead; every variable is written, changed or not
	if (_shootCur >= 1 && _shootCur <= 3)
		_shootDead[_shootCur - 1] = true;
	for (int i = 0; i < 3; i++) {
		if (_shootVars[i])
			setVariable(_shootVars[i], _shootDead[i]);
	}
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

} // End of namespace XFiles
