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
#include "common/rect.h"
#include "common/str.h"
#include "common/str-array.h"
#include "common/ustr.h"

#include "xfiles/computers.h"
#include "xfiles/hooks.h"
#include "xfiles/phone.h"
#include "xfiles/vcdata.h"

namespace Common {
class WinResources;
}

namespace XFiles {

/** The phone's VCIFaceLayout, "Phone (On)". */
static const uint32 kPhoneLayout = 15297;

/** The arguments of RegPreferences, in order: each is a preference variable. */
enum Preference {
	kPref3DSound = 0,
	kPrefArtificialIntuition = 1,
	kPrefAutoRewind = 2,
	kPrefClosedCaptioning = 3,
	kPrefRolloverText = 4,
	kPrefSaveGameOnExit = 5,
	kPrefSkipClips = 6,
	kPrefSkipIntro = 7,
	kPrefSmoothText = 8,
	kPrefSubliminalMessages = 9,
	kPrefTransitions = 10,
	kPrefTranslucency = 11,
	kPrefActionSceneDifficulty = 12,
	kPrefAutoSetDepth = 13,
	kPrefFlashlightBeam = 14,
	kPrefFlashlightBrightness = 15,
	kPrefInventory = 16,
	kPrefVolumeDialog = 17,
	kPrefVolumeEffect = 18,
	kPrefVolumeGame = 19,
	kPrefVolumeMusic = 20,
	kPrefAutoSetResolution = 21,
	kPrefHighQualityVideo = 22,
	kPrefLiveEnvironments = 23,
	kPrefCount = 24
};

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

	/**
	 * A C++ Function action other than DoActionList, after the Script's own hooks ran.
	 * Returns whether the engine acted on it (the hook coverage, tools/hook_coverage.py).
	 */
	virtual bool callFunction(const HookCall &call) { return false; }
	/**
	 * A play, loop or stop of an asset that is not a blocking video.
	 * positional: a 3D Sound action, at (x, y) on the viewpoint grid.
	 */
	virtual void playAsset(uint32 id, byte mode, bool positional, int16 x, int16 y) {}
	/** PlayAssetAt: draw the graphic clip once with its top-left at (x, y) over the screen; Script::finishSound ends it. */
	virtual void playClip(uint32 id, int16 x, int16 y) {}
	/**
	 * A sound playing in a restored save: it plays (and a loop loops) from
	 * startMs in its file (the sound's in point, 0x4a88d0).
	 */
	virtual void restoreSound(uint32 id, bool loop, uint32 startMs) {}
	/**
	 * Whether an inventory item has an icon in the bar, if the engine knows
	 * (it draws the icons); false leaves the answer to the Script, which
	 * assumes an icon for every enabled VCInventory.
	 */
	virtual bool queryInventoryIcon(uint32 id, bool &hasIcon) const { return false; }
	/**
	 * The item in hand changed through the script (SelectInv, DeselectInv, the
	 * held item disabled): id is the item now in hand, 0 when it was put back.
	 */
	virtual void heldItemChanged(uint32 id) {}
	/** The bytes of the file of a file asset (an e-mail's text), false if there is none. */
	virtual bool readAssetFile(uint32 id, Common::Array<byte> &data) { return false; }
	/** The width in pixels of a text in an item's font (Courier New, or Arial when sans), -1 if unknown. */
	virtual int measureText(const Common::U32String &text, int size, bool bold, bool sans) const { return -1; }
	/**
	 * The value the registry holds for a preference (RegPreferences, 0x4ccda0)
	 * that this port takes from the ScummVM settings: false leaves the answer
	 * to Script (a setPreference() value, else the variable's .gam value, as
	 * on the exe's first start). Asked for every Preference.
	 */
	virtual bool queryPreference(uint index, int32 &value) const { return false; }
	/**
	 * StopEnvSounds (0x4cc9c0) ends the sounds: stop every player of a sound
	 * asset. Script ends the sounds' objects itself afterwards.
	 */
	virtual void stopAllSounds() {}
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
	// The areas with their own class use the Script services listed in docs/subsystems/interpreter.md
	friend class ComputerHooks;
	friend class PhoneHooks;

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
	/** The string table of XFilesT.dll, for phone book entries that name a string (may be nullptr). */
	void setTexts(Common::WinResources *texts) { _texts = texts; }

