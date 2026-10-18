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
#include "common/debug.h"
#include "common/serializer.h"
#include "common/stream.h"
#include "common/textconsole.h"

#include "xfiles/xfiles.h"
#include "xfiles/movie.h"
#include "xfiles/vcdata.h"

namespace XFiles {

/**
 * The stream of a save: what the original's save holds (docs/subsystems/save-load.md,
 * docs/formats/saves.md), in ScummVM's stream. Little-endian; ScummVM appends its own trailer.
 * Add fields only with a version bump and syncAs...(x, firstVersion).
 */
enum {
	kSaveVersion = 2
};

/** A list of records of per values each: the number of records, then the values. */
static bool syncValues(Common::Serializer &s, Common::Array<uint32> &list, uint per, uint32 limit) {
	uint32 count = list.size() / per;
	s.syncAsUint32LE(count);
	if (s.isLoading()) {
		// Every value takes four bytes of the stream
		if (count > limit)
			return false;
		list.resize(count * per);
	}
	for (uint i = 0; i < list.size(); i++)
		s.syncAsUint32LE(list[i]);
	return true;
}

static bool syncStrings(Common::Serializer &s, Script::SavedValues &values, uint32 limit) {
	uint32 count = values.stringIds.size();
	s.syncAsUint32LE(count);
	if (s.isLoading()) {
		if (count > limit)
			return false;
		values.stringIds.resize(count);
		values.strings.resize(count);
	}
	for (uint i = 0; i < count; i++) {
		s.syncAsUint32LE(values.stringIds[i]);
		s.syncString(values.strings[i]);
	}
	return true;
}

/** Save or load according to s; limit: the stream's size, which bounds every count. false if the data is not a save. */
static bool syncSave(Common::Serializer &s, Script::SavedValues &values, VCSave &save, uint32 limit) {
	if (!s.matchBytes("XFSV", 4))
		return false;
	if (!s.syncVersion(kSaveVersion))
		return false;

	bool ok = syncValues(s, values.variables, 2, limit) && syncValues(s, values.enabled, 2, limit) && syncStrings(s, values, limit);
	if (!ok)
		return false;

	// The VCGameState in the order of docs/formats/saves.md (+6 to +130)
	for (int i = 0; i < 4; i++)
		s.syncAsUint32LE(save.position[i]);
	ok = syncValues(s, save.history, 1, limit) && syncValues(s, save.notes, 1, limit) && syncValues(s, save.emails, 1, limit) &&
		syncValues(s, save.emailsRead, 1, limit) && syncValues(s, save.emailsPending, 1, limit);
	if (!ok)
		return false;
	s.syncAsSint32LE(save.disc);
	s.syncAsSint32LE(save.photos);
	s.syncAsUint32LE(save.hintList);
	s.syncAsUint32LE(save.hintTrigger);
	s.syncAsSint32LE(save.hintGlow);
	s.syncAsSint32LE(save.credits);
	ok = syncValues(s, save.sounds, 3, limit) && syncValues(s, save.inventory, 1, limit) && syncValues(s, save.evidence, 2, limit) &&
		syncValues(s, save.kit, 1, limit) && syncValues(s, save.timers, 3, limit);
	if (!ok)
		return false;
	s.syncBytes(save.flashlight, sizeof(save.flashlight));
	s.syncAsByte(save.tint);
	s.syncBytes(save.tintColor, sizeof(save.tintColor));
	s.syncAsUint32LE(save.clip);
	s.syncAsByte(save.clipFlag);
	s.syncAsSint32LE(save.clipTime);
	s.syncAsUint32LE(save.dialogBox);
	s.syncAsUint32LE(save.layout);
	s.syncAsSint16LE(save.positionMode);

	// Version 2: the photos, after the state
	uint32 count = save.photoList.size();
	s.syncAsUint32LE(count, 2);
	if (s.getVersion() >= 2) {
		if (s.isLoading()) {
			if (count > limit)
				return false;
			save.photoList.resize(count);
		}
		for (uint i = 0; i < count; i++) {
			VCPhoto &photo = save.photoList[i];
			s.syncAsUint32LE(photo.asset);
			s.syncAsUint32LE(photo.clipTime);
			for (int j = 0; j < 4; j++)
				s.syncAsSint16LE(photo.crop[j]);
			s.syncAsByte(photo.disc);
			byte downloaded = photo.downloaded;
			s.syncAsByte(downloaded);
			photo.downloaded = downloaded != 0;
		}
	}
	return true;
}

Common::Error XFilesEngine::saveGameStream(Common::WriteStream *stream, bool isAutosave) {
	if (!_script)
		return Common::kWritingFailed;
	// An icon still waiting for the reveal's timer is saved in the bar
	flushPendingIcons(false);
	VCSave save;
	fillSnapshot(save);
	Script::SavedValues values;
	_script->getSaved(values);

	Common::Serializer s(nullptr, stream);
	syncSave(s, values, save, 0);
	debugC(1, kDebugScript, "-- game saved (%u bytes)", s.bytesSynced());
	return stream->err() ? Common::kWritingFailed : Common::kNoError;
}

Common::Error XFilesEngine::loadGameStream(Common::SeekableReadStream *stream) {
	// Only parse here: this runs from inside the event polling (the global
	// main menu), where the game cannot be torn down. The next frame loads.
	PendingLoad load;
	Common::Serializer s(stream, nullptr);
	if (!syncSave(s, load.values, load.save, stream->size()) || stream->err() || stream->eos()) {
		warning("The save is not valid");
		return Common::kReadingFailed;
	}
	load.valid = true;
	_pendingLoad = load;
	debugC(1, kDebugScript, "-- game load pending");
	return Common::kNoError;
}

bool XFilesEngine::canSaveGameStateCurrently(Common::U32String *msg) {
	// The moment of the original's save: the view is active (no travel, no
	// held view), no video plays or waits, no phone button is down. The
	// menu being open does not matter, it is where the original saves.
	return _script && !_pendingLoad.valid && _script->isViewActive() && _script->getVideoQueue().empty() && !_video->isOpen() &&
		_pressedItems.empty();
}

bool XFilesEngine::canLoadGameStateCurrently(Common::U32String *msg) {
	return _data != nullptr;
}

void XFilesEngine::fillSnapshot(VCSave &save) {
	// GameState_Save (0x4d9450); Script knows its part
	_script->snapshot(save);
	if (_dialogBox.isOpen())
		save.dialogBox = _dialogBox.getCharView();

	// Timers in the order they were started: a one-shot one keeps its
	// remaining time, a periodic one its period
	uint32 now = gameMillis();
	const Common::HashMap<uint, Script::Timer> &timers = _script->getTimers();
	Common::Array<uint> numbers;
	for (Common::HashMap<uint, Script::Timer>::const_iterator it = timers.begin(); it != timers.end(); ++it)
		numbers.push_back(it->_key);
	for (uint i = 1; i < numbers.size(); i++) {
		for (uint j = i; j > 0 && timers.getVal(numbers[j]).serial < timers.getVal(numbers[j - 1]).serial; j--)
			SWAP(numbers[j], numbers[j - 1]);
	}
	save.timers.clear();
	for (uint i = 0; i < numbers.size(); i++) {
		const Script::Timer &timer = timers.getVal(numbers[i]);
		uint32 time = timer.ms;
		if (!timer.periodic && _timerStates.contains(numbers[i]) && _timerStates[numbers[i]].serial == timer.serial) {
			// A timer the PDA paused keeps what it had left
			const TimerState &state = _timerStates[numbers[i]];
			uint32 elapsed = state.paused ? 0 : now - state.started;
			time = elapsed < state.ms ? state.ms - elapsed : 0;
		}
		save.timers.push_back(numbers[i]);
		save.timers.push_back(time);
		save.timers.push_back(timer.periodic ? 1 : 0);
	}

	// Sounds, with their position in ms from the start of the file
	save.sounds.clear();
	_sound.getSaved(save.sounds);

	// The asset the view shows, with its time in the file
	save.clip = 0;
	save.clipFlag = 0;
	save.clipTime = 0;
	if (!_layers.empty() && !_viewAssets.empty()) {
		VCAsset asset;
		if (_data->getAsset(_viewAssets[0], asset)) {
			save.clip = asset.id;
			save.clipFlag = (asset.flags & kAssetLoop) != 0;
			save.clipTime = _layers[0]->getTime();
		}
	}
	save.credits = _headCredits.getLastCredit();
}

} // End of namespace XFiles
