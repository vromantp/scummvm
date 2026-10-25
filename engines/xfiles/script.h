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
#include "xfiles/pda.h"
#include "xfiles/phone.h"
#include "xfiles/prefs.h"
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
	/**
	 * A phone key tone (PhoneButton, 0x4c8870): the exe plays them as movies of their own, not sound objects. Stops
	 * the previous tone and restarts the digit's from its beginning; 0 only stops. Nothing is finished at its end.
	 */
	virtual void playPhoneTone(uint32 id) {}
	/**
	 * The camera's shutter (Camera_OnMouseClick, 0x4f3090): the asset (34747, or 57104 when the camera is full) plays
	 * as a movie of its own, not a sound object, from its beginning.
	 */
	virtual void playShutter(uint32 id) {}
	/**
	 * The gun's own click (0x4cb520): the shot (34743) plays as a movie of its own from its beginning, restarted by
	 * the next click; with a remark (nonzero) the remark plays as a sound once the shot has ended.
	 */
	virtual void playGunShot(uint32 id, uint32 remark) {}
	/**
	 * A SwapDisc travel is about to run (0x5068b0 snapshots the game first): the checkpoint "Try again" restarts
	 * from (B-176).
	 */
	virtual void checkpoint() {}
	/** The firing cursor after a gun click on a .HOT hotspot (0x4b4a20): an animated cursor asset. */
	virtual void showCursorAnimation(uint32 id) {}
	/** A travel (0x493090) shows the AI icon again if there is one: its show handler re-applies the glow from the level. */
	virtual void reshowAIIcon() {}
	/** The player's current time in the clip shown in the band (0x41cdd0), for the photo the camera takes. */
	virtual uint32 getPhotoTime() const { return 0; }
	/**
	 * The binoculars' view change (0x51cf10): the playing time in ms of the clip the view shows. False if the view has
	 * no playing clip or its clip has ended (no swap then). The default answers like a clip at 0.
	 */
	virtual bool getViewClipTime(uint32 &ms) const {
		ms = 0;
		return true;
	}
	/**
	 * TimedText_Pause 0x5078f0 (what TextOn does): true pauses the head credits and the subliminal object (0x46c970),
	 * false resumes them (0x49fae0). The held tool objects call it when they activate and after they deactivate.
	 */
	virtual void pauseTimedText(bool pause) {}
	/** The view's clip time (+0x170) for the next view play: the first non-background clip of it starts at ms. */
	virtual void setViewClipStart(uint32 ms) {}
	/** PlayAssetAt: draw the graphic clip once with its top-left at (x, y) over the screen; Script::finishSound ends it. */
	virtual void playClip(uint32 id, int16 x, int16 y) {}
	/** A Stop action on an asset without a sound object (0x504c90): a shown view asset runs to its end and holds. */
	virtual void stopViewAsset(uint32 id) {}
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
	/**
	 * Play an asset (as Script::playReaction) ms from now, in game time. Returns false when the engine does not
	 * delay: the script then plays it at once.
	 */
	virtual bool delayPlay(uint32 asset, uint ms) { return false; }
	/**
	 * An inventory item was enabled while it has no icon (0x4201a0 → 0x4a1fb0). Returns true when the engine adds
	 * the icon later through Script::addInventoryIcon (the hidden bar's reveal: the icon joins 500 ms later); false
	 * appends it at once, as the exe does with the bar shown.
	 */
	virtual bool deferInventoryIcon(uint32 id) { return false; }
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
	 * The PDA opens (OnIntInvClick 0x467fb0 → 0x48e640): the running timers
	 * stop until no layout is shown (0x4a4fa0). A one-shot timer goes on with
	 * the time it had left, a periodic one with its full period.
	 */
	virtual void pauseTimers() {}
	/**
	 * StopEnvSounds (0x4cc9c0) ends the sounds: stop every player of a sound
	 * asset. Script ends the sounds' objects itself afterwards.
	 */
	virtual void stopAllSounds() {}
	/** A .HOT click that ends the game (actions 27, 28): the screen fades to black (0x5103b0), as FadeToBlack. */
	virtual void fadeToBlack() {}
	/** Scene_SuspendAll(scene, 1): the timers and the scene's objects stop (as the X menu's). */
	virtual void suspendScene() {}
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
	friend class PdaHooks;
	friend class PhoneHooks;
	friend class PrefHooks;

