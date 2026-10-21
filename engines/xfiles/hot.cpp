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
#include "common/stream.h"

#include "xfiles/detection.h"
#include "xfiles/discfiles.h"
#include "xfiles/hot.h"
#include "xfiles/vcdata.h"

namespace XFiles {

// The 600x240 video frame (the rectangle 0x6b9990 an entry is clipped to)
static const int kFrameWidth = 600;
static const int kFrameHeight = 240;

bool HotFile::load(const DiscFiles &files, const VCAsset &asset, int32 difficulty) {
	_entries.clear();
	for (uint i = 0; i < asset.files.size(); i++) {
		Common::String name = asset.files[i].file;
		int dot = name.findLastOf('.');
		if (dot < 0)
			continue;
		name = name.substr(0, dot) + ".HOT";
		Common::SeekableReadStream *stream = files.openFile(asset.files[i].disc, name);
		if (!stream)
			continue;
		if (stream->size() < 8 || stream->readUint32BE() != MKTAG('H', 'S', 'P', 'T')) {
			delete stream;
			continue;
		}
		uint32 count = stream->readUint32LE();
		for (uint32 n = 0; n < count && stream->size() - stream->pos() >= 20; n++) {
			HotEntry e;
			e.kind = stream->readUint32LE();
			int x1 = stream->readUint16LE(), y1 = stream->readUint16LE();
			int x2 = stream->readUint16LE(), y2 = stream->readUint16LE();
			e.startMs = stream->readUint32LE();
			e.endMs = stream->readUint32LE();
			// The action scene difficulty adjusts the rectangle (0x419e10, docs/formats/hot.md): "E" (the default)
			// grows each side by 15 % of the width or height, cut off, and clips to the frame; "H" moves each
			// side in by 12.5 %, a side of 10 px or less not at all, no clip; "S" and anything else leave it.
			// H and S are read from the exe only (B-031 run (a))
			int w = x2 - x1, h = y2 - y1;
			if (difficulty == 'E') {
				int dx = w * 15 / 100, dy = h * 15 / 100;
				e.rect = Common::Rect(MAX(x1 - dx, 0), MAX(y1 - dy, 0), MIN(x2 + dx, kFrameWidth), MIN(y2 + dy, kFrameHeight));
			} else if (difficulty == 'H') {
				int dx = w > 10 ? w * 125 / 1000 : 0, dy = h > 10 ? h * 125 / 1000 : 0;
				e.rect = Common::Rect(x1 + dx, y1 + dy, x2 - dx, y2 - dy);
			} else {
				e.rect = Common::Rect(x1, y1, x2, y2);
			}
			_entries.push_back(e);
		}
		delete stream;
		debugC(2, kDebugScript, "HotFile: %s, %u entries", name.c_str(), _entries.size());
		return !_entries.empty();
	}
	return false;
}

const HotEntry *HotFile::find(uint32 timeMs, const Common::Point &clipPos) const {
	for (uint i = 0; i < _entries.size(); i++) {
		const HotEntry &e = _entries[i];
		if (timeMs >= e.startMs && timeMs < e.endMs && e.rect.contains(clipPos))
			return &e;
	}
	return nullptr;
}

} // End of namespace XFiles