	/** Fire the title's Object Activation (the game's start). */
	void start();
	/**
	 * Continue from a save (VCData::getSave) instead of start(): the data is
	 * a save, so variables and enable flags are already its own. Restores the
	 * position, the history, the e-mail lists, the inventory and evidence
	 * lists, the kit, the timers and the sounds playing.
	 */
	void restore(const VCSave &save);
	/**
	 * The engine's GameState_Save (0x4d9450) as far as the Script knows it:
	 * position (mode 1), history, notes, e-mail lists, disc, hint list and
	 * glow, flashlight, letterbox tint, inventory, evidence, kit, dialog box
	 * and layout. What only the engine knows (timers, sounds, the view's clip,
	 * the head credits) and the photos are left as they are in save.
	 */
	void snapshot(VCSave &save) const;

	/** What a save holds besides the VCSave: the values the game changed. */
	struct SavedValues {
		Common::Array<uint32> variables;   ///< Pairs (VCVariable ID, value)
		Common::Array<uint32> enabled;     ///< Pairs (VCEnabled ID, flag)
		Common::Array<uint32> stringIds;   ///< VCString IDs of the strings that changed
		Common::StringArray strings;       ///< Their values
	};
	/** Every variable, enable flag and changed string now. */
	void getSaved(SavedValues &values) const;
	/**
	 * Put the values of a save over those of the .gam; call before restore(), on a Script that
	 * has not started. Unknown IDs are ignored.
	 */
	void applySaved(const SavedValues &values);

	/**
	 * The value RegPreferences writes into a preference variable (Preference
	 * enum) when the hooks do not answer: the registry's value in a run that
	 * is compared with a reference run. Without it the variable keeps its
	 * .gam value. Set it before start() or restore().
	 */
	void setPreference(uint index, int32 value) { _preferences[index] = value; }

	/**
	 * Run one vc_sim.py step: wait, click:ID, timer:N, nav:ID, goto:ID or
	 * ask:ID.
	 */
	void step(const Common::String &step);

