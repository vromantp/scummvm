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

#include "common/endian.h"

#include "xfiles/vcdata.h"

namespace XFiles {

// Offset of the embedded child list of the scene classes and of the list in *List objects
enum {
	kFieldStart = 6
};

const char *getClassName(uint32 classId) {
	static const char *const names[] = {
		"VCTitle", "VCNode", "VCLocaton", "VCViewPoint", "VCView",                         // 39
		"VCCharacter", "VCCharView", "VCCharViewList", "VCName", nullptr,                  // 44
		"VCConversation", "VCConversationList", "VCNav", "VCNav_List", "VCAssetRef",       // 49
		"VCAssetRefList", "VCExplorable", "VCExplorableList", "VCHotSpot", "VCHotSpotList", // 54
		"VCStdHotSpotList", "VCAssetCategory", "VCCursor", "VCDefaultCursors", nullptr,    // 59
		"VCDefaultHotSpots", "VCAction", "VCActionList", "VCBlob", "VCPlayer",             // 64
		nullptr, "VCEnabled", "VCActionIcon", "VCEmotionIcon", "VCEvidenceIcon",           // 69
		"VCIdeaIcon", "VCInventory", "VCIdeaMap", "VCIdeaMapList", "VCIFaceLayout",        // 74
		"VCInterfaceItem", "VCString", "VCTrigger", "VCTriggerList", "VCVariable",         // 79
		"VCStdAction", "VCStdActionList", "VCConversationHistory", "VCGameState",          // 84
		"VCPDANotes", "VCPhoto", "VCEmail", "VCEmailRead", "VCEmailPending"                // 88
	};
	if (classId < kClassTitle || classId - kClassTitle >= ARRAYSIZE(names))
		return nullptr;
	return names[classId - kClassTitle];
}

static bool isPythonWhiteSpace(Common::u32char_type_t c) {
	return (c >= 0x09 && c <= 0x0d) || (c >= 0x1c && c <= 0x20) || c == 0x85 || c == 0xa0 || c == 0x1680 ||
		(c >= 0x2000 && c <= 0x200a) || c == 0x2028 || c == 0x2029 || c == 0x202f || c == 0x205f || c == 0x3000;
}

Common::U32String stripWhiteSpace(const Common::U32String &str) {
	uint start = 0, end = str.size();
	while (start < end && isPythonWhiteSpace(str[start]))
		start++;
	while (end > start && isPythonWhiteSpace(str[end - 1]))
		end--;
	return Common::U32String(str.c_str() + start, end - start);
}

/**
 * Big-endian reads from a blob that fail, instead of reading past the end.
 */
class BlobReader {
public:
	BlobReader(const byte *data, uint32 size) : _data(data), _size(size), _pos(0), _ok(true) {}

	bool check(uint32 n) {
		if (_pos > _size || _size - _pos < n)
			_ok = false;
		return _ok;
	}
	byte readByte() {
		if (!check(1))
			return 0;
		return _data[_pos++];
	}
	uint16 readUint16() {
		if (!check(2))
			return 0;
		_pos += 2;
		return READ_BE_UINT16(_data + _pos - 2);
	}
	uint32 readUint32() {
		if (!check(4))
			return 0;
		_pos += 4;
		return READ_BE_UINT32(_data + _pos - 4);
	}

