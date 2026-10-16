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

#include "xfiles/computers.h"
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

ComputerHooks::ComputerHooks(Script &script) : _script(script), _loginState(0), _connectedBefore(false), _wrongTries(0), _mediaCounter(0) {
}

void ComputerHooks::addHooks(HookTable<Script> &t) {
	t.add("LoginInit", [](Script &s, const HookCall &c) {
		if (!c.has(1) || (char)c.args[0] != 'C')
			return false;
		s._computer.loginInit('C'); // TODO B-075: LoginInit('P') (0x4b2970) is not read
		return true;
	});
	t.add("LoginKeyPress", [](Script &s, const HookCall &c) {
		if (!c.has(2) || c.args[0] < 1 || c.args[0] > 3)
			return false;
		s._computer.loginKeyPress(c.args[0], c.args[1], c.event, c.fields); // TODO B-075: 4 and 5
		return true;
	});
	t.add("LoginClick", [](Script &s, const HookCall &c) {
		if (!c.has(1) || c.args[0] < 1 || c.args[0] > 4)
			return false;
		s._computer.loginClick(c.args[0]);
		return true;
	});
	t.add("PswdConnect", [](Script &s, const HookCall &c) {
		if (!c.isVariable(0))
			return false;
		s._computer.pswdConnect(c.action.args[0].value);
		return true;
	});
	t.add("INGInit", [](Script &s, const HookCall &c) {
		s._computer.ingInit();
		return true;
	});
	t.add("INGKeyPressed", [](Script &s, const HookCall &c) {
		if (!c.has(1))
			return false;
		s._computer.ingKeyPress(c.args[0], c.event, c.fields);
		return true;
	});
	t.add("PlayAssetAt", [](Script &s, const HookCall &c) {
		if (!c.has(3))
			return false;
		s.playAssetAt(c.args[0], c.args[1], c.args[2]);
		return true;
	});
	t.add("MediaInit", [](Script &s, const HookCall &c) {
		s._computer.mediaInit();
		return true;
	});
	// They release the screen's font only, or do nothing
	static const char *const kNothingToDo[] = {"INGDestroy", "MediaDestroy", "PswdInit", "PwbkInit"};
	for (uint i = 0; i < ARRAYSIZE(kNothingToDo); i++)
		t.add(kNothingToDo[i], [](Script &s, const HookCall &c) { return true; });
}

