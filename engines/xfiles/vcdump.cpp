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

#include "xfiles/vcdump.h"
#include "xfiles/script.h"
#include "xfiles/vcdata.h"

namespace XFiles {

// The engine's own display tables (XFiles.exe VA 0x6b4270, 0x6b42e8, 0x6b4740, 0x6b4590)
static const char *const kConditionOps[] = { "==", "!=", ">", "<", ">=", "<=", "and", "or" };
static const char *const kStatementOps[] = { "=", "++", "--", "+=", "-=", "*=", "/=", "%=" };
static const char *const kAssetModes[] = { "preload", "play_once", "play_loop", "stop", "unload" };
static const char *const kTimerCommands[] = { "start", "stop" };
static const char *const kEventNames[] = {
	"Mouse Click", "Mouse Double-Click", "Mouse Move", "Key Press", "Roll Into", "Roll Out Of", "Drop",
	"Timer Expiration", "Object Activation", "Object Deactivation", "VC Window Activate", "VC Window Deactivate"
};
static const char *const kDirections[] = {
	"", "North", "NorthEast", "East", "SouthEast", "South", "SouthWest", "West", "NorthWest"
};

// Names of Event::n operands per event type (XFiles.exe 0x5d0a90)
static const char *const kPointerFields[] = { "XCoord", "YCoord", "IsDragging", "DragObjClass", "DragObjID", nullptr };
static const char *const kClickFields[] = { "XCoord", "YCoord", nullptr };
static const char *const kKeyFields[] = { "Key", "IsControlDown", "IsAltDown", "IsOptionDown", nullptr };
static const char *const kDropFields[] = { "XCoord", "YCoord", "DropObjClass", "DropObjID", nullptr };
static const char *const kTimerFields[] = { "TimerID", nullptr };
static const char *const kObjectFields[] = { "ObjClass", "ObjID", nullptr };
static const char *const *const kEventFields[] = {
	kClickFields, kClickFields, kPointerFields, kKeyFields, kPointerFields, kPointerFields, kDropFields,
	kTimerFields, kObjectFields, kObjectFields
};

/** Name from a table, or the number if it is out of range. */
template<size_t N>
static Common::String tableName(const char *const (&table)[N], uint value) {
	return value < N ? Common::String(table[value]) : Common::String::format("%u", value);
}

static Common::String utf8(const Common::U32String &str) {
	return str.encode(Common::kUtf8);
}

Common::String nameOrNone(const VCData &data, uint32 id) {
	Common::U32String name;
	return data.getName(id, name) ? utf8(name) : Common::String("None");
}

static Common::String cursorName(const VCData &data, uint32 id) {
	VCCursor cursor;
	return data.getCursor(id, cursor) ? nameOrNone(data, cursor.name) : Common::String("None");
}

Common::String pythonRepr(const Common::U32String &str) {
	bool hasSingle = false, hasDouble = false;
	for (uint i = 0; i < str.size(); i++) {
		hasSingle |= str[i] == '\'';
		hasDouble |= str[i] == '"';
	}
	Common::u32char_type_t quote = (hasSingle && !hasDouble) ? '"' : '\'';
	Common::U32String out;
	out += quote;
	for (uint i = 0; i < str.size(); i++) {
		Common::u32char_type_t c = str[i];
		if (c == quote || c == '\\') {
			out += '\\';
			out += c;
		} else if (c == '\t') {
			out += Common::U32String("\\t");
		} else if (c == '\n') {
			out += Common::U32String("\\n");
		} else if (c == '\r') {
			out += Common::U32String("\\r");
		} else if (c < 0x20 || (c >= 0x7f && c <= 0xa0) || c == 0xad) {
			out += Common::U32String(Common::String::format("\\x%02x", c));
		} else {
			out += c;
		}
	}
	out += quote;
	return utf8(out);
}

Common::String dumpClassTable(const NeoFile &db) {
	Common::String out = Common::String::format("version %u  classes %u  next id %u  class root 0x%x  freelist 0x%x\n",
		db._version, db._classCount, db._nextId, db._classRoot, db._freeListRoot);
	const Common::Array<NeoClass> &classes = db.getClasses();
	for (uint i = 0; i < classes.size(); i++) {
		const NeoClass &c = classes[i];
		Common::Array<NeoEntry> entries;
		db.walk(c.root, entries);
		out += Common::String::format("  class %3u  objects %6u  root 0x%06x  key %s", c.id, c.count, c.root,
			pythonRepr(Common::U32String(c.key, Common::kISO8859_1)).c_str());
		if (entries.size() != c.count)
			out += Common::String::format("  (walked %u)", entries.size());
		out += '\n';
	}
	return out;
}

Common::String getObjectLabel(const VCData &data, uint32 id) {
	uint32 classId, mark;
	if (!data.lookup(id, classId, mark))
		return Common::String::format("?#%u", id);
	const char *className = getClassName(classId);
	Common::String text = className ? Common::String::format("%s#%u", className, id) : Common::String::format("%u#%u", classId, id);

	Common::String name;
	if (classId == kClassView) {
		VCView view;
		data.getView(id, view);
		Common::U32String text32;
		if (view.facing < ARRAYSIZE(kDirections))
			name = kDirections[view.facing];
		else if (data.getName(view.facing, text32))
			name = utf8(text32);
	} else {
		// The scene classes start with their child list
		uint32 offset = 6;
		if (classId == kClassTitle)
			offset = 60;
		else if (classId == kClassNode || classId == kClassLocation)
			offset = 32;
		else if (classId == kClassViewPoint)
			offset = 36;
		Common::U32String text32;
		if (data.getName(data.getHdb().readUint32(mark + offset), text32))
			name = utf8(text32);
	}
	return name.empty() ? text : text + " \"" + name + "\"";
}

static Common::String describeOperand(const VCData &data, const VCOperand &operand, int event) {
	Common::U32String name;
	switch (operand.kind) {
	case kOperandVariable:
		if (data.getVariableName(operand.value, name) && !name.empty())
			return utf8(name);
		return Common::String::format("var#%u", operand.value);
	case kOperandLocal:
		return Common::String::format("local#%u", operand.value);
	case kOperandChar:
		if (operand.value >= 32 && operand.value < 127)
			return pythonRepr(Common::U32String(Common::String((char)operand.value)));
		return Common::String::format("char#%u", operand.value);
	case kOperandEvent:
		if (event >= 0 && event < (int)ARRAYSIZE(kEventFields)) {
			const char *const *fields = kEventFields[event];
			for (uint i = 0; fields[i]; i++) {
				if (i == operand.value)
					return Common::String("Event::") + fields[i];
			}
		}
		return Common::String::format("Event::%u", operand.value);
	case kOperandObject:
		return getObjectLabel(data, operand.value);
	default:
		return Common::String::format("%u", operand.value);
	}
}

static Common::String describeExpression(const VCData &data, const VCExpression &e, const Common::String &op, int event) {
	return describeOperand(data, e.lhs, event) + " " + op + " " + describeOperand(data, e.rhs, event);
}

Common::String describeAction(const VCData &data, uint32 id, int event, bool withCondition) {
	VCAction a;
	if (!data.getAction(id, a))
		return Common::String::format("<bad action #%u>", id);

	Common::String text;
	if (a.conditional && withCondition)
		text = "if " + describeExpression(data, a.condition, tableName(kConditionOps, a.condition.op), event) + ": ";

	switch (a.type) {
	case kActionStatement:
		text += describeExpression(data, a.statement, tableName(kStatementOps, a.statement.op), event);
		break;
	case kActionEnable: {
		uint32 target;
		bool enabled;
		text += a.value ? "enable " : "disable ";
		if (data.getEnabled(a.target, target, enabled) && target)
			text += getObjectLabel(data, target);
		else
			text += Common::String::format("<missing VCEnabled #%u>", a.target);
		break;
	}
	case kActionSetView:
		text += "set_view ";
		for (int i = 0; i < 4; i++)
			text += (i ? " / " : "") + getObjectLabel(data, a.path[i]);
		break;
	case kActionFunction:
		text += Common::U32String(a.function, Common::kISO8859_1).encode(Common::kUtf8) + "(";
		for (uint i = 0; i < a.args.size(); i++)
			text += (i ? ", " : "") + describeOperand(data, a.args[i], event);
		text += ")";
		break;
	case kActionAsset:
		text += tableName(kAssetModes, a.mode) + " " + getObjectLabel(data, a.assetList);
		break;
	case kActionSound3D:
		text += tableName(kAssetModes, a.mode) + " " + getObjectLabel(data, a.assetList) +
			Common::String::format(" at (%d, %d)", a.x, a.y);
		break;
	case kActionInterface:
		text += (a.mode ? "unshow " : "display ") + getObjectLabel(data, a.layout);
		break;
	case kActionTimer:
		text += Common::String::format("timer %u ", a.timer) + tableName(kTimerCommands, a.command);
		if (a.command == 0)
			text += Common::String::format(" %u ms", a.ms) + (a.periodic ? " periodic" : "");
		break;
	default:
		text += Common::String::format("%u {'raw': '", a.type);
		for (uint i = 0; i < a.raw.size(); i++)
			text += Common::String::format("%02x", a.raw[i]);
		text += "'}";
		break;
	}
	return text;
}

Common::String dumpActions(const VCData &data, uint limit) {
	Common::Array<NeoEntry> actions;
	data.getHdb().getObjects(kClassAction, actions);
	Common::String out;
	for (uint i = 0; i < actions.size() && i < limit; i++) {
		Common::String hex = Common::String::format("0x%x", actions[i].key);
		while (hex.size() < 7)
			hex = " " + hex;
		out += hex + "  " + describeAction(data, actions[i].key) + "\n";
	}
	return out;
}

static void dumpTriggers(const VCData &data, uint32 triggerList, const char *indent, Common::String &out) {
	Common::Array<uint32> triggers;
	data.getMembers(triggerList, triggers);
	for (uint i = 0; i < triggers.size(); i++) {
		VCTrigger t;
		if (!data.getTrigger(triggers[i], t))
			continue;
		out += Common::String(indent) + "on " + tableName(kEventNames, t.event) + ":\n";
		for (uint j = 0; j < t.actions.size(); j++)
			out += Common::String(indent) + "    " + describeAction(data, t.actions[j], t.event) + "\n";
	}
}

Common::String dumpSceneTree(const VCData &data, const Common::String &nodeFilter, bool withTriggers) {
	Common::String out;
	VCTitle title;
	if (!data.getTitle(title))
		return out;
	out += Common::String::format("Title #%u \"", title.id) + nameOrNone(data, title.name) + "\"\n";
	if (withTriggers)
		dumpTriggers(data, title.triggers, "  ", out);

	Common::String filter = nodeFilter;
	filter.toLowercase();
	for (uint n = 0; n < title.nodes.size(); n++) {
		VCNode node;
		data.getNode(title.nodes[n], node);
		Common::String nodeName = nameOrNone(data, node.name);
		Common::String lower = nodeName;
		lower.toLowercase();
		if (!filter.empty() && !lower.contains(filter))
			continue;
		out += Common::String::format("  Node #%u \"", node.id) + nodeName + "\"\n";
		if (withTriggers)
			dumpTriggers(data, node.triggers, "    ", out);

		for (uint l = 0; l < node.locations.size(); l++) {
			VCLocation loc;
			data.getLocation(node.locations[l], loc);
			out += Common::String::format("    Location #%u \"", loc.id) + nameOrNone(data, loc.name) + "\"\n";
			if (withTriggers)
				dumpTriggers(data, loc.triggers, "      ", out);

			for (uint p = 0; p < loc.viewPoints.size(); p++) {
				VCViewPoint vp;
				data.getViewPoint(loc.viewPoints[p], vp);
				out += Common::String::format("      ViewPoint #%u \"", vp.id) + nameOrNone(data, vp.name) +
					Common::String::format("\" at (%d, %d)\n", vp.x, vp.y);
				if (withTriggers)
					dumpTriggers(data, vp.triggers, "        ", out);

				for (uint v = 0; v < vp.views.size(); v++) {
					VCView view;
					data.getView(vp.views[v], view);
					Common::String viewName = view.facing < ARRAYSIZE(kDirections) ?
						Common::String(kDirections[view.facing]) : nameOrNone(data, view.facing);
					out += Common::String::format("        View #%u ", view.id) + viewName;

					Common::Array<uint32> navs;
					data.getMembers(view.navs, navs);
					for (uint i = 0; i < navs.size(); i++) {
						VCNav nav;
						VCHotSpot hotSpot;
						data.getNav(navs[i], nav);
						data.getHotSpot(nav.hotSpot, hotSpot);
						out += (i ? ", " : "  [") + cursorName(data, hotSpot.cursor) + Common::String::format(" -> #%u", nav.target);
					}
					out += navs.empty() ? "\n" : "]\n";
					if (withTriggers)
						dumpTriggers(data, view.triggers, "          ", out);
				}
			}
		}
	}
	return out;
}

/** JSON {"id": value, ...}, ten pairs to a line. */
static Common::String jsonMap(const Common::Array<uint32> &ids, const Common::Array<int32> &values) {
	Common::String out = "{";
	for (uint i = 0; i < ids.size(); i++)
		out += Common::String::format("%s\"%u\": %d", i ? (i % 10 ? ", " : ",\n  ") : "\n  ", ids[i], values[i]);
	return out + (ids.empty() ? "}" : "\n }");
}

/** JSON `,\n "name": [id, ...]`. */
static Common::String jsonList(const char *name, const Common::Array<uint32> &ids) {
	Common::String out = Common::String::format(",\n \"%s\": [", name);
	for (uint i = 0; i < ids.size(); i++)
		out += Common::String::format("%s%u", i ? ", " : "", ids[i]);
	return out + "]";
}

/** JSON `  "name": [v, v, ...]` of a game_state list. */
static Common::String jsonStateList(const char *name, const Common::Array<uint32> &list) {
	Common::String out = Common::String::format("  \"%s\": [", name);
	for (uint i = 0; i < list.size(); i++)
		out += Common::String::format("%s%u", i ? ", " : "", list[i]);
	return out + "]";
}

Common::String dumpState(const VCData &data, const Script &script, const VCSave *snapshot) {
	Common::Array<NeoEntry> entries;
	Common::Array<uint32> ids;
	Common::Array<int32> values;
	data.getGam().getObjects(kClassVariable, entries);
	for (uint i = 0; i < entries.size(); i++) {
		ids.push_back(entries[i].key);
		values.push_back(script.getVariable(entries[i].key));
	}
	Common::String out = "{\n \"format\": \"xfiles-engine-state 1\",\n \"variables\": " + jsonMap(ids, values);

	// VCString values the game changed, as hex bytes: {"id": "hex", ...}
	Script::SavedValues saved;
	script.getSaved(saved);
	out += ",\n \"strings\": {";
	for (uint i = 0; i < saved.stringIds.size(); i++) {
		out += Common::String::format("%s\"%u\": \"", i ? ", " : "", saved.stringIds[i]);
		for (uint j = 0; j < saved.strings[i].size(); j++)
			out += Common::String::format("%02x", (byte)saved.strings[i][j]);
		out += "\"";
	}
	out += "}";

	// By VCEnabled ID, as in a save's delta (the Script keeps them by target)
	entries.clear();
	ids.clear();
	values.clear();
	data.getGam().getObjects(kClassEnabled, entries);
	for (uint i = 0; i < entries.size(); i++) {
		uint32 target;
		bool enabled;
		if (data.getEnabled(entries[i].key, target, enabled)) {
			ids.push_back(entries[i].key);
			values.push_back(script.isEnabled(target));
		}
	}
	out += ",\n \"enabled\": " + jsonMap(ids, values);
	out += jsonList("emails", script.getEmails());
	out += jsonList("emails_read", script.getEmailsRead());
	out += jsonList("emails_pending", script.getEmailsPending());
	out += jsonList("pda_notes", script.getPDANotes());
	out += jsonList("inventory", script.getInventory());
	VCSave own;
	if (!snapshot) {
		script.snapshot(own);
		snapshot = &own;
	}
	out += jsonList("conversation_history", snapshot->history);
	// Running timers: number -> [ms, periodic]
	out += ",\n \"timers\": {";
	const Common::HashMap<uint, Script::Timer> &timers = script.getTimers();
	Common::Array<uint> numbers;
	for (Common::HashMap<uint, Script::Timer>::const_iterator it = timers.begin(); it != timers.end(); ++it)
		numbers.push_back(it->_key);
	Common::sort(numbers.begin(), numbers.end());
	for (uint i = 0; i < numbers.size(); i++)
		out += Common::String::format("%s\"%u\": [%u, %d]", i ? ", " : "", numbers[i], timers.getVal(numbers[i]).ms, timers.getVal(numbers[i]).periodic ? 1 : 0);
	out += "}";
	// The save's game state with the keys of save_delta.py (docs/formats/saves.md)
	const VCSave &s = *snapshot;
	out += ",\n \"game_state\": {\n";
	out += Common::String::format("  \"position\": [%u, %u, %u, %u],\n", script.getNode(), script.getLocation(), script.getViewPoint(),
		script.getView());
	out += Common::String::format("  \"disc\": %d, \"photos_on_camera\": %d, \"ai_actions\": %u, \"ai_trigger\": %u, \"ai_glow\": %d,\n",
		s.disc, s.photos, s.hintList, s.hintTrigger, s.hintGlow);
	out += Common::String::format("  \"credits\": %d, \"flashlight\": [%u, %u, %u], \"lbox_tint\": %u, \"lbox_color\": [%u, %u, %u],\n", s.credits,
		s.flashlight[0], s.flashlight[1], s.flashlight[2], s.tint, s.tintColor[0], s.tintColor[1], s.tintColor[2]);
	out += Common::String::format("  \"view_clip\": %u, \"view_clip_flag\": %u, \"view_clip_time\": %d, \"dbox_charview\": %u, \"layout\": %u, \"position_mode\": %d,\n",
		s.clip, s.clipFlag, s.clipTime, s.dialogBox, s.layout, s.positionMode);
	out += jsonStateList("sounds", s.sounds) + ",\n";
	out += jsonStateList("inventory", s.inventory) + ",\n";
	out += jsonStateList("evidence", s.evidence) + ",\n";
	out += jsonStateList("evidence_kit", s.kit) + ",\n";
	out += jsonStateList("timers", s.timers) + "\n }\n}\n";
	return out;
}

} // End of namespace XFiles
