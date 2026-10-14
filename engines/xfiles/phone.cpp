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

#include "common/formats/winexe.h"

#include "xfiles/script.h"

namespace XFiles {

// The phone of the inventory: layout "Phone (On)" with a keypad, a display
// and a phone book. Its items call the Phone hooks (XFiles.exe 0x4c7f00 to
// 0x4c8bd0), which keep the dialled number in a string variable, draw it on
// the display and look it up when Send is pressed.

/** The display (VCInterfaceItem). */
static const uint32 kPhoneDisplay = 39513;
/** The item whose asset list is the phone book; entries are enabled as they are learnt. */
static const uint32 kPhoneBook = 45309;
/** Key tones for 0-9 (0x6b1910); 9 has the tone of 0. */
static const uint32 kPhoneTones[10] = { 58637, 58629, 58630, 58631, 58632, 58633, 58634, 58635, 58636, 58637 };
/** The display's text: Courier New, 15 pixels, bold, dark green (0x4c7f00, 0x6ba560). */
static const uint32 kPhoneTextColor = 0x1b300f;
static const int kPhoneTextSize = 15;
/** Digits the display takes (0x4c8870). */
static const uint kPhoneMaxDigits = 13;

/** A number that answers: its text asset, and the trigger whose actions play the call (0x656040). */
struct PhoneCall {
	uint32 number;
	uint32 trigger;
	uint32 actions;
};

static const PhoneCall kPhoneCalls[] = {
	{ 67269, 67324, 67326 },
	{ 67271, 67331, 67333 },
	{ 67273, 67338, 67340 },
	{ 67275, 67345, 67347 },
	{ 67277, 67352, 67354 },
	{ 67279, 67361, 67363 },
	{ 67281, 67368, 67370 },
	{ 67283, 67375, 67377 },
	{ 67287, 67382, 67384 },
	{ 67285, 67389, 67391 },
	{ 67289, 67398, 67400 },
	{ 67443, 74576, 74580 },   // The Lone Gunmen
	{ 83868, 83863, 83865 }
};

/** Recordings for numbers that do not answer (trigger, actions; 0x6560e0). */
static const uint32 kPhoneFailures[5][2] = {
	{ 67405, 67407 },
	{ 67410, 67412 },
	{ 67415, 67422 },
	{ 67451, 67453 },
	{ 83303, 83305 }           // Ten digits outside area code 206
};
enum {
	kFailureTooLong = 2,
	kFailureLongDistance = 4
};
static const char *const kPhoneAreaCode = "206";

/** The Lone Gunmen answer from node 2 on, until they have been called once. */
static const uint32 kLoneGunmenNumber = 67443;
static const uint32 kHasNotCalledLoneGunmen = 2623;
/** The rings before a call is answered: the actions of trigger 78788 (0x133c4). */
static const uint32 kPhoneRingActions = 78790;

/** The keys PhoneKeyPress takes, as Windows virtual-key codes, and the button triggers they run (0x4c8340). */
struct PhoneKey {
	uint vk;
	uint32 trigger;
	uint32 actions;
};

static const PhoneKey kPhoneKeys[] = {
	{ 0x0d, 57171, 57172 },    // Return: Send
	{ '0', 43274, 43276 },
	{ '1', 43238, 43240 },
	{ '2', 43242, 43244 },
	{ '3', 43246, 43248 },
	{ '4', 43250, 43252 },
	{ '5', 43254, 43256 },
	{ '6', 43258, 43260 },
	{ '7', 43262, 43264 },
	{ '8', 43266, 43268 },
	{ '9', 43270, 43272 }
};

static uint32 getVariableArg(const VCAction &a, uint i) {
	return i < a.args.size() && a.args[i].kind == kOperandVariable ? a.args[i].value : 0;
}

bool Script::callPhone(const VCAction &a, const Common::Array<int32> &args, int event, const EventFields &fields) {
	const Common::String &name = a.function;
	bool handled = true;
	if (name == "PhoneInit")
		phoneInit(getVariableArg(a, 0));
	else if (name == "PhoneButton" && args.size() >= 3)
		phoneButton((char)args[0], getVariableArg(a, 1), args[2]);
	else if (name == "PhoneClear")
		phoneClear(getVariableArg(a, 0));
	else if (name == "PhoneMenuInit")
		phoneMenuInit(getVariableArg(a, 0));
	else if (name == "PhoneFlipName" && args.size() >= 2)
		phoneFlipName(args[0] != 0, getVariableArg(a, 1));
	else if (name == "PhoneSend")
		phoneSend(getVariableArg(a, 0), getVariableArg(a, 1), getVariableArg(a, 2), event, fields);
	else if (name == "PhoneKeyPress" && !args.empty())
		phoneKeyPress(args[0], event, fields);
	else
		handled = false; // PhoneDestroy only frees the display's font
	return handled;
}

void Script::setPhoneDisplay(const Common::U32String &text) {
	if (text.empty()) {
		_itemTexts.erase(kPhoneDisplay);
		return;
	}
	// Not wrapped, bold, a line per newline; 17 px lines
	setItemText(kPhoneDisplay, text, kPhoneTextSize, true, kPhoneTextColor, kAlignLeft, false, 0);
}

void Script::phoneInit(uint32 numberVar) {
	// 0x4c7f00: an empty display and number (the phone book and the entry
	// shown are kept)
	setPhoneDisplay(Common::U32String());
	setString(numberVar, Common::String());
}

void Script::phoneButton(char key, uint32 numberVar, uint32 pressedItem) {
	// 0x4c8870: a digit is added to the number, unless it has 13; one
	// pressed while an entry of the phone book is shown starts a new number.
	// It plays its tone, stopping the previous one.
	if (key >= '0' && key <= '9') {
		Common::String number = getString(numberVar);
		if (number.size() < kPhoneMaxDigits || _phone.showsName) {
			if (_phone.showsName) {
				_phone.showsName = false;
				number.clear();
			}
			number += key;
			setString(numberVar, number);
			setPhoneDisplay(Common::U32String(number));
			log("      phone: " + number);
			if (_phone.tone)
				playAsset(_phone.tone, kModeStop, nullptr);
			_phone.tone = kPhoneTones[key - '0'];
			playAsset(_phone.tone, kModePlayOnce, nullptr);
		}
	}
	// The button's pressed picture, enabled by the click, goes 100 ms later
	if (pressedItem)
		_pressed.push_back(pressedItem);
}

void Script::phoneClear(uint32 numberVar) {
	// 0x4c8b10: whether an entry was shown is kept
	setPhoneDisplay(Common::U32String());
	setString(numberVar, Common::String());
}

void Script::phoneMenuInit(uint32 numberVar) {
	// 0x4c8550: the book is made again from its enabled entries; the entry
	// shown last time is shown again
	setString(numberVar, Common::String());
	VCInterfaceItem item;
	Common::Array<uint32> entries;
	_phone.book.clear();
	if (_data.getInterfaceItem(kPhoneBook, item))
		_data.getMembers(item.assets, entries);
	for (uint i = 0; i < entries.size(); i++) {
		if (isEnabled(entries[i]))
			_phone.book.push_back(entries[i]);
	}
	_phone.bookSize = _phone.book.size();
	if (_phone.bookIndex < _phone.book.size()) {
		_phone.showsName = true;
		phoneShowEntry(numberVar);
	}
}

void Script::phoneFlipName(bool back, uint32 numberVar) {
	// 0x4c81c0: only while an entry is shown and the book has two or more
	if (!_phone.showsName || _phone.bookSize < 2)
		return;
	if (back)
		_phone.bookIndex = (_phone.bookIndex + _phone.bookSize - 1) % _phone.bookSize;
	else
		_phone.bookIndex = (_phone.bookIndex + 1) % _phone.bookSize;
	if (_phone.bookIndex < _phone.book.size())
		phoneShowEntry(numberVar);
}

void Script::phoneShowEntry(uint32 numberVar) {
	// An entry is a text asset, or names a string of XFilesT.dll: name lines,
	// then the number; the number is its last 10 characters
	uint32 entry = _phone.book[_phone.bookIndex];
	Common::U32String text;
	getTextAsset(entry, text);
	Common::U32String lines;
	for (uint i = 0; i < text.size(); i++) {
		if (text[i] != '\r')
			lines += text[i];
	}
	setPhoneDisplay(lines);
	Common::String number = text.encode();
	if (number.size() > 10)
		number = Common::String(number.c_str() + number.size() - 10);
	setString(numberVar, number);
	log(Common::String::format("      phone: entry %u, ", entry) + number);
}

void Script::phoneSend(uint32 numberVar, uint32 triggerVar, uint32 actionsVar, int event, const EventFields &fields) {
	// 0x4c8bd0: look the number up in the call table; a known number rings,
	// then its trigger's actions play (iCallTriggerID, iCallActionID, run by
	// the rings' end). Unknown numbers play a recording at once.
	Common::String number = getString(numberVar);
	if (number.size() > 11) {
		runActionList(kPhoneFailures[kFailureTooLong][1], event, fields);
		return;
	}
	if (number.size() == 10 && strncmp(number.c_str(), kPhoneAreaCode, 3) != 0) {
		runActionList(kPhoneFailures[kFailureLongDistance][1], event, fields);
		return;
	}
	Common::U32String text;
	if (_data.getText(kLoneGunmenNumber, text) && text.encode() == number) {
		int32 node = _currNodeVariable ? getVariable(_currNodeVariable) : 0;
		if (node < 2 || !getVariable(kHasNotCalledLoneGunmen)) {
			runActionList(kPhoneFailures[kFailureTooLong][1], event, fields);
			return;
		}
	}
	for (uint i = 0; i < ARRAYSIZE(kPhoneCalls); i++) {
		if (!_data.getText(kPhoneCalls[i].number, text))
			continue;
		// Numbers in area code 206 also answer to their last 7 digits
		Common::String known = text.encode();
		bool local = known.hasPrefix(kPhoneAreaCode) && known.size() >= 7 &&
			number == known.c_str() + known.size() - 7;
		if (known != number && !local)
			continue;
		log(Common::String::format("      phone: calls #%u", kPhoneCalls[i].number));
		_variables[triggerVar] = kPhoneCalls[i].trigger;
		_variables[actionsVar] = kPhoneCalls[i].actions;
		runActionList(kPhoneRingActions, event, fields);
		return;
	}
	// Otherwise the last digit picks the recording, for 7, 10 and 11 digits
	uint failure = 1;
	if (number.size() == 7 || number.size() == 10 || number.size() == 11) {
		static const byte kByLastDigit[10] = { 2, 1, 2, 0, 2, 2, 3, 2, 3, 3 };
		char last = number.lastChar();
		failure = last >= '0' && last <= '9' ? kByLastDigit[last - '0'] : 1;
	}
	log(Common::String::format("      phone: no answer (%u)", failure));
	runActionList(kPhoneFailures[failure][1], event, fields);
}

void Script::phoneKeyPress(uint vk, int event, const EventFields &fields) {
	// 0x4c8340: digits of the main keys or the keypad (VK_NUMPAD0-9) press
	// their button, Return presses Send
	if (vk >= 0x60 && vk <= 0x69)
		vk = vk - 0x60 + '0';
	for (uint i = 0; i < ARRAYSIZE(kPhoneKeys); i++) {
		if (kPhoneKeys[i].vk == vk) {
			runActionList(kPhoneKeys[i].actions, event, fields);
			return;
		}
	}
}

void Script::phoneClosed() {
	// 0x4c87c0 empties the phone book, 0x4c8150 stops the key tone
	_phone.book.clear();
	if (_phone.tone)
		playAsset(_phone.tone, kModeStop, nullptr);
	_phone.tone = 0;
}

} // End of namespace XFiles
