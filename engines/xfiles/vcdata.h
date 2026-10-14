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

#ifndef XFILES_VCDATA_H
#define XFILES_VCDATA_H

#include "common/array.h"
#include "common/hashmap.h"
#include "common/str.h"
#include "common/ustr.h"

#include "xfiles/neoaccess.h"

namespace XFiles {

/**
 * VirtualCinema class IDs. One ID space is shared by XFiles.hdb (structure),
 * XFiles.gam (variables, enable flags) and saves (86-92).
 */
enum VCClassId {
	kClassTitle = 39,
	kClassNode = 40,
	kClassLocation = 41,
	kClassViewPoint = 42,
	kClassView = 43,
	kClassCharacter = 44,
	kClassCharView = 45,
	kClassCharViewList = 46,
	kClassName = 47,
	kClassConversation = 49,
	kClassConversationList = 50,
	kClassNav = 51,
	kClassNavList = 52,
	kClassAssetRef = 53,
	kClassAssetRefList = 54,
	kClassExplorable = 55,
	kClassExplorableList = 56,
	kClassHotSpot = 57,
	kClassHotSpotList = 58,
	kClassStdHotSpotList = 59,
	kClassAssetCategory = 60,
	kClassCursor = 61,
	kClassDefaultCursors = 62,
	kClassDefaultHotSpots = 64,
	kClassAction = 65,
	kClassActionList = 66,
	kClassBlob = 67,
	kClassPlayer = 68,
	kClassEnabled = 70,
	kClassActionIcon = 71,
	kClassEmotionIcon = 72,
	kClassEvidenceIcon = 73,
	kClassIdeaIcon = 74,
	kClassInventory = 75,
	kClassIdeaMap = 76,
	kClassIdeaMapList = 77,
	kClassIFaceLayout = 78,
	kClassInterfaceItem = 79,
	kClassString = 80,
	kClassTrigger = 81,
	kClassTriggerList = 82,
	kClassVariable = 83,
	kClassStdAction = 84,
	kClassStdActionList = 85,
	kClassConversationHistory = 86,
	kClassGameState = 87,
	kClassPDANotes = 88,
	kClassPhoto = 89,
	kClassEmail = 90,
	kClassEmailRead = 91,
	kClassEmailPending = 92
};

/** Class name as registered by XFiles.exe, or nullptr. */
const char *getClassName(uint32 classId);

enum VCActionType {
	kActionStatement = 0,
	kActionAsset = 1,
	kActionTimer = 2,
	kActionEnable = 3,
	kActionSetView = 4,
	kActionInterface = 5,
	kActionFunction = 6,
	kActionSound3D = 7
};

enum VCOperandKind {
	kOperandVariable = 0, ///< VCVariable in .gam
	kOperandLocal = 1,
	kOperandInt = 2,
	kOperandChar = 3,
	kOperandEvent = 4,    ///< Field of the triggering event
	kOperandObject = 5    ///< .hdb object
};

enum VCEventType {
	kEventMouseClick = 0,
	kEventMouseDoubleClick = 1,
	kEventMouseMove = 2,
	kEventKeyPress = 3,
	kEventRollInto = 4,
	kEventRollOutOf = 5,
	kEventDrop = 6,
	kEventTimerExpiration = 7,
	kEventObjectActivation = 8,
	kEventObjectDeactivation = 9,
	kEventWindowActivate = 10,
	kEventWindowDeactivate = 11,
	kEventTypeCount = 12
};

enum VCAssetFlags {
	kAssetFile = 0x01,     ///< File-based media, the blob lists a file per disc
	kAssetRange = 0x02,    ///< The blob has an in/out range
	kAssetText = 0x04,
	kAssetLoop = 0x08,
	kAssetStill = 0x40     ///< Background: a nav movie frame or a .pff picture
};

struct VCRect {
	int16 left, top, right, bottom;
};

struct VCOperand {
	byte kind;
	uint32 value;
};

/** A two-operand expression: a condition or a statement. */
struct VCExpression {
	VCOperand lhs, rhs;
	byte op;
};

struct VCAction {
	uint32 id;
	byte type;           ///< VCActionType, or an unknown value
	bool conditional;
	VCExpression condition;

	VCExpression statement;   ///< kActionStatement
	uint32 assetList;         ///< kActionAsset, kActionSound3D
	byte mode;                ///< Asset mode (kActionAsset, kActionSound3D), 0 = display, 1 = unshow (kActionInterface)
	int16 x, y;               ///< kActionSound3D position
	uint32 ms;                ///< kActionTimer
	byte timer;
	byte command;
	bool periodic;
	uint32 target;            ///< kActionEnable: a VCEnabled in .gam
	byte value;
	uint32 path[4];           ///< kActionSetView: VCView, VCNode, VCLocaton, VCViewPoint
	uint32 layout;            ///< kActionInterface
	Common::String function;  ///< kActionFunction
	Common::Array<VCOperand> args;
	Common::Array<byte> raw;  ///< Body of an unknown action type