	/** The video on screen ended: fire its Object Deactivation, start the next one. */
	void finishVideo() { endVideo("ended"); }
	/** A sound played by an Asset action ended (or was stopped by the engine): its Object Deactivation. */
	void finishSound(uint32 id) {
		endSound(id, "ended");
		endClip(id, "ended");
	}
	/** Mouse Click on an object (icon, hotspot, explorable...), if it is enabled. */
	void click(uint32 id);
	/** Mouse-down on an interface item: the click, and the item stays held (IsIItemHeld) until release(). */
	void press(uint32 id);
	/** The mouse button goes up. */
	void release();
	/** Mouse Double-Click on an object, if it is enabled. */
	void doubleClick(uint32 id);
	/** The mouse moved onto (Roll Into) or off (Roll Out Of) an object, if it is enabled. */
	void roll(uint32 id, bool into);
	/**
	 * Timer n expired: fire Timer Expiration on every active object that
	 * holds the timer. A periodic timer keeps running.
	 */
	/** A timer expires; a step list may also replay an expiry already queued when the timer was stopped (stale). */
	void expireTimer(uint n, bool stale = false);
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
	/** A random number generator (Random_Seed 0x468810). */
	struct Dice {
		bool own;     ///< Seeded for this use (setFixedSeeds); otherwise it draws from _random
		uint32 state;
	};
	/** The generator XFiles.exe builds at site, the return address of its Random_Seed call. */
	Dice newDice(uint32 site);
	/** A number from 1 to 10, as XFiles.exe draws them (0x468df0). */
	int roll(Dice &dice) { return roll(dice, 1, 10); }
	/** A number from min to max (min >= 0), as XFiles.exe draws them (0x468df0). */
	int roll(Dice &dice, int min, int max);

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
	/** The first member of an asset list that is enabled (XFiles.exe 0x41fe60), 0 if none. */
	uint32 firstEnabled(uint32 listId) const;
	/**
	 * Whether a Mouse Click trigger of an object runs an action whose
	 * condition tests a variable and holds now (XFiles.exe 0x4d23c0 asks
	 * this of iCurrInventory: the object takes the item in hand).
	 */
	bool isTestedOnClick(uint32 id, uint32 variable) const;
	/**
	 * An emotion or action icon is spent when an icon of its class is
	 * clicked, and comes back once it is disabled and enabled again.
	 */
	bool isSpent(uint32 id) const { return _spent.contains(id); }
	/** How often a conversation topic was asked. */
	uint getAskCount(uint32 id) const { return _asked.getValOrDefault(id, 0); }
	int32 getVariable(uint32 id) const;
	/** The e-mail lists, for the state dump. */
	const Common::Array<uint32> &getEmails() const { return _emails; }
	const Common::Array<uint32> &getEmailsRead() const { return _emailsRead; }
	const Common::Array<uint32> &getEmailsPending() const { return _emailPending; }
	/** The notepad's notes (VCPDANotes), for the state dump. */
	const Common::Array<uint32> &getPDANotes() const { return _notes; }
	void setVariable(uint32 id, int32 value) { _variables[id] = value; }
	/** The variable registered by RegCurrInvVar (iCurrInventory); 0 before it. */
	uint32 getHeldVariable() const { return _heldVariable; }
	/** An item is in hand: iCurrInventory is not Inventory_None. */
	bool isHolding() const;
	/** The held-item object's deactivation (XFiles.exe 0x437c20): iCurrInventory is Inventory_None again. */
	void clearHeldItem();
	/** Inv_PutBack (0x429f00): acts only while something is in hand; tells the engine. */
	void putBack();
	/** The click on an inventory icon (InvIcon_OnMouseClick 0x49c180): put back, take the item, run its Mouse Click triggers. */
	void selectInventory(uint32 id);
	/** Whether the item has an icon in the bar: asks the engine, else every enabled VCInventory has one (0x4201a0). */
	bool hasInventoryIcon(uint32 id) const;

	struct Timer {
		uint32 ms;
		uint32 serial;    ///< Changes each time a timer is (re)started
		bool periodic;    ///< Restarts when it expires
	};
	const Common::HashMap<uint, Timer> &getTimers() const { return _timers; }

	/** A shown VCIFaceLayout and its items that were enabled when it was shown, in the exe's layer order. */
	struct Layout {
		uint32 id;
		Common::Array<uint32> items;
	};
	/** Shown layouts, oldest first. */
	const Common::Array<Layout> &getLayouts() const { return _layouts; }
	/** Disc in use: 1 at the start, the save's (+42) after a restore. */
	int32 getDisc() const { return _disc; }
	/** Whether restore() ran, and the save it continued from (parts the engine applies or only logs). */
	bool isRestored() const { return _restored; }
	const VCSave &getRestoredSave() const { return _restoredSave; }
	/** Whether the row of collected evidence is shown (a restore sets it; the engine does not draw it). */
	bool isEvidenceRowShown() const { return _evidenceRowShown; }
	/** The inventory in the order of the bar: the save's after a restore, a new game's class order, then as items are enabled and disabled. */
	const Common::Array<uint32> &getInventory() const { return _inventory; }
	/** Evidence icons outside the kit: (VCEvidenceIcon, shown) pairs as restored from a save, and the icons enabled since (shown 0). */
	const Common::Array<uint32> &getEvidence() const { return _evidence; }
	/** Conversations asked, with repeats, as restored from a save and asked since. */
	const Common::Array<uint32> &getHistory() const { return _history; }
	/** Evidence icons in the evidence kit (EnableKitIcon), in order. */
	const Common::Array<uint32> &getKitIcons() const { return _kit; }

