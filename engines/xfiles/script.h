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

/** Mode of an asset action (and of ScriptHooks::playAsset). */
enum AssetMode {
	kModePreload = 0,
	kModePlayOnce = 1,
	kModePlayLoop = 2,
	kModeStop = 3,
	kModeUnload = 4
};

/**
 * What the engine does for the script beyond the interpreter: C++ functions
 * and media that do not block the script (sounds, overlays).
 */
class ScriptHooks {
public:
	virtual ~ScriptHooks() {}

	/** A C++ Function action other than SetStartView and DoActionList; args are the evaluated operands. */
	virtual void callFunction(const VCAction &action, const Common::Array<int32> &args) {}
	/**
	 * A play, loop or stop of an asset that is not a blocking video.
	 * positional: a 3D Sound action, at (x, y) on the viewpoint grid.
	 */
	virtual void playAsset(uint32 id, byte mode, bool positional, int16 x, int16 y) {}
};

/**
 * The VirtualCinema interpreter: runtime state (variables, enable flags,
 * timers, the current view) and the triggers and actions of the database.
 *
 * Variables and enable flags start from the .gam. Entering a view fires
 * Object Activation down the scene graph (Node > Locaton > ViewPoint >
 * View) for every level that changes, and Object Deactivation on the levels
 * left. Videos (.xmv and .nmv assets, played once or looped, with or without
 * a range) play one after another: a video asset's Object Activation fires
 * when it starts, its Object Deactivation when it ends or is stopped.
 *
 * Everything that happens is written to log(), in the format of the
 * research tool vc_sim.py, so the two can be diffed. C++ functions other
 * than SetStartView and DoActionList are only logged.
 */
class Script {
public:
	/** A character's stock replies: the atoms of its topic without a label. */
	enum {
		kStockWhatsTheMatter = 2,   ///< "What's the matter with you?"
		kStockAtomCount = 7
	};

	Script(const VCData &data);
	virtual ~Script() {}

	/** Where C++ functions and non-blocking media go; nullptr (the default) only logs them. */
	void setHooks(ScriptHooks *hooks) { _hooks = hooks; }

	/** Fire the title's Object Activation (the game's start). */
	void start();

	/**
	 * Run one vc_sim.py step: wait, click:ID, timer:N, nav:ID, goto:ID or
	 * ask:ID.
	 */
	void step(const Common::String &step);

	/** The video on screen ended: fire its Object Deactivation, start the next one. */
	void finishVideo() { endVideo("ended"); }
	/** Mouse Click on an object (icon, hotspot, explorable...), if it is enabled. */
	void click(uint32 id);
	/** Mouse Double-Click on an object, if it is enabled. */
	void doubleClick(uint32 id);
	/** The mouse moved onto (Roll Into) or off (Roll Out Of) an object, if it is enabled. */
	void roll(uint32 id, bool into);
	/**
	 * Timer n expired: fire Timer Expiration on every active object that
	 * holds the timer. A periodic timer keeps running.
	 */
	void expireTimer(uint n);
	/** Follow a VCNav to its view, in the current location. */
	void followNav(uint32 navId);
	/** Follow the shortest path of enabled navs to a view in the current location. */
	void goTo(uint32 viewId);
	/**
	 * Open the conversation box of a character view: the Object Activation
	 * of each of its enabled topics.
	 */
	void openBox(uint32 charViewId);
	/** The conversation box was closed. */
	void closeBox() { _box = 0; }
	/**
	 * Ask a conversation topic of a character in the current view, opening
	 * its box if needed: the question and the answer, then the topic's
	 * Mouse Click. Asked again, the character may give the topic's short
	 * atom or a stock reply instead.
	 */
	void ask(uint32 conversationId);
	/** Seed the random numbers of ask(); the same seed gives the same replies. */
	void setRandomSeed(uint32 seed) { _random = seed; }
	/**
	 * Seed every generator of ask() the way a reference run's xfref.dll
	 * seeds XFiles.exe's (step seed:N): the n-th one built at a call site
	 * gets a hash of (base, site, n). Until then all draw from one sequence
	 * (setRandomSeed).
	 */
	void setFixedSeeds(uint32 base);

	uint32 getNode() const { return _node; }
	uint32 getLocation() const { return _location; }
	uint32 getViewPoint() const { return _viewPoint; }
	uint32 getView() const { return _view; }
	/** The current view has been activated (not just entered, or held while a video plays). */
	bool isViewActive() const { return _viewActive; }
	/** Queued videos; the first one is on screen. */
	const Common::Array<uint32> &getVideoQueue() const { return _videoQueue; }
	/** How long the first video plays, in ms; 0 for all of it. */
	uint32 getVideoLimit() const { return _videoPlays.empty() ? 0 : _videoPlays[0].limit; }
	/** Whether the first video loops: it ends only when a Stop action stops it. */
	bool isVideoLooped() const { return !_videoPlays.empty() && _videoPlays[0].loop; }
	bool isEnabled(uint32 id) const;
	/**
	 * An emotion or action icon is spent when an icon of its class is
	 * clicked, and comes back once it is disabled and enabled again.
	 */
	bool isSpent(uint32 id) const { return _spent.contains(id); }
	/** How often a conversation topic was asked. */
	uint getAskCount(uint32 id) const { return _asked.getValOrDefault(id, 0); }
	int32 getVariable(uint32 id) const;
	void setVariable(uint32 id, int32 value) { _variables[id] = value; }