	VCAction();
};

struct VCTrigger {
	uint32 id;
	byte event;               ///< VCEventType
	Common::Array<uint32> actions;
};

struct VCTitle {
	uint32 id;
	uint32 name;
	Common::Array<uint32> nodes;
	uint32 triggers;
};

struct VCNode {
	uint32 id;
	uint32 name;
	Common::Array<uint32> locations;
	uint32 triggers;
};

struct VCLocation {
	uint32 id;
	uint32 name;
	Common::Array<uint32> viewPoints;
	uint32 assets;
	uint32 triggers;
	byte unknown;
};

struct VCViewPoint {
	uint32 id;
	uint32 name;
	Common::Array<uint32> views;
	int16 x, y;
	uint32 triggers;
};

struct VCView {
	uint32 id;
	uint32 facing;            ///< Below 9 a direction, else a VCName
	uint32 assets;
	uint32 characters;
	uint32 navs;
	uint32 explorables;
	uint32 triggers;
};

struct VCNav {
	uint32 id;
	uint32 target;            ///< VCView
	uint32 hotSpot;
	uint32 triggers;
};

struct VCHotSpot {
	uint32 id;
	uint32 cursor;            ///< VCCursor shown over it
	VCRect rect;
	uint32 name;              ///< VCName or 0 (the default hotspots: Left, Right, Forward)
};

struct VCVariable {
	uint32 id;
	Common::U32String name;
	uint32 value;
	uint32 owner;
	byte ownerClass;
	byte flags;
};

struct VCCharacter {
	uint32 id;
	uint32 name;
};

struct VCCharView {
	uint32 id;
	uint32 character;
	uint32 cursor;
	VCRect rect;
	uint32 conversations;
	uint32 ideaMaps;
	uint32 assets;
	uint32 triggers;
};

struct VCConversation {
	uint32 id;
	uint32 name;
	uint32 label;
	uint32 answer;
	uint32 triggers;
	uint32 question;
	uint32 atom;
	byte flag;
};

struct VCExplorable {
	uint32 id;
	uint32 cursor;
	VCRect rect;
	uint32 triggers;
};

struct VCIdeaMap {
	uint32 id;
	uint32 icon;
	uint32 conversation;
};

/** VCActionIcon, VCEmotionIcon, VCEvidenceIcon, VCIdeaIcon or VCInventory. */
struct VCIcon {
	uint32 id;
	uint32 classId;
	uint32 name;
	uint32 rollover;
	uint32 graphic;
	uint32 rolloverText;
	uint32 triggers;
	uint32 cursor;
};

struct VCCursor {
	uint32 id;
	int16 hotX, hotY;
	uint32 name;
	uint32 assets;
};

struct VCLayout {
	uint32 id;
	uint32 name;
	Common::Array<uint32> items;
};

struct VCInterfaceItem {
	uint32 id;
	uint32 cursor;
	VCRect rect;
	uint32 assets;
	uint32 triggers;
	uint16 layer;
	bool layerFlag;
};

/** Where one media asset lives on one disc (0 = hard disk, 1-7 = CD, 8 = larger installs). */
struct VCMediaFile {
	uint32 disc;
	Common::String file;      ///< Mac-style path, e.g. "XV:19812.xmv"
	uint32 start, end;        ///< Milliseconds into the file, or a picture index
};

struct VCAsset {
	uint32 id;                ///< Also the media file number
	uint32 name;
	byte flags;               ///< VCAssetFlags
	byte category;
	uint32 triggers;
	bool readable;            ///< False if the media blob does not decode
	Common::String path;      ///< Authoring path
	bool hasRange;
	uint32 rangeIn, rangeOut;
	Common::Array<VCMediaFile> files;
};

/** What a .X save holds besides the variables and enable flags (VCGameState and the lists it names). */
struct VCSave {
	uint32 position[4];              ///< VCNode, VCLocaton, VCViewPoint, VCView
	int16 positionMode;              ///< 0 the start view, 1 the current view, 2 from the pending Set View
	Common::Array<uint32> history;   ///< VCConversation IDs asked
	Common::Array<uint32> notes;     ///< VCPDANotes
	Common::Array<uint32> emails, emailsRead, emailsPending;
	int32 disc;
	uint32 hintList, hintTrigger;
	int32 hintGlow;
	Common::Array<uint32> sounds;    ///< Triples (VCAssetRef, by action, time)
	Common::Array<uint32> inventory; ///< VCInventory IDs, in order
	Common::Array<uint32> evidence;  ///< Pairs (VCEvidenceIcon, shown), outside the kit
	Common::Array<uint32> kit;       ///< VCEvidenceIcon IDs in the kit
	Common::Array<uint32> timers;    ///< Triples (timer, time, stopped)
	uint32 clip;                     ///< VCAssetRef playing in the current view, or 0
	int32 clipTime;
	uint32 dialogBox;                ///< VCCharView whose box was open, or 0
	uint32 layout;                   ///< Full-screen interface layout shown, or 0
};

/**
 * Both databases of one installation (XFiles.hdb and XFiles.gam, or a save
 * instead of the .gam), with the class decoders.
 *
 * Object IDs overlap between the files, so each reference is resolved in a
 * specific one: structure lives in the .hdb, variables and enable flags in
 * the .gam. The getters return false if the object is missing or of
 * another class. Name fields are VCName IDs; resolve them with getName().
 */
class VCData {
public:
	bool load(Common::SeekableReadStream &hdb, Common::SeekableReadStream &gam);

