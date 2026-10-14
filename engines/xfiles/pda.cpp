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
#include "common/formats/winexe.h"

#include "xfiles/script.h"

namespace XFiles {

// The PDA's e-mail: three ID lists (VCEmail, VCEmailRead, VCEmailPending)
// and the inbox rows built from them (XFiles.exe 0x484810 to 0x48c680).
// The texts of the inbox, the reader, the address book and the notepad
// (docs/subsystems/pda.md, "Text drawing").

/** The e-mail DisplayInBox drops from the list (0x48bfd0). */
static const uint32 kRemovedEmail = 63611;
/** Rows the inbox shows at once. */
static const uint kInboxRows = 8;
/** Action lists EmailInit('a') and EmailInit('b') run when the lists differ in size (0x4754e0). */
static const uint32 kEmailInitA = 86310;
static const uint32 kEmailInitB = 86316;
/** The reader item whose e-mails show other items than the inbox reader's. */
static const uint32 kEmailReadItem = 54950;
/** Courier New 12 (EmailInit('P'), the PDA) and 14 (the E-Mail layout) (0x475170). */
static const int kPDAFontSize = 12;
static const int kMailFontSize = 14;

/** The items InitEmailRead shows and hides for one e-mail (0x43df10). */
struct EmailReadItems {
	bool readerItem;  ///< For item 54950; otherwise for any other item
	uint32 email;
	uint32 show[4];
	uint32 hide;
	uint32 textItem;  ///< Where the text goes
};

static const EmailReadItems kEmailReadItems[] = {
	{ true, 61775, { 72713, 72710, 72715, 72722 }, 69527, 72758 },
	{ true, 61765, { 72713, 72710, 72723, 91939 }, 69527, 72758 },
	{ true, 64527, { 72713, 72710, 91038, 91007 }, 0, 72758 },
	{ false, 61775, { 72741, 72760, 72751, 91947 }, 71372, 72761 },
	{ false, 64527, { 72741, 72760, 72751, 91947 }, 0, 72761 },
	{ false, 61765, { 72741, 72760, 72743, 72759 }, 71372, 72761 }
};

/** The inbox slots' items (tables 0x6bd8d8, 0x6bd7f0, 0x6bd7e8). */
static uint32 fromItem(bool pda, uint slot) {
	return (pda ? 40539 : 38282) + slot;
}

static uint32 subjectItem(bool pda, uint slot) {
	return (pda ? 42749 : 42733) + slot;
}

static uint32 markerItem(bool pda, uint slot) {
	if (pda)
		return 40876 + 2 * slot;
	return slot == 0 ? 15293 : 38253 + 2 * (slot - 1);
}

/** The address book (0x4ad130, 0x4acec0). */
static const uint32 kAddressSource = 45307;
static const uint32 kAddressItem = 35320;
static const int kAddressFontSize = 14;
static const uint32 kAddressEnable[2] = { 42906, 42908 };

/** The notepad (0x4e6220; tables 0x6584b8 and 0x658520). */
static const uint kNoteDayCount = 23;
static const uint32 kNoteDayItems[kNoteDayCount] = {
	45344, 45345, 45346, 45348, 45347, 45349, 45350, 50378, 63395, 63389, 63393, 63397,
	63399, 63401, 63403, 63405, 63409, 63411, 63413, 63415, 63417, 63419, 63421
};
/** The first u32 of each day's entry of 0x658520: the note that sets the marker. */
static const uint32 kNoteKeys[kNoteDayCount] = {
	63249, 63273, 63280, 63292, 63290, 63300, 63312, 63324, 63332, 63336, 63338, 63340,
	63346, 63348, 63352, 63354, 63358, 63362, 63364, 63366, 63370, 63379, 5
};
static const uint32 kNoteTextItem = 51444;
static const uint32 kNoteLabelItem = 67486;
static const uint32 kNoteMarkerItem = 63423;
static const uint32 kNoteMarkerExtra[2] = { 63282, 63284 };
static const uint kNoteMarkerDay = 2;
static const int kNoteFontSize = 12;

/** Pixels from line to line (font.metrics of pda-usage-run1: 12 gives 15, 14 gives 17; the phone's 15 gives 17). */
static int lineHeightOf(int size) {
	return size <= 12 ? 15 : 17;
}

/** Courier New's advance: 7 px at 12, 8 px at 14. */
static int charWidthOf(int size) {
	return (size * 6 + 5) / 10;
}

/** The width of a text: Courier New's fixed advance, or Arial through the hook (an estimate without one). */
struct TextMeter {
	const ScriptHooks *hooks;
	int size;
	bool bold, sans;