public:
	/** A character's stock replies: the atoms of its topic without a label. */
	enum {
		kStockWhatsTheMatter = 2,   ///< "What's the matter with you?"
		kStockAtomCount = 7,
		kReactionSlots = 9,         ///< A character view's reaction slots: S0-S6, then 19656 and 19654
		kReactionNone = 10          ///< No slot for the held item (CharView_ReactionSlot 0x490d50)
	};

	Script(const VCData &data);
	virtual ~Script() {}

	/** Where C++ functions and non-blocking media go; nullptr (the default) only logs them. */
	/** The engine ends sounds at their clip end (SoundPlayer), so a play of an asset that still has a sound object is dropped. */
	void setHooks(ScriptHooks *hooks) {
		_hooks = hooks;
		_soundEnds = hooks != nullptr;
	}
	/** The string table of XFilesT.dll, for phone book entries that name a string (may be nullptr). */
	void setTexts(Common::WinResources *texts) { _texts = texts; }

	/** The photos in database order, and how many are on the camera (VCGameState +46). */
	const Common::Array<VCPhoto> &getPhotos() const { return _photos; }
	int32 getPhotosOnCamera() const { return _photosOnCamera; }
	const ComputerHooks &getComputerHooks() const { return _computer; }
	/**
	 * The letterbox band's background colour after SetLBoxTint (0x45d2f0 gives the band 0x6c0870 the fill map's
	 * entry 0). False when there is no tint: the band is black.
	 */
	bool getBandFill(byte &r, byte &g, byte &b) const {
		if (!_tint)
			return false;
		r = _tintColor[2];
		g = _tintColor[1];
		b = _tintColor[0];
		return true;
	}
	/**
	 * The camera is in hand and was taken outside a special view (Camera +0x1c0): a click in the band is its
	 * cameraClick and the viewfinder shows.
	 */
	/** The view mode of the view change in progress (Screen_PlayView 0x464330). */
	enum {
		kTransitionCut,
		kTransitionLeft,   ///< slide
		kTransitionRight,  ///< slide, the other way
		kTransitionForward ///< wipe opening out from the hotspot
	};
	int getTransition() const { return _transition; }
	/** The wipe's centre in screen coordinates: the hotspot's centre, the click point when its rectangle is empty. */
	bool getTransitionCentre(Common::Point &centre) const {
		centre = _transitionCentre;
		return true;
	}
	/** The point of the click that follows a nav, for a wipe from an empty hotspot rectangle. */
	void setClickPoint(const Common::Point &p) { _clickPoint = p; }
	void clearTransition() { _transition = kTransitionCut; }
	bool isCameraEnabled() const { return _cameraEnabled && isHolding() && getVariable(_heldVariable) == 3; }
	/**
	 * Camera_OnMouseClick (0x4f26a0) at a click in the band, for the camera in hand: the shutter, the photo and the
	 * count, the camera actions of the objects under the viewfinder, the second reaction of a character.
	 */
	void cameraClick(const Common::Point &pos);

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
	 * the head credits) are left as they are in save.
	 */
	void snapshot(VCSave &save) const;

	/** What a save holds besides the VCSave: the values the game changed. */
	struct SavedValues {
		Common::Array<uint32> variables;   ///< Pairs (VCVariable ID, value)
		Common::Array<uint32> enabled;     ///< Pairs (VCEnabled ID, flag)
		Common::Array<uint32> stringIds;   ///< VCString IDs of the strings that changed
		Common::StringArray strings;       ///< Their values
		uint32 nextId = 0;                 ///< The next free object ID (0: not saved)
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
	 * A preference's value as the exe's prefs object holds it: what a Preferences screen hook or
	 * setPreference() stored, else its variable's value (RegPreferences wrote it), 0 before RegPreferences.
	 */
	int32 getPreferenceValue(uint index) const;
	/** Prefs_ApplyAIIcon (0x452780) with Artificial Intuition off: every action icon is deactivated. */
	void removeActionIcons() { spendIcons(kClassActionIcon); }

	/**
	 * Run one vc_sim.py step: wait, click:ID, timer:N, nav:ID, goto:ID,
	 * ask:ID, idea:ICON, icon:ID... (tools/ref/steps.py lists them).
	 */
	void step(const Common::String &step);
	/** Display a layout from exe code, as the main menu's Options and Help do (the `layout:` step). */
	void displayLayout(uint32 layoutId) { showLayout(layoutId); }

	/** The video on screen ended: fire its Object Deactivation, start the next one. */
	void finishVideo() { endVideo("ended"); }
	/** A sound played by an Asset action ended (or was stopped by the engine): its Object Deactivation. */
	void finishSound(uint32 id) {
		endSound(id, "ended");
		endClip(id, "ended");
	}
	/** Mouse Click on an object (icon, hotspot, explorable...), if it is enabled. A character view's click is at  at
	 * (the middle of its rectangle for a scripted run); CharTeleport reads it. */
	void click(uint32 id, const Common::Point *at = nullptr);
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
	/**
	 * A click on an idea icon while a character's box is open: the topic of
	 * the icon's VCIdeaMap plays as a dropped item's topic does (0x4c5500).
	 * Does nothing if the box's character has no enabled map for the icon.
	 */
	void idea(uint32 iconId, uint32 charView = 0);
	/** The enabled idea icons of the open, shown box's character, in map order; empty otherwise. */
	void getBoxIdeaIcons(Common::Array<uint32> &icons) const;
	/** Whether the character view has an enabled idea icon in its map (0x49bce0). */
	bool hasEnabledIdea(uint32 charViewId) const;
	bool hasEnabledConversation(uint32 charViewId) const;
	bool hasTopicOrIdea(uint32 charViewId) const;
	/** The character view a CharTeleport trigger of the character view leads to (same character in the target view,
	 * enabled) when it has a topic or idea, else 0 (0x407cd0). */
	uint32 getTeleportTarget(uint32 charViewId) const;
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
	/** The out point of the first video when a topic merged the next range of its file into it (ms), 0 for its own. */
	uint32 getVideoEnd() const { return _videoPlays.empty() ? 0 : _videoPlays[0].end; }
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
	bool isTestedOnClick(uint32 id, uint32 variable, int event = kEventMouseClick) const;
	/** The held item has a reaction slot at a character view with an Atoms topic (0x418520: no dice, no empty test) */
	bool hasHeldReactionSlot(uint32 charViewId) const;
	/**
	 * The last click() on a character view leaves the dialog box to the caller (0x436080): nothing is in hand, or the
	 * held item has no reaction clip and no trigger took it.
	 */
	bool opensBoxOnClick() const { return _clickOpensBox; }
	/** A tool's delayed reaction clip is due (the engine's timer for ScriptHooks::delayPlay). */
	void playReaction(uint32 asset) { playStock(asset); }
	/**
	 * The icon of an enabled item joins the bar now, at the end (its object activates): the reveal's timer of
	 * ScriptHooks::deferInventoryIcon, or the `icon:` step. Nothing when the item is no longer enabled.
	 */
	void addInventoryIcon(uint32 id);
	/** How often a conversation topic was asked. */
	uint getAskCount(uint32 id) const { return _asked.getValOrDefault(id, 0); }
	int32 getVariable(uint32 id) const;
	/** The e-mail lists, for the state dump. */
	const Common::Array<uint32> &getEmails() const { return _pda.getEmails(); }
	const Common::Array<uint32> &getEmailsRead() const { return _pda.getEmailsRead(); }
	const Common::Array<uint32> &getEmailsPending() const { return _pda.getEmailsPending(); }
	/** The notepad's notes (VCPDANotes), for the state dump. */
	const Common::Array<uint32> &getPDANotes() const { return _pda.getNotes(); }
	void setVariable(uint32 id, int32 value) { _variables[id] = value; }
	/** EndGame(var) ran (0x4cc870): the dialog box is closed, the answer is the engine's (B-176). */
	bool isEndGamePending() const { return _endGamePending; }
	/** The value of the EndGame variable when it ran: non-zero asks "Try again?" if the preference is on. */
	int32 getEndGameValue() const { return _endGameValue; }
	/** The end is taken: asked (the question was shown) writes 0 back to the variable, as 0x4fbd60 does. */
	void answerEndGame(bool asked);
	/** The variable registered by RegCurrInvVar (iCurrInventory); 0 before it. */
	uint32 getHeldVariable() const { return _heldVariable; }
	/** An item is in hand: iCurrInventory is not Inventory_None. */
	bool isHolding() const;
	/** The held-item object's deactivation (XFiles.exe 0x437c20): iCurrInventory is Inventory_None again. */
	void clearHeldItem();
	/** Inv_PutBack (0x429f00): acts only while something is in hand; tells the engine. */
	void putBack();
	/** A gun click on a .HOT hotspot of the given kind (HotSpotObj_OnMouseClick 0x4b4470). */
	void hotClick(uint32 kind);
	/** The value of the held inventory item (Inv_CurrentValue), or 0 when nothing is held. */
	int32 getHeldValue() const { return isHolding() ? getVariable(_heldVariable) : 0; }
	/** The gun (OnGunClick's inventory value) is in hand: .HOT hotspots of the shootout take clicks and show the tool cursor. */
	bool isGunHeld() const { return _gunValue && isHolding() && getVariable(_heldVariable) == (int32)_gunValue; }
	/** The gun in hand and a click no object took (0x4cb520): the shot, and in some locations a remark after it. */
	void gunClick();
	/** A sound that follows a movie of its own (the gun's remark): played as a Do Asset action would. */
	void playFollowUp(uint32 id) { playAsset(id, kModePlayOnce, nullptr); }
	/** The AI icon glows (flag 0x6b2aa8, apart from the level). */
	bool isHintGlowing() const { return _hintGlowOn; }
	/** AI_GlowLevel: the view a click on the glowing icon goes to, 0 for none. */
	int32 getHintGlowLevel() const { return _hintGlow; }
	/** The AI icon's show handler (0x509280) applies the glow from the level alone: after SetAIGlow(1) a re-show clears it. */
	void aiIconShown() { _hintGlowOn = _hintGlow != 0; }
	/** The click on the glowing AI icon (0x509aa0): a Set View to the level's view; level 0 does nothing. */
	void clickAIIcon();
	/** The click on a not glowing AI icon (0x5097e0): the next applicable action of the hint list, the cursor cycling. */
	void clickAIHint();
	/** 0x508c40: a hint is available when the glow flag is set or an action of the hint list has a condition that holds. */
	bool isHintAvailable();
	/** The AI icon is suspended (slot +0x20 with 1, 0x4a04f0): grey, no hit. */
	bool isAISuspended() const { return _aiSuspended; }
	/** 0x508e40 (SetAIList, ClearAIList, the travel re-show, the icon's making): suspended without a hint, else resumed. */
	void recheckAIIcon() { _aiSuspended = !isHintAvailable(); }
	/** 0x509520 (a modal layout closes): resumes the icon, only with a hint; never suspends. */
	void resumeAIIcon() {
		if (_aiSuspended && isHintAvailable())
			_aiSuspended = false;
	}
	/**
	 * The end or stop of a sound object (0x43a970) queues event 0x16 for the AI icon (slot +0x78, 0x509330): it
	 * suspends the icon when no hint is available, never resumes it. Run after the frame's script work.
	 */
	void aiSoundEndCheck() {
		if (!_aiSoundEndPending)
			return;
		_aiSoundEndPending = false;
		if (!isHintAvailable())
			_aiSuspended = true;
	}
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
	/** Whether the row of collected evidence is shown (DispEvIcons or a restore with a shown icon). */
	bool isEvidenceRowShown() const { return _evidenceRowShown; }
	/** The inventory in the order of the bar: the save's after a restore, a new game's class order, then as items are enabled and disabled. */
	const Common::Array<uint32> &getInventory() const { return _inventory; }
	/** Evidence icons outside the kit: (VCEvidenceIcon, shown) pairs: as restored from a save, DispEvIcons sets all, an icon enabled later is hidden (0). */
	const Common::Array<uint32> &getEvidence() const { return _evidence; }
	/** Conversations recorded as asked (see recordTopic()), sorted, as restored from a save and asked since. */
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
	 * A key went down while an item has the focus: its Key Press triggers, with the key as the event's field: the
	 * character of WM_CHAR for a printable key, else the Windows virtual-key code (Backspace, Tab, Return, Delete).
	 */
	void keyPress(uint key);
	/** RegUberVars: the variable iUberParanoia, 0 before it (for iUberParanoia += n, 0x4bf500). */
	void registerUberParanoia(uint32 variable) { _uberParanoia = variable; }
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
	/**
	 * A branch the script took, for vcdump's coverage (docs/engine/debugging.md): kind 'A' an action ran, 'F' a
	 * conditional action's condition was false, 'T' a trigger's action list started, 'O' an object's Object
	 * Activation fired, 'S' an asset was played; id is the object's ID. The default does nothing.
	 */
	virtual void cover(char kind, uint32 id) {}

private:
	typedef Common::Array<int32> EventFields;
	typedef Common::HashMap<uint32, int32> LocalVariables;

	uint32 getTriggers(uint32 id) const;
	void fire(uint32 id, int event, const EventFields &fields = EventFields());
	void fireTriggers(uint32 id, int event, const EventFields &fields);
	void runActions(const Common::Array<uint32> &actions, int event, const EventFields &fields);
	bool isViewChangeQueued() const;
	void execute(const VCAction &action, int event, const EventFields &fields, LocalVariables &locals);
	int32 getValue(const VCOperand &operand, const EventFields &fields, const LocalVariables &locals) const;
	bool test(const VCExpression &condition, const EventFields &fields, const LocalVariables &locals) const;
	/** Whether the operand is a string variable (type 3), with its text. */
	bool getStringOperand(const VCOperand &operand, Common::String &text) const;
	/** == of the exe (0x5d5ef0), which compares string variables by their text. */
	bool testEqual(const VCExpression &condition, int32 a, int32 b) const;
	void logHook(const Common::String &name, HookStatus status);
	/** Fill _hookTable: the Script's own hooks, then those of each area file. */
	void addHooks();
	void addCoreHooks();
	void addGameHooks();
	void addNodeHooks();
	void addToolHooks();
	void addInterfaceHooks();
	void addStateHooks();
	/** SetStartView of a restore: enter the saved position and rebuild the objects (Game_BuildObjects). */
	void restoreView();
	void addEvidence(uint32 id);
	void removeEvidence(uint32 id);
	void callFunction(const VCAction &action, int event, const EventFields &fields, const LocalVariables &locals);

	void playAsset(uint32 id, byte mode, const VCAction *sound3D);
	void playList(uint32 listId);
	void playTopicClips(uint32 conversationId, bool inBox);
	void mergeTopicClips();
	void playStock(uint32 assetId);
	void playClip(uint32 assetId);
	void playTopic(uint32 conversationId, bool inBox);
	void recordTopic(uint32 conversationId);
	void activateTopic(uint32 conversationId);
	void beforeLead();
	void beforeAnswer();
	void queueViewLoop();
	/** The looping asset of the current view, or 0. */
	uint32 getViewLoop() const;
	/** The character view a topic is asked of (0x4e9e50); false if none. */
	bool findTopicCharView(uint32 conversationId, bool inBox, VCCharView &charView) const;
	/** The atoms of the first topic of a character view, if it has no label (0x43d3d0). */
	bool getCharViewAtoms(const VCCharView &charView, uint32 (&atoms)[kStockAtomCount]) const;
	/** A character view's reaction slots (CharView_ctor +0x13c); false, all 0, without an Atoms topic. */
	bool getReactionSlots(uint32 charViewId, uint32 (&slots)[kReactionSlots]) const;
	/** The reaction clip of the item in hand on a character view (0x490d50), 0 for none. Rolls for the cattle prod. */
	uint32 getHeldReaction(uint32 charViewId);
	/** The conditional actions of an object's Mouse Click triggers that test iCurrInventory and hold, each run alone (0x4d22b0); whether any ran. */
	bool runCameraActions(uint32 id);
	/** Tool_CharReaction 0x4e2160: the first enabled character view of the view with the item's slot plays it 2 s later. */
	void toolReaction(int32 item);
	/** Tool_SpecialView 0x51bc70 for the binoculars: one of the views with a view change of its own. */
	bool isSpecialView() const;
	/** Tool_SpecialView 0x51bc70: the binoculars taken (on) or put back swap the view's asset; false in no special view. */
	bool binocularsView(bool on);
	/** Tool_SpecialView's mode out (conversation-reactions.md "Mode out"): 2 for the pairs and 1, 1 for 1228, 1236, 84307, else 0. */
	int binocularsMode() const;
	/**
	 * The held tool type (0x5194a0, the five slots at +0x88): binoculars (item 2) 1, camera (3) 2, flashlight (8) 3,
	 * night vision (13) 4, gun (9) 7, any other held item 0, nothing in hand 8 (tool-objects.md "Tool types").
	 */
	int heldToolType() const;
	/** A tool object (binoculars, camera, flashlight, night vision) pauses the timed text while held; the gun and plain items do not. */
	static bool isPausingTool(int32 item) { return item == 2 || item == 3 || item == 8 || item == 13; }
	/** The held variable goes from `from` to `to` (the click hooks): the old tool object deactivates (resume), the new one activates (pause). */
	void toolPause(int32 from, int32 to);
	/** The topics of a character view, in box order. */
	void getBoxTopics(uint32 charViewId, Common::Array<uint32> &topics) const;
	bool isBoxTopic(uint32 charViewId, uint32 conversationId) const;
	bool findIdeaTopic(uint32 charViewId, uint32 iconId, uint32 &conversation) const;
	void showLayout(uint32 layoutId);
	void hideLayout(uint32 layoutId);
	void insertLayoutItem(Common::Array<uint32> &items, uint32 itemId);
	void updateLayoutItem(uint32 itemId, bool enabled);
	/** Hide or show an item's object without touching its enabled flag or firing triggers (Queue_Show). */
	void queueShowItem(uint32 itemId, bool shown);
	void enableKitIcon(uint32 id);
	/** RandGen(var, n): a number from 0 to n (0x49f6d0). */
	void randGen(uint32 variableId, int32 n);
	void isIItemHeld(uint32 variableId, int32 item);
	/** The Smolnikoff shootout hooks (docs/subsystems/smolnikoff-shootout.md). */
	void shootoutReset();
	void doSmolShootout(const HookCall &call);
	void smIsWillShot(uint32 timer);
	void smolGoonShot();
	/** The .HOT click actions (HotSpot_ClickAction 0x4b4a20, docs/formats/hot.md "Click actions", B-031). */
	int hotActionFor(uint32 kind, bool &gunAnimation) const;
	void hotAction(int n);
	void setViewState(char letter);
	void setViewByExe(uint32 view);
	void setShootoutVariables();
	/** The node, location and viewpoint that hold a view: the current viewpoint first, else the whole database. */
	bool findViewPath(uint32 view, uint32 (&path)[4]) const;
	/** RegShanksNode2/3/6: keep the argument variables of node slot 0, 1 or 2. */
	bool regShanksNode(int slot, uint count, const HookCall &call);
	/** SetShanksVar (0x4c4710): compute the "tell will" flag, then the warehouse e-mail check. */
	void setShanksVar(uint32 variableId);
	/** RegNode2..6Vars, RegLink2/3Vars: keep the argument variables of slot 0..4 (nodes 2..6) or 5, 6 (links 2, 3). */
	bool regNodeVars(int slot, uint count, const HookCall &call);
	/** SetIsNode2..6Done, SetIsLink2/3Done (0x4ad400..0x4ad760): write the slot's "complete" flag from the current node. */
	bool setNodeDone(int slot);
	/** The box comes back after an idea's topic: fire the topic's Object Deactivation once its clips have ended. */
	void endIdeaTopic();
	/** The VCIdeaMaps of the open box's character; false if there are none. */
	bool getBoxIdeaMaps(Common::Array<uint32> &maps) const;
	void activateIdeaIcons();
	bool isIdeaIconOfBox(uint32 iconId) const;

	/** Set an interface item's enabled flag; an item of a shown layout comes or goes with it. */
	void setItemEnabled(uint32 item, bool enabled);
	/** What an Enable action does to its target (an asset, item, icon...), for code that enables one itself (0x4083d0). */
	void enableTarget(uint32 target, bool value);
	/** iUberParanoia += n (0x4bf500). */
	void addUberParanoia(int32 n);

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

	// Services for the area classes (PhoneHooks, ComputerHooks, PdaHooks) besides the public ones
	void clearItemText(uint32 item) { _itemTexts.erase(item); }
	/** The text of an item to change in place (a scroll), or nullptr. */
	ItemText *findItemText(uint32 item) { return _itemTexts.contains(item) ? &_itemTexts[item] : nullptr; }
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
	/** A phone key tone (0 = stop): logged, and played by the engine's hooks. */
	void playPhoneTone(uint32 id);
	/** DoActionList(trigger, actionList): another trigger's actions. */
	void runActionList(uint32 actionList, int event, const EventFields &fields);
	void endSound(uint32 id, const char *how);
	/** The Object Deactivation of the sounds and clips a Stop action stopped, once the event is over (B-112). */
	void flushStops();
	/** The asset has a sound object: a sound, a clip, or a video playing or queued (0x491380). */
	bool isPlaying(uint32 id) const;
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
	void queueVideo(uint32 id, uint32 limit, bool loop, bool viewLoop = false);
	void startVideo();
	/** The video on screen ended or was stopped (how is logged). */
	void endVideo(const char *how);
	/** Starts the sounds the videos held back, in the order they were played. */
	void releaseHeldSounds();
	/** A travel ends every sound, clip and queued video as a stop (0x493090), between the old and new activations. */
	void stopAllForTravel();
	void setNavTransition(const VCNav &nav);
	void enter(uint32 node, uint32 location, uint32 viewPoint, uint32 view, bool deactivateAll = false);
	void travel();
	void showView();
	/** Logs the asset the current view shows. */
	void logViewAssets();
	void logViewAsset(uint32 id, uint32 background);
	/** Activate what the current view shows: its first asset, character views, navs, explorables. */
	void activateView();
	void activateOrHoldView();
	void activateViewObjects(bool before);
	void deactivateView();
	void enterLeavePath(); ///< Enters the Set View deferred while the view was left (B-205)
	void deactivateViewAsset();
	void deactivateViewObjects();
	/** An enabled character view, nav or explorable of the current view gets activated. */
	void activateViewObject(uint32 id);
	void deactivateViewObject(uint32 id);
	/** An asset of the current view enabled or disabled: the view picks the asset it shows again. */
	void updateViewAsset(uint32 id);
	void activateViewAsset();
	/**
	 * An asset object counts its activations (XFiles.exe +0x14c): Object Activation fires on the first,
	 * Object Deactivation when the last user lets go.
	 */
	void activateAsset(uint32 id);
	void deactivateAsset(uint32 id, bool fireEvent = true);
	bool isInView(uint32 id) const;
	void spendIcons(uint32 classId);
	/** Whether the Timer Expiration actions of an object test for timer n. */
	bool holdsTimer(uint32 id, uint n) const;

	const VCData &_data;
	ScriptHooks *_hooks;
	HookTable<Script> _hookTable; ///< The C++ function hooks Script implements, by name
	PhoneHooks _phone;            ///< The phone's hooks and state (phone.cpp)
	ComputerHooks _computer;      ///< The computers' text entry (computers.cpp)
	PdaHooks _pda;                ///< The PDA: e-mail, address book, notepad (pda.cpp)
	PrefHooks _prefs;             ///< The Game Preferences screens (prefs.cpp)
	Common::WinResources *_texts;
	Common::HashMap<uint32, int32> _variables;
	Common::HashMap<uint32, bool> _enabled;    ///< Objects without a VCEnabled are always enabled
	Common::HashMap<uint, Timer> _timers;      ///< Running timers
	uint32 _timerSerial;
	Common::HashMap<uint32, uint> _asked;      ///< How often each conversation was asked
	uint32 _random;                            ///< State of the random numbers
	bool _soundEnds;                           ///< Sound ends are known (`media` step, or an engine): see playAsset()
	bool _fixedSeeds;                          ///< See setFixedSeeds()
	uint32 _seedBase;
	Common::HashMap<uint32, uint32> _diceBuilt; ///< Generators built per call site, with _fixedSeeds
	uint32 _box;                               ///< Character view whose conversation box is open, or 0
	Common::Array<Layout> _layouts;
	Common::Array<uint32> _kit;
	Common::Array<uint32> _inventory, _evidence, _history;
	Common::Array<VCPhoto> _photos;            ///< The VCPhotos in database order
	int32 _photosOnCamera;                     ///< VCGameState +46: taken, not yet downloaded
	int _navTransition = kTransitionCut;       ///< The mode of the nav being followed, taken by enter()
	int _transition = kTransitionCut;          ///< The mode of the view entered last, until the engine plays it
	Common::Point _navTransitionCentre, _transitionCentre;
	bool _navTransitionEmpty = true, _transitionEmpty = true;
	Common::Point _clickPoint = Common::Point(320, 240); ///< The last click on a nav, the screen centre when none
	bool _cameraEnabled;                       ///< The camera in hand was taken outside a special view (Camera +0x1c0)
	bool firesOnRestore(uint32 id) const;
	int _restoreMode;                          ///< Position mode while restore() runs, else -1: which Object Activation fires
	int32 _disc;                               ///< Disc in use (VCGameState +42)
	bool _evidenceRowShown;                    ///< EvIcons_SetRowShown, set by a shown evidence icon of a restore
	uint32 _hintList, _hintTrigger;            ///< The last SetAIList, 0 after ClearAIList
	uint _hintCursor;                          ///< Next member of _hintList a click tries (0x5097e0); not saved
	int32 _hintGlow;                           ///< AI_GlowLevel: the last SetAIGlow above 1, else 0 (the save keeps this)
	bool _aiSoundEndPending;                   ///< A sound object ended since the last aiSoundEndCheck; not saved
	bool _aiSuspended;                         ///< The AI icon's suspension (0x4a04f0), set by recheckAIIcon; not saved
	bool _hintGlowOn;                          ///< The glow flag (0x6b2aa8): SetAIGlow(1) sets it with level 0
	byte _flashlight[3];                       ///< SetFLightProps as the save keeps it (FLight_SetProps 0x4d7d20 via 0x44bda0)
	byte _tint;                                ///< SetLBoxTint (1) or ClearLBoxTint (0)
	byte _tintColor[3];                        ///< The tint in the order the save stores it (blue, green, red)
	VCSave _restoredSave;                      ///< The save restore() continued from
	bool _restored;
	bool _restorePending;                              ///< restore() set the save aside for SetStartView, which has not run yet
	Common::HashMap<uint32, Common::String> _strings;  ///< VCString values that changed
	Common::HashMap<uint32, ItemText> _itemTexts;
	uint32 _focus;
	uint32 _currNodeVariable;                  ///< RegCurrNode
	bool _boxShown;                            ///< The open box is shown (hidden while one of its topics plays)
	uint32 _ideaTopic;                         ///< The topic PlayItemTopic queued, deactivated when its clips end (0 for none)
	Common::Array<uint32> _shanksVars[3];      ///< RegShanksNode2, 3 and 6: their variables, in argument order
	Common::Array<uint32> _nodeVars[7];        ///< RegNode2..6Vars, RegLink2/3Vars: their variables, in argument order
	uint32 _viewSpecialVariable;               ///< RegViewSpecial: iCurrViewSpecial
	bool _clickOpensBox;                       ///< See opensBoxOnClick()
	uint32 _heldVariable;                      ///< RegCurrInvVar: iCurrInventory
	int32 _heldNone;                           ///< Its Inventory_None value
	uint32 _gunValue;                          ///< The inventory value OnGunClick set (0 until the gun is taken)
	uint32 _heldCursorItem;                    ///< Inv_Held() + 4 (0x6b8974): the VCInventory ID whose cursor is held
	int _binocularsMode;                       ///< Mode out of the held binoculars object (+0x10C); the drawing is B-005
	bool _binocularsHeld;                      ///< The binoculars' held object (0x51baf0) exists: putBack() runs its view change back
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
		uint32 end;     ///< Out point override in ms, 0 for none
		bool loop;      ///< Plays until stopped
		uint serial;    ///< _fireSerial of the event that queued it
		bool viewWait;  ///< Queued before a held view was entered but not started: the view waits for it
		bool activated; ///< Its Object Activation has fired already (at queueing, see queueVideo())
		bool viewLoop;  ///< A piece of the view's loop: its asset is the view's, so it fires no Activation or Deactivation
	};
	Common::Array<VideoPlay> _videoPlays;      ///< Per queued video
	bool _videoNextPending;                    ///< The queue's first video has not started yet (a video just ended)
	/** A sound's play held back by a video (see playAsset()). */
	struct HeldSound {
		uint32 id;
		byte mode;
		bool positional;
		int x, y;
	};
	Common::Array<HeldSound> _heldSounds;      ///< Sounds waiting for the videos to end
	uint32 _prefVariables[kPrefCount];         ///< The variable RegPreferences gave each preference, or 0
	uint32 _prefArtificialIntuition;           ///< The variable RegPreferences gives Pref_bArtificialIntuition, or 0
	uint32 _cookEmailCount;                    ///< The variable RegCookEmail gave iNumWillmoreEMailsToCook, or 0
	uint32 _cookEmailFlag;                     ///< The variable it gave IFL_bPDAEmailedCookAboutNSA, or 0
	uint32 _uberParanoia;                      ///< The variable RegUberVars gives iUberParanoia, or 0

	// The Smolnikoff shootout: exe globals a save does not hold (0x6b1ef0..0x6b1f10, 0x6be8a4/7cc/834)
	uint32 _shootVars[3];                      ///< The variables InitSmShootout was given (left, center, right goon dead)
	int _shootIdx[3];                          ///< Next index into each goon's view table, at most 2
	bool _shootDead[3];                        ///< Goon shot
	int _shootCur;                             ///< Goon on screen, 0 for none
	int _shootPerm[3][3];                      ///< Permutations of 1, 2, 3 the reset fills (no reader known)
	bool _fbiShot;                             ///< GetPlayFBIShot gives 1 once per process
	bool _endGamePending;                      ///< EndGame ran and waits for the engine's answer
	uint32 _endGameVar;                        ///< Its variable (bReplay)
	int32 _endGameValue;                       ///< and the value it had

	/** FixJumpCut(first, later) of a topic's Object Activation (0x47ca30). */
	struct JumpCut {
		bool first;   ///< The view's loop before the first clip after the question
		bool later;   ///< The view's loop before the answer when a clip came before it
	};
	Common::HashMap<uint32, JumpCut> _jumpCuts;
	JumpCut _jumpCut;                          ///< Of the topic being played
	struct TopicClip {
		uint32 id;
		uint32 limit;   ///< 750 for a piece of the view loop, 0 for an asset
		uint32 end;     ///< Out point override after a merge
	};
	bool _planning;                            ///< playTopic collects its clips in _topicClips
	Common::Array<TopicClip> _topicClips;
	uint32 _nextVideoEnd;                      ///< For queueVideo() while a merged clip is played
	bool _clipsActivated;                      ///< mergeTopicClips() fired the clips' Object Activation: queueing and playing skip it
	bool _clipQueued;                          ///< A clip after the question is queued (topic +0x150)
	uint32 _node, _location, _viewPoint, _view;
	Common::Array<uint32> _viewObjects;        ///< Activated with the view, in activation order
	Common::HashMap<uint32, uint> _assetActive; ///< Asset ID -> users of its asset object, see activateAsset()
	uint _fireSerial;                          ///< Counts the events fired from outside (see _viewSerial)
	bool _inEvent;                             ///< An event spans several fire() calls: they share a _fireSerial
	uint _viewSerial;                          ///< _fireSerial of the event that held the view: the videos it waits for
	uint _fireDepth;                           ///< Nesting of fire(): a pending travel runs when it returns to 0
	Common::Array<uint32> _pendingStops;       ///< Stopped sounds and clips whose Object Deactivation waits for the event's end
	struct ActionFrame {
		const Common::Array<uint32> *actions;
		uint next; ///< Index of the next action to run
		ActionFrame(const Common::Array<uint32> *a = nullptr, uint n = 0) : actions(a), next(n) {}
	};
	Common::Array<ActionFrame> _actionFrames;  ///< The running action lists, outermost first (isViewChangeQueued)
	uint32 _nextId;                            ///< The database's next free object ID (new VCStrings take it)
	bool _navViewQueued;                       ///< A nav click is running: its view change is queued (0x46f210)
	bool _travelPending;                       ///< SwapDisc was called in this event (0x6b19a8)
	uint32 _travelTo[4];                       ///< Node, location, viewpoint and view of the pending travel
	bool _viewHeld;                            ///< The view is entered but not activated until the video ends
	bool _viewActive;                          ///< The view's activation has run
	bool _viewGone;                            ///< A gun click already deactivated the view (hotClick): enter() skips it
	bool _enterDeactivating;                   ///< enter() runs the old path's deactivations (AlienBlast drops the queued travel)
	bool _travelDropped;                       ///< AlienBlast ran in those deactivations: enter() activates nothing (B-204)
	bool _viewLeaving;                         ///< deactivateView() runs: a Set View in it is deferred (B-205)
	bool _leavePending;                        ///< A Set View ran while the view was left: its path waits in _leavePath
	uint32 _leavePath[4];                      ///< Node, location, viewpoint and view of that Set View
	uint32 _viewStateVariable;                 ///< The variable RegViewState registered; hotAction writes letters into it
	bool _viewAssetHeld;                       ///< The view's new asset is activated when the video ends
};

} // End of namespace XFiles

#endif // XFILES_SCRIPT_H
