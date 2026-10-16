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

#ifndef XFILES_PHONE_H
#define XFILES_PHONE_H

#include "common/array.h"
#include "common/str.h"
#include "common/ustr.h"

#include "xfiles/hooks.h"

namespace XFiles {

class Script;
class VCData;

/**
 * The phone of the inventory (layout "Phone (On)"): its hooks (XFiles.exe 0x4c7f00 to 0x4c8bd0) and its state.
 * Owned by Script, which calls addHooks once and closed when the layout is hidden. It works through the Script
 * services listed in the research repo's docs/subsystems/interpreter.md ("Script areas").
 */
class PhoneHooks {
public:
	PhoneHooks(Script &script, const VCData &data);

	/** Add the Phone hooks to the Script's table. */
	void addHooks(HookTable<Script> &table);
	/** The phone's layout was hidden (0x4c87c0, 0x4c8150): the book is emptied, the key tone stops. */
	void closed();

private:
	void init(uint32 numberVar);
	void button(char key, uint32 numberVar, uint32 pressedItem);
	void clear(uint32 numberVar);
	void menuInit(uint32 numberVar);
	void flipName(bool back, uint32 numberVar);
	void send(uint32 numberVar, uint32 triggerVar, uint32 actionsVar, int event, const Common::Array<int32> &fields);
	void keyPress(uint vk, int event, const Common::Array<int32> &fields);
	/** Show the current phone book entry and take its number. */
	void showEntry(uint32 numberVar);
	void setDisplay(const Common::U32String &text);

	Script &_script;
	const VCData &_data;
	// XFiles.exe globals 0x6b1900-0x6b190c, list 0x6ba5c8
	Common::Array<uint32> _book; ///< Enabled phone book entries, filled by PhoneMenuInit
	uint _bookSize;              ///< Kept when the book is emptied on closing
	uint _bookIndex;             ///< The entry shown
	bool _showsName;             ///< The display shows an entry, not a dialled number
	uint32 _tone;                ///< The key tone playing, or 0
};

} // End of namespace XFiles

#endif // XFILES_PHONE_H
