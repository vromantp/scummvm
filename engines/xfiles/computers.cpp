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

// The computers: the FBI log-in (hooks LoginInit, LoginKeyPress, PswdConnect) and the ING query (INGInit,
// INGKeyPressed), XFiles.exe 0x470a90 to 0x4865f0. The behaviour is in docs/subsystems/computers.md; what that
// file does not say is marked TODO B-071.

/** The log-in's items: the user and password fields (Key Press) and the second pair. */
static const uint32 kLoginUserKey = 50747;
static const uint32 kLoginPasswordKey = 50748;
static const uint32 kLoginSecondKey2 = 50750;
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

/** The ING query: its text item, the caret item, the Search button's actions on Return (trigger 40395). */
static const uint32 kIngText = 50742;
static const uint32 kIngCaret = 64481;
static const uint32 kIngHiddenItem = 36320;
static const uint32 kIngReturnActions = 40838;
static const uint kIngMax = 30;
/** The caret: x = 332 + the text's width, y = 252 (0x4867e0). */
static const int kIngCaretX = 332;
static const int kIngCaretY = 252;

/** Courier New 14 (font 4): 8 px per character. TODO B-071: the colour of the fields is not read. */
static const int kFieldTextSize = 14;
static const int kFieldCharWidth = 8;
static const uint32 kFieldTextColor = 0xffffff;

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
		loginInit('C'); // TODO B-071: LoginInit('P') (0x4b2970) is not read
	else if (name == "LoginKeyPress" && args.size() >= 2 && args[0] >= 1 && args[0] <= 3)
		loginKeyPress(args[0], args[1], event, fields); // TODO B-071: 4 and 5
	else if (name == "PswdConnect" && !a.args.empty() && a.args[0].kind == kOperandVariable)
		pswdConnect(a.args[0].value);
	else if (name == "INGInit")
		ingInit();
	else if (name == "INGKeyPressed" && !args.empty())
		ingKeyPress(args[0], event, fields);
	else
		return false;
	return true;
}

bool Script::editText(uint key, Common::String &buffer, uint max) {
	// 0x486e60: Backspace and 0x2e delete the last character, also when the buffer is full. The characters
	// 0x486430 turns a key into are digits, letters, space (and - ( ) , .); a-z are upper-cased. The keys
	// arrive as virtual-key codes, whose digits and letters are those characters.
	// TODO B-071: 0x486430 is not read: the numpad, - ( ) , and . are not typed (0x2e deletes first)
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

void Script::loginInit(char mode) {
	// 0x468f00: the buffers are empty. Once the first connection was made the user name is filled in and the
	// password is next; otherwise the user name is. TODO B-071: a preference (0x41b800) does the same
	if (mode != 'C')
		return;
	_computer.user.clear();
	_computer.password.clear();
	if (getVariable(kConnectedBefore)) {
		Common::U32String name;
		if (getTextAsset(kLoginUserName, name))
			_computer.user = name.encode();
		_focus = kLoginPasswordKey;
	} else {
		_focus = kLoginUserKey;
	}
	setFieldText(kLoginUserText, _computer.user);
	setFieldText(kLoginPasswordText, Common::String());
	log("      login: " + Common::String::format("focus #%u", _focus));
}

void Script::loginKeyPress(int field, uint key, int event, const EventFields &fields) {
	// 0x4b31e0 first: Return presses the Connect button (field 3: the second pair's), Tab moves between the two
	// fields of the pair. TODO B-071: the caret item (0x4867e0) is not moved
	if (key == kVkReturn) {
		runActionList(field == 3 ? kLoginReturn2Actions : kLoginReturnActions, event, fields);
		return;
	}
	if (key == kVkTab) {
		static const uint32 kNext[3] = { kLoginPasswordKey, kLoginUserKey, kLoginSecondKey2 };
		_focus = kNext[field - 1];
		return;
	}
	switch (field) {
	case 1:
		if (editText(key, _computer.user, kLoginUserMax))
			setFieldText(kLoginUserText, _computer.user);
		break;
	case 2:
		// One * per character
		if (editText(key, _computer.password, kLoginPasswordMax))
			setFieldText(kLoginPasswordText, Common::String(_computer.password.size(), '*'));
		break;
	default:
		if (editText(key, _computer.field3, kLoginField3Max))
			setFieldText(kLoginField3Text, _computer.field3);
		break;
	}
}

void Script::pswdConnect(uint32 variableId) {
	// 0x45a3a0: the user name and the password against two text assets. The compare (0x459e00) ignores the
	// case: the editor upper-cases the keys and the texts are lower case. TODO B-071: the compare was not
	// read; the failure branch (text 92074 and more) leaves the variable 0
	Common::U32String user, password;
	if (getTextAsset(kLoginUserName, user) && getTextAsset(kLoginPassword, password) &&
			!scumm_stricmp(_computer.user.c_str(), user.encode().c_str()) &&
			!scumm_stricmp(_computer.password.c_str(), password.encode().c_str())) {
		_variables[variableId] = 1;
		if (!getVariable(kConnectedBefore))
			_variables[kConnectedBefore] = 1;
		log("      login: accepted");
	} else {
		log("      login: refused (not ported)");
	}
}

void Script::ingInit() {
	// 0x407b90: font 4 size 14, item 36320 hidden, the buffer drawn, the focus on the query. TODO B-071: whether
	// the buffer is cleared (0x6be930 against the edited 0x6be918) was not read
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

} // End of namespace XFiles
