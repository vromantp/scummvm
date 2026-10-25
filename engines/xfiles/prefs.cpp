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

#include "audio/mixer.h"
#include "common/config-manager.h"
#include "common/tokenizer.h"
#include "common/util.h"

namespace XFiles {

// Preferences

// The ScummVM setting that keeps each preference (the exe's registry value), by Preference. Shared with ScummVM's
// own menu: subtitles and the dialog, effect and music volumes; the rest are the engine's game-only keys.
static const char *const kPrefSettings[kPrefCount] = {
	"xfiles_3d_sound", "xfiles_artificial_intuition", "xfiles_auto_rewind", "subtitles", "xfiles_rollover_text",
	"xfiles_save_on_exit", "xfiles_skip_clips", "xfiles_skip_intro", "xfiles_smooth_text",
	"xfiles_subliminal_messages", "xfiles_transitions", "xfiles_translucency", "xfiles_action_difficulty",
	"xfiles_auto_depth", "xfiles_flashlight_beam", "xfiles_flashlight_brightness", "xfiles_inventory",
	"speech_volume", "sfx_volume", "xfiles_game_volume", "music_volume", "xfiles_auto_resolution",
	"xfiles_high_quality_video", "xfiles_live_environments"
};

static_assert(Preferences::kCount == kPrefCount, "Preferences::kCount must be Script's kPrefCount");

/** The preferences ScummVM's menu changes too: kept as ScummVM's keys, synced when its menu closes. */
static bool isShared(uint index) {
	return index == kPrefClosedCaptioning || index == kPrefVolumeDialog || index == kPrefVolumeEffect ||
		index == kPrefVolumeMusic;
}

/** The preferences whose value is a letter (the exe's registry keeps its ASCII code), kept in the settings as the letter. */
static bool isLetter(uint index) {
	return index == kPrefSaveGameOnExit || index == kPrefActionSceneDifficulty || index == kPrefAutoSetDepth ||
		index == kPrefFlashlightBeam || index == kPrefFlashlightBrightness || index == kPrefInventory ||
		index == kPrefAutoSetResolution;
}

/** A letter preference's setting: one letter (any case) or the legacy ASCII code. False for anything else. */
static bool parseLetter(const Common::String &text, int32 &value) {
	if (text.size() == 1 && Common::isAlpha(text[0])) {
		value = toupper((unsigned char)text[0]);
		return true;
	}
	int32 number;
	if (!text.empty() && sscanf(text.c_str(), "%d", &number) == 1) {
		value = number;
		return true;
	}
	return false;
}

/** The preferences kept as a mixer volume (0 to 256) instead of the game's 0 to 100. */
static bool isMixerVolume(uint index) {
	return index == kPrefVolumeDialog || index == kPrefVolumeEffect || index == kPrefVolumeMusic;
}

/** The on/off preferences, kept as true and false. */
static bool isBoolean(uint index) {
	return !isLetter(index) && !isMixerVolume(index) && index != kPrefVolumeGame;
}

/** Rounded both ways, so a game volume comes back unchanged. */
static int32 toMixerVolume(int32 volume) {
	return (CLIP<int32>(volume, 0, 100) * Audio::Mixer::kMaxMixerVolume + 50) / 100;
}

static int32 fromMixerVolume(int32 volume) {
	return CLIP<int32>((volume * 100 + Audio::Mixer::kMaxMixerVolume / 2) / Audio::Mixer::kMaxMixerVolume, 0, 100);
}

Preferences::Preferences() : _script(nullptr), _scripted(false) {
	memset(_scriptedValues, 0, sizeof(_scriptedValues));
	memset(_hasScripted, 0, sizeof(_hasScripted));
	memset(_variables, 0, sizeof(_variables));
	memset(_snapshot, 0, sizeof(_snapshot));
	memset(_hasSnapshot, 0, sizeof(_hasSnapshot));
}

void Preferences::setScripted(const Common::String &spec) {
	_scripted = true;
	Common::StringTokenizer tokens(spec);
	while (!tokens.empty()) {
		Common::String token = tokens.nextToken();
		const char *colon = strchr(token.c_str(), ':');
		if (!colon)
			continue;
		int index = atoi(token.c_str());
		if (index >= 0 && index < kCount) {
			_scriptedValues[index] = atoi(colon + 1);
			_hasScripted[index] = true;
		}
	}
}

void Preferences::registerVariables(const VCAction &action) {
	// RegPreferences(24 variables), run by Script before a game starts or continues (Script::loadPreferences) and
	// by the title. XFiles.exe keeps the preferences in the registry and writes them into these variables; Script
	// does the writes, asking query() for the registry's value.
	for (uint i = 0; i < kCount; i++)
		_variables[i] = i < action.args.size() && action.args[i].kind == kOperandVariable ? action.args[i].value : 0;
}

bool Preferences::read(uint index, int32 &value) const {
	const char *setting = kPrefSettings[index];
	if (index == kPrefClosedCaptioning) {
		value = ConfMan.getBool(setting);
	} else if (!ConfMan.hasKey(setting)) {
		return false;
	} else if (isBoolean(index)) {
		// getBool also takes the legacy 0 and 1
		value = ConfMan.getBool(setting);
	} else if (isLetter(index)) {
		return parseLetter(ConfMan.get(setting), value);
	} else if (isMixerVolume(index)) {
		value = fromMixerVolume(ConfMan.getInt(setting));
	} else {
		value = ConfMan.getInt(setting);
	}
	return true;
}

void Preferences::write(uint index, int32 value) const {
	const char *setting = kPrefSettings[index];
	if (index == kPrefClosedCaptioning || isBoolean(index))
		ConfMan.setBool(setting, value != 0);
	else if (isLetter(index) && value >= 'A' && value <= 'Z')
		ConfMan.set(setting, Common::String((char)value));
	else if (isMixerVolume(index))
		ConfMan.setInt(setting, toMixerVolume(value));
	else
		ConfMan.setInt(setting, value);
}

bool Preferences::query(uint index, int32 &value) const {
	// A scripted run sees only its own registry; a preference without a value keeps its .gam value, as on the
	// exe's first start
	if (_scripted) {
		value = _scriptedValues[index];
		return _hasScripted[index];
	}
	if (!read(index, value))
		return false;
	if (isShared(index)) {
		_snapshot[index] = value;
		_hasSnapshot[index] = true;
	}
	return true;
}

bool Preferences::wantsReducedDepth() const {
	int32 value;
	return query(kPrefAutoSetDepth, value) && value == 'L';
}

void Preferences::save() {
	// A scripted run leaves the player's settings alone
	if (!_script || _scripted)
		return;
	for (uint index = 0; index < kCount; index++) {
		if (!_variables[index])
			continue;
		int32 value = _script->getPreferenceValue(index);
		write(index, value);
		if (isShared(index)) {
			_snapshot[index] = value;
			_hasSnapshot[index] = true;
		}
	}
	ConfMan.flushToDisk();
}

void Preferences::syncFromScummVM() {
	if (!_script || _scripted)
		return;
	for (uint index = 0; index < kCount; index++) {
		int32 value;
		if (!isShared(index) || !read(index, value))
			continue;
		// An unsaved in-game change (a slider moved, the menu opened before Return) stays: only a setting that
		// differs from what the engine last read or wrote is ScummVM's menu change
		if (_hasSnapshot[index] && _snapshot[index] == value)
			continue;
		_snapshot[index] = value;
		_hasSnapshot[index] = true;
		if (!_variables[index])
			continue; // before RegPreferences: query() reads it
		_script->setPreference(index, value);
		_script->setVariable(_variables[index], value);
	}
}

bool Preferences::isSet(uint index) const {
	return _variables[index] && _script->getVariable(_variables[index]);
}

int32 Preferences::gameVolume() const {
	int32 game = 100;
	if (_script && _variables[kPrefVolumeGame])
		game = _script->getPreferenceValue(kPrefVolumeGame);
	else if (_scripted && _hasScripted[kPrefVolumeGame])
		game = _scriptedValues[kPrefVolumeGame];
	else if (!_scripted && ConfMan.hasKey(kPrefSettings[kPrefVolumeGame]))
		game = ConfMan.getInt(kPrefSettings[kPrefVolumeGame]);
	return CLIP<int32>(game, 0, 100);
}

// PrefHooks

// The Game Preferences screens: PrefInit, AudioInit, PrefDialogVol, PrefMusicVol, PrefEffectVol, PrefVolume, the
// cycles, the toggles and PrefDefaults. The values go into the preference store (Script::setPreference, which
// RegPreferences writes into the variables at the next game) and into the hook's variable. XFilesEngine carries
// them to the ScummVM settings at ReturnMainMenu and to the running game at the menu's Previous (Prefs_Apply).

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

struct PrefDefault {
	uint index;
	int32 value;
};
/** PrefDefaults('P') (0x4c74f0): the Performance values. */
static const PrefDefault kPerformance[] = {
	{ kPrefAutoSetDepth, 'L' }, { kPrefClosedCaptioning, 0 }, { kPrefSubliminalMessages, 0 }, { kPrefTransitions, 0 },
	{ kPrefTranslucency, 0 }, { kPrefSmoothText, 0 }, { kPrefHighQualityVideo, 0 }, { kPrefLiveEnvironments, 0 },
	{ kPrefFlashlightBeam, 'S' }, { kPrefVolumeMusic, 0 }, { kPref3DSound, 0 }
};
/** PrefDefaults('Q') (0x4c7620): the Quality values. */
static const PrefDefault kQuality[] = {
	{ kPref3DSound, 1 }, { kPrefAutoSetDepth, 'H' }, { kPrefAutoSetResolution, 'Y' }, { kPrefFlashlightBeam, 'W' },
	{ kPrefHighQualityVideo, 1 }, { kPrefLiveEnvironments, 1 }, { kPrefRolloverText, 1 }, { kPrefSmoothText, 1 },
	{ kPrefSubliminalMessages, 1 }, { kPrefTransitions, 1 }, { kPrefTranslucency, 1 }, { kPrefVolumeDialog, 80 },
	{ kPrefVolumeEffect, 80 }, { kPrefVolumeMusic, 40 }, { kPrefVolumeGame, 70 }
};

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

