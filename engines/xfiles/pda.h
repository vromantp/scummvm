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

#ifndef XFILES_PDA_H
#define XFILES_PDA_H

#include "common/array.h"
#include "common/hashmap.h"
#include "common/str-array.h"
#include "common/ustr.h"

#include "xfiles/hooks.h"

namespace XFiles {

class Script;
class VCData;
struct VCSave;

/**
 * The PDA (and the E-Mail layout): the e-mail lists and the inbox, the reader, the scroll texts, the address book
 * and the notepad (XFiles.exe 0x484810 to 0x48c680, 0x4ad130, 0x4e6220). Owned by Script on the pattern of
 * PhoneHooks (the research repo's docs/subsystems/interpreter.md, "Script areas").
 */
class PdaHooks {
public:
	PdaHooks(Script &script, const VCData &data);

	/** Add the e-mail, scroll text, address book and notepad hooks to the Script's table. */
	void addHooks(HookTable<Script> &table);

	// What a save keeps (Script::snapshot and restore)
	/** Add an e-mail to VCEmail unless it is there (0x41ab40, as EmailFlushMess does). */
	void addEmail(uint32 id);
	const Common::Array<uint32> &getEmails() const { return _emails; }
	const Common::Array<uint32> &getEmailsRead() const { return _emailsRead; }
	const Common::Array<uint32> &getEmailsPending() const { return _emailPending; }
	const Common::Array<uint32> &getNotes() const { return _notes; }
	/** EnablePDANote (0x4e6660): a note is kept; one of the next day opens that day. */
	void enablePDANote(uint32 id);
	/** 0x4be560: the current scroll text gets another text, in its style; nothing if there is none. */
	void replaceScrollText(const Common::U32String &text);
	/** Take the lists from a save. */
	void restore(const VCSave &save);
	/** Write the lists into a save. */
	void snapshot(VCSave &save) const;

private:
	void displayInBox(int32 value);
	void initEmailRead(int32 item, int32 email);
	/** Lines of an e-mail's file (To, From, Subject, body), empty if it has none; cached. */
	const Common::U32StringArray &getEmailLines(uint32 email);
	void showInboxTexts();
	void setScrollText(uint32 item, const Common::U32String &text, int size, uint32 color = 0, bool bold = false,
		bool sans = false);
	void scrollTextList(int32 item, int32 asset, int32 color, int32 font, int32 size);
	void mainScroll(int32 n);
	void pdaInitAddr();
	void pdaFlipAddr(int32 n);
	void showAddress();
	void buildAddressList();
	void enableAllNotes();
	void notePadInit();
	void noteFlipDay(int32 n);
	void showNoteDayText();
	void showNoteMarker();
	bool hasNote(uint32 id) const;
	bool dayHasNote(uint day, uint32 note) const;

	Script &_script;
	const VCData &_data;
	/** VCEmail, VCEmailRead and VCEmailPending, and the inbox (globals 0x6bd914, 0x6bd86c). */
	Common::Array<uint32> _emails, _emailsRead, _emailPending;
	struct InboxRow {
		uint32 id;
		bool read;
	};
	Common::Array<InboxRow> _inboxRows;
	uint _inboxTop;
	int _pdaFontSize;                          ///< EmailInit(c): 12 for 'P', else 14
	bool _pdaLayout;                           ///< EmailInit('P'): the PDA's item tables
	Common::HashMap<uint32, Common::U32StringArray> _emailLines;  ///< Lines of the e-mail files read
	uint32 _scrollItem;                        ///< The item of the scroll text (0x42ca30), or 0
	int _scrollSize;                           ///< Its style, as setScrollText was given it
	uint32 _scrollColor;
	bool _scrollBold, _scrollSans;
	Common::Array<uint32> _addressList;        ///< Enabled assets of item 45307 (0x6b8390)
	uint _addressIndex;                        ///< 0x6b16b0
	void addNote(uint32 id);
	Common::Array<uint32> _notes;              ///< VCPDANotes
	uint _noteDay, _noteDays;                  ///< 0x6b1a4c, 0x6b1a54
};

} // End of namespace XFiles

#endif // XFILES_PDA_H
