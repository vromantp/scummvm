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

// The computers: the FBI log-in (hooks LoginInit, LoginKeyPress, LoginClick, PswdConnect), the ING search (INGInit,
// INGKeyPressed, INGSearch, INGClear), the media index (MediaInit, MediaSearch, MSKeyPressed), the Powerbook log-in
// (LoginInit('P'), LoginKeyPress 3 to 5, PwbkOK), SBase and PlayAssetAt, XFiles.exe 0x470a90 to 0x4865f0. The
// behaviour is in docs/subsystems/computers.md.

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

/** The photo viewer: Download and its greyed twin, Delete and its greyed twin, the popup message, limits (0x4e2870). */
static const uint32 kPhotoDownloadItem = 51397;
static const uint32 kPhotoDownloadGrey = 70853;
static const uint32 kPhotoDeleteItem = 51398;
static const uint32 kPhotoDeleteGrey = 70855;
static const uint32 kPhotoFullMessage = 67505;
static const uint kPhotoMax = 60;
static const uint kPhotoThumbs = 20;
static const uint kPhotoRow = 5;

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

/** The Powerbook log-in (LoginInit('P'), PwbkOK): the texts, the field texts and the popup messages. */
static const uint32 kPwbkUserName = 68913;
static const uint32 kPwbkPassword = 68910;
static const uint32 kLoginMaskText = 51352;
static const uint32 kLoginField3Item = 50749;
static const uint32 kLoginField4Item = 50750;
static const uint32 kMessageLockedPwbk = 89627;
static const int kPwbkTriesMax = 3;
static const uint kLoginPasswordPwbkMax = 17;
/** The hidden command line (LoginKeyPress 5): its item with the commands' texts, and its size. */
static const uint32 kCommandItem = 71949;
static const uint kCommandMax = 50;

/** The ING search: the variables the hook reads, the results and the texts. */
static const uint32 kIngResultText = 58344;
static const uint32 kIngNotFound = 58348;
static const uint32 kIngNotFoundGone = 91191;
static const uint32 kIngNameGone = 2107;
static const int kIngNameGoneEntry = 13;
static const uint32 kIngDefaultRecord = 38133;
static const uint32 kIngFoundActions = 90694;
static const uint32 kIngNotFoundActions = 90038;
static const uint32 kIngPlateG = 88648;
static const uint32 kIngPlateZ = 88650;
static const uint32 kIngPlateGText = 58350;
static const uint32 kIngPlateZText = 88652;
static const uint32 kIngPlateNote = 63294;
static const uint32 kIngPlateSeen = 4713;
static const uint32 kIngCookVariable = 5095;
static const uint kIngCookRecord = 3;
static const uint32 kIngCookActions = 90360;
static const uint32 kIngPhoneText = 89863;
static const uint32 kIngPhoneSpecialText = 42225;
static const uint32 kIngPhoneSpecialActions = 75767;
static const uint32 kIngPhoneSpecialVariable = 5051;
static const int kIngPhoneLength = 10;
static const int kIngPlateSize = 16;
static const uint32 kColorBlue = 0x0000ff; ///< Colour 6 (0x429930)

/** The media index: the query's echo, the caret and the Return actions (trigger 59660). */
static const uint32 kMediaEcho = 55039;
static const uint32 kMediaCaret = 64442;
static const uint32 kMediaReturnActions = 59661;
static const uint kMediaMax = 25;
static const int kMediaCaretX = 377;
static const int kMediaCaretY = 50;
static const uint32 kMediaArticleSpecial = 55591;
static const uint32 kMediaArticleEnables = 21068;

/** SBase: the focused item, the caret and the buffer's size. */
static const uint32 kSbaseFocusItem = 55825;
static const uint32 kSbaseCaret = 64444;
static const uint kSbaseMax = 31;
static const int kSbaseCaretX = 170;
static const int kSbaseCaretStep = 10;
static const int kSbaseCaretY = 200;

/** Courier New 14 (font 4, the fields' default size): 8 px per character; colour 0x12, black (0). */
static const int kFieldCharWidth = 8;

enum {
	kVkBack = 0x08,
	kVkTab = 0x09,
	kVkReturn = 0x0d,
	kVkSpace = 0x20,
	kVkDelete = 0x2e
};

// clang-format off
/** The ING results screen (docs/data/computer-tables.tsv, from XFiles.exe 0x659a80 and others). */
static const uint32 kIngLabels[8] = {
	35551, 35553, 35554, 35555, 35556, 35557, 35558, 35559
};

static const uint32 kIngFields[8] = {
	35560, 35562, 35563, 35564, 35565, 35566, 35567, 35568
};