	/** Value of a string variable: its value names a VCString of the .gam. */
	Common::String getString(uint32 variableId) const;
	void setString(uint32 variableId, const Common::String &value);

	/** Horizontal alignment of an item text (SetIItemText's c, l, r). */
	enum TextAlign {
		kAlignLeft,
		kAlignCenter,
		kAlignRight
	};
	/** Text drawn into an interface item (the phone's display, the PDA's texts). */
	struct ItemText {
		Common::U32StringArray lines;  ///< Laid out lines
		uint32 color;   ///< RGB
		bool bold;
		bool sans;      ///< Arial (Liberation Sans) instead of Courier New
		int size;       ///< Character height in pixels (GDI's -lfHeight)
		int lineHeight; ///< Pixels from one line to the next
		TextAlign align;
		bool shadow;    ///< A black pass at (+1, +1) first
		uint firstLine; ///< Scrolled text: the first line drawn at the item's top
	};
	/** The text of an interface item, or nullptr; drawn from the item's top left and clipped to it. */
	const ItemText *getItemText(uint32 itemId) const;
	/** Where a script moved an interface item to (its top left), false if it did not. */
	bool getItemOrigin(uint32 itemId, Common::Point &origin) const;
	/** The interface item with the keyboard focus (SetIItemFocus), or 0. */
	uint32 getFocus() const { return _focus; }
	/**
	 * A key went down while an item has the focus: its Key Press triggers,
	 * with the key's Windows virtual-key code as the event's field.
	 */
	void keyPress(uint vk);
	/**
	 * Take the phone's buttons pressed since the last call. Each goes up
	 * (is disabled again) 100 ms after it was pressed: releaseItem().
	 */
	void takePressedItems(Common::Array<uint32> &items);
	void releaseItem(uint32 itemId);

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
	void fireTriggers(uint32 id, int event, const EventFields &fields);
	void runActions(const Common::Array<uint32> &actions, int event, const EventFields &fields);
	void execute(const VCAction &action, int event, const EventFields &fields, LocalVariables &locals);
	int32 getValue(const VCOperand &operand, const EventFields &fields, const LocalVariables &locals) const;
	bool test(const VCExpression &condition, const EventFields &fields, const LocalVariables &locals) const;
	void logHook(const Common::String &name, HookStatus status);
	/** Fill _hookTable: the Script's own hooks, then those of each area file. */
	void addHooks();
	void addCoreHooks();
	void addPDAHooks();      // pda.cpp
	void addEvidence(uint32 id);
	void removeEvidence(uint32 id);
	void callFunction(const VCAction &action, int event, const EventFields &fields, const LocalVariables &locals);

	void playAsset(uint32 id, byte mode, const VCAction *sound3D);
	void playList(uint32 listId);
	void playStock(uint32 assetId);
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
	void insertLayoutItem(Common::Array<uint32> &items, uint32 itemId);
	void updateLayoutItem(uint32 itemId, bool enabled);
	void enableKitIcon(uint32 id);
	/** RandGen(var, n): a number from 0 to n (0x49f6d0). */
	void randGen(uint32 variableId, int32 n);
	void isIItemHeld(uint32 variableId, int32 item);

	/** Set an interface item's enabled flag; an item of a shown layout comes or goes with it. */
	void setItemEnabled(uint32 item, bool enabled);

	// Item texts (itemtext.cpp)
	/** Lay out and set the text of an item (0x4be2c0); wrapWidth 0: a line per newline only. */
	void setItemText(uint32 item, const Common::U32String &text, int size, bool bold, uint32 color, TextAlign align,
		bool shadow, int wrapWidth, bool sans = false);
	/** PDA text: Courier New, regular, black, left, no shadow, wrapped at width (0x4be2c0); ScrollTextList's colour and Arial are optional. */
	void setPDAText(uint32 item, const Common::U32String &text, int size, bool doubleWidth, uint32 color = 0,
		bool bold = false, bool sans = false);
	/** The text of a text asset (or a string of XFilesT.dll), false if it has none. */
	bool getTextAsset(uint32 entry, Common::U32String &text);
	/** The text of a file asset: Windows-1252, carriage returns dropped. */
	bool readTextFile(uint32 asset, Common::U32String &text);