	t.add("PrefDefaults", [](Script &s, const HookCall &c) {
		if (!c.has(1))
			return false;
		s._prefs.defaults(c.args[0]);
		return true;
	});
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
	// A value outside the table gives its second character: the search ends at the table's length, and the
	// exe takes (length + 1) mod length
	const char *next = found ? found + 1 : table + 1;
	setValue(index, variable, static_cast<byte>(*next ? *next : table[0]));
}

void PrefHooks::toggle(uint index, uint32 variable) {
	click();
	setValue(index, variable, getValue(index, variable) ? 0 : 1);
}

void PrefHooks::defaults(int32 set) {
	const PrefDefault *values;
	uint count;
	if (set == 'P') {
		values = kPerformance;
		count = ARRAYSIZE(kPerformance);
	} else if (set == 'Q') {
		values = kQuality;
		count = ARRAYSIZE(kQuality);
	} else {
		return;
	}
	click();
	for (uint i = 0; i < count; i++)
		_script.setPreference(values[i].index, values[i].value);
	// RegPreferences mode 5 (0x4310f0) writes all 24 preferences into their variables
	for (uint index = 0; index < kPrefCount; index++) {
		if (_script._prefVariables[index])
			_script.setVariable(_script._prefVariables[index], _script.getPreferenceValue(index));
	}
}

} // End of namespace XFiles
