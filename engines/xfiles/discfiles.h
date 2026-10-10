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

#ifndef XFILES_DISCFILES_H
#define XFILES_DISCFILES_H

#include "common/fs.h"
#include "common/path.h"
#include "common/str.h"

namespace XFiles {

/**
 * Finds media files by disc.
 *
 * The original game swaps between seven CDs, and 219 file names exist on
 * more than one of them, so the discs cannot share one directory. The
 * expected layout is the installed game (XFiles.hdb, XFiles.gam, and the
 * hard disk media) with each CD copied to a subdirectory cd1 ... cd7:
 *
 *   game/XFiles.hdb, game/XV/..., game/cd2/XN/..., ..., game/cd7/XG/...
 *
 * Disc 0 ("always on the hard disk") and 8 (copied by the larger install
 * options) are looked up in the game directory. Disc 1-7 are looked up in
 * cdN first and then in the game directory, so a copy of CD 1 can be merged
 * into it. Names are matched without regard to case.
 */
class DiscFiles {
public:
	enum {
		kDiscCount = 9 ///< 0 = hard disk, 1-7 = CDs, 8 = larger installs
	};

	DiscFiles();
	~DiscFiles();

	void init(const Common::FSNode &gameDir);

	/** True if the directory of a CD (cd1 ... cd7) exists. */
	bool hasDiscDirectory(uint disc) const;

	/**
	 * Locate a file of a disc, named as in the asset data with ':' as the
	 * separator ("XV:19812.xmv", "nav2.nmv"). Returns an empty path if the
	 * file is missing; the path is relative to the game directory.
	 */
	Common::Path findFile(uint disc, const Common::String &name) const;

	/** Open a file located with findFile(); nullptr if it is missing. */
	Common::SeekableReadStream *openFile(uint disc, const Common::String &name) const;

private:
	const Common::FSDirectory *findDirectory(uint disc, const Common::Path &path) const;

	Common::FSNode _gameDir;
	Common::FSDirectory *_root;
	Common::FSDirectory *_discs[kDiscCount];
};

} // End of namespace XFiles

#endif // XFILES_DISCFILES_H
