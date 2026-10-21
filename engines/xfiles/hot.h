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

#ifndef XFILES_HOT_H
#define XFILES_HOT_H

#include "common/array.h"
#include "common/rect.h"

namespace XFiles {

class DiscFiles;
struct VCAsset;

/** One entry of a clip's .HOT file: a timed hotspot over the clip (docs/formats/hot.md). */
struct HotEntry {
	uint32 kind;
	Common::Rect rect;   ///< In clip coordinates, grown for the "E" action scene difficulty
	uint32 startMs, endMs;
};

/**
 * The .HOT file next to a clip (XV:19754.xmv -> XV:19754.HOT). The exe makes an object for an entry when its
 * window starts and drops it when it ends (0x4386c0, 0x4388c0); a click is tested against the rectangle moved by
 * the band origin, and only while the window is open.
 */
class HotFile {
public:
	/** Load the entries of the asset's first file that has a .HOT; false if there is none. */
	bool load(const DiscFiles &files, const VCAsset &asset);
	void clear() { _entries.clear(); }
	bool isEmpty() const { return _entries.empty(); }
	const Common::Array<HotEntry> &getEntries() const { return _entries; }

	/** The first entry whose window holds timeMs and whose rectangle holds the clip point; nullptr if none. */
	const HotEntry *find(uint32 timeMs, const Common::Point &clipPos) const;

private:
	Common::Array<HotEntry> _entries;
};

} // End of namespace XFiles

#endif // XFILES_HOT_H
