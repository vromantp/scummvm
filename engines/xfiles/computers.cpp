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

#include "xfiles/script.h"

namespace XFiles {

// The computers: the FBI log-in (hooks LoginInit, LoginKeyPress, LoginClick, PswdConnect), the ING query (INGInit,
// INGKeyPressed), the media index (MediaInit) and PlayAssetAt, XFiles.exe 0x470a90 to 0x4865f0. The behaviour is in
// docs/subsystems/computers.md; what that file does not say is marked TODO B-075.

/** The log-in's items: the four fields (Key Press), the first pair being the FBI user and password. */
static const uint32 kLoginKeys[4] = { 50747, 50748, 50749, 50750 };
/** The caret items (0x4867e0): for fields 1 and 2, for fields 3 and 4. */
static const uint32 kLoginCarets[2] = { 64485, 64489 };
/** Where each field's text starts (the static points the focus change copies to 0x6b9158). */
static const int16 kLoginOrigins[4][2] = { { 260, 227 }, { 260, 288 }, { 264, 134 }, { 293, 160 } };
/** The caret only moves while the buffer is shorter than this. */
static const uint kCaretMax = 24;
/** Where the user name and the password are drawn. */
static const uint32 kLoginUserText = 50811;
static const uint32 kLoginPasswordText = 50828;
static const uint32 kLoginField3Text = 51351;
/** The text assets the user name and password are compared with (0x45a3a0). */
static const uint32 kLoginUserName = 57905;
static const uint32 kLoginPassword = 68915;
/** Set once the first connection was made. */
static const uint32 kConnectedBefore = 4946;
/** The Connect button's actions on Return (trigger 39475), and those of the second pair (trigger 39584). */
static const uint32 kLoginReturnActions = 39477;
static const uint32 kLoginReturn2Actions = 39586;
static const uint kLoginUserMax = 23;
static const uint kLoginPasswordMax = 23;
static const uint kLoginField3Max = 21;
/** A failed connection (0x45a3a0): the popup layout, and the message items for the other cases. */
static const uint32 kPopupLayout = 60115;
static const uint32 kMessageWrongUser = 67487;
static const uint32 kMessageLocked = 67500;
static const int kWrongTriesMax = 3;

/** The ING query: its text item, the caret item, the Search button's actions on Return (trigger 40395). */
static const uint32 kIngText = 50742;
static const uint32 kIngCaret = 64481;
static const uint32 kIngHiddenItem = 36320;
static const uint32 kIngReturnActions = 40838;
static const uint kIngMax = 30;
/** The caret: x = 332 + the text's width, y = 252 (0x4867e0). */
static const int kIngCaretX = 332;
static const int kIngCaretY = 252;

/** The media index (MediaInit): the item it hides and the item it focuses. */
static const uint32 kMediaHiddenItem = 55036;
static const uint32 kMediaFocusItem = 50746;

/** Courier New 14 (font 4): 8 px per character; colour 0x12, black. */
static const int kFieldTextSize = 14;
static const int kFieldCharWidth = 8;
static const uint32 kFieldTextColor = 0;

enum {
	kVkBack = 0x08,
	kVkTab = 0x09,
	kVkReturn = 0x0d,
	kVkSpace = 0x20,
	kVkDelete = 0x2e
};

bool Script::callComputer(const VCAction &a, const Common::Array<int32> &args, int event, const EventFields &fields) {
	const Common::String &name = a.function;
	if (name == "LoginInit" && !args.empty() && (char)args[0] == 'C')
		loginInit('C'); // TODO B-075: LoginInit('P') (0x4b2970) is not read
	else if (name == "LoginKeyPress" && args.size() >= 2 && args[0] >= 1 && args[0] <= 3)
		loginKeyPress(args[0], args[1], event, fields); // TODO B-075: 4 and 5
	else if (name == "LoginClick" && !args.empty() && args[0] >= 1 && args[0] <= 4)
		loginClick(args[0]);
	else if (name == "PswdConnect" && !a.args.empty() && a.args[0].kind == kOperandVariable)
		pswdConnect(a.args[0].value);
	else if (name == "INGInit")
		ingInit();
	else if (name == "INGKeyPressed" && !args.empty())
		ingKeyPress(args[0], event, fields);
	else if (name == "PlayAssetAt" && args.size() >= 3)
		playAssetAt(args[0], args[1], args[2]);
	else if (name == "MediaInit")
		mediaInit();
	else if (name == "INGDestroy" || name == "MediaDestroy" || name == "PswdInit" || name == "PwbkInit")
		return true; // release the screen's font only, or nothing
	else
		return false;
	return true;
}

bool Script::editText(uint key, Common::String &buffer, uint max) {
	// 0x486e60: Backspace and 0x2e delete the last character, also when the buffer is full. The characters
	// 0x486430 turns a key into are digits, letters, space (and - ( ) , .); a-z are upper-cased. The keys
	// arrive as virtual-key codes, whose digits and letters are those characters.
	// TODO B-075: 0x486430 is not read: the numpad, - ( ) , and . are not typed (0x2e deletes first)
	if (key == kVkBack || key == kVkDelete) {
		if (buffer.empty())
			return false;
		buffer.deleteLastChar();
		return true;
	}
	bool accepted = (key >= '0' && key <= '9') || (key >= 'A' && key <= 'Z') || (key >= 'a' && key <= 'z') ||
		key == kVkSpace;
	if (!accepted || buffer.size() >= max)
		return false;
	if (key >= 'a' && key <= 'z')
		key = key - 'a' + 'A';
	buffer += (char)key;
	return true;
}

void Script::setFieldText(uint32 item, const Common::String &text) {
	if (text.empty()) {
		_itemTexts.erase(item);
		return;
	}
	// 0x4be2c0: not wrapped
	setItemText(item, Common::U32String(text), kFieldTextSize, false, kFieldTextColor, kAlignLeft, false, 0);
}

const Common::String &Script::loginBuffer(int field) const {
	switch (field) {
	case 1:
		return _computer.user;
	case 2:
		return _computer.password; // the caret is measured on the plain password
	case 3:
		return _computer.field3;
	default:
		return _computer.field4;
	}
}

void Script::moveLoginCaret(int field) {
	// 0x4867e0: behind the text from the field's static point, only while the buffer is shorter than 24
	const Common::String &buffer = loginBuffer(field);
	if (buffer.size() >= kCaretMax)
		return;
	_itemOrigins[kLoginCarets[field > 2 ? 1 : 0]] = Common::Point(
		kLoginOrigins[field - 1][0] + buffer.size() * kFieldCharWidth, kLoginOrigins[field - 1][1]);
}

void Script::loginFocus(int field) {
	_computer.loginState = field - 1;
	_focus = kLoginKeys[field - 1];
	moveLoginCaret(field);
}

void Script::loginInit(char mode) {
	// 0x468f00: the buffers are empty. Once the first connection was made, or with Artificial Intuition on, the
	// user name is filled in and the password is next; otherwise the user name is
	if (mode != 'C')
		return;
	_computer.user.clear();
	_computer.password.clear();
	_computer.connectedBefore = getVariable(kConnectedBefore) != 0;
	if (_computer.connectedBefore || (_prefArtificialIntuition && getVariable(_prefArtificialIntuition))) {
		Common::U32String name;
		if (getTextAsset(kLoginUserName, name))
			_computer.user = name.encode();
		loginFocus(2);
	} else {
		loginFocus(1);
	}
	setFieldText(kLoginUserText, _computer.user);
	setFieldText(kLoginPasswordText, Common::String());
	log("      login: " + Common::String::format("focus #%u", _focus));
}

void Script::loginClick(int field) {
	// 0x4b1200: the focus moves to the field as Tab does, only when the other field of its pair has it
	static const int kNeededState[4] = { 1, 0, 3, 2 };
	if (_computer.loginState == kNeededState[field - 1])
		loginFocus(field);
}

void Script::loginKeyPress(int field, uint key, int event, const EventFields &fields) {
	// 0x4b31e0 first: Return presses the Connect button (field 3: the second pair's), Tab moves between the two
	// fields of the pair
	if (key == kVkReturn) {
		runActionList(field == 3 ? kLoginReturn2Actions : kLoginReturnActions, event, fields);
		return;
	}
	if (key == kVkTab) {
		static const int kNext[3] = { 2, 1, 4 };
		loginFocus(kNext[field - 1]);
		return;
	}
	switch (field) {
	case 1:
		if (!editText(key, _computer.user, kLoginUserMax))
			return;
		setFieldText(kLoginUserText, _computer.user);
		break;
	case 2:
		// One * per character
		if (!editText(key, _computer.password, kLoginPasswordMax))
			return;
		setFieldText(kLoginPasswordText, Common::String(_computer.password.size(), '*'));
		break;
	default:
		if (!editText(key, _computer.field3, kLoginField3Max))
			return;
		setFieldText(kLoginField3Text, _computer.field3);
		break;
	}
	moveLoginCaret(field);
}

bool Script::sameLoginText(const Common::String &buffer, uint32 textAsset) {
	// 0x459e00 and 0x459fb0: two empty strings are equal, otherwise _mbsicmp
	Common::U32String text;
	Common::String other;
	if (getTextAsset(textAsset, text))
		other = text.encode();
	return !scumm_stricmp(buffer.c_str(), other.c_str());
}

void Script::pswdConnect(uint32 variableId) {
	// 0x45a3a0: the user name and the password against two text assets
	bool userOk = sameLoginText(_computer.user, kLoginUserName);
	if (userOk && sameLoginText(_computer.password, kLoginPassword)) {
		_variables[variableId] = 1;
		_computer.wrongTries = 0;
		if (!_computer.connectedBefore) {
			_computer.connectedBefore = true;
			_variables[kConnectedBefore] = 1;
		}
		log("      login: accepted");
		return;
	}
	_variables[variableId] = 0;
	if (userOk)
		_computer.wrongTries++;
	uint32 message = _computer.wrongTries < kWrongTriesMax ? kMessageWrongUser : kMessageLocked;
	if (sameLoginText(_computer.user, 92074) && sameLoginText(_computer.password, 92076)) {
		message = 92089;
	} else if (sameLoginText(_computer.password, 92087)) {
		if (sameLoginText(_computer.user, 92078))
			message = 92097;
		else if (sameLoginText(_computer.user, 92083))
			message = 92105;
		else if (sameLoginText(_computer.user, 92085))
			message = 92113;
	}
	// 0x4be800: the popup layout is shown (a type 5 action appended to the hook's list, which the
	// exe runs without an action.run line), then the item
	showLayout(kPopupLayout);
	setItemEnabled(message, true);
	log(Common::String::format("      login: refused (message #%u)", message));
	// Then the fields are reset as LoginInit('C') does
	loginInit('C');
}

void Script::ingInit() {
	// 0x407b90: font 4 size 14, item 36320 hidden (0x42abc0), the kept buffer drawn, the focus on the query
	setItemEnabled(kIngHiddenItem, false);
	setFieldText(kIngText, _computer.query);
	_focus = kIngText;
}

void Script::ingKeyPress(uint key, int event, const EventFields &fields) {
	// 0x486470: Return runs the Search button's click; any other key edits the query, which is drawn, and the
	// caret goes behind it
	if (key == kVkReturn) {
		runActionList(kIngReturnActions, event, fields);
		return;
	}
	if (!editText(key, _computer.query, kIngMax))
		return;
	setFieldText(kIngText, _computer.query);
	_itemOrigins[kIngCaret] = Common::Point(kIngCaretX + _computer.query.size() * kFieldCharWidth, kIngCaretY);
}

void Script::mediaInit() {
	// 0x45c320: the counter, item 55036 hidden, the focus on item 50746
	_computer.mediaCounter = 0;
	setItemEnabled(kMediaHiddenItem, false);
	_focus = kMediaFocusItem;
}

void Script::playAssetAt(uint32 asset, int x, int y) {
	// 0x491de0: starts the asset as a Do Asset action would, unless it is already playing (0x491380).
	// TODO B-075: the point (x, y) is not used: the engine has no placement for a clip
	if (_sounds.contains(asset) || Common::find(_videoQueue.begin(), _videoQueue.end(), asset) != _videoQueue.end())
		return;
	playAsset(asset, kModePlayOnce, nullptr);
}

} // End of namespace XFiles
