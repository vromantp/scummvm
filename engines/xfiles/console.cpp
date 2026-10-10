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

#include "xfiles/console.h"
#include "xfiles/script.h"
#include "xfiles/vcdata.h"
#include "xfiles/vcdump.h"
#include "xfiles/xfiles.h"

namespace XFiles {

Console::Console() : GUI::Debugger() {
	registerCmd("classes", WRAP_METHOD(Console, Cmd_classes));
	registerCmd("object", WRAP_METHOD(Console, Cmd_object));
	registerCmd("action", WRAP_METHOD(Console, Cmd_action));
	registerCmd("tree", WRAP_METHOD(Console, Cmd_tree));
	registerCmd("asset", WRAP_METHOD(Console, Cmd_asset));
	registerCmd("discs", WRAP_METHOD(Console, Cmd_discs));
	registerCmd("step", WRAP_METHOD(Console, Cmd_step));
	registerCmd("state", WRAP_METHOD(Console, Cmd_state));
}

Console::~Console() {
}

static uint32 parseId(const char *arg) {
	return (uint32)strtoul(arg, nullptr, 0);
}

bool Console::Cmd_classes(int argc, const char **argv) {
	if (argc > 2 || (argc == 2 && strcmp(argv[1], "gam"))) {
		debugPrintf("Usage: %s [gam]\n", argv[0]);
		return true;
	}
	const VCData &data = g_engine->getData();
	debugPrintf("%s", dumpClassTable(argc == 2 ? data.getGam() : data.getHdb()).c_str());
	return true;
}

bool Console::Cmd_object(int argc, const char **argv) {
	if (argc != 2) {
		debugPrintf("Usage: %s <id>\n", argv[0]);
		return true;
	}
	const VCData &data = g_engine->getData();
	uint32 id = parseId(argv[1]), classId, mark;
	if (data.lookup(id, classId, mark))
		debugPrintf("%s at 0x%x\n", getObjectLabel(data, id).c_str(), mark);
	if (data.lookupGam(id, classId, mark))
		debugPrintf(".gam: class %u at 0x%x\n", classId, mark);
	return true;
}

bool Console::Cmd_action(int argc, const char **argv) {
	if (argc < 2) {
		debugPrintf("Usage: %s <action, trigger or trigger list id>...\n", argv[0]);
		return true;
	}
	const VCData &data = g_engine->getData();
	for (int i = 1; i < argc; i++) {
		uint32 id = parseId(argv[i]);
		Common::Array<uint32> triggers;
		if (data.getClassOf(id) == kClassTriggerList)
			data.getMembers(id, triggers);
		else if (data.getClassOf(id) == kClassTrigger)
			triggers.push_back(id);
		else
			debugPrintf("%s\n", describeAction(data, id).c_str());

		for (uint j = 0; j < triggers.size(); j++) {
			VCTrigger t;
			data.getTrigger(triggers[j], t);
			debugPrintf("on event %u:\n", t.event);
			for (uint k = 0; k < t.actions.size(); k++)
				debugPrintf("    %s\n", describeAction(data, t.actions[k], t.event).c_str());
		}
	}
	return true;
}

bool Console::Cmd_tree(int argc, const char **argv) {
	Common::String filter;
	bool withTriggers = false;
	for (int i = 1; i < argc; i++) {
		if (!strcmp(argv[i], "-t"))
			withTriggers = true;
		else
			filter = argv[i];
	}
	if (filter.empty() && withTriggers) {
		debugPrintf("Usage: %s [node name] [-t]; -t needs a node\n", argv[0]);
		return true;
	}
	debugPrintf("%s", dumpSceneTree(g_engine->getData(), filter, withTriggers).c_str());
	return true;
}

bool Console::Cmd_asset(int argc, const char **argv) {
	if (argc != 2) {
		debugPrintf("Usage: %s <id>\n", argv[0]);
		return true;
	}
	const VCData &data = g_engine->getData();
	VCAsset asset;
	if (!data.getAsset(parseId(argv[1]), asset)) {
		debugPrintf("Not a VCAssetRef\n");
		return true;
	}
	debugPrintf("%s  flags 0x%02x  category %u  triggers %u\n", getObjectLabel(data, asset.id).c_str(),
		asset.flags, asset.category, asset.triggers);
	if (!asset.path.empty())
		debugPrintf("  %s\n", asset.path.c_str());
	for (uint i = 0; i < asset.files.size(); i++) {
		const VCMediaFile &f = asset.files[i];
		Common::Path path = g_engine->getDiscFiles().findFile(f.disc, f.file);
		debugPrintf("  disc %u  %s  %u-%u  -> %s\n", f.disc, f.file.c_str(), f.start, f.end,
			path.empty() ? "(missing)" : path.toString().c_str());
	}
	return true;
}

bool Console::Cmd_discs(int argc, const char **argv) {
	for (uint i = 1; i <= 7; i++)
		debugPrintf("cd%u: %s\n", i, g_engine->getDiscFiles().hasDiscDirectory(i) ? "present" : "missing");
	return true;
}

bool Console::Cmd_step(int argc, const char **argv) {
	Script *script = g_engine->getScript();
	if (argc < 2 || !script) {
		debugPrintf("Usage: %s <step>... (wait, click:ID, timer:N, nav:ID, goto:ID, ask:ID; in a game)\n", argv[0]);
		debugPrintf("The log goes to the Script debug channel.\n");
		return true;
	}
	for (int i = 1; i < argc; i++)
		script->step(argv[i]);
	return true;
}

bool Console::Cmd_state(int argc, const char **argv) {
	Script *script = g_engine->getScript();
	if (!script) {
		debugPrintf("No game running\n");
		return true;
	}
	const VCData &data = g_engine->getData();
	debugPrintf("%s / %s / %s / %s\n", getObjectLabel(data, script->getNode()).c_str(),
		getObjectLabel(data, script->getLocation()).c_str(), getObjectLabel(data, script->getViewPoint()).c_str(),
		getObjectLabel(data, script->getView()).c_str());
	const Common::Array<uint32> &queue = script->getVideoQueue();
	for (uint i = 0; i < queue.size(); i++)
		debugPrintf("video queue: %s\n", getObjectLabel(data, queue[i]).c_str());
	for (int i = 1; i < argc; i++) {
		uint32 id = parseId(argv[i]);
		VCVariable v;
		if (data.getVariable(id, v))
			debugPrintf("var #%u = %d\n", id, script->getVariable(id));
		debugPrintf("#%u is %s\n", id, script->isEnabled(id) ? "enabled" : "disabled");
	}
	return true;
}

} // End of namespace XFiles
