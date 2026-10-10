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
#include "common/endian.h"
#include "common/textconsole.h"

#include "xfiles/neoaccess.h"

namespace XFiles {

enum {
	kHeaderSize = 0x14,
	kClassEntrySize = 43,
	kNodeHeaderSize = 8,
	kLeafFlag = 0x8000
};

NeoFile::NeoFile() : _version(0), _freeListRoot(0), _classRoot(0), _classCount(0), _nextId(0) {
}

bool NeoFile::load(Common::SeekableReadStream &stream) {
	_classes.clear();
	_data.resize(stream.size());
	if (stream.size() < kHeaderSize || stream.read(_data.data(), _data.size()) != _data.size())
		return false;

	_version = readUint32(0);
	_freeListRoot = readUint32(4);
	_classRoot = readUint32(8);
	_classCount = readUint32(12);
	_nextId = readUint32(16);
	if (_version != 5) {
		warning("NeoFile: unsupported version %u", _version);
		return false;
	}
	return readClassTable(_classRoot);
}

byte NeoFile::readByte(uint32 offset) const {
	return offset < _data.size() ? _data[offset] : 0;
}

uint16 NeoFile::readUint16(uint32 offset) const {
	if (offset > _data.size() || _data.size() - offset < 2)
		return 0;
	return READ_BE_UINT16(&_data[offset]);
}

uint32 NeoFile::readUint32(uint32 offset) const {
	if (offset > _data.size() || _data.size() - offset < 4)
		return 0;
	return READ_BE_UINT32(&_data[offset]);
}

bool NeoFile::readNodeHeader(uint32 mark, bool &leaf, uint16 &count) const {
	if (mark > _data.size() || _data.size() - mark < kNodeHeaderSize || readUint32(mark + 2) != 1) {
		warning("NeoFile: no node at 0x%x", mark);
		return false;
	}
	leaf = (readUint16(mark) & kLeafFlag) != 0;
	count = readUint16(mark + 6);
	return true;
}

bool NeoFile::walk(uint32 root, Common::Array<NeoEntry> &entries) const {
	if (!root)
		return true;
	bool leaf;
	uint16 count;
	if (!readNodeHeader(root, leaf, count))
		return false;
	for (uint i = 0; i < count; i++) {
		if (leaf) {
			NeoEntry entry;
			entry.mark = readUint32(root + kNodeHeaderSize + 8 * i);
			entry.key = readUint32(root + kNodeHeaderSize + 8 * i + 4);
			entries.push_back(entry);
		} else if (!walk(readUint32(root + kNodeHeaderSize + 4 * i), entries)) {
			return false;
		}
	}
	return true;
}

bool NeoFile::readClassTable(uint32 root) {
	bool leaf;
	uint16 count;
	if (!readNodeHeader(root, leaf, count))
		return false;
	for (uint i = 0; i < count; i++) {
		if (!leaf) {
			if (!readClassTable(readUint32(root + kNodeHeaderSize + 4 * i)))
				return false;
			continue;
		}
		uint32 e = root + kNodeHeaderSize + kClassEntrySize * i;
		NeoClass c;
		c.id = readUint32(e + 11);
		c.count = readUint32(e + 15);
		c.subclasses = readByte(e + 9);
		c.root = readUint32(e + 31);
		for (int j = 0; j < 4; j++)
			c.key += (char)readByte(e + 39 + j);

		// A later entry with the same ID replaces an earlier one
		bool replaced = false;
		for (uint j = 0; j < _classes.size(); j++) {
			if (_classes[j].id == c.id) {
				_classes[j] = c;
				replaced = true;
			}
		}
		if (!replaced)
			_classes.push_back(c);
	}
	Common::sort(_classes.begin(), _classes.end(), [](const NeoClass &a, const NeoClass &b) {
		return a.id < b.id;
	});
	return true;
}

const NeoClass *NeoFile::getClass(uint32 id) const {
	for (uint i = 0; i < _classes.size(); i++) {
		if (_classes[i].id == id)
			return &_classes[i];
	}
	return nullptr;
}

bool NeoFile::getObjects(uint32 classId, Common::Array<NeoEntry> &entries) const {
	const NeoClass *c = getClass(classId);
	return c && walk(c->root, entries);
}

bool NeoFile::readIdIndex(uint32 root, Common::Array<uint32> &ids) const {
	if (!root)
		return true;
	bool leaf;
	uint16 count;
	if (!readNodeHeader(root, leaf, count))
		return false;
	for (uint i = 0; i < count; i++) {
		uint32 value = readUint32(root + kNodeHeaderSize + 4 * i);
		if (leaf)
			ids.push_back(value);
		else if (!readIdIndex(value, ids))
			return false;
	}
	return true;
}

bool NeoFile::readObjectList(uint32 mark, Common::Array<uint32> &ids) const {
	uint32 count = readUint32(mark);
	uint32 start = ids.size();
	if (!readIdIndex(readUint32(mark + 4), ids))
		return false;
	if (ids.size() - start != count) {
		warning("NeoFile: list at 0x%x has %u IDs, header says %u", mark, ids.size() - start, count);
		return false;
	}
	return true;
}

Common::U32String NeoFile::getString(uint32 mark, uint32 length) const {
	Common::String raw;
	for (uint32 i = 0; i < length && mark + i < _data.size() && _data[mark + i]; i++)
		raw += (char)_data[mark + i];
	return Common::U32String(raw, Common::kMacRoman);
}

} // End of namespace XFiles
