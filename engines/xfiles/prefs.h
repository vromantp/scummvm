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

#ifndef XFILES_PREFS_H
#define XFILES_PREFS_H

#include "xfiles/hooks.h"

namespace XFiles {

class Script;

/**
 * The Game Preferences screens' hooks (PrefInit, AudioInit, the four volume sliders, the cycles, the toggles
 * and PrefDefaults), XFiles.exe 0x481d90, 0x4c6e50 to 0x4cdc90. Owned by Script on the pattern of PhoneHooks (the
 * research repo's docs/subsystems/interpreter.md, "Script areas"). The behaviour is in docs/exe/hooks/system.md
 * ("Game Preferences screen hooks").
 */
class PrefHooks {
public:
	explicit PrefHooks(Script &script);

	/** Add the preference hooks to the Script's table. */
	void addHooks(HookTable<Script> &table);

private:
	/** The volume sliders: 0 dialog, 1 music, 2 effect, 3 game. */
	enum { kSliderCount = 4 };

	void audioInit();
	void slider(int row, uint32 variable, int32 n);
	void cycle(uint index, uint32 variable, const char *table);
	void toggle(uint index, uint32 variable);
	/** PrefDefaults(c), 0x4cd4e0: 'P' the Performance values, 'Q' the Quality ones. */
	void defaults(int32 set);
	/** The preference's value: what a hook stored, else the variable's (RegPreferences wrote it). */
	int32 getValue(uint index, uint32 variable) const;
	void setValue(uint index, uint32 variable, int32 value);
	/** Show or hide the tick item of a slider's level. */
	void showTick(int row, int level, bool show);
	/** 0x4c7800: rewind and start the click sound. */
	void click();

	Script &_script;
	int _levels[kSliderCount]; ///< The slider levels, 0 to 9
};

} // End of namespace XFiles

#endif // XFILES_PREFS_H