	const byte *_data;
	uint32 _size;
	uint32 _pos;
	bool _ok;
};

VCAction::VCAction() : id(0), type(0), conditional(false), assetList(0), mode(0), x(0), y(0), ms(0), timer(0),
	command(0), periodic(false), target(0), value(0), layout(0) {
	condition.lhs.kind = condition.rhs.kind = condition.op = 0;
	condition.lhs.value = condition.rhs.value = 0;
	statement = condition;
	for (int i = 0; i < 4; i++)
		path[i] = 0;
}

bool VCData::buildIndex(const NeoFile &db, ObjectIndex &index) {
	const Common::Array<NeoClass> &classes = db.getClasses();
	for (uint i = 0; i < classes.size(); i++) {
		Common::Array<NeoEntry> entries;
		if (!db.walk(classes[i].root, entries))
			return false;
		for (uint j = 0; j < entries.size(); j++) {
			ObjectRef ref;
			ref.classId = classes[i].id;
			ref.mark = entries[j].mark;
			index[entries[j].key] = ref;
		}
	}
	return true;
}

bool VCData::load(Common::SeekableReadStream &hdb, Common::SeekableReadStream &gam) {
	_hdbIndex.clear();
	_gamIndex.clear();
	return _hdb.load(hdb) && _gam.load(gam) && buildIndex(_hdb, _hdbIndex) && buildIndex(_gam, _gamIndex);
}

bool VCData::lookup(uint32 id, uint32 &classId, uint32 &mark) const {
	ObjectIndex::const_iterator it = _hdbIndex.find(id);
	if (it == _hdbIndex.end())
		return false;
	classId = it->_value.classId;
	mark = it->_value.mark;
	return true;
}

bool VCData::lookupGam(uint32 id, uint32 &classId, uint32 &mark) const {
	ObjectIndex::const_iterator it = _gamIndex.find(id);
	if (it == _gamIndex.end())
		return false;
	classId = it->_value.classId;
	mark = it->_value.mark;
	return true;
}

uint32 VCData::getClassOf(uint32 id) const {
	uint32 classId, mark;
	return lookup(id, classId, mark) ? classId : 0;
}

bool VCData::getObject(uint32 id, uint32 classId, uint32 &mark) const {
	uint32 found;
	return lookup(id, found, mark) && found == classId;
}

bool VCData::getName(uint32 id, Common::U32String &name) const {
	uint32 mark;
	if (!getObject(id, kClassName, mark))
		return false;
	name = stripWhiteSpace(_hdb.getString(_hdb.readUint32(mark + 6), _hdb.readUint32(mark + 10)));
	return true;
}

bool VCData::getVariableName(uint32 id, Common::U32String &name) const {
	uint32 classId, mark;
	if (!lookupGam(id, classId, mark) || classId != kClassVariable)
		return false;
	name = stripWhiteSpace(_gam.getString(_gam.readUint32(mark + 6), _gam.readUint32(mark + 10)));
	return true;
}

bool VCData::getMembers(uint32 listId, Common::Array<uint32> &ids) const {
	if (!listId)
		return true;
	uint32 classId, mark;
	return lookup(listId, classId, mark) && _hdb.readObjectList(mark + kFieldStart, ids);
}

static void readRect(const NeoFile &db, uint32 offset, VCRect &rect) {
	rect.left = db.readSint16(offset);
	rect.top = db.readSint16(offset + 2);
	rect.right = db.readSint16(offset + 4);
	rect.bottom = db.readSint16(offset + 6);
}

// -- Scene graph: Title > Node > Locaton > ViewPoint > View ------------------

bool VCData::getTitle(VCTitle &title) const {
	const NeoClass *c = _hdb.getClass(kClassTitle);
	Common::Array<NeoEntry> entries;
	if (!c || !_hdb.walk(c->root, entries) || entries.empty())
		return false;
	uint32 m = entries[0].mark;
	title.id = entries[0].key;
	title.name = _hdb.readUint32(m + 60);
	title.triggers = _hdb.readUint32(m + 64);
	title.nodes.clear();
	return _hdb.readObjectList(m + kFieldStart, title.nodes);
}

bool VCData::getNode(uint32 id, VCNode &node) const {
	uint32 m;
	if (!getObject(id, kClassNode, m))
		return false;
	node.id = id;
	node.name = _hdb.readUint32(m + 32);
	node.triggers = _hdb.readUint32(m + 36);
	node.locations.clear();
	return _hdb.readObjectList(m + kFieldStart, node.locations);
}

bool VCData::getLocation(uint32 id, VCLocation &location) const {
	uint32 m;
	if (!getObject(id, kClassLocation, m))
		return false;
	location.id = id;
	location.name = _hdb.readUint32(m + 32);
	location.assets = _hdb.readUint32(m + 36);
	location.triggers = _hdb.readUint32(m + 40);
	location.unknown = _hdb.readByte(m + 44);
	location.viewPoints.clear();
	return _hdb.readObjectList(m + kFieldStart, location.viewPoints);
}

bool VCData::getViewPoint(uint32 id, VCViewPoint &viewPoint) const {
	uint32 m;
	if (!getObject(id, kClassViewPoint, m))
		return false;
	viewPoint.id = id;
	viewPoint.x = _hdb.readSint16(m + 32);
	viewPoint.y = _hdb.readSint16(m + 34);
	viewPoint.name = _hdb.readUint32(m + 36);
	viewPoint.triggers = _hdb.readUint32(m + 40);
	viewPoint.views.clear();
	return _hdb.readObjectList(m + kFieldStart, viewPoint.views);
}

bool VCData::getView(uint32 id, VCView &view) const {
	uint32 m;
	if (!getObject(id, kClassView, m))
		return false;
	view.id = id;
	view.facing = _hdb.readUint32(m + 6);
	view.assets = _hdb.readUint32(m + 10);
	view.characters = _hdb.readUint32(m + 14);
	view.navs = _hdb.readUint32(m + 18);
	view.explorables = _hdb.readUint32(m + 22);
	view.triggers = _hdb.readUint32(m + 26);
	return true;
}

bool VCData::getNav(uint32 id, VCNav &nav) const {
	uint32 m;
	if (!getObject(id, kClassNav, m))
		return false;
	nav.id = id;
	nav.target = _hdb.readUint32(m + 6);
	nav.hotSpot = _hdb.readUint32(m + 10);
	nav.triggers = _hdb.readUint32(m + 14);
	return true;
}

bool VCData::getHotSpot(uint32 id, VCHotSpot &hotSpot) const {
	uint32 m;
	if (!getObject(id, kClassHotSpot, m))
		return false;
	hotSpot.id = id;
	hotSpot.cursor = _hdb.readUint32(m + 6);
	readRect(_hdb, m + 10, hotSpot.rect);
	hotSpot.name = _hdb.readUint32(m + 18);
	return true;
}

bool VCData::getDefaultHotSpots(uint32 hotSpots[3]) const {
	const NeoClass *c = _hdb.getClass(kClassDefaultHotSpots);
	Common::Array<NeoEntry> entries;
	if (!c || !_hdb.walk(c->root, entries) || entries.empty())
		return false;
	for (int i = 0; i < 3; i++)
		hotSpots[i] = _hdb.readUint32(entries[0].mark + kFieldStart + 4 * i);
	return true;
}

// -- Triggers and actions -----------------------------------------------------

bool VCData::getTrigger(uint32 id, VCTrigger &trigger) const {
	uint32 m;
	if (!getObject(id, kClassTrigger, m))
		return false;
	trigger.id = id;
	trigger.event = _hdb.readByte(m + 10);
	trigger.actions.clear();
	return getMembers(_hdb.readUint32(m + 6), trigger.actions);
}

bool VCData::getAction(uint32 id, VCAction &action) const {
	uint32 m;
	if (!getObject(id, kClassAction, m))
		return false;
	uint32 ptr = _hdb.readUint32(m + 6);
	uint32 length = _hdb.readUint32(m + 10);
	if (ptr > _hdb.getSize() || _hdb.getSize() - ptr < length)
		return false;
	action = VCAction();
	action.id = id;
	return decodeAction(_hdb.getData() + ptr, length, _hdb.readByte(m + 14), action);
}

static void readExpression(BlobReader &r, VCExpression &e) {
	e.lhs.value = r.readUint32();
	e.rhs.value = r.readUint32();
	e.lhs.kind = r.readByte();
	e.rhs.kind = r.readByte();
	e.op = r.readByte();
}

bool VCData::decodeAction(const byte *blob, uint32 size, byte code, VCAction &action) {
	BlobReader r(blob, size);
	action.conditional = (code & 0x80) != 0;
	if (action.conditional) {
		readExpression(r, action.condition);
		r.readByte(); // Unknown
	}
	action.type = code & 0x7f;
	uint32 start = r._pos;

	switch (action.type) {
	case kActionStatement:
		readExpression(r, action.statement);
		break;
	case kActionAsset:
	case kActionSound3D:
		action.assetList = r.readUint32();
		action.mode = r.readByte();
		if (action.type == kActionSound3D) {
			// Same grid as VCViewPoint positions
			r._pos = start + 6;
			action.x = (int16)r.readUint16();
			action.y = (int16)r.readUint16();
		}
		break;
	case kActionTimer: {
		action.ms = r.readUint32();
		action.timer = r.readByte();
		byte flags = r.readByte();
		action.command = flags & 0x7f;
		action.periodic = (flags & 0x80) != 0;
		break;
	}
	case kActionEnable:
		action.target = r.readUint32();
		r.readUint32(); // Always 0
		action.value = r.readByte();
		break;
	case kActionSetView:
		for (int i = 0; i < 4; i++)
			action.path[i] = r.readUint32();
		break;
	case kActionInterface:
		action.layout = r.readUint32();
		action.mode = r.readByte();
		break;
	case kActionFunction: {
		// char name[15] (NUL-terminated, then stale bytes), u8 argc, argc x u32 value, argc x u8 kind
		uint32 end = start;
		while (end < size && blob[end])
			end++;
		if (end >= size)
			return false;
		action.function = Common::String((const char *)blob + start, end - start);
		r._pos = start + 15;
		byte argc = r.readByte();
		for (uint i = 0; i < argc; i++) {
			uint32 kindPos = start + 16 + 4 * argc + i;
			if (kindPos >= size)
				break;
			VCOperand arg;
			arg.value = r.readUint32();
			arg.kind = blob[kindPos];
			action.args.push_back(arg);
		}
		break;
	}
	default:
		if (start <= size)
			action.raw = Common::Array<byte>(blob + start, size - start);
		break;
	}
	return r._ok;
}

// -- Assets -------------------------------------------------------------------

/**
 * Fields of a media blob: Pascal strings holding decimal numbers, or a
 * length followed by raw characters.
 */
class MediaBlobReader {
public:
	MediaBlobReader(const byte *data, uint32 size) : _data(data), _size(size), _pos(0), _ok(true) {}