	int width(const Common::U32String &text) const {
		if (!sans)
			return text.size() * charWidthOf(size);
		int w = hooks ? hooks->measureText(text, size, bold, sans) : -1;
		// Without fonts (vcdump): Arial 12 averages 5 px per character
		return w >= 0 ? w : text.size() * size * 5 / 12;
	}
};

/** Word-wrap a line (DT_WORDBREAK): break after the last space that fits, or inside a word longer than a line. */
static void wrapLine(const Common::U32String &line, int limit, const TextMeter &meter, Common::U32StringArray &out) {
	Common::U32String rest = line;
	while (meter.width(rest) > limit) {
		int at = -1;
		uint fit = 0; // the characters that fit
		for (uint i = 0; i < rest.size(); i++) {
			if (meter.width(rest.substr(0, i + 1)) > limit)
				break;
			fit = i + 1;
		}
		for (uint i = 0; i <= fit && i < rest.size(); i++) {
			if (rest[i] == ' ')
				at = i;
		}
		if (at > 0) {
			out.push_back(rest.substr(0, at));
			while ((uint)at < rest.size() && rest[at] == ' ')
				at++;
			rest = rest.substr(at);
		} else {
			fit = MAX<uint>(fit, 1);
			out.push_back(rest.substr(0, fit));
			rest = rest.substr(fit);
		}
	}
	out.push_back(rest);
}

static bool listHas(const Common::Array<uint32> &list, uint32 id) {
	return Common::find(list.begin(), list.end(), id) != list.end();
}

static void listRemove(Common::Array<uint32> &list, uint32 id) {
	Common::Array<uint32>::iterator it;
	while ((it = Common::find(list.begin(), list.end(), id)) != list.end())
		list.erase(it);
}

void Script::callEmail(const VCAction &a, const Common::Array<int32> &args, int event, const EventFields &fields) {
	const Common::String &name = a.function;
	if (name == "ScrollTextList" && args.size() >= 5) {
		scrollTextList(args[0], args[1], args[2], args[3], args[4]);
	} else if (name == "ScrollTextInit" && !args.empty()) {
		scrollTextList(args[0], 0, 'b', 'c', kPDAFontSize);
	} else if (name == "SendEmail" && !args.empty()) {
		_emailPending.push_back(args[0]);
	} else if (name == "EmailFlushMess") {
		for (uint i = 0; i < _emailPending.size(); i++) {
			if (!listHas(_emails, _emailPending[i]))
				_emails.push_back(_emailPending[i]);
		}
		_emailPending.clear();
	} else if (name == "EmailDelete" && !args.empty()) {
		listRemove(_emails, args[0]);
		listRemove(_emailsRead, args[0]);
	} else if (name == "EmailInit" && !args.empty()) {
		if (args[0] == 'a' || args[0] == 'b') {
			if (_emails.size() != _emailsRead.size())
				runActionList(args[0] == 'a' ? kEmailInitA : kEmailInitB, event, fields);
		} else {
			_inboxRows.clear();
			_inboxTop = 0;
			_pdaLayout = args[0] == 'P';
			_pdaFontSize = _pdaLayout ? kPDAFontSize : kMailFontSize;
		}
	} else if (name == "DisplayInBox" && !args.empty()) {
		displayInBox(args[0]);
	} else if (name == "GetCurrEmail" && args.size() >= 2 && a.args[1].kind == kOperandVariable) {
		uint i = _inboxTop + args[0];
		_variables[a.args[1].value] = i < _inboxRows.size() ? (int32)_inboxRows[i].id : 0;
	} else if (name == "MarkEmailRead" && !args.empty()) {
		uint i = _inboxTop + args[0];
		if (i < _inboxRows.size() && !_inboxRows[i].read) {
			_emailsRead.push_back(_inboxRows[i].id);
			_inboxRows[i].read = true;
		}
	} else if (name == "InitEmailRead" && args.size() >= 2) {
		initEmailRead(args[0], args[1]);
	} else if (name == "MainScroll" && !args.empty()) {
		mainScroll(args[0]);
	} else if (name == "ScrollTextKill") {
		_itemTexts.erase(_scrollItem);
		_scrollItem = 0;
	} else if (name == "PDAInitAddr") {
		pdaInitAddr();
	} else if (name == "PDAFlipAddr" && !args.empty()) {
		pdaFlipAddr(args[0]);
	} else if (name == "EnablePDANote" && !args.empty()) {
		enablePDANote(args[0]);
	} else if (name == "EnableAllNotes") {
		enableAllNotes();
	} else if (name == "NotePadInit") {
		notePadInit();
	} else if (name == "NoteFlipDay" && !args.empty()) {
		noteFlipDay(args[0]);
	}
}

void Script::displayInBox(int32 value) {
	listRemove(_emails, kRemovedEmail);
	_inboxRows.clear();
	for (uint i = 0; i < _emails.size(); i++) {
		InboxRow row;
		row.id = _emails[i];
		row.read = listHas(_emailsRead, row.id);
		_inboxRows.push_back(row);
	}
	uint rows = _inboxRows.size();
	if (value == 0) {
		_inboxTop = rows > kInboxRows ? rows - kInboxRows : 0;
	} else if (_emails.size() > kInboxRows) {
		uint index = _emails.size();
		for (uint i = 0; i < _emails.size(); i++) {
			if ((int32)_emails[i] == value) {
				index = i;
				break;
			}
		}
		_inboxTop += index;
	}
	if (rows >= kInboxRows && _inboxTop > rows - kInboxRows)
		_inboxTop = rows - kInboxRows;
	showInboxTexts();
}

void Script::setItemEnabled(uint32 item, bool enabled) {
	_enabled[item] = enabled;
	updateLayoutItem(item, enabled);
}

void Script::initEmailRead(int32 item, int32 email) {
	// 0x43df10: the whole file goes into a scroll text over the item, or over the text item
	// of the three special e-mails
	uint32 textItem = item;
	for (uint i = 0; i < ARRAYSIZE(kEmailReadItems); i++) {
		const EmailReadItems &e = kEmailReadItems[i];
		if ((int32)e.email != email || e.readerItem != (item == (int32)kEmailReadItem))
			continue;
		for (uint j = 0; j < 4; j++)
			setItemEnabled(e.show[j], true);
		if (e.hide)
			setItemEnabled(e.hide, false);
		textItem = e.textItem;
		break;
	}
	Common::U32String text;
	if (readTextFile(email, text))
		setScrollText(textItem, text, _pdaFontSize);
}

void Script::isIItemHeld(uint32 variableId, int32 item) {
	// 0x4b3850: 1 when the object the last mouse-down captured is the interface item
	_variables[variableId] = _held && _held == (uint32)item ? 1 : 0;
	Common::U32String name;
	_data.getVariableName(variableId, name);
	log(Common::String::format("      %s = %d", name.encode().c_str(), _variables[variableId]));
}

// -- Text ---------------------------------------------------------------------

void Script::setItemText(uint32 item, const Common::U32String &text, int size, bool bold, uint32 color, TextAlign align,
		bool shadow, int wrapWidth, bool sans) {
	ItemText &t = _itemTexts[item];
	t.lines.clear();
	t.color = color;
	t.bold = bold;
	t.sans = sans;
	t.size = size;
	t.lineHeight = lineHeightOf(size);
	t.align = align;
	t.shadow = shadow;
	t.firstLine = 0;
	// Courier New wraps at whole characters; Arial at the pixel width
	TextMeter meter = { _hooks, size, bold, sans };
	int limit = sans ? wrapWidth : MAX(1, wrapWidth / charWidthOf(size)) * charWidthOf(size);
	Common::U32String line;
	for (uint i = 0; i <= text.size(); i++) {
		if (i < text.size() && text[i] != '\n') {
			line += text[i];
			continue;
		}
		if (wrapWidth > 0)
			wrapLine(line, limit, meter, t.lines);
		else
			t.lines.push_back(line);
		line.clear();
	}
}

void Script::setPDAText(uint32 item, const Common::U32String &text, int size, bool doubleWidth, uint32 color, bool bold,
		bool sans) {
	// 0x4be2c0: Courier New 400, black, left, no shadow; laid out at twice the item's width unless
	// +0x194 is set (it is 0 for every PDA text, which the callers pass as doubleWidth)
	VCInterfaceItem data;
	int width = 0;
	if (_data.getInterfaceItem(item, data))
		width = (data.rect.right - data.rect.left) * (doubleWidth ? 2 : 1);
	setItemText(item, text, size, bold, color, kAlignLeft, false, width, sans);
}

bool Script::getTextAsset(uint32 entry, Common::U32String &text) {
	// A text asset, or one that names a string of XFilesT.dll
	uint32 type, resource;
	if (_data.getResourceRef(entry, type, resource)) {
		if (!_texts || type != 6)
			return false;
		text = _texts->loadString(resource).decode(Common::kWindows1252);
		return true;
	}
	return _data.getText(entry, text);
}

bool Script::readTextFile(uint32 asset, Common::U32String &text) {
	Common::Array<byte> data;
	if (!_hooks || !_hooks->readAssetFile(asset, data))
		return false;
	Common::String raw;
	for (uint i = 0; i < data.size(); i++) {
		if (data[i] != '\r')
			raw += (char)data[i];
	}
	text = Common::U32String(raw, Common::kWindows1252);
	return true;
}

const Common::U32StringArray &Script::getEmailLines(uint32 email) {
	if (!_emailLines.contains(email)) {
		// 0x48ca50: split on newlines: To, From, Subject, then the body
		Common::U32StringArray &lines = _emailLines[email];
		Common::U32String text;
		if (readTextFile(email, text)) {
			Common::U32String line;
			for (uint i = 0; i <= text.size(); i++) {
				if (i < text.size() && text[i] != '\n') {
					line += text[i];
				} else {
					lines.push_back(line);
					line.clear();
				}
			}
		}
	}
	return _emailLines[email];
}

void Script::showInboxTexts() {
	// 0x48c680: rows top ... top + 7 fill the slots: the From and Subject lines, and the
	// marker if the e-mail was read. Slots past the last row are empty.
	for (uint k = 0; k < kInboxRows; k++) {
		uint32 from = fromItem(_pdaLayout, k), subject = subjectItem(_pdaLayout, k);
		uint row = _inboxTop + k;
		if (row < _inboxRows.size()) {
			const Common::U32StringArray &lines = getEmailLines(_inboxRows[row].id);
			setPDAText(from, lines.size() > 1 ? lines[1] : Common::U32String(), _pdaFontSize, true);
			setPDAText(subject, lines.size() > 2 ? lines[2] : Common::U32String(), _pdaFontSize, true);
			setItemEnabled(markerItem(_pdaLayout, k), _inboxRows[row].read);
		} else {
			_itemTexts.erase(from);
			_itemTexts.erase(subject);
			setItemEnabled(markerItem(_pdaLayout, k), false);
		}
	}
}

void Script::setScrollText(uint32 item, const Common::U32String &text, int size, uint32 color, bool bold, bool sans) {
	// 0x4be340: a scroll text replaces the earlier one; wrapped at the item's width
	if (_scrollItem && _scrollItem != item)
		_itemTexts.erase(_scrollItem);
	_scrollItem = item;
	setPDAText(item, text, size, false, color, bold, sans);
}

void Script::scrollTextList(int32 item, int32 asset, int32 color, int32 font, int32 size) {
	// 0x481160 / 0x45e280 -> 0x4be340. The asset is a file asset (its file, as for an e-mail), a text asset
	// or a string of XFilesT.dll (0x45f690). 'H' is Arial made bold, anything else Courier New 400.
	// The colours: 'w' white, 'x' Color_TextBlue, 'b' and anything else black.
	uint32 rgb = color == 'w' ? 0xffffff : color == 'x' ? 0x2e6b8c : 0;
	bool arial = font == 'H';
	Common::U32String text;
	if (asset && !readTextFile(asset, text))
		getTextAsset(asset, text);
	// Arial 12 is 15 px per line (font.metrics, regular); the bold one is assumed to match
	setScrollText(item, text, size, rgb, arial, arial);
}

void Script::mainScroll(int32 n) {
	// 0x42ce60: a line or a page (the lines that fit the item) up or down, kept inside the text
	if (!_scrollItem || !_itemTexts.contains(_scrollItem))
		return;
	ItemText &t = _itemTexts[_scrollItem];
	VCInterfaceItem data;
	if (!_data.getInterfaceItem(_scrollItem, data))
		return;
	uint page = MAX(1, (data.rect.bottom - data.rect.top) / t.lineHeight);
	uint last = t.lines.size() > page ? t.lines.size() - page : 0;
	uint first = t.firstLine;
	switch (n) {
	case 0:
		first = first > 0 ? first - 1 : 0;
		break;
	case 1:
		first++;
		break;
	case 2:
		first = first > page ? first - page : 0;
		break;
	case 3:
		first += page;
		break;
	default:
		return;
	}
	t.firstLine = MIN(first, last);
}

// -- Address book -------------------------------------------------------------

void Script::buildAddressList() {
	// 0x435590: the enabled assets of the item's list
	_addressList.clear();
	VCInterfaceItem item;
	Common::Array<uint32> entries;
	if (_data.getInterfaceItem(kAddressSource, item))
		_data.getMembers(item.assets, entries);
	for (uint i = 0; i < entries.size(); i++) {
		if (isEnabled(entries[i]))
			_addressList.push_back(entries[i]);
	}
}

void Script::showAddress() {
	Common::U32String text, entry;
	if (_addressIndex < _addressList.size() && getTextAsset(_addressList[_addressIndex], entry)) {
		for (uint i = 0; i < entry.size(); i++) {
			if (entry[i] != '\r')
				text += entry[i];
		}
	}
	setPDAText(kAddressItem, text, kAddressFontSize, true);
}

void Script::pdaInitAddr() {
	buildAddressList();
	if (_addressIndex >= _addressList.size())
		_addressIndex = 0;
	showAddress();
}

void Script::pdaFlipAddr(int32 n) {
	// Only with two or more entries
	uint count = _addressList.size();
	if (count <= 1)
		return;
	switch (n) {
	case 0:
		_addressIndex = (_addressIndex + 1) % count;
		showAddress();
		break;
	case 1:
		_addressIndex = (_addressIndex + count - 1) % count;
		showAddress();
		break;
	case 2:
	case 3:
		for (uint i = 0; i < 2; i++)
			setItemEnabled(kAddressEnable[i], n == 2);
		break;
	case 4:
		buildAddressList();
		break;
	default:
		break;
	}
}

// -- Notepad ------------------------------------------------------------------

bool Script::hasNote(uint32 id) const {
	return listHas(_notes, id);
}

bool Script::dayHasNote(uint day, uint32 note) const {
	VCInterfaceItem item;
	Common::Array<uint32> members;
	if (day >= kNoteDayCount || !_data.getInterfaceItem(kNoteDayItems[day], item) || !_data.getMembers(item.assets, members))
		return false;
	return listHas(members, note);
}

void Script::enablePDANote(uint32 id) {
	// 0x4e6660: a note of the next day opens that day
	_notes.push_back(id);
	if (_noteDays < kNoteDayCount && dayHasNote(_noteDays, id))
		_noteDays++;
}

void Script::enableAllNotes() {
	// 0x4e6810: every note of every day, in order
	for (uint day = 0; day < kNoteDayCount; day++) {
		VCInterfaceItem item;
		Common::Array<uint32> members;
		if (_data.getInterfaceItem(kNoteDayItems[day], item))
			_data.getMembers(item.assets, members);
		for (uint i = 0; i < members.size(); i++)
			_notes.push_back(members[i]);
	}
	_noteDays = kNoteDayCount;
}

void Script::showNoteDayText() {
	// 0x4e71a0: the scroll text starts with a blank, then each note of the day in list order
	VCInterfaceItem item;
	Common::Array<uint32> members;
	if (_noteDay < kNoteDayCount && _data.getInterfaceItem(kNoteDayItems[_noteDay], item))
		_data.getMembers(item.assets, members);
	Common::U32String text(" ");
	for (uint i = 0; i < _notes.size(); i++) {
		Common::U32String note;
		if (listHas(members, _notes[i]) && readTextFile(_notes[i], note))
			text += note;
	}
	setScrollText(kNoteTextItem, text, kNoteFontSize);
}

void Script::showNoteMarker() {
	// The day label, and the marker: the day's key note is in the list (day 2 needs two more)
	setPDAText(kNoteLabelItem, Common::U32String(Common::String::format("%u", _noteDay + 1)), kNoteFontSize, true);
	bool shown = _noteDay < kNoteDayCount && hasNote(kNoteKeys[_noteDay]);
	if (shown && _noteDay == kNoteMarkerDay)
		shown = hasNote(kNoteMarkerExtra[0]) && hasNote(kNoteMarkerExtra[1]);
	setItemEnabled(kNoteMarkerItem, shown);
}

void Script::notePadInit() {
	// 0x4e6220: the days are those from the first on that each have a note, at least one
	uint days = 0;
	while (days < kNoteDayCount) {
		bool any = false;
		for (uint i = 0; i < _notes.size() && !any; i++)
			any = dayHasNote(days, _notes[i]);
		if (!any)
			break;
		days++;
	}
	_noteDays = MAX<uint>(1, days);
	showNoteDayText();
	showNoteMarker();
}

void Script::noteFlipDay(int32 n) {
	// 0x4e6a70
	uint day = _noteDay;
	if (n == 0 && _noteDay + 1 < _noteDays)
		day++;
	else if (n == 1 && _noteDay > 0)
		day--;
	if (day != _noteDay) {
		_noteDay = day;
		setItemEnabled(kNoteTextItem, false);
		showNoteDayText();
		setItemEnabled(kNoteTextItem, true);
	}
	showNoteMarker();
}

} // End of namespace XFiles