	// PDA e-mail (pda.cpp)
	void displayInBox(int32 value);
	void initEmailRead(int32 item, int32 email);
	/** Lines of an e-mail's file (To, From, Subject, body), empty if it has none; cached. */
	const Common::U32StringArray &getEmailLines(uint32 email);
	void showInboxTexts();
	void setScrollText(uint32 item, const Common::U32String &text, int size, uint32 color = 0, bool bold = false,
		bool sans = false);
	void scrollTextList(int32 item, int32 asset, int32 color, int32 font, int32 size);
	void mainScroll(int32 n);
	void pdaInitAddr();
	void pdaFlipAddr(int32 n);
	void showAddress();
	void buildAddressList();
	void enablePDANote(uint32 id);
	void enableAllNotes();
	void notePadInit();
	void noteFlipDay(int32 n);
	void showNoteDay();
	void showNoteDayText();
	void showNoteMarker();
	bool hasNote(uint32 id) const;
	bool dayHasNote(uint day, uint32 note) const;

	// Services for the area classes (PhoneHooks, ComputerHooks) besides the public ones
	void clearItemText(uint32 item) { _itemTexts.erase(item); }
	/** Show an interface item pressed: its picture, enabled by the click, goes 100 ms later (takePressedItems). */
	void pressItem(uint32 item) { _pressed.push_back(item); }
	/** The node RegCurrNode's variable holds, 0 before RegCurrNode. */
	int32 getCurrentNode() const { return _currNodeVariable ? getVariable(_currNodeVariable) : 0; }
	void setFocus(uint32 item) { _focus = item; }
	/** Where an item's text or picture is drawn from (getItemOrigin), e.g. a caret behind a field's text. */
	void setItemOrigin(uint32 item, const Common::Point &origin) { _itemOrigins[item] = origin; }
	/** The preference Pref_bArtificialIntuition is on (its variable from RegPreferences). */
	bool isArtificialIntuitionOn() const { return _prefArtificialIntuition && getVariable(_prefArtificialIntuition); }
	/** PlayAssetAt (0x491de0): start the asset unless it already plays; an XG clip is drawn at (x, y). */
	void playAssetAt(uint32 asset, int x, int y);
	/** DoActionList(trigger, actionList): another trigger's actions. */
	void runActionList(uint32 actionList, int event, const EventFields &fields);
	void endSound(uint32 id, const char *how);
	/** A placed clip (PlayAssetAt) ends: its Object Deactivation fires as at a sound's end. */
	void endClip(uint32 id, const char *how);
	/** PlayAssetAt of an XG clip: it plays once with its top-left at (x, y); no hold. */
	void startClip(const VCAsset &a, int x, int y);
	/** StopEnvSounds (0x4cc9c0): every sound without a picture ends. */
	void stopEnvSounds();
	/** RegPreferences (0x4ccda0): the registry's values go into the variables. */
	void regPreferences(const VCAction &a);
	/**
	 * Game_Start's 0x4310f0: before the game begins (new or restored), the
	 * first RegPreferences among the title's trigger actions runs, unlogged.
	 */
	void loadPreferences();
	void queueVideo(uint32 id, uint32 limit, bool loop);
	void startVideo();
	/** The video on screen ended or was stopped (how is logged). */
	void endVideo(const char *how);
	void enter(uint32 node, uint32 location, uint32 viewPoint, uint32 view, bool deactivateAll = false);
	void travel();
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
	HookTable<Script> _hookTable; ///< The C++ function hooks Script implements, by name
	PhoneHooks _phone;            ///< The phone's hooks and state (phone.cpp)
	ComputerHooks _computer;      ///< The computers' text entry (computers.cpp)
	Common::WinResources *_texts;
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
	Common::Array<uint32> _inventory, _evidence, _history;
	bool firesOnRestore(uint32 id) const;
	int _restoreMode;                          ///< Position mode while restore() runs, else -1: which Object Activation fires
	int32 _disc;                               ///< Disc in use (VCGameState +42)
	bool _evidenceRowShown;                    ///< EvIcons_SetRowShown, set by a shown evidence icon of a restore
	uint32 _hintList, _hintTrigger;            ///< The last SetAIList, 0 after ClearAIList
	int32 _hintGlow;                           ///< The last SetAIGlow above 1, else 0
	byte _flashlight[3];                       ///< SetFLightProps as the save keeps it (FLight_SetProps 0x4d7d20 via 0x44bda0)
	byte _tint;                                ///< SetLBoxTint (1) or ClearLBoxTint (0)
	byte _tintColor[3];                        ///< The tint in the order the save stores it (blue, green, red)
	VCSave _restoredSave;                      ///< The save restore() continued from
	bool _restored;
	Common::HashMap<uint32, Common::String> _strings;  ///< VCString values that changed
	Common::HashMap<uint32, ItemText> _itemTexts;
	uint32 _focus;
	uint32 _currNodeVariable;                  ///< RegCurrNode
	uint32 _heldVariable;                      ///< RegCurrInvVar: iCurrInventory
	int32 _heldNone;                           ///< Its Inventory_None value
	uint32 _heldCursorItem;                    ///< Inv_Held() + 4 (0x6b8974): the VCInventory ID whose cursor is held
	Common::Array<uint32> _pressed;            ///< See takePressedItems()
	uint32 _held;                              ///< The interface item the mouse button is down on (press()), 0 for none
	Common::HashMap<uint32, bool> _sounds;     ///< Sounds playing (asset objects active)
	Common::HashMap<uint32, bool> _clips;      ///< Graphic clips placed by PlayAssetAt and playing
	Common::HashMap<uint, int32> _preferences; ///< See setPreference()