	const NeoFile &getHdb() const { return _hdb; }
	const NeoFile &getGam() const { return _gam; }

	/** Class and file offset of an .hdb object. */
	bool lookup(uint32 id, uint32 &classId, uint32 &mark) const;
	/** Class and file offset of a .gam object. */
	bool lookupGam(uint32 id, uint32 &classId, uint32 &mark) const;
	/** Class of an .hdb object, 0 if there is none. */
	uint32 getClassOf(uint32 id) const;

	/** Text of a VCName, stripped of surrounding white space. */
	bool getName(uint32 id, Common::U32String &name) const;
	/** Name of a VCVariable (.gam). */
	bool getVariableName(uint32 id, Common::U32String &name) const;
	/** Member IDs of a *List object; an empty list for ID 0. */
	bool getMembers(uint32 listId, Common::Array<uint32> &ids) const;

	bool getTitle(VCTitle &title) const;
	bool getNode(uint32 id, VCNode &node) const;
	bool getLocation(uint32 id, VCLocation &location) const;
	bool getViewPoint(uint32 id, VCViewPoint &viewPoint) const;
	bool getView(uint32 id, VCView &view) const;
	bool getNav(uint32 id, VCNav &nav) const;
	bool getHotSpot(uint32 id, VCHotSpot &hotSpot) const;
	/** The standard navigation areas (VCDefaultHotSpots): left, right and forward hotspots. */
	bool getDefaultHotSpots(uint32 hotSpots[3]) const;
	bool getTrigger(uint32 id, VCTrigger &trigger) const;
	bool getAction(uint32 id, VCAction &action) const;
	bool getAsset(uint32 id, VCAsset &asset) const;
	bool getText(uint32 id, Common::U32String &text) const;
	/**
	 * A media-less asset that names a Windows resource: its blob holds the
	 * resource type and ID as numbers ("6", "1" = string 1 of XFilesT.dll).
	 */
	bool getResourceRef(uint32 id, uint32 &type, uint32 &resource) const;
	bool getVariable(uint32 id, VCVariable &variable) const;
	/** The save's VCGameState (the .gam has none): false if the database is not a save. */
	bool getSave(VCSave &save) const;
	/** Text of a VCString (.gam), the value of a string variable. */
	bool getString(uint32 id, Common::String &text) const;
	/** The .hdb object a VCEnabled (.gam) switches, and its initial state. */
	bool getEnabled(uint32 id, uint32 &target, bool &enabled) const;
	bool getCharacter(uint32 id, VCCharacter &character) const;
	bool getCharView(uint32 id, VCCharView &charView) const;
	bool getConversation(uint32 id, VCConversation &conversation) const;
	bool getExplorable(uint32 id, VCExplorable &explorable) const;
	bool getIdeaMap(uint32 id, VCIdeaMap &ideaMap) const;
	bool getIcon(uint32 id, VCIcon &icon) const;
	bool getCursor(uint32 id, VCCursor &cursor) const;
	bool getLayout(uint32 id, VCLayout &layout) const;
	bool getInterfaceItem(uint32 id, VCInterfaceItem &item) const;

	/** Decode the expression blob of a VCAction; code = its type byte (bit 7 = conditional). */
	static bool decodeAction(const byte *blob, uint32 size, byte code, VCAction &action);
	/** Decode the blob of a file-based VCAssetRef (flags bit 0). */
	static bool decodeMediaBlob(const byte *blob, uint32 size, byte flags, VCAsset &asset);

private:
	struct ObjectRef {
		uint32 classId;
		uint32 mark;
	};
	typedef Common::HashMap<uint32, ObjectRef> ObjectIndex;

	static bool buildIndex(const NeoFile &db, ObjectIndex &index);
	bool getObject(uint32 id, uint32 classId, uint32 &mark) const;

	NeoFile _hdb;
	NeoFile _gam;
	ObjectIndex _hdbIndex;
	ObjectIndex _gamIndex;
};

/** Python's str.strip(): remove surrounding Unicode white space. */
Common::U32String stripWhiteSpace(const Common::U32String &str);

} // End of namespace XFiles

#endif // XFILES_VCDATA_H