struct IngPhone {
	uint32 text;
	int result; ///< An index into kIngNames, -1 or -2
};

static const IngPhone kIngPhones[12] = {
	{ 67269, 0 }, { 67271, 1 }, { 67273, 2 }, { 67275, 10 },
	{ 67279, 3 }, { 67281, 17 }, { 67283, 7 }, { 67287, 12 },
	{ 89560, 15 }, { 89562, 16 }, { 67441, -1 }, { 83868, -2 }
};

struct IngName {
	uint32 name;   ///< The item whose asset list holds the database letters, then the names
	uint32 field;  ///< The item whose asset list holds the record's 8 fields
	uint32 showA;
	uint32 showB;
	uint32 text;   ///< The record text, 0 for kIngDefaultRecord
};

static const IngName kIngNames[60] = {
	{ 60627, 79934, 15286, 15124, 0 }, { 60628, 79936, 17337, 15122, 0 },
	{ 60629, 79937, 17339, 15124, 81304 }, { 60630, 79938, 17341, 15122, 81305 },
	{ 60631, 79939, 17343, 15124, 0 }, { 60632, 79940, 17345, 15122, 0 },
	{ 60633, 79941, 17347, 15124, 81321 }, { 60634, 79942, 17349, 15122, 81308 },
	{ 60635, 79943, 17351, 15124, 81320 }, { 60636, 79944, 17353, 15122, 0 },
	{ 60637, 79945, 17355, 15124, 0 }, { 60638, 79946, 17357, 15122, 0 },
	{ 60639, 79947, 17359, 15124, 81310 }, { 60640, 79948, 17361, 15122, 81311 },
	{ 60641, 79949, 17363, 15124, 0 }, { 60642, 79950, 50591, 15124, 81306 },
	{ 60643, 79951, 50589, 15122, 81307 }, { 60644, 79952, 50587, 15124, 81309 },
	{ 80008, 79994, 79806, 15122, 69917 }, { 88599, 88600, 79818, 15124, 88643 },
	{ 89079, 89080, 79830, 15122, 89077 }, { 80011, 79972, 79810, 15124, 70043 },
	{ 80025, 79986, 79840, 15124, 70007 }, { 80015, 79976, 79822, 15124, 82206 },
	{ 80019, 79980, 79836, 15122, 70061 }, { 80009, 79970, 80362, 15124, 69935 },
	{ 80027, 79988, 79844, 15122, 70099 }, { 80010, 79971, 79846, 15122, 69953 },
	{ 80013, 79974, 79814, 15122, 69971 }, { 80014, 79975, 79816, 15124, 69989 },
	{ 80016, 79977, 79820, 15122, 70117 }, { 80018, 79979, 79826, 15124, 70025 },
	{ 80022, 79983, 79832, 15124, 70080 }, { 80024, 79985, 79838, 15122, 70135 },
	{ 88597, 88598, 79828, 15122, 88637 }, { 89596, 89597, 79824, 15124, 89578 },
	{ 89598, 89599, 79848, 15122, 89594 }, { 80017, 79978, 80366, 15122, 86076 },
	{ 80020, 79981, 79852, 15124, 86106 }, { 80021, 79982, 79933, 15122, 86094 },
	{ 80023, 79984, 79919, 15124, 86096 }, { 80026, 79987, 79896, 15122, 86090 },
	{ 80030, 79993, 79929, 15124, 86112 }, { 80031, 79991, 79911, 15122, 86098 },
	{ 80032, 79992, 79892, 15124, 86100 }, { 87503, 87504, 79915, 15122, 86078 },
	{ 87505, 87506, 79900, 15124, 86092 }, { 87507, 87508, 79894, 15122, 86086 },
	{ 87515, 87516, 79906, 15124, 86080 }, { 87517, 87518, 79925, 15124, 86088 },
	{ 87523, 87524, 79898, 15122, 86082 }, { 87525, 87526, 79902, 15124, 86108 },
	{ 87527, 87528, 79913, 15122, 86084 }, { 87535, 87536, 79927, 15124, 86102 },
	{ 87537, 87538, 79921, 15122, 86074 }, { 87539, 87540, 79904, 15124, 86072 },
	{ 87541, 87542, 79917, 15122, 86110 }, { 87543, 87544, 79931, 15124, 86104 },
	{ 90721, 90722, 80368, 15122, 90727 }, { 91148, 91149, 79923, 15124, 91154 }
};

/** The media index's items: member 0 is the article, the others the keywords. */
static const uint32 kMediaItems[16] = {
	55604, 55574, 55575, 55576, 55577, 55578, 55579, 55580,
	55581, 55582, 91114, 91117, 91118, 91125, 91127, 91131
};

