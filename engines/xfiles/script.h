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

#ifndef XFILES_SCRIPT_H
#define XFILES_SCRIPT_H

#include "common/array.h"
#include "common/hashmap.h"
#include "common/str.h"

#include "xfiles/vcdata.h"

namespace XFiles {

/**
 * The VirtualCinema interpreter: runtime state (variables, enable flags,
 * timers, the current view) and the triggers and actions of the database.
 *
 * Variables and enable flags start from the .gam. Entering a view fires
 * Object Activation down the scene graph (Node > Locaton > ViewPoint >
 * View) for every level that changes, and Object Deactivation on the levels
 * left. Play-once videos play one after another: a video asset's Object
 * Activation fires when it starts, its Object Deactivation when it ends.
 *
 * Everything that happens is written to log(), in the format of the
 * research tool vc_sim.py, so the two can be diffed. C++ functions other
 * than SetStartView and DoActionList are only logged.
 */
class Script {
public:
	Script(const VCData &data);
	virtual ~Script() {}

	/** Fire the title's Object Activation (the game's start). */
	void start();

	/**
	 * Run one vc_sim.py step: wait, click:ID, timer:N, nav:ID, goto:ID or
	 * ask:ID.
	 */
	void step(const Common::String &step);

	/** The video on screen ended: fire its Object Deactivation, start the next one. */
	void finishVideo();
	/** Mouse Click on an object (icon, hotspot, explorable...), if it is enabled. */
	void click(uint32 id);
	/** Timer n expired: fire Timer Expiration on the scene levels and the title. */
	void expireTimer(uint n);
	/** Follow a VCNav to its view, in the current location. */
	void followNav(uint32 navId);
	/** Follow the shortest path of enabled navs to a view in the current location. */
	void goTo(uint32 viewId);
	/** Ask a conversation topic: its Mouse Click, then the question and answer (or the short atom). */
	void ask(uint32 conversationId);

	uint32 getNode() const { return _node; }
	uint32 getLocation() const { return _location; }
	uint32 getViewPoint() const { return _viewPoint; }
	uint32 getView() const { return _view; }
	/** Play-once videos; the first one is on screen. */
	const Common::Array<uint32> &getVideoQueue() const { return _videoQueue; }
	bool isEnabled(uint32 id) const;
	int32 getVariable(uint32 id) const;

	struct Timer {
		uint32 ms;
		uint32 serial;    ///< Changes each time a timer is (re)started
	};
	const Common::HashMap<uint, Timer> &getTimers() const { return _timers; }

	/**
	 * What the current view shows when no video plays: its enabled media
	 * assets in list order, of which background is the first enabled still
	 * (0 if none; later stills are fallbacks and left out).
	 */
	void getViewAssets(Common::Array<uint32> &assets, uint32 &background) const;

protected:
	/** One line of the log; the default writes it to the Script debug channel. */
	virtual void log(const Common::String &line);

private:
	typedef Common::Array<int32> EventFields;
	typedef Common::HashMap<uint32, int32> LocalVariables;

	uint32 getTriggers(uint32 id) const;
	void fire(uint32 id, int event, const EventFields &fields = EventFields());
	void runActions(const Common::Array<uint32> &actions, int event, const EventFields &fields);
	void execute(const VCAction &action, int event, const EventFields &fields, LocalVariables &locals);
	int32 getValue(const VCOperand &operand, const EventFields &fields, const LocalVariables &locals) const;
	bool test(const VCExpression &condition, const EventFields &fields, const LocalVariables &locals) const;
	void callFunction(const VCAction &action, int event, const EventFields &fields, const LocalVariables &locals);

	void playAsset(uint32 id, byte mode, const Common::String &where);
	void startVideo();
	void enter(uint32 node, uint32 location, uint32 viewPoint, uint32 view);
	void showView();

	const VCData &_data;
	Common::HashMap<uint32, int32> _variables;
	Common::HashMap<uint32, bool> _enabled;    ///< Objects without a VCEnabled are always enabled
	Common::HashMap<uint, Timer> _timers;      ///< Running timers
	uint32 _timerSerial;
	Common::HashMap<uint32, bool> _asked;      ///< Conversations asked before
	Common::Array<uint32> _videoQueue;
	uint32 _node, _location, _viewPoint, _view;
};

} // End of namespace XFiles

#endif // XFILES_SCRIPT_H
