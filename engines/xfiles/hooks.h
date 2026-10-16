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

#ifndef XFILES_HOOKS_H
#define XFILES_HOOKS_H

#include "common/array.h"
#include "common/hash-str.h"
#include "common/hashmap.h"
#include "common/str.h"
#include "common/textconsole.h"

#include "xfiles/vcdata.h"

namespace XFiles {

/**
 * One call of a C++ function hook (an action of type Function): the action, the values of its arguments, and the
 * event whose trigger runs it.
 */
struct HookCall {
	HookCall(const VCAction &a, const Common::Array<int32> &values, int e, const Common::Array<int32> &f)
		: action(a), args(values), event(e), fields(f) {}

	const VCAction &action;
	const Common::Array<int32> &args;   ///< The arguments' values
	int event;                          ///< The type of the trigger that runs the action, -1 for none
	const Common::Array<int32> &fields; ///< That event's fields

	/** At least n arguments. */
	bool has(uint n) const { return args.size() >= n; }
	/** Argument i is a variable (one the hook writes or keeps). */
	bool isVariable(uint i) const { return i < action.args.size() && action.args[i].kind == kOperandVariable; }
	/** The VCVariable of argument i, 0 if it is not a variable. */
	uint32 variableArg(uint i) const { return isVariable(i) ? action.args[i].value : 0; }
};

/** What became of a hook call, for the hook coverage (tools/hook_coverage.py). */
enum HookStatus {
	kHookUnknown, ///< Not implemented, or these arguments are not taken
	kHookLogged,  ///< Known, and does nothing on purpose
	kHookHandled  ///< Does what the exe does
};

/**
 * The hooks one owner (Script, XFilesEngine) implements, by name: one lookup per call. A handler returns true when
 * it did what the exe does, false when it does not take these arguments or only records them. A name added without
 * a handler is known and does nothing on purpose.
 */
template<class T>
class HookTable {
public:
	typedef bool (*Handler)(T &owner, const HookCall &call);

	void add(const char *name, Handler handler) {
		if (_handlers.contains(name))
			warning("Hook %s is registered twice", name);
		_handlers[name] = handler;
	}
	void addLogged(const char *name) { add(name, nullptr); }
	bool contains(const Common::String &name) const { return _handlers.contains(name); }

	HookStatus run(T &owner, const HookCall &call) const {
		typename HandlerMap::const_iterator it = _handlers.find(call.action.function);
		if (it == _handlers.end())
			return kHookUnknown;
		if (!it->_value)
			return kHookLogged;
		return it->_value(owner, call) ? kHookHandled : kHookUnknown;
	}

private:
	typedef Common::HashMap<Common::String, Handler> HandlerMap;
	HandlerMap _handlers;
};

} // End of namespace XFiles

#endif // XFILES_HOOKS_H
