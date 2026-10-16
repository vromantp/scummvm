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
 * The computers' text entry: the FBI log-in (LoginInit, LoginKeyPress, LoginClick, PswdConnect), the ING query
 * (INGInit, INGKeyPressed) and the media index (MediaInit), XFiles.exe 0x470a90 to 0x4865f0. Owned by Script on
 * the pattern of PhoneHooks (the research repo's docs/subsystems/interpreter.md, "Script areas").
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
	/** Focus a log-in field (state 0 to 3) and put the caret behind its text (0x4867e0). */
	void loginFocus(int field);
	void moveLoginCaret(int field);
	const Common::String &loginBuffer(int field) const;
	/** The text asset equals the buffer (0x459e00): case ignored, two empty strings are equal. */
	bool sameLoginText(const Common::String &buffer, uint32 textAsset);
	/** Draw a computer field (Courier New 14, 0x4be2c0) into an item; an empty text clears it. */
	void setFieldText(uint32 item, const Common::String &text);

	Script &_script;
	// The text entry buffers (XFiles.exe 0x6be918 and others)
	Common::String _user;     ///< LoginKeyPress 1, max 23
	Common::String _password; ///< LoginKeyPress 2, max 23
	Common::String _field3;   ///< LoginKeyPress 3, max 21
	Common::String _query;    ///< INGKeyPressed, max 30
	Common::String _field4;   ///< The second pair's 4th field (LoginKeyPress 4 is not ported: stays empty)
	int _loginState;          ///< 0x6b9120: the focused log-in field, 0 to 3
	bool _connectedBefore;    ///< 0x6b16bc: copy of variable 4946, set by the first connection
	int _wrongTries;          ///< 0x6b16c4: failed connections with the right user name; not in a save
	int _mediaCounter;        ///< 0x6b9b94 (MediaInit)
};

} // End of namespace XFiles

#endif // XFILES_COMPUTERS_H