/** The editor of 0x486e60: a key edits the buffer (at most max characters); true if it changed. */
static bool editText(uint key, Common::String &buffer, uint max) {
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

void ComputerHooks::setFieldText(uint32 item, const Common::String &text) {
	if (text.empty()) {
		_script.clearItemText(item);
		return;
	}
	// 0x4be2c0: not wrapped
	_script.setItemText(item, Common::U32String(text), kFieldTextSize, false, kFieldTextColor, Script::kAlignLeft, false, 0);
}

const Common::String &ComputerHooks::loginBuffer(int field) const {
	switch (field) {
	case 1:
		return _user;
	case 2:
		return _password; // the caret is measured on the plain password
	case 3:
		return _field3;
	default:
		return _field4;
	}
}

void ComputerHooks::moveLoginCaret(int field) {
	// 0x4867e0: behind the text from the field's static point, only while the buffer is shorter than 24
	const Common::String &buffer = loginBuffer(field);
	if (buffer.size() >= kCaretMax)
		return;
	_script.setItemOrigin(kLoginCarets[field > 2 ? 1 : 0],
		Common::Point(kLoginOrigins[field - 1][0] + buffer.size() * kFieldCharWidth, kLoginOrigins[field - 1][1]));
}

void ComputerHooks::loginFocus(int field) {
	_loginState = field - 1;
	_script.setFocus(kLoginKeys[field - 1]);
	moveLoginCaret(field);
}

void ComputerHooks::loginInit(char mode) {
	// 0x468f00: the buffers are empty. Once the first connection was made, or with Artificial Intuition on, the
	// user name is filled in and the password is next; otherwise the user name is
	if (mode != 'C')
		return;
	_user.clear();
	_password.clear();
	_connectedBefore = _script.getVariable(kConnectedBefore) != 0;
	if (_connectedBefore || _script.isArtificialIntuitionOn()) {
		Common::U32String name;
		if (_script.getTextAsset(kLoginUserName, name))
			_user = name.encode();
		loginFocus(2);
	} else {
		loginFocus(1);
	}
	setFieldText(kLoginUserText, _user);
	setFieldText(kLoginPasswordText, Common::String());
	_script.log("      login: " + Common::String::format("focus #%u", _script.getFocus()));
}

void ComputerHooks::loginClick(int field) {
	// 0x4b1200: the focus moves to the field as Tab does, only when the other field of its pair has it
	static const int kNeededState[4] = { 1, 0, 3, 2 };
	if (_loginState == kNeededState[field - 1])
		loginFocus(field);
}

void ComputerHooks::loginKeyPress(int field, uint key, int event, const Common::Array<int32> &fields) {
	// 0x4b31e0 first: Return presses the Connect button (field 3: the second pair's), Tab moves between the two
	// fields of the pair
	if (key == kVkReturn) {
		_script.runActionList(field == 3 ? kLoginReturn2Actions : kLoginReturnActions, event, fields);
		return;
	}
	if (key == kVkTab) {
		static const int kNext[3] = { 2, 1, 4 };
		loginFocus(kNext[field - 1]);
		return;
	}
	switch (field) {
	case 1:
		if (!editText(key, _user, kLoginUserMax))
			return;
		setFieldText(kLoginUserText, _user);
		break;
	case 2:
		// One * per character
		if (!editText(key, _password, kLoginPasswordMax))
			return;
		setFieldText(kLoginPasswordText, Common::String(_password.size(), '*'));
		break;
	default:
		if (!editText(key, _field3, kLoginField3Max))
			return;
		setFieldText(kLoginField3Text, _field3);
		break;
	}
	moveLoginCaret(field);
}

bool ComputerHooks::sameLoginText(const Common::String &buffer, uint32 textAsset) {
	// 0x459e00 and 0x459fb0: two empty strings are equal, otherwise _mbsicmp
	Common::U32String text;
	Common::String other;
	if (_script.getTextAsset(textAsset, text))
		other = text.encode();
	return !scumm_stricmp(buffer.c_str(), other.c_str());
}

void ComputerHooks::pswdConnect(uint32 variableId) {
	// 0x45a3a0: the user name and the password against two text assets
	bool userOk = sameLoginText(_user, kLoginUserName);
	if (userOk && sameLoginText(_password, kLoginPassword)) {
		_script.setVariable(variableId, 1);
		_wrongTries = 0;
		if (!_connectedBefore) {
			_connectedBefore = true;
			_script.setVariable(kConnectedBefore, 1);
		}
		_script.log("      login: accepted");
		return;
	}
	_script.setVariable(variableId, 0);
	if (userOk)
		_wrongTries++;
	uint32 message = _wrongTries < kWrongTriesMax ? kMessageWrongUser : kMessageLocked;
	if (sameLoginText(_user, 92074) && sameLoginText(_password, 92076)) {
		message = 92089;
	} else if (sameLoginText(_password, 92087)) {
		if (sameLoginText(_user, 92078))
			message = 92097;
		else if (sameLoginText(_user, 92083))
			message = 92105;
		else if (sameLoginText(_user, 92085))
			message = 92113;
	}
	// 0x4be800: the popup layout is shown (a type 5 action appended to the hook's list, which the
	// exe runs without an action.run line), then the item
	_script.showLayout(kPopupLayout);
	_script.setItemEnabled(message, true);
	_script.log(Common::String::format("      login: refused (message #%u)", message));
	// Then the fields are reset as LoginInit('C') does
	loginInit('C');
}

void ComputerHooks::ingInit() {
	// 0x407b90: font 4 size 14, item 36320 hidden (0x42abc0), the kept buffer drawn, the focus on the query
	_script.setItemEnabled(kIngHiddenItem, false);
	setFieldText(kIngText, _query);
	_script.setFocus(kIngText);
}

void ComputerHooks::ingKeyPress(uint key, int event, const Common::Array<int32> &fields) {
	// 0x486470: Return runs the Search button's click; any other key edits the query, which is drawn, and the
	// caret goes behind it
	if (key == kVkReturn) {
		_script.runActionList(kIngReturnActions, event, fields);
		return;
	}
	if (!editText(key, _query, kIngMax))
		return;
	setFieldText(kIngText, _query);
	_script.setItemOrigin(kIngCaret, Common::Point(kIngCaretX + _query.size() * kFieldCharWidth, kIngCaretY));
}

void ComputerHooks::mediaInit() {
	// 0x45c320: the counter, item 55036 hidden, the focus on item 50746
	_mediaCounter = 0;
	_script.setItemEnabled(kMediaHiddenItem, false);
	_script.setFocus(kMediaFocusItem);
}

} // End of namespace XFiles