	Common::String readField() {
		if (_pos >= _size) {
			_ok = false;
			return Common::String();
		}
		uint32 n = _data[_pos];
		uint32 end = MIN(_pos + 1 + n, _size);
		Common::String text((const char *)_data + _pos + 1, end - _pos - 1);
		_pos += 1 + n;
		return text;
	}

	int32 readNumber() {
		Common::String text = readField();
		text.trim();
		uint i = 0;
		bool negative = false;
		if (i < text.size() && (text[i] == '-' || text[i] == '+'))
			negative = text[i++] == '-';
		if (i == text.size())
			_ok = false;
		int32 value = 0;
		for (; i < text.size(); i++) {
			if (!Common::isDigit(text[i]))
				_ok = false;
			value = value * 10 + (text[i] - '0');
		}
		return negative ? -value : value;
	}

	/** A length and the raw characters; Mac path separators (0x7f) become ':'. */
	Common::String readString() {
		int32 n = readNumber();
		if (!_ok || n < 0)
			return Common::String();
		Common::String text;
		for (int32 i = 0; i < n && _pos + i < _size; i++)
			text += _data[_pos + i] == 0x7f ? ':' : (char)_data[_pos + i];
		_pos += n;
		return text;
	}

	bool atEnd() const {
		return _pos >= _size || !_data[_pos];
	}

