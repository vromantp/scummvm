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

#ifndef XFILES_VCDUMP_H
#define XFILES_VCDUMP_H

#include "common/str.h"
#include "common/ustr.h"

namespace XFiles {

class NeoFile;
class Script;
class VCData;

/*
 * Readable dumps of the databases for the debugger. The text is UTF-8 and
 * matches the research tools (neoaccess.py, vc_classes.py) line for line,
 * so the two can be diffed.
 */

/** Header and class table of a database, with the number of objects found by walking each index. */
Common::String dumpClassTable(const NeoFile &db);

/** Class name, ID and name of an .hdb object, e.g. `VCNode#1234 "Node 1: Setup"`. */
Common::String getObjectLabel(const VCData &data, uint32 id);

/**
 * One-line readable form of a VCAction. event is the type of the trigger
 * that runs it (to name Event:: operands), or -1. withCondition = false
 * leaves out the "if ...: " prefix.
 */
Common::String describeAction(const VCData &data, uint32 id, int event = -1, bool withCondition = true);

/** Text of a VCName as UTF-8, or "None" like the Python tools. */
Common::String nameOrNone(const VCData &data, uint32 id);

/** Python's repr() of a string, as UTF-8. */
Common::String pythonRepr(const Common::U32String &str);

/** The first limit actions, one per line. */
Common::String dumpActions(const VCData &data, uint limit);

/**
 * The scene graph Title > Node > Locaton > ViewPoint > View with each
 * view's exits; optionally only nodes whose name contains nodeFilter, and
 * with every object's triggers.
 */
Common::String dumpSceneTree(const VCData &data, const Common::String &nodeFilter, bool withTriggers);

/**
 * The script's state as JSON with the keys of a save's delta (the research
 * repo's tools/save_delta.py): every variable's value, every VCEnabled's
 * flag (by VCEnabled ID) and game_state.position [node, location,
 * viewpoint, view]. tools/save_compare.py compares it with a save.
 */
Common::String dumpState(const VCData &data, const Script &script);

} // End of namespace XFiles

#endif // XFILES_VCDUMP_H