/** Powerbook command line: the trigger and the action list for each member of item 71949. */
struct LoginCommand {
	uint32 trigger;
	uint32 actions;
};

static const LoginCommand kLoginCommands[26] = {
	{ 71960, 71961 }, { 74704, 74706 }, { 65501, 65503 }, { 82108, 82109 },
	{ 83057, 84102 }, { 65506, 65508 }, { 84564, 84566 }, { 84569, 84570 },
	{ 84572, 84573 }, { 84577, 84578 }, { 84580, 84581 }, { 84583, 84584 },
	{ 84586, 84587 }, { 84589, 84590 }, { 85523, 85524 }, { 85538, 85539 },
	{ 85590, 85591 }, { 85593, 85594 }, { 85596, 85597 }, { 85599, 85600 },
	{ 85602, 85603 }, { 85605, 85606 }, { 85611, 85612 }, { 58704, 58706 },
	{ 86353, 86354 }, { 86642, 86643 }
};

/** SBase: the item shown for each typed character. */
static const uint32 kSbaseItems[31] = {
	55838, 55840, 55842, 55844, 55846, 55848, 55850, 55852,
	55854, 55856, 55858, 55860, 55862, 55864, 55866, 55868,
	55870, 55874, 55876, 55878, 55880, 55882, 55884, 55886,
	55888, 55890, 55892, 55894, 55896, 55898, 55900
};
// clang-format on

ComputerHooks::ComputerHooks(Script &script) : _script(script), _loginState(0), _connectedBefore(false), _wrongTries(0), _pwbkTries(0), _record(0), _cookShown(false), _recordShown(false), _sbaseCount(0), _photosDownloaded(0), _photoOffset(0), _photoEnlarged(-1), _photoOpen(false) {
}

