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

#include "xfiles/prefs.h"
#include "xfiles/script.h"

namespace XFiles {

// The Game Preferences screens: PrefInit, AudioInit, PrefDialogVol, PrefMusicVol, PrefEffectVol, PrefVolume, the
// cycles and the toggles. The values go into the preference store (Script::setPreference, which RegPreferences
// writes into the variables at the next game) and into the hook's variable. TODO B-068: Prefs_Apply (0x452b20),
// which carries them to the registry and the running game when the screen closes, and PrefDefaults.

/** PrefClick.amv, played by every hook that changes a value. */
static const uint32 kClickSound = 67518;
/** The volume of level 9 is 99 (0x5384a0() / 9 = 11 per level). */
static const int32 kVolumeStep = 11;
static const int kMaxLevel = 9;
/** The tick items of the sliders' levels (0x655fa0), 10 per slider: dialog, music, effect, game. */
static const uint32 kTicks[4][10] = {
	{ 60190, 60192, 60196, 60198, 60200, 60202, 60204, 60206, 60208, 60210 },
	{ 60212, 60214, 60216, 60218, 60220, 60222, 60224, 60226, 60228, 60230 },
	{ 60232, 60234, 60236, 60238, 60240, 60242, 60244, 60246, 60248, 60250 },
	{ 60252, 60254, 60256, 60258, 60260, 60262, 60264, 60266, 60268, 60270 }
};
/** The volume preference of each slider. */
static const uint kVolumePrefs[4] = { kPrefVolumeDialog, kPrefVolumeMusic, kPrefVolumeEffect, kPrefVolumeGame };

/** The cycle hooks: the next character after the current one in the table, the first after the last. */
static const char kAskYesNo[] = "AYN";
static const char kInventory[] = "HV";
static const char kBrightness[] = "MBD";
static const char kBeam[] = "MWS";
static const char kDifficulty[] = "SHE";
static const char kDepth[] = "NALH";

PrefHooks::PrefHooks(Script &script) : _script(script) {
	for (int i = 0; i < kSliderCount; i++)
		_levels[i] = 0;
}

void PrefHooks::addHooks(HookTable<Script> &t) {
	// 0x4817d0 makes the click's player; the sound is started per click instead
	t.add("PrefInit", [](Script &s, const HookCall &c) {
		return true;
	});
	t.add("AudioInit", [](Script &s, const HookCall &c) {
		s._prefs.audioInit();
		return true;
	});
	t.add("PrefDialogVol", [](Script &s, const HookCall &c) {
		if (!c.has(2) || !c.isVariable(0))
			return false;
		s._prefs.slider(0, c.variableArg(0), c.args[1]);
		return true;
	});
	t.add("PrefMusicVol", [](Script &s, const HookCall &c) {
		if (!c.has(2) || !c.isVariable(0))
			return false;
		s._prefs.slider(1, c.variableArg(0), c.args[1]);
		return true;
	});
	t.add("PrefEffectVol", [](Script &s, const HookCall &c) {
		if (!c.has(2) || !c.isVariable(0))
			return false;
		s._prefs.slider(2, c.variableArg(0), c.args[1]);
		return true;
	});
	t.add("PrefVolume", [](Script &s, const HookCall &c) {
		if (!c.has(2) || !c.isVariable(0))
			return false;
		s._prefs.slider(3, c.variableArg(0), c.args[1]);
		return true;
	});

#define XF_CYCLE(name, index, chars) \
	t.add(name, [](Script &s, const HookCall &c) { \
		if (!c.isVariable(0)) \
			return false; \
		s._prefs.cycle(index, c.variableArg(0), chars); \
		return true; \
	})
	XF_CYCLE("PrefAutoRes", kPrefAutoSetResolution, kAskYesNo);
	XF_CYCLE("PrefSaveOnExit", kPrefSaveGameOnExit, kAskYesNo);
	XF_CYCLE("PrefInventory", kPrefInventory, kInventory);
	XF_CYCLE("PrefFlashBrght", kPrefFlashlightBrightness, kBrightness);
	XF_CYCLE("PrefFlashBeam", kPrefFlashlightBeam, kBeam);
	XF_CYCLE("PrefActionDiff", kPrefActionSceneDifficulty, kDifficulty);
	XF_CYCLE("PrefAutoDepth", kPrefAutoSetDepth, kDepth);
#undef XF_CYCLE

#define XF_TOGGLE(name, index) \
	t.add(name, [](Script &s, const HookCall &c) { \
		if (!c.isVariable(0)) \
			return false; \
		s._prefs.toggle(index, c.variableArg(0)); \
		return true; \
	})
	XF_TOGGLE("Pref3DSound", kPref3DSound);
	XF_TOGGLE("PrefArtIntuit", kPrefArtificialIntuition);
	XF_TOGGLE("PrefAutoRewind", kPrefAutoRewind);
	XF_TOGGLE("PrefClosedCap", kPrefClosedCaptioning);
	XF_TOGGLE("PrefHighVideo", kPrefHighQualityVideo);
	XF_TOGGLE("PrefLiveEnviro", kPrefLiveEnvironments);
	XF_TOGGLE("PrefRollover", kPrefRolloverText);
	XF_TOGGLE("PrefSkipIntro", kPrefSkipIntro);
	XF_TOGGLE("PrefSkipVideo", kPrefSkipClips);
	XF_TOGGLE("PrefSmoothText", kPrefSmoothText);
	XF_TOGGLE("PrefSubliminal", kPrefSubliminalMessages);
	XF_TOGGLE("PrefTransition", kPrefTransitions);
	XF_TOGGLE("PrefTranslucnt", kPrefTranslucency);
#undef XF_TOGGLE
}

int32 PrefHooks::getValue(uint index, uint32 variable) const {
	return _script._preferences.contains(index) ? _script._preferences[index] : _script.getVariable(variable);
}

void PrefHooks::setValue(uint index, uint32 variable, int32 value) {
	_script.setPreference(index, value);
	_script.setVariable(variable, value);
}

void PrefHooks::click() {
	if (_script._hooks)
		_script._hooks->playAsset(kClickSound, kModePlayOnce, false, 0, 0);
}

void PrefHooks::showTick(int row, int level, bool show) {
	_script.setItemEnabled(kTicks[row][level], show);
}

void PrefHooks::audioInit() {
	// 0x4c6e50: the levels from the four volumes, each level's tick shown
	for (int row = 0; row < kSliderCount; row++) {
		uint index = kVolumePrefs[row];
		int32 volume = _script._prefVariables[index] ? getValue(index, _script._prefVariables[index]) : 0;
		_levels[row] = CLIP<int32>(volume / kVolumeStep, 0, kMaxLevel);
		showTick(row, _levels[row], true);
	}
}

void PrefHooks::slider(int row, uint32 variable, int32 n) {
	click();
	// 0x4c7840: 0 lowers the level, 11 raises it, 1 to 10 sets level n - 1
	int level = _levels[row];
	if (n == 0 && level > 0)
		level--;
	else if (n == 11 && level < kMaxLevel)
		level++;
	else if (n >= 1 && n <= 10)
		level = n - 1;
	showTick(row, _levels[row], false);
	_levels[row] = level;
	showTick(row, level, true);
	setValue(kVolumePrefs[row], variable, level * kVolumeStep);
}

void PrefHooks::cycle(uint index, uint32 variable, const char *table) {
	click();
	int32 value = getValue(index, variable);
	const char *found = value > 0 && value < 256 ? strchr(table, value) : nullptr;
	if (!found)
		return; // TODO B-068: what the exe does with a value outside the table is not known
	found++;
	setValue(index, variable, static_cast<byte>(*found ? *found : table[0]));
}

void PrefHooks::toggle(uint index, uint32 variable) {
	click();
	setValue(index, variable, getValue(index, variable) ? 0 : 1);
}

} // End of namespace XFiles
