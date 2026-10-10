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

#include "common/archive.h"
#include "common/debug.h"
#include "common/stream.h"

#include "xfiles/detection.h"
#include "xfiles/discfiles.h"

namespace XFiles {

// Media files sit at most one directory deep on each disc (XV/19812.XMV)
static const int kDiscDepth = 2;

DiscFiles::DiscFiles() : _root(nullptr) {
	for (uint i = 0; i < kDiscCount; i++)
		_discs[i] = nullptr;
}

DiscFiles::~DiscFiles() {
	delete _root;
	for (uint i = 0; i < kDiscCount; i++)
		delete _discs[i];
}

void DiscFiles::init(const Common::FSNode &gameDir) {
	_gameDir = gameDir;
	delete _root;
	_root = new Common::FSDirectory(gameDir, kDiscDepth);

	for (uint i = 0; i < kDiscCount; i++) {
		delete _discs[i];
		_discs[i] = nullptr;
	}
	Common::FSList children;
	if (!gameDir.getChildren(children, Common::FSNode::kListDirectoriesOnly))
		return;
	for (uint i = 0; i < children.size(); i++) {
		Common::String name = children[i].getName();
		if (name.size() == 3 && name.hasPrefixIgnoreCase("cd") && name[2] >= '1' && name[2] <= '7') {
			uint disc = name[2] - '0';
			delete _discs[disc];
			_discs[disc] = new Common::FSDirectory(children[i], kDiscDepth);
			debugC(1, kDebugFiles, "Disc %u: %s", disc, children[i].getPath().toString().c_str());
		}
	}
}

bool DiscFiles::hasDiscDirectory(uint disc) const {
	return disc < kDiscCount && _discs[disc];
}

const Common::FSDirectory *DiscFiles::findDirectory(uint disc, const Common::Path &path) const {
	if (!_root || disc >= kDiscCount)
		return nullptr;
	if (_discs[disc] && _discs[disc]->hasFile(path))
		return _discs[disc];
	if (_root->hasFile(path))
		return _root;
	return nullptr;
}

Common::Path DiscFiles::findFile(uint disc, const Common::String &name) const {
	Common::Path path(name, ':');
	const Common::FSDirectory *dir = findDirectory(disc, path);
	if (!dir)
		return Common::Path();
	Common::Path found = dir->getMember(path)->getPathInArchive();
	return dir == _root ? found : Common::Path(dir->getFSNode().getName()).join(found);
}

Common::SeekableReadStream *DiscFiles::openFile(uint disc, const Common::String &name) const {
	Common::Path path(name, ':');
	const Common::FSDirectory *dir = findDirectory(disc, path);
	return dir ? dir->createReadStreamForMember(path) : nullptr;
}

} // End of namespace XFiles
