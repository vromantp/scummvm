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


#include "common/debug.h"
#include "common/serializer.h"
#include "common/stream.h"
#include "common/textconsole.h"

#include "xfiles/xfiles.h"

namespace XFiles {

/**
 * The stream of a save: what the original's save holds (docs/subsystems/save-load.md,
 * docs/formats/saves.md), in ScummVM's stream. Little-endian; ScummVM appends its own trailer.
 * Add fields only with a version bump and syncAs...(x, firstVersion).
 */
enum {
	kSaveVersion = 1
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
	return true;
}

Common::Error XFilesEngine::saveGameStream(Common::WriteStream *stream, bool isAutosave) {
	if (!_script)
		return Common::kWritingFailed;
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

} // End of namespace XFiles