	const byte *_data;
	uint32 _size;
	uint32 _pos;
	bool _ok;
};

bool VCData::decodeMediaBlob(const byte *blob, uint32 size, byte flags, VCAsset &asset) {
	MediaBlobReader r(blob, size);
	asset.path = r.readString();
	asset.hasRange = (flags & kAssetRange) != 0;
	if (asset.hasRange) {
		asset.rangeIn = r.readNumber();
		asset.rangeOut = r.readNumber();
	}
	r.readField(); // Always 0
	while (r._ok && !r.atEnd()) {
		VCMediaFile file;
		file.disc = r.readNumber();
		file.file = r.readString();
		file.start = r.readNumber();
		file.end = r.readNumber();
		asset.files.push_back(file);
	}
	return r._ok;
}

bool VCData::getAsset(uint32 id, VCAsset &asset) const {
	uint32 m;
	if (!getObject(id, kClassAssetRef, m))
		return false;
	uint32 ptr = _hdb.readUint32(m + 6);
	uint32 length = _hdb.readUint32(m + 10);
	// +14 and +18 hold a 24-bit object ID with a flag byte on top (meaning unknown)
	uint32 extra = _hdb.readUint32(m + 18) & 0xffffff;
	asset.id = id;
	asset.name = _hdb.readUint32(m + 14) & 0xffffff;
	asset.flags = _hdb.readByte(m + 22);
	asset.category = _hdb.readByte(m + 23);
	asset.triggers = getClassOf(extra) == kClassTriggerList ? extra : 0;
	asset.readable = true;
	asset.path.clear();
	asset.hasRange = false;
	asset.rangeIn = asset.rangeOut = 0;
	asset.files.clear();
	if (asset.flags & kAssetFile) {
		// One orphaned asset shares its blob with an action
		if (ptr > _hdb.getSize() || _hdb.getSize() - ptr < length ||
			!decodeMediaBlob(_hdb.getData() + ptr, length, asset.flags, asset)) {
			asset.readable = false;
			asset.files.clear();
		}
	}
	return true;
}

bool VCData::getText(uint32 id, Common::U32String &text) const {
	uint32 m;
	if (!getObject(id, kClassAssetRef, m))
		return false;
	uint32 ptr = _hdb.readUint32(m + 6);
	uint32 length = _hdb.readUint32(m + 10);
	if (ptr > _hdb.getSize() || _hdb.getSize() - ptr < length)
		return false;
	// A text asset's blob: the text length as a Pascal string, the text, then 7 numbers (style, not decoded)
	MediaBlobReader r(_hdb.getData() + ptr, length);
	int32 n = r.readNumber();
	if (!r._ok || n < 0 || r._pos + n > length)
		return false;
	text = Common::U32String(Common::String((const char *)_hdb.getData() + ptr + r._pos, n), Common::kMacRoman);
	return true;
}

bool VCData::getResourceRef(uint32 id, uint32 &type, uint32 &resource) const {
	uint32 m;
	if (!getObject(id, kClassAssetRef, m) || (_hdb.readByte(m + 22) & kAssetFile))
		return false;
	uint32 ptr = _hdb.readUint32(m + 6);
	uint32 length = _hdb.readUint32(m + 10);
	if (!length || ptr > _hdb.getSize() || _hdb.getSize() - ptr < length)
		return false;
	MediaBlobReader r(_hdb.getData() + ptr, length);
	int32 t = r.readNumber();
	int32 n = r.readNumber();
	if (!r._ok || t <= 0 || n < 0)
		return false;
	type = t;
	resource = n;
	return true;
}

// -- .gam objects -------------------------------------------------------------

bool VCData::getVariable(uint32 id, VCVariable &variable) const {
	uint32 classId, m;
	if (!lookupGam(id, classId, m) || classId != kClassVariable)
		return false;
	variable.id = id;
	variable.name = stripWhiteSpace(_gam.getString(_gam.readUint32(m + 6), _gam.readUint32(m + 10)));
	variable.value = _gam.readUint32(m + 14);
	variable.owner = _gam.readUint32(m + 18);
	variable.ownerClass = _gam.readByte(m + 22);
	variable.flags = _gam.readByte(m + 23);
	return true;
}

bool VCData::getString(uint32 id, Common::String &text) const {
	uint32 classId, m;
	if (!lookupGam(id, classId, m) || classId != kClassString)
		return false;
	// A NUL-terminated string; length 0 for an empty one
	uint32 ptr = _gam.readUint32(m + 6);
	uint32 length = _gam.readUint32(m + 10);
	if (ptr > _gam.getSize() || _gam.getSize() - ptr < length)
		return false;
	const char *p = (const char *)_gam.getData() + ptr;
	uint32 n = 0;
	while (n < length && p[n])
		n++;
	text = Common::String(p, n);
	return true;
}

bool VCData::getEnabled(uint32 id, uint32 &target, bool &enabled) const {
	uint32 classId, m;
	if (!lookupGam(id, classId, m) || classId != kClassEnabled)
		return false;
	target = _gam.readUint32(m + 6);
	enabled = _gam.readByte(m + 10) != 0;
	return true;
}

/** The u32 list of the blob whose (offset, length) is at mark; the lists are little-endian. */
static void readBlobList(const NeoFile &db, uint32 mark, Common::Array<uint32> &list) {
	uint32 offset = db.readUint32(mark), length = db.readUint32(mark + 4);
	list.clear();
	for (uint32 i = 0; i + 4 <= length; i += 4) {
		uint32 at = offset + i;
		list.push_back(db.readByte(at) | db.readByte(at + 1) << 8 | db.readByte(at + 2) << 16 | (uint32)db.readByte(at + 3) << 24);
	}
}

VCSave::VCSave() : positionMode(1), disc(1), photos(0), hintList(0), hintTrigger(0), hintGlow(0), credits(-1), tint(0), clip(0),
	clipFlag(0), clipTime(0), dialogBox(0), layout(0) {
	memset(position, 0, sizeof(position));
	memset(flashlight, 0, sizeof(flashlight));
	memset(tintColor, 0, sizeof(tintColor));
}

bool VCData::getSave(VCSave &save) const {
	Common::Array<NeoEntry> states;
	if (!_gam.getObjects(kClassGameState, states) || states.empty())
		return false;
	uint32 m = states[0].mark;
	for (int i = 0; i < 4; i++)
		save.position[i] = _gam.readUint32(m + 6 + 4 * i);
	save.positionMode = _gam.readSint16(m + 130);

	static const uint32 kListClasses[5] = { kClassConversationHistory, kClassPDANotes, kClassEmail, kClassEmailRead, kClassEmailPending };
	Common::Array<uint32> *lists[5] = { &save.history, &save.notes, &save.emails, &save.emailsRead, &save.emailsPending };
	for (int i = 0; i < 5; i++) {
		uint32 classId, mark;
		lists[i]->clear();
		if (lookupGam(_gam.readUint32(m + 22 + 4 * i), classId, mark) && classId == kListClasses[i])
			readBlobList(_gam, mark + 6, *lists[i]);
	}
	save.disc = (int32)_gam.readUint32(m + 42);
	save.photos = (int32)_gam.readUint32(m + 46);
	save.hintList = _gam.readUint32(m + 50);
	save.hintTrigger = _gam.readUint32(m + 54);
	save.hintGlow = (int32)_gam.readUint32(m + 58);
	save.credits = (int32)_gam.readUint32(m + 62);
	for (int i = 0; i < 3; i++) {
		save.flashlight[i] = _gam.readByte(m + 106 + i);
		save.tintColor[i] = _gam.readByte(m + 110 + i);
	}
	save.tint = _gam.readByte(m + 109);
	readBlobList(_gam, m + 66, save.sounds);
	readBlobList(_gam, m + 74, save.inventory);
	readBlobList(_gam, m + 82, save.evidence);
	readBlobList(_gam, m + 90, save.kit);
	readBlobList(_gam, m + 98, save.timers);
	save.clip = _gam.readUint32(m + 113);
	save.clipFlag = _gam.readByte(m + 117);
	save.clipTime = (int32)_gam.readUint32(m + 118);
	save.dialogBox = _gam.readUint32(m + 122);
	save.layout = _gam.readUint32(m + 126);
	return true;
}

// -- Characters, conversations, icons, interface ------------------------------

bool VCData::getCharacter(uint32 id, VCCharacter &character) const {
	uint32 m;
	if (!getObject(id, kClassCharacter, m))
		return false;
	character.id = id;
	character.name = _hdb.readUint32(m + 6);
	return true;
}

bool VCData::getCharView(uint32 id, VCCharView &charView) const {
	uint32 m;
	if (!getObject(id, kClassCharView, m))
		return false;
	charView.id = id;
	charView.character = _hdb.readUint32(m + 6);
	charView.cursor = _hdb.readUint32(m + 10);
	readRect(_hdb, m + 14, charView.rect);
	charView.conversations = _hdb.readUint32(m + 22);
	charView.ideaMaps = _hdb.readUint32(m + 26);
	charView.assets = _hdb.readUint32(m + 30);
	charView.triggers = _hdb.readUint32(m + 34);
	return true;
}

bool VCData::getConversation(uint32 id, VCConversation &conversation) const {
	uint32 m;
	if (!getObject(id, kClassConversation, m))
		return false;
	conversation.id = id;
	conversation.name = _hdb.readUint32(m + 6);
	conversation.label = _hdb.readUint32(m + 10);
	conversation.answer = _hdb.readUint32(m + 14);
	conversation.triggers = _hdb.readUint32(m + 18);
	conversation.question = _hdb.readUint32(m + 22);
	conversation.atom = _hdb.readUint32(m + 26);
	conversation.recorded = _hdb.readByte(m + 30) != 0;
	return true;
}

bool VCData::getExplorable(uint32 id, VCExplorable &explorable) const {
	uint32 m;
	if (!getObject(id, kClassExplorable, m))
		return false;
	explorable.id = id;
	explorable.cursor = _hdb.readUint32(m + 6);
	readRect(_hdb, m + 10, explorable.rect);
	explorable.triggers = _hdb.readUint32(m + 18);
	return true;
}

bool VCData::getIdeaMap(uint32 id, VCIdeaMap &ideaMap) const {
	uint32 m;
	if (!getObject(id, kClassIdeaMap, m))
		return false;
	ideaMap.id = id;
	ideaMap.icon = _hdb.readUint32(m + 6);
	ideaMap.conversation = _hdb.readUint32(m + 10);
	return true;
}

bool VCData::getIcon(uint32 id, VCIcon &icon) const {
	uint32 m;
	if (!lookup(id, icon.classId, m) || icon.classId < kClassActionIcon || icon.classId > kClassInventory)
		return false;
	icon.id = id;
	icon.name = _hdb.readUint32(m + 6);
	icon.rollover = _hdb.readUint32(m + 10);
	icon.graphic = _hdb.readUint32(m + 14);
	icon.rolloverText = _hdb.readUint32(m + 22);
	icon.triggers = _hdb.readUint32(m + 26);
	icon.cursor = _hdb.readUint32(m + 30);
	return true;
}

bool VCData::getCursor(uint32 id, VCCursor &cursor) const {
	uint32 m;
	if (!getObject(id, kClassCursor, m))
		return false;
	cursor.id = id;
	cursor.hotX = _hdb.readSint16(m + 6);
	cursor.hotY = _hdb.readSint16(m + 8);
	cursor.name = _hdb.readUint32(m + 10);
	cursor.assets = _hdb.readUint32(m + 14);
	return true;
}

bool VCData::getLayout(uint32 id, VCLayout &layout) const {
	uint32 m;
	if (!getObject(id, kClassIFaceLayout, m))
		return false;
	layout.id = id;
	layout.name = _hdb.readUint32(m + 32);
	layout.items.clear();
	return _hdb.readObjectList(m + kFieldStart, layout.items);
}

bool VCData::getInterfaceItem(uint32 id, VCInterfaceItem &item) const {
	uint32 m;
	if (!getObject(id, kClassInterfaceItem, m))
		return false;
	item.id = id;
	item.cursor = _hdb.readUint32(m + 6);
	readRect(_hdb, m + 10, item.rect);
	item.assets = _hdb.readUint32(m + 18);
	item.triggers = _hdb.readUint32(m + 22);
	uint16 layer = _hdb.readUint16(m + 26);
	item.layer = layer & 0x7fff;
	item.layerFlag = (layer & 0x8000) != 0;
	return true;
}

} // End of namespace XFiles
