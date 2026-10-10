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

#ifndef XFILES_NEOACCESS_H
#define XFILES_NEOACCESS_H

#include "common/array.h"
#include "common/hashmap.h"
#include "common/str.h"
#include "common/stream.h"
#include "common/ustr.h"

namespace XFiles {

/**
 * One entry of the class table of a NeoAccess database.
 */
struct NeoClass {
	uint32 id;
	uint32 count;      ///< Number of objects, as recorded in the class table
	uint32 root;       ///< Root of the class's primary index, 0 for an empty class
	byte subclasses;
	Common::String key; ///< Index key tag: "null" or "ID  "
};

/**
 * A leaf entry of a class index: the object's file offset and its ID.
 */
struct NeoEntry {
	uint32 mark;
	uint32 key;
};

/**
 * Reader for the NeoAccess 5 object databases of the game (XFiles.hdb,
 * XFiles.gam and .X saves).
 *
 * The whole file is kept in memory. All values are big-endian, file space
 * is allocated in 8-byte units and every stored object starts with a 6-byte
 * header (u16 flags, u32 version 1), so class data starts at offset 6.
 * Index nodes add a u16 count; leaves (flag 0x8000) then hold (u32 mark,
 * u32 key) entries, inodes hold u32 child marks.
 */
class NeoFile {
public:
	NeoFile();

	/** Read a database from a stream; returns false if it is not one. */
	bool load(Common::SeekableReadStream &stream);

	uint32 _version;
	uint32 _freeListRoot;
	uint32 _classRoot;
	uint32 _classCount;
	uint32 _nextId;

	/** Classes in ID order. */
	const Common::Array<NeoClass> &getClasses() const { return _classes; }
	const NeoClass *getClass(uint32 id) const;

	/** Append every leaf entry under root to entries, in key order. Returns false on a bad node. */
	bool walk(uint32 root, Common::Array<NeoEntry> &entries) const;

	/** Append (mark, object ID) for every object of a class. */
	bool getObjects(uint32 classId, Common::Array<NeoEntry> &entries) const;

	/** Append the object IDs of a list index (leaves hold bare u32 IDs). */
	bool readIdIndex(uint32 root, Common::Array<uint32> &ids) const;

	/**
	 * Read the object IDs of an embedded 26-byte object list at mark
	 * (u32 count, u32 index root, ...). Returns false if the index does not
	 * hold count IDs.
	 */
	bool readObjectList(uint32 mark, Common::Array<uint32> &ids) const;

	/** NUL-terminated Mac Roman string stored at (mark, length). */
	Common::U32String getString(uint32 mark, uint32 length) const;

	/** Bounds-checked big-endian reads; out-of-range bytes read as 0. */
	byte readByte(uint32 offset) const;
	uint16 readUint16(uint32 offset) const;
	uint32 readUint32(uint32 offset) const;
	int16 readSint16(uint32 offset) const { return (int16)readUint16(offset); }

	const byte *getData() const { return _data.data(); }
	uint32 getSize() const { return _data.size(); }

private:
	bool readNodeHeader(uint32 mark, bool &leaf, uint16 &count) const;
	bool readClassTable(uint32 root);

	Common::Array<byte> _data;
	Common::Array<NeoClass> _classes;
};

} // End of namespace XFiles

#endif // XFILES_NEOACCESS_H