	/** The computer text entry buffers (XFiles.exe 0x6be918 and others). */
	Common::HashMap<uint32, Common::Point> _itemOrigins; ///< See getItemOrigin()
	Common::Array<uint32> _videoQueue;
	struct VideoPlay {
		uint32 limit;   ///< ms to play, 0 for all
		bool loop;      ///< Plays until stopped
	};
	Common::Array<VideoPlay> _videoPlays;      ///< Per queued video
	/** PDA e-mail: VCEmail, VCEmailRead and VCEmailPending, and the inbox (globals 0x6bd914, 0x6bd86c). */
	Common::Array<uint32> _emails, _emailsRead, _emailPending;
	struct InboxRow {
		uint32 id;
		bool read;
	};
	Common::Array<InboxRow> _inboxRows;
	uint _inboxTop;
	int _pdaFontSize;                          ///< EmailInit(c): 12 for 'P', else 14
	bool _pdaLayout;                           ///< EmailInit('P'): the PDA's item tables
	Common::HashMap<uint32, Common::U32StringArray> _emailLines;  ///< Lines of the e-mail files read
	uint32 _prefArtificialIntuition;           ///< The variable RegPreferences gives Pref_bArtificialIntuition, or 0
	uint32 _scrollItem;                        ///< The item of the scroll text (0x42ca30), or 0
	Common::Array<uint32> _addressList;        ///< Enabled assets of item 45307 (0x6b8390)
	uint _addressIndex;                        ///< 0x6b16b0
	Common::Array<uint32> _notes;              ///< VCPDANotes
	uint _noteDay, _noteDays;                  ///< 0x6b1a4c, 0x6b1a54

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
	uint _fireDepth;                           ///< Nesting of fire(): a pending travel runs when it returns to 0
	bool _travelPending;                       ///< SwapDisc was called in this event (0x6b19a8)
	uint32 _travelTo[4];                       ///< Node, location, viewpoint and view of the pending travel
	bool _viewHeld;                            ///< The view is entered but not activated until the video ends
	bool _viewActive;                          ///< The view's activation has run
};

} // End of namespace XFiles

#endif // XFILES_SCRIPT_H