	struct Timer {
		uint32 ms;
		uint32 serial;    ///< Changes each time a timer is (re)started
		bool periodic;    ///< Restarts when it expires
	};
	const Common::HashMap<uint, Timer> &getTimers() const { return _timers; }

	/** A shown VCIFaceLayout and its items that were enabled when it was shown. */
	struct Layout {
		uint32 id;
		Common::Array<uint32> items;
	};
	/** Shown layouts, oldest first. */
	const Common::Array<Layout> &getLayouts() const { return _layouts; }
	/** Evidence icons in the evidence kit (EnableKitIcon), in order. */
	const Common::Array<uint32> &getKitIcons() const { return _kit; }

	/**
	 * What the current view shows when no video plays: the first enabled
	 * media asset of its list (none if no asset is enabled); background is
	 * that asset if it is a still, else 0.
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

	void playAsset(uint32 id, byte mode, const VCAction *sound3D);
	void playList(uint32 listId);
	void playStock(uint32 assetId);
	/** A generator of numbers from 1 to 10 (Random_Seed 0x468810). */
	struct Dice {
		bool own;     ///< Seeded for this use (setFixedSeeds); otherwise it draws from _random
		uint32 state;
	};
	/** The generator XFiles.exe builds at site, the return address of its Random_Seed call. */
	Dice newDice(uint32 site);
	/** A number from 1 to 10, as XFiles.exe draws them (0x468df0). */
	int roll(Dice &dice);
	void playTopic(uint32 conversationId, bool inBox);
	void activateTopic(uint32 conversationId);
	void beforeLead();
	void beforeAnswer();
	void queueViewLoop();
	/** The looping asset of the current view, or 0. */
	uint32 getViewLoop() const;
	bool getStockAtoms(uint32 conversationId, uint32 (&atoms)[kStockAtomCount]) const;
	/** The topics of a character view, in box order. */
	void getBoxTopics(uint32 charViewId, Common::Array<uint32> &topics) const;
	bool isBoxTopic(uint32 charViewId, uint32 conversationId) const;
	void showLayout(uint32 layoutId);
	void hideLayout(uint32 layoutId);
	void updateLayoutItem(uint32 itemId, bool enabled);
	void enableKitIcon(uint32 id);
	void queueVideo(uint32 id, uint32 limit, bool loop);
	void startVideo();
	/** The video on screen ended or was stopped (how is logged). */
	void endVideo(const char *how);
	void enter(uint32 node, uint32 location, uint32 viewPoint, uint32 view);
	void showView();
	/** Activate what the current view shows: its first asset, character views, navs, explorables. */
	void activateView();
	void activateViewObjects();
	void deactivateViewObjects();
	/** An enabled character view, nav or explorable of the current view gets activated. */
	void activateViewObject(uint32 id);
	void deactivateViewObject(uint32 id);
	bool isInView(uint32 id) const;
	void spendIcons(uint32 classId);
	/** Whether the Timer Expiration actions of an object test for timer n. */
	bool holdsTimer(uint32 id, uint n) const;

	const VCData &_data;
	ScriptHooks *_hooks;
	Common::HashMap<uint32, int32> _variables;
	Common::HashMap<uint32, bool> _enabled;    ///< Objects without a VCEnabled are always enabled
	Common::HashMap<uint32, bool> _spent;      ///< Spent emotion and action icons
	Common::HashMap<uint, Timer> _timers;      ///< Running timers
	uint32 _timerSerial;
	Common::HashMap<uint32, uint> _asked;      ///< How often each conversation was asked
	uint32 _random;                            ///< State of the random numbers
	bool _fixedSeeds;                          ///< See setFixedSeeds()
	uint32 _seedBase;
	Common::HashMap<uint32, uint32> _diceBuilt; ///< Generators built per call site, with _fixedSeeds
	uint32 _box;                               ///< Character view whose conversation box is open, or 0
	Common::Array<Layout> _layouts;
	Common::Array<uint32> _kit;
	Common::Array<uint32> _videoQueue;
	struct VideoPlay {
		uint32 limit;   ///< ms to play, 0 for all
		bool loop;      ///< Plays until stopped
	};
	Common::Array<VideoPlay> _videoPlays;      ///< Per queued video

	/** FixJumpCut(first, later) of a topic's Object Activation (0x47ca30). */
	struct JumpCut {
		bool first;   ///< The view's loop before the first clip after the question
		bool later;   ///< The view's loop before the answer when a clip came before it
	};
	Common::HashMap<uint32, JumpCut> _jumpCuts;
	JumpCut _jumpCut;                          ///< Of the topic being played
	bool _clipQueued;                          ///< A clip after the question is queued (topic +0x150)
	uint32 _node, _location, _viewPoint, _view;
	Common::Array<uint32> _viewObjects;        ///< Activated with the view, in activation order
	bool _viewHeld;                            ///< The view is entered but not activated until the video ends
	bool _viewActive;                          ///< The view's activation has run
};

} // End of namespace XFiles

#endif // XFILES_SCRIPT_H