void ComputerHooks::addHooks(HookTable<Script> &t) {
	t.add("LoginInit", [](Script &s, const HookCall &c) {
		if (!c.has(1) || ((char)c.args[0] != 'C' && (char)c.args[0] != 'P'))
			return false;
		s._computer.loginInit((char)c.args[0]);
		return true;
	});
	t.add("LoginKeyPress", [](Script &s, const HookCall &c) {
		if (!c.has(2) || c.args[0] < 1 || c.args[0] > 5)
			return false;
		s._computer.loginKeyPress(c.args[0], c.args[1], c.event, c.fields);
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
	t.add("INGSearch", [](Script &s, const HookCall &c) {
		if (!c.has(2))
			return false;
		s._computer.ingSearch((char)c.args[0], (char)c.args[1], c.event, c.fields);
		return true;
	});
	t.add("INGClear", [](Script &s, const HookCall &c) {
		s._computer.ingClear();
		return true;
	});
	t.add("INGDisplayCook", [](Script &s, const HookCall &c) {
		s._computer.ingDisplayCook(c.event, c.fields);
		return true;
	});
	t.add("MediaSearch", [](Script &s, const HookCall &c) {
		s._computer.mediaSearch(c.event, c.fields);
		return true;
	});
	t.add("MSKeyPressed", [](Script &s, const HookCall &c) {
		if (!c.has(1))
			return false;
		s._computer.mediaKeyPress(c.args[0], c.event, c.fields);
		return true;
	});
	t.add("PwbkOK", [](Script &s, const HookCall &c) {
		if (!c.isVariable(0))
			return false;
		s._computer.pwbkOK(c.action.args[0].value);
		return true;
	});
	t.add("SBaseInit", [](Script &s, const HookCall &c) {
		s._computer.sbaseInit();
		return true;
	});
	t.add("SBaseKeyPress", [](Script &s, const HookCall &c) {
		if (!c.has(1))
			return false;
		s._computer.sbaseKeyPress(c.args[0]);
		return true;
	});
	t.add("PhotoInit", [](Script &s, const HookCall &c) {
		s._computer.photoInit();
		return true;
	});
	t.add("PhotoDownload", [](Script &s, const HookCall &c) {
		s._computer.photoDownload();
		return true;
	});
	t.add("PhotoEnlarge", [](Script &s, const HookCall &c) {
		if (!c.has(1))
			return false;
		s._computer.photoEnlarge(c.args[0]);
		return true;
	});
	t.add("PhotoDelete", [](Script &s, const HookCall &c) {
		s._computer.photoDelete();
		return true;
	});
	t.add("PhotoScroll", [](Script &s, const HookCall &c) {
		if (!c.has(1))
			return false;
		s._computer.photoScroll(c.args[0] != 0);
		return true;
	});
	t.add("PhotoClose", [](Script &s, const HookCall &c) {
		s._computer.photoClose();
		return true;
	});
	// PhotoForward returns at once (0x4dfa10)
	t.add("PhotoForward", [](Script &s, const HookCall &c) { return true; });
	// They release the screen's font only, or do nothing
	static const char *const kNothingToDo[] = {"INGDestroy", "MediaDestroy", "PswdInit", "PwbkInit"};
	for (uint i = 0; i < ARRAYSIZE(kNothingToDo); i++)
		t.add(kNothingToDo[i], [](Script &s, const HookCall &c) { return true; });
}

/** The editor of 0x486e60: a key edits the buffer (at most max characters); true if it changed. */
static bool editText(uint key, Common::String &buffer, uint max) {
	// 0x486e60: Backspace and 0x2e ('.' or the Delete key) delete the last character, also when the buffer is
	// full. The key is the character of WM_CHAR (0x486430): digits, letters, space and - ( ) , are appended, and
	// a-z are upper-cased
	if (key == kVkBack || key == kVkDelete) {
		if (buffer.empty())
			return false;
		buffer.deleteLastChar();
		return true;
	}
	bool accepted = (key >= '0' && key <= '9') || (key >= 'A' && key <= 'Z') || (key >= 'a' && key <= 'z');
	if (key == kVkSpace || key == '-' || key == '(' || key == ')' || key == ',')
		accepted = true;
	if (!accepted || buffer.size() >= max)
		return false;
	if (key >= 'a' && key <= 'z')
		key = key - 'a' + 'A';
	buffer += (char)key;
	return true;
}

void ComputerHooks::setFieldText(uint32 item, const Common::String &text, int size, uint32 color) {
	if (text.empty()) {
		_script.clearItemText(item);
		return;
	}
	// 0x4be2c0: not wrapped
	_script.setItemText(item, Common::U32String(text), size, false, color, Script::kAlignLeft, false, 0);
}

void ComputerHooks::setFieldAsset(uint32 item, uint32 asset, int size, uint32 color) {
	// 0x4be1e0 / 0x4be180: the text of an asset; an asset without one clears the item
	Common::U32String text;
	if (!loadText(asset, text) || text.empty()) {
		_script.clearItemText(item);
		return;
	}
	_script.setItemText(item, text, size, false, color, Script::kAlignLeft, false, 0);
}

bool ComputerHooks::loadText(uint32 asset, Common::U32String &text) {
	// 0x45f690: a file asset's file first (getTextAsset would return its name)
	return _script.readTextFile(asset, text) || _script.getTextAsset(asset, text);
}

void ComputerHooks::itemMembers(uint32 item, Common::Array<uint32> &members) {
	// 0x4654a0: the asset list of the item (FindObject(79, item)), in order, with no enabled filter
	VCInterfaceItem data;
	members.clear();
	if (_script._data.getInterfaceItem(item, data))
		_script._data.getMembers(data.assets, members);
}

const Common::String &ComputerHooks::loginBuffer(int field) const {
	switch (field) {
	case 1:
		return _user;
	case 2:
		return _password; // the caret is measured on the plain password
	case 3:
		return _user; // the second pair shares the buffers of the first
	default:
		return _password;
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
	if (mode == 'P') {
		// 0x4b2970: the Powerbook log-in, fields 3 and 4
		Common::U32String name;
		_user.clear();
		if (_script.getTextAsset(kPwbkUserName, name))
			_user = name.encode();
		_password.clear();
		loginFocus(3);
		setFieldText(kLoginField3Text, _user);
		setFieldText(kLoginMaskText, Common::String());
		// Past the tries, the fields are hidden
		if (_pwbkTries > kPwbkTriesMax) {
			_script.setItemEnabled(kLoginField3Item, false);
			_script.setItemEnabled(kLoginField4Item, false);
		}
		return;
	}
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
	if (field == 5) {
		commandKeyPress(key, event, fields);
		return;
	}
	// 0x4b31e0 first: Return presses the Connect button (fields 3 and 4: the second pair's), Tab moves between the
	// two fields of the pair
	if (key == kVkReturn) {
		_script.runActionList(field > 2 ? kLoginReturn2Actions : kLoginReturnActions, event, fields);
		return;
	}
	if (key == kVkTab) {
		static const int kNext[4] = {2, 1, 4, 3};
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
	case 3:
		if (!editText(key, _user, kLoginField3Max))
			return;
		setFieldText(kLoginField3Text, _user);
		break;
	default:
		if (!editText(key, _password, kLoginPasswordPwbkMax))
			return;
		setFieldText(kLoginMaskText, Common::String(_password.size(), '*'));
		break;
	}
	moveLoginCaret(field);
}

void ComputerHooks::commandKeyPress(uint key, int event, const Common::Array<int32> &fields) {
	// 0x4b2f50: Return compares the buffer with the texts of item 71949's assets and runs the pair of the one it
	// equals; the buffer is emptied either way. Then the key edits the buffer
	if (key == kVkReturn) {
		Common::Array<uint32> members;
		itemMembers(kCommandItem, members);
		uint index = 0;
		for (uint i = 0; i < members.size(); i++) {
			if (!members[i])
				continue;
			if (sameText(_command, members[i]) && index < ARRAYSIZE(kLoginCommands)) {
				_command.clear();
				_script.runActionList(kLoginCommands[index].actions, event, fields);
				break;
			}
			index++;
		}
		_command.clear();
	}
	editText(key, _command, kCommandMax);
}

void ComputerHooks::pwbkOK(uint32 variableId) {
	// 0x4b2db0: right user and password; the tries go up either way, and the popup shows
	if (sameText(_user, kPwbkUserName) && sameText(_password, kPwbkPassword))
		_script.setVariable(variableId, 1);
	_pwbkTries++;
	uint32 message = _pwbkTries <= kPwbkTriesMax ? kMessageWrongUser : kMessageLockedPwbk;
	_script.showLayout(kPopupLayout);
	_script.setItemEnabled(message, true);
	loginInit('P');
}

void ComputerHooks::photoRebuild() {
	// 0x4e39f0: a thumbnail for each downloaded photo from the scroll offset on, at most 20. Pictures are not drawn,
	// so only the photos they stand for are kept
	_photoThumbs.clear();
	uint skip = _photoOffset;
	const Common::Array<VCPhoto> &photos = _script._photos;
	for (uint i = 0; i < photos.size() && _photoThumbs.size() < kPhotoThumbs; i++) {
		if (!photos[i].downloaded)
			continue;
		if (skip) {
			skip--;
			continue;
		}
		_photoThumbs.push_back(i);
	}
}

void ComputerHooks::photoInit() {
	// 0x4e2870: the photos downloaded are counted, the others (n) decide which of Download and its grey twin shows.
	// The photos are the Script's list: a restored save's, and those the camera made (B-140)
	_photoOpen = true;
	_photoOffset = 0;
	_photoEnlarged = -1;
	_photosDownloaded = 0;
	uint notDownloaded = 0;
	for (uint i = 0; i < _script._photos.size(); i++) {
		if (_script._photos[i].downloaded)
			_photosDownloaded++;
		else
			notDownloaded++;
	}
	_script.setItemEnabled(kPhotoDownloadGrey, notDownloaded == 0);
	_script.setItemEnabled(kPhotoDownloadItem, notDownloaded != 0);
	_script.setItemEnabled(kPhotoDeleteGrey, true);
	_script.setItemEnabled(kPhotoDeleteItem, false);
	photoRebuild();
}

void ComputerHooks::photoDownload() {
	// 0x4e3130: nothing unless a photo is not downloaded; at 60 downloaded the popup and stop
	Common::Array<VCPhoto> &photos = _script._photos;
	uint notDownloaded = 0;
	for (uint i = 0; i < photos.size(); i++) {
		if (!photos[i].downloaded)
			notDownloaded++;
	}
	if (!notDownloaded)
		return;
	for (uint i = 0; i < photos.size(); i++) {
		if (photos[i].downloaded)
			continue;
		if (_photosDownloaded >= kPhotoMax) {
			_script.showLayout(kPopupLayout);
			_script.setItemEnabled(kPhotoFullMessage, true);
			break;
		}
		photos[i].downloaded = true;
		_photosDownloaded++;
		notDownloaded--;
	}
	if (!notDownloaded) {
		_script.setItemEnabled(kPhotoDownloadGrey, true);
		_script.setItemEnabled(kPhotoDownloadItem, false);
	}
	// GameState_SetPhotosOnCamera(n): the photos left (those not downloaded)
	_script._photosOnCamera = notDownloaded;
	_photoOffset = 0;
	photoRebuild();
}

void ComputerHooks::photoEnlarge(int slot) {
	// 0x4e33e0: only for a thumbnail that exists; the picture itself is not drawn
	if (slot < 0 || slot >= (int)_photoThumbs.size() || (uint)slot >= _photosDownloaded - _photoOffset)
		return;
	_photoEnlarged = _photoThumbs[slot];
	_script.setItemEnabled(kPhotoDeleteGrey, false);
	_script.setItemEnabled(kPhotoDeleteItem, true);
}

void ComputerHooks::photoDelete() {
	// 0x4e3630: only with an enlarged picture; its photo leaves the database, the later thumbnails move back
	if (_photoEnlarged < 0)
		return;
	_script._photos.remove_at(_photoEnlarged);
	_photoEnlarged = -1;
	_photosDownloaded--;
	photoRebuild();
	_script.setItemEnabled(kPhotoDeleteGrey, true);
	_script.setItemEnabled(kPhotoDeleteItem, false);
}

void ComputerHooks::photoScroll(bool down) {
	// 0x4e2be0 (up) and 0x4e2e80 (down, only with 20 thumbnails): five photos, a row; no item changes
	if (!down && _photoOffset > 0)
		_photoOffset -= kPhotoRow;
	else if (down && _photoThumbs.size() == kPhotoThumbs)
		_photoOffset += kPhotoRow;
	else
		return;
	photoRebuild();
}

void ComputerHooks::photoClose() {
	// 0x4e2a70: the pictures go; the counters stay until the next PhotoInit
	_photoThumbs.clear();
	_photoEnlarged = -1;
	_photoOpen = false;
}

uint32 ComputerHooks::getPhotoSlotItem(uint slot) {
	static const uint32 kSlots[kPhotoThumbs] = {51376, 51378, 51379, 51377, 51380, 51381, 51384, 51385, 51386, 51387,
												51382, 51388, 51389, 51390, 51391, 51383, 51392, 51393, 51394, 51395};
	return slot < kPhotoThumbs ? kSlots[slot] : 0;
}

bool ComputerHooks::sameText(const Common::String &buffer, uint32 textAsset) {
	// 0x459e00 and 0x459fb0: two empty strings are equal, otherwise _mbsicmp
	Common::U32String text;
	Common::String other;
	if (_script.getTextAsset(textAsset, text))
		other = text.encode();
	return !scumm_stricmp(buffer.c_str(), other.c_str());
}

void ComputerHooks::pswdConnect(uint32 variableId) {
	// 0x45a3a0: the user name and the password against two text assets
	bool userOk = sameText(_user, kLoginUserName);
	if (userOk && sameText(_password, kLoginPassword)) {
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
	if (sameText(_user, 92074) && sameText(_password, 92076)) {
		message = 92089;
	} else if (sameText(_password, 92087)) {
		if (sameText(_user, 92078))
			message = 92097;
		else if (sameText(_user, 92083))
			message = 92105;
		else if (sameText(_user, 92085))
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
	moveIngCaret();
}

void ComputerHooks::moveIngCaret() {
	_script.setItemOrigin(kIngCaret, Common::Point(kIngCaretX + _query.size() * kFieldCharWidth, kIngCaretY));
}

void ComputerHooks::moveMediaCaret() {
	// 0x498080: only while the query is shorter than 26
	if (_mediaQuery.size() > kMediaMax)
		return;
	_script.setItemOrigin(kMediaCaret, Common::Point(kMediaCaretX + _mediaQuery.size() * kFieldCharWidth, kMediaCaretY));
}

// -- ING search ---------------------------------------------------------------

/** The query without its spaces, - ( and ): the plate and phone searches (0x4fa880, 0x4fac40). */
static Common::String stripped(const Common::String &query) {
	Common::String result;
	for (uint i = 0; i < query.size(); i++) {
		char c = query[i];
		if (c != ' ' && c != '-' && c != '(' && c != ')')
			result += c;
	}
	return result;
}

void ComputerHooks::clearResults() {
	// 0x42b0c0
	for (uint i = 0; i < ARRAYSIZE(kIngNames); i++) {
		_script.setItemEnabled(kIngNames[i].showA, false);
		_script.setItemEnabled(kIngNames[i].showB, false);
	}
	for (uint i = 0; i < ARRAYSIZE(kIngFields); i++)
		_script.clearItemText(kIngFields[i]);
	_script._pda.replaceScrollText(Common::U32String());
	for (uint i = 0; i < ARRAYSIZE(kIngLabels); i++)
		_script.setItemEnabled(kIngLabels[i], false);
}

void ComputerHooks::noRecord() {
	// After a special result: no record is shown
	_record = 0;
	_script.setItemEnabled(kIngHiddenItem, false);
	_recordShown = false;
}

void ComputerHooks::showRecord(uint index) {
	// 0x467070
	const IngName &entry = kIngNames[index];
	_script.clearItemText(kIngResultText);
	for (uint i = 0; i < ARRAYSIZE(kIngNames); i++) {
		_script.setItemEnabled(kIngNames[i].showA, false);
		_script.setItemEnabled(kIngNames[i].showB, false);
	}
	_script.setItemEnabled(entry.showA, true);
	_script.setItemEnabled(entry.showB, true);
	Common::U32String text;
	loadText(entry.text ? entry.text : kIngDefaultRecord, text);
	_script._pda.replaceScrollText(text);
	Common::Array<uint32> members;
	itemMembers(entry.field, members);
	for (uint i = 0; i < ARRAYSIZE(kIngFields); i++) {
		if (i < members.size())
			setFieldAsset(kIngFields[i], members[i]);
		else
			_script.clearItemText(kIngFields[i]);
	}
	for (uint i = 0; i < ARRAYSIZE(kIngLabels); i++)
		_script.setItemEnabled(kIngLabels[i], true);
	_record = members.empty() ? 0 : members[0];
	_recordShown = true;
}

bool ComputerHooks::searchName(const Common::String &query, char database, uint32 &notFoundText) {
	// 0x465810: the first entry with a name equal to the query decides
	for (uint i = 0; i < ARRAYSIZE(kIngNames); i++) {
		Common::Array<uint32> members;
		itemMembers(kIngNames[i].name, members);
		bool match = false;
		for (uint j = 1; j < members.size() && !match; j++)
			match = sameText(query, members[j]);
		if (!match)
			continue;
		if ((int)i == kIngNameGoneEntry && _script.getVariable(kIngNameGone) == 0) {
			notFoundText = kIngNotFoundGone;
			return false;
		}
		Common::U32String databases;
		if (!loadText(members[0], databases) || databases.encode().find(database) == Common::String::npos)
			return false;
		showRecord(i);
		return true;
	}
	return false;
}

bool ComputerHooks::searchPlate(const Common::String &query, char database) {
	// 0x4fa880: two special plates
	Common::U32String plateG, plateZ;
	if (!_script.getTextAsset(kIngPlateZ, plateZ) || !_script.getTextAsset(kIngPlateG, plateG))
		return false;
	Common::String plate = query.empty() ? query : stripped(query);
	if (plate == plateG.encode() && database == 'G') {
		clearResults();
		setFieldAsset(kIngResultText, kIngPlateGText, kIngPlateSize, kColorBlue);
		_script._pda.enablePDANote(kIngPlateNote);
		if (_script.getVariable(kIngPlateSeen) == 0) {
			_script.addUberParanoia(2);
			_script.setVariable(kIngPlateSeen, 1);
		}
		noRecord();
		return true;
	}
	if (plate == plateZ.encode() && database == 'Z') {
		clearResults();
		setFieldAsset(kIngResultText, kIngPlateZText);
		noRecord();
		return true;
	}
	return false;
}

bool ComputerHooks::searchPhone(const Common::String &query, int event, const Common::Array<int32> &fields) {
	// 0x4fac40: only ten characters are searched, against the last ten characters of 12 texts
	Common::String number = query.empty() ? query : stripped(query);
	if ((int)number.size() != kIngPhoneLength)
		return false;
	for (uint i = 0; i < ARRAYSIZE(kIngPhones); i++) {
		Common::U32String text;
		if (!_script.getTextAsset(kIngPhones[i].text, text))
			continue;
		Common::String full = text.encode();
		if ((int)full.size() < kIngPhoneLength || full.substr(full.size() - kIngPhoneLength) != number)
			continue;
		int result = kIngPhones[i].result;
		if (result >= 0) {
			showRecord(result);
			return true;
		}
		clearResults();
		if (result == -2) {
			setFieldAsset(kIngResultText, kIngPhoneText);
		} else {
			setFieldAsset(kIngResultText, kIngPhoneSpecialText);
			_script.runActionList(kIngPhoneSpecialActions, event, fields);
			_script.setVariable(kIngPhoneSpecialVariable, 1);
		}
		noRecord();
		return true;
	}
	return false;
}

void ComputerHooks::ingSearch(char category, char database, int event, const Common::Array<int32> &fields) {
	// 0x465810: the query is lower-cased for the search; the not-found branch keeps it as typed
	Common::String typed = _query;
	Common::String query = _query;
	query.toLowercase();
	uint32 notFoundText = kIngNotFound;
	bool found = false;
	switch (category) {
	case 'N':
		found = searchName(query, database, notFoundText);
		break;
	case 'L':
		found = searchPlate(query, database);
		break;
	case 'P':
		found = searchPhone(query, event, fields);
		break;
	default:
		break; // H and F have no search
	}
	if (found) {
		_script.runActionList(kIngFoundActions, event, fields);
		if (_recordShown)
			_script.setItemEnabled(kIngHiddenItem, true);
		_script.clearItemText(kIngText);
		_query.clear();
		moveIngCaret();
		return;
	}
	_script.runActionList(kIngNotFoundActions, event, fields);
	clearResults();
	setFieldAsset(kIngResultText, notFoundText);
	_script.setItemEnabled(kIngHiddenItem, false);
	_query = typed;
}

void ComputerHooks::ingClear() {
	// 0x4914e0
	clearResults();
	_script.clearItemText(kIngResultText);
	_script.clearItemText(kIngText);
	_query.clear();
	moveIngCaret();
	_script.setItemEnabled(kIngHiddenItem, false);
}

void ComputerHooks::ingDisplayCook(int event, const Common::Array<int32> &fields) {
	// 0x4fa650 (variable 5095): 0: record 3 is shown (0x467070) and, once per run of the program, the action list
	// 90360 runs (items 35551..35559 and the clip 55941); otherwise text 58350 is drawn into 58344 as for the plate
	if (_script.getVariable(kIngCookVariable) == 0) {
		showRecord(kIngCookRecord);
		if (!_cookShown) {
			_script.runActionList(kIngCookActions, event, fields);
			_cookShown = true;
		}
	} else {
		setFieldAsset(kIngResultText, kIngPlateGText, kIngPlateSize, kColorBlue);
	}
}

// -- Media index --------------------------------------------------------------

void ComputerHooks::mediaSearch(int event, const Common::Array<int32> &fields) {
	// 0x4260f0: the query as typed is echoed; the search compares it lower-cased with each item's keywords
	setFieldText(kMediaEcho, _mediaQuery);
	Common::String query = _mediaQuery;
	query.toLowercase();
	bool found = false;
	for (uint i = 0; i < ARRAYSIZE(kMediaItems) && !found; i++) {
		Common::Array<uint32> members;
		itemMembers(kMediaItems[i], members);
		for (uint j = 1; j < members.size() && !found; j++) {
			if (!sameText(query, members[j]))
				continue;
			found = true;
			Common::U32String article;
			loadText(members[0], article);
			_script._pda.replaceScrollText(article);
			_record = members[0];
			_script.setItemEnabled(kMediaHiddenItem, true);
			_script.runActionList(kIngFoundActions, event, fields);
			if (members[0] == kMediaArticleSpecial)
				_script.enableTarget(kMediaArticleEnables, true);
		}
	}
	if (!found) {
		_script.runActionList(kIngNotFoundActions, event, fields);
		Common::U32String text;
		loadText(kIngNotFound, text);
		_script._pda.replaceScrollText(text);
		_record = 0;
		_script.setItemEnabled(kMediaHiddenItem, false);
	}
	_mediaQuery.clear();
	_script.clearItemText(kMediaFocusItem);
	moveMediaCaret();
}

void ComputerHooks::mediaKeyPress(uint key, int event, const Common::Array<int32> &fields) {
	// 0x4f0ae0: Return runs the search button's click; any other key edits the query
	if (key == kVkReturn) {
		_script.runActionList(kMediaReturnActions, event, fields);
		return;
	}
	if (!editText(key, _mediaQuery, kMediaMax))
		return;
	setFieldText(kMediaFocusItem, _mediaQuery);
	moveMediaCaret();
}

// -- SBase --------------------------------------------------------------------

void ComputerHooks::sbaseInit() {
	// 0x4d8bd0
	_sbaseCount = 0;
	_sbase.clear();
	for (uint i = 0; i < ARRAYSIZE(kSbaseItems); i++)
		_script.setItemEnabled(kSbaseItems[i], false);
	_script.setFocus(kSbaseFocusItem);
}

void ComputerHooks::sbaseKeyPress(uint key) {
	// 0x4d8c60: one item per typed character, at most one change per key
	editText(key, _sbase, kSbaseMax);
	uint length = _sbase.size();
	if (length > _sbaseCount) {
		_script.setItemEnabled(kSbaseItems[_sbaseCount], true);
		_sbaseCount++;
	} else if (_sbaseCount > length) {
		_sbaseCount--;
		_script.setItemEnabled(kSbaseItems[_sbaseCount], false);
	}
	if (length < kSbaseMax)
		_script.setItemOrigin(kSbaseCaret, Common::Point(kSbaseCaretX + kSbaseCaretStep * length, kSbaseCaretY));
}

void ComputerHooks::mediaInit() {
	// 0x45c320: the record cleared, item 55036 hidden, the focus on item 50746
	_record = 0;
	_script.setItemEnabled(kMediaHiddenItem, false);
	_script.setFocus(kMediaFocusItem);
}

} // End of namespace XFiles
