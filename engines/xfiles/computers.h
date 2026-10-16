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

#ifndef XFILES_COMPUTERS_H
#define XFILES_COMPUTERS_H

#include "common/array.h"
#include "common/str.h"

#include "xfiles/hooks.h"

namespace XFiles {

class Script;

/**
 * The computers' text entry: the FBI log-in (LoginInit, LoginKeyPress, LoginClick, PswdConnect), the ING search
 * (INGInit, INGKeyPressed, INGSearch, INGClear), the media index (MediaInit, MediaSearch, MSKeyPressed), the
 * Powerbook log-in (LoginInit('P'), LoginKeyPress 3 to 5, PwbkOK) and SBase, XFiles.exe 0x470a90 to 0x4865f0.
 * Owned by Script on the pattern of PhoneHooks (the research repo's docs/subsystems/interpreter.md, "Script
 * areas").
 */
class ComputerHooks {
public:
	explicit ComputerHooks(Script &script);

	/** Add the computer hooks to the Script's table. */
	void addHooks(HookTable<Script> &table);

private:
	void loginInit(char mode);
	void loginKeyPress(int field, uint key, int event, const Common::Array<int32> &fields);
	void pswdConnect(uint32 variableId);
	void ingInit();
	void ingKeyPress(uint key, int event, const Common::Array<int32> &fields);
	void loginClick(int field);
	void mediaInit();
	void ingSearch(char category, char database, int event, const Common::Array<int32> &fields);
	void ingClear();
	void mediaSearch(int event, const Common::Array<int32> &fields);
	void mediaKeyPress(uint key, int event, const Common::Array<int32> &fields);
	void pwbkOK(uint32 variableId);
	void commandKeyPress(uint key, int event, const Common::Array<int32> &fields);
	void sbaseInit();
	void sbaseKeyPress(uint key);
	/** The name search (0x465810, 'N'): true if found. */
	bool searchName(const Common::String &query, char database, uint32 &notFoundText);
	bool searchPlate(const Common::String &query, char database);
	bool searchPhone(const Common::String &query, int event, const Common::Array<int32> &fields);
	/** 0x467070: show the record of a name table entry. */
	void showRecord(uint index);
	/** 0x42b0c0: empty the ING results. */
	void clearResults();
	/** A search found nothing the record display shows: no record, item 36320 hidden. */
	void noRecord();
	void moveIngCaret();
	void moveMediaCaret();
	/** The text of a text asset (or a file asset), false if it has none. */
	bool loadText(uint32 asset, Common::U32String &text);
	/** The members of an interface item's asset list, in order, enabled or not. */
	void itemMembers(uint32 item, Common::Array<uint32> &members);
	/** Focus a log-in field (state 0 to 3) and put the caret behind its text (0x4867e0). */
	void loginFocus(int field);
	void moveLoginCaret(int field);
	const Common::String &loginBuffer(int field) const;
	/** The text asset equals the buffer (0x459e00): case ignored, two empty strings are equal. */
	bool sameText(const Common::String &buffer, uint32 textAsset);
	/** Draw a computer field (Courier New 14, 0x4be2c0) into an item; an empty text clears it. */
	void setFieldText(uint32 item, const Common::String &text, int size = 14, uint32 color = 0);
	/** Draw the text of an asset (0x4be1e0); an asset without one clears the item. */
	void setFieldAsset(uint32 item, uint32 asset, int size = 14, uint32 color = 0);

	Script &_script;
	// The text entry buffers (XFiles.exe 0x6be918 and others)
	Common::String _user;       ///< LoginKeyPress 1, max 23
	Common::String _password;   ///< LoginKeyPress 2, max 23
	Common::String _query;      ///< INGKeyPressed, max 30 (0x6be918)
	Common::String _mediaQuery; ///< MSKeyPressed, max 25 (0x6bdd90)
	Common::String _command;    ///< LoginKeyPress 5, max 50 (0x6b90a0)
	Common::String _sbase;      ///< SBaseKeyPress, max 31 (0x6bb480)
	int _loginState;            ///< 0x6b9120: the focused log-in field, 0 to 3. Fields 3 and 4 share _user and _password
	bool _connectedBefore;      ///< 0x6b16bc: copy of variable 4946, set by the first connection
	int _wrongTries;            ///< 0x6b16c4: failed connections with the right user name; not in a save
	int _pwbkTries;             ///< 0x6b16c0: Powerbook log-in tries; not in a save
	uint32 _record;             ///< 0x6b9b94: the asset of the record shown, 0 if none (MediaInit clears it)
	bool _recordShown;          ///< 0x6b1f1c: an ING record is shown
	uint _sbaseCount;           ///< 0x6b19cc: SBase items shown
};

} // End of namespace XFiles

#endif // XFILES_COMPUTERS_H
