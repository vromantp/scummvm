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

#include "xfiles/script.h"

namespace XFiles {

// The PDA's e-mail: three ID lists (VCEmail, VCEmailRead, VCEmailPending)
// and the inbox rows built from them (XFiles.exe 0x484810 to 0x48c680).

/** The e-mail DisplayInBox drops from the list (0x48bfd0). */
static const uint32 kRemovedEmail = 63611;
/** Rows the inbox shows at once. */
static const uint kInboxRows = 8;
/** Action lists EmailInit('a') and EmailInit('b') run when the lists differ in size (0x4754e0). */
static const uint32 kEmailInitA = 86310;
static const uint32 kEmailInitB = 86316;
/** The reader item whose e-mails show other items than the inbox reader's. */
static const uint32 kEmailReadItem = 54950;

/** The items InitEmailRead shows and hides for one e-mail (0x43df10). */
struct EmailReadItems {
	bool readerItem;  ///< For item 54950; otherwise for any other item
	uint32 email;
	uint32 show[4];
	uint32 hide;
};

static const EmailReadItems kEmailReadItems[] = {
	{ true, 61775, { 72713, 72710, 72715, 72722 }, 69527 },
	{ true, 61765, { 72713, 72710, 72723, 91939 }, 69527 },
	{ true, 64527, { 72713, 72710, 91038, 91007 }, 0 },
	{ false, 61775, { 72741, 72760, 72751, 91947 }, 71372 },
	{ false, 64527, { 72741, 72760, 72751, 91947 }, 0 },
	{ false, 61765, { 72741, 72760, 72743, 72759 }, 71372 }
};

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
	if (name == "SendEmail" && !args.empty()) {
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
}

void Script::setItemEnabled(uint32 item, bool enabled) {
	_enabled[item] = enabled;
	updateLayoutItem(item, enabled);
}

void Script::initEmailRead(int32 item, int32 email) {
	for (uint i = 0; i < ARRAYSIZE(kEmailReadItems); i++) {
		const EmailReadItems &e = kEmailReadItems[i];
		if ((int32)e.email != email || e.readerItem != (item == (int32)kEmailReadItem))
			continue;
		for (uint j = 0; j < 4; j++)
			setItemEnabled(e.show[j], true);
		if (e.hide)
			setItemEnabled(e.hide, false);
		break;
	}
}

} // End of namespace XFiles
