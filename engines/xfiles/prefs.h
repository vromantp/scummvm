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

#include "common/str.h"
#include "xfiles/hooks.h"
#include "xfiles/vcdata.h"

namespace XFiles {

class Script;

/**
 * The preferences' owner (B-207): where each one is kept between games, in ScummVM's settings. The shared ones
 * (closed captioning and the dialog, effect and music volumes) are ScummVM's own keys, changed in both its menu and
 * the Game Preferences screens; the others are `xfiles_*` keys only the screens change. The store stays in Script
 * (the vcdump's --pref uses it); a save is never a source. Precedence at RegPreferences: a hook's value this
 * session, the scripted-run source, the ScummVM setting, the .gam value. The behaviour is in
 * docs/subsystems/preferences.md ("Engine").
 */
class Preferences {
public:
	/** The number of preferences (Script's kPrefCount; prefs.cpp checks it). */
	enum { kCount = 24 };

	Preferences();

	void setScript(Script *script) { _script = script; }
	/**
	 * A scripted run (xfiles_steps) takes its registry from `spec` (the setting xfiles_prefs, "INDEX:VALUE ...",
	 * the syntax of vcdump's --pref) instead of the ScummVM settings, and writes nothing back.
	 */
	void setScripted(const Common::String &spec);
	/** RegPreferences' 24 variables (0 for a preference without one). */
	void registerVariables(const VCAction &action);
	/** The registry's value for Script::regPreferences, false: the .gam value stays. Takes the snapshot. */
	bool query(uint index, int32 &value) const;
	/** ReturnMainMenu (0x490450): the store into the ScummVM settings. */
	void save();
	/** The ScummVM menu closed: a shared preference whose setting changed since the last query or save. */
	void syncFromScummVM();
	/** A preference's variable, 0 before RegPreferences. */
	uint32 variable(uint index) const { return _variables[index]; }
	/** Auto Set Depth is L (B-217): the screen is asked for 16 bits, else for the best depth. Valid before RegPreferences. */
	bool wantsReducedDepth() const;
	/** A preference as a switch, false before RegPreferences. */
	bool isSet(uint index) const;
	/** The game volume, 0 to 100 (the exe's wave-out master volume). */
	int32 gameVolume() const;

private:
	/** The preference's value in the ScummVM settings, false if it has none. */
	bool read(uint index, int32 &value) const;
	void write(uint index, int32 value) const;

	Script *_script;
	bool _scripted;
	int32 _scriptedValues[kCount];
	bool _hasScripted[kCount];
	uint32 _variables[kCount];
	/** The ScummVM value last read or written, for the shared preferences. */
	mutable int32 _snapshot[kCount];
	mutable bool _hasSnapshot[kCount];
};

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
