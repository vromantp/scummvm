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

#ifndef XFILES_XFILES_H
#define XFILES_XFILES_H

#include "common/array.h"
#include "common/path.h"
#include "common/str-array.h"
#include "common/hashmap.h"
#include "common/random.h"
#include "common/rect.h"
#include "common/scummsys.h"
#include "common/error.h"
#include "common/keyboard.h"
#include "engines/engine.h"
#include "graphics/managed_surface.h"
#include "graphics/surface.h"

#include "xfiles/captions.h"
#include "xfiles/cursors.h"
#include "xfiles/detection.h"
#include "xfiles/dialogbox.h"
#include "xfiles/discfiles.h"
#include "xfiles/headcredits.h"
#include "xfiles/interface.h"
#include "xfiles/script.h"
#include "xfiles/sound.h"
#include "xfiles/subliminal.h"

namespace Common {
class WinResources;
}

namespace XFiles {

class Movie;
class PictureAsset;
class VCData;

class XFilesEngine : public Engine, public ScriptHooks {
public:
	XFilesEngine(OSystem *syst, const ADGameDescription *gameDesc);
	~XFilesEngine() override;

	bool hasFeature(EngineFeature f) const override {
		return f == kSupportsReturnToLauncher || f == kSupportsSubtitleOptions || f == kSupportsLoadingDuringRuntime ||
			f == kSupportsSavingDuringRuntime;
	}

	Common::Language getLanguage() const { return _gameDescription->language; }

	const VCData &getData() const { return *_data; }
	const DiscFiles &getDiscFiles() const { return _discFiles; }
	/** The running game's interpreter, or nullptr before New Game. */
	Script *getScript() { return _script; }
	/**
	 * The engine's GameState_Save (XFiles.exe 0x4d9450): the Script's snapshot and what only
	 * the engine knows: the timers' times, the sounds' positions, the view's clip and its
	 * time, the head credits, the open dialog box. Photos on the camera are 0 until the
	 * camera is ported. (saveload.cpp)
	 */
	void fillSnapshot(VCSave &save);

	// Saves (saveload.cpp): the original's save content in ScummVM's stream
	Common::Error saveGameStream(Common::WriteStream *stream, bool isAutosave = false) override;
	/** Parses the save and queues the load, which runs at the start of the next frame. */
	Common::Error loadGameStream(Common::SeekableReadStream *stream) override;
	/**
	 * Saving needs a running game whose view is active: no video on screen or queued, no
	 * travel or held view, no pressed item. The open in-game menu does not prevent it.
	 */
	bool canSaveGameStateCurrently(Common::U32String *msg = nullptr) override;
	/** Loading is possible whenever the databases are loaded. */
	bool canLoadGameStateCurrently(Common::U32String *msg = nullptr) override;
	/** The game screen as it was when the in-game menu opened; nullptr while the menu is not up. */
	Graphics::ManagedSurface *getMenuShot() { return _menuOpen ? &_menuShot : nullptr; }

	// ScriptHooks
	bool callFunction(const HookCall &call) override;
	void playAsset(uint32 id, byte mode, bool positional, int16 x, int16 y) override;
	void restoreSound(uint32 id, bool loop, uint32 startMs) override;
	void playPhoneTone(uint32 id) override;
	void playShutter(uint32 id) override;
	uint32 getPhotoTime() const override;
	void playClip(uint32 id, int16 x, int16 y) override;
	void stopViewAsset(uint32 id) override;
	bool readAssetFile(uint32 id, Common::Array<byte> &data) override;
	int measureText(const Common::U32String &text, int size, bool bold, bool sans) const override;
	bool queryInventoryIcon(uint32 id, bool &hasIcon) const override;
	void heldItemChanged(uint32 id) override;
	bool delayPlay(uint32 asset, uint ms) override;
	bool deferInventoryIcon(uint32 id) override;
	bool queryPreference(uint index, int32 &value) const override;
	void stopAllSounds() override;
	void pauseTimers() override;
	/** ScummVM's volumes, scaled by the game volume preference (the exe's wave-out master volume). */
	void syncSoundSettings() override;

protected:
	Common::Error run() override;
	/** The GMM and the save dialogs pause the game like the in-game menu does (nested). */
	void pauseEngineIntern(bool pause) override;

private:
	/** Input gathered by pollInput() for one frame. */
	struct Input {
		bool click;
		Common::Point pos;
		bool skip;
		bool escape;
	};

	/** The main menu's entries, in order from the top. */
	enum MenuEntry {
		kMenuNone = -1,
		kMenuNew,
		kMenuLoad,
		kMenuSave,
		kMenuPrevious,
		kMenuOptions,
		kMenuHelp,
		kMenuQuit,
		kMenuEntryCount
	};

	struct TimerState {
		uint32 serial;
		uint32 started;
		uint32 ms;   ///< Delay of this run: the timer's, or what was left when the PDA paused it
		bool paused; ///< Stopped by pauseTimers() until no layout is shown
	};

	/** Load XFiles.hdb and the game state; fromSave: xfiles_save takes the place of XFiles.gam. */
	bool loadDatabases(bool fromSave);
	Input pollInput();
	/**
	 * What a Key Press event carries (Game_KeyDown 0x458280): the virtual-key code of Backspace, Tab, Return and
	 * Delete, else the character of WM_CHAR (lowercase letters, digits, - ( ) , and so on). 0 for other keys.
	 */
	static uint getKeyValue(const Common::KeyState &kbd);
	void present();
	void blit(const Graphics::Surface &src, int x, int y);
	Common::Point getBandPosition(const Movie &movie) const;
	bool openMovie(Movie &movie, uint32 assetId);

	void runGame();
	void createScript();
	/** The new-game half of runGame (Game_Start, XFiles.exe 0x48cd70). */
	void startNewGame();
	/** Game_Start's teardown, then a fresh game from XFiles.gam. */
	void restartGame();
	/** Game_Start's teardown: the running game goes, XFiles.gam is the database again. false: it cannot be read. */
	bool teardownGame();
	/** Continue from a save: the original's restore (Script::restore) and the engine's parts of it. */
	void restoreGame(const VCSave &save);
	/** A queued load (loadGameStream): teardown, a new Script with the saved values, restore. */
	void applyPendingLoad();

	// The intro and the main menu, also the one the X corner opens during play (menu.cpp)
	bool playIntro();
	bool runMenu();
	void startMenu();
	void closeMenu();
	void updateMenu();
	void drawMenu();
	MenuEntry hitMenu(const Common::Point &pos) const;
	/** A click on the menu: the entry hit (it plays its sound), kMenuNone if none. */
	MenuEntry clickMenu(const Common::Point &pos);
	/** What an entry does while the menu is open over the game. */
	void menuChoice(MenuEntry entry);
	/** XCorner_OpenMenu (0x4ab9e0): put the item back, pause, show the menu. */
	void openGameMenu();
	/** Options and Help: hide the menu, show the layout over the paused game (the pause stays). */
	void openMenuLayout(uint32 layout);
	/** ReturnMainMenu / ReturnFromHelp, once the layout is unshown: the menu again. */
	void reopenMenu();
	/** Escape over Options or Help: Game_KeyDown fires the Mouse Click of item 50464 or 49841. */
	void escapeMenuLayout();
	/**
	 * Menu_AskSave before New and Quit, by Pref_bSaveGameOnExit: 'A' asks (yes opens the save
	 * dialog), 'Y' writes the autosave slot, 'N' does nothing. false: the player cancelled the
	 * save, so New or Quit must not go on.
	 */
	bool saveOnExit();
	/** The start-up menu's Previous: load the most recent save (autosave included). false if there is none. */
	bool loadMostRecentSave();

	/** The game's own clock: getMillis() without the time spent paused. */
	uint32 gameMillis() const;
	/** Freeze or resume the game's time, its videos and sounds. Nested: each pause needs its resume. */
	void pauseGame(bool pause);

	void playQueuedVideo(const Input &input);
	void updateTimers();
	void syncView();
	void closeView();
	void updateInterface();
	void drawView();
	/** The camera in hand: the viewfinder over the band at the mouse, with the photos left (Camera, 0x4f2b30). */
	void drawViewfinder();
	void handleClick(const Common::Point &pos);

	/** A view object under the mouse, found in the order clicks take them. */
	struct ViewHit {
		enum Kind {
			kNone,
			kExplorable,
			kCharView,
			kNav
		} kind;
		uint32 id;
		uint32 cursor;    ///< Its VCCursor (a nav's: its hotspot's)
	};

	ViewHit hitView(const Common::Point &pos) const;
	void updateCursor();
	/** The VCCursor to show for what is at a point; 0 for the default cursor. */
	uint32 getCursorAt(const Common::Point &pos) const;
	/** The second click of a double-click: Mouse Double-Click instead of Mouse Click. */
	void handleDoubleClick(const Interface::Hit &hit);
	void clickInventory(uint32 inventory);
	/** An item is in hand: iCurrInventory (RegCurrInvVar) is not Inventory_None. */
	bool isHolding() const;
	/** Put the item in hand back into the bar (Inv_PutBack, XFiles.exe 0x429f00). */
	void putBack();
	/** Fill _hookTable with the hooks the engine implements. */
	void addHooks();
	void openDialogBox(const VCCharView &charView);
	/** Close the box if its character left the view, and update its topics. */
	void updateDialogBox();
	void askTopic(uint32 conversation);
	void drawCaptions(const Movie &movie, const Common::Point &pos);
	void startVideoText(const VCAction &action, const Common::Array<int32> &args);
	void registerPreferences(const VCAction &action);
	/** ReturnMainMenu (0x490450) saves the preferences in the registry: here the ScummVM settings. */
	void savePreferences();
	/**
	 * Prefs_Apply (0x452b20): the AI icon, the volumes and the subliminal messages to the running game; resume:
	 * the menu's Previous (else SetStartView, before the icons are made).
	 */
	void applyPreferences(bool resume);
	/** A preference's value, 0 before RegPreferences. */
	bool getPreference(uint index) const;
	void registerUberVariables(const VCAction &action);
	/** Loss (0), paranoia (1) or X-track (2), 0 before RegUberVars. */
	int32 getUberVariable(uint index) const;

	const ADGameDescription *_gameDescription;
	Common::RandomSource _randomSource;
	VCData *_data;
	DiscFiles _discFiles;
	Script *_script;
	Graphics::Surface _screen;
	Captions _captions;
	HeadCredits _headCredits;
	Subliminal _subliminal;
	DialogBox _dialogBox;
	uint32 _dialogBoxView;                ///< The view the box was opened in
	Common::SeekableReadStream *_textsDll; ///< XFilesT.dll; _texts reads it lazily, so it lives as long as _texts
	Common::WinResources *_texts;         ///< XFilesT.dll: topic texts (string table)
	Common::Point _mouse;
	HookTable<XFilesEngine> _hookTable;   ///< The C++ function hooks the engine implements, by name
	Movie *_menuX;                        ///< The main menu's movies (49583 the X, 49587 the entries)
	Movie *_menuEntries;
	Movie *_menuClick;                    ///< The click sound of an entry
	Movie *_menuLoop;                     ///< 49585, the X loop (MIDloop.mov)
	Movie *_menuLoopSound;                ///< 94320, the loop's sound
	Movie *_menuTop;                      ///< 49589, the top strip (a still)
	PictureAsset *_viewfinder;            ///< 34749 moveableviewfinder.pic, loaded when the camera first shows
	PictureAsset *_menuReturn;            ///< 72895 return.pic over Previous, only from the game
	bool _menuIntroDone;                  ///< g_menuIntroDone (0x6b1980): the fade-in has played this run, never cleared
	bool _menuFading;                     ///< The X is still fading in, alone
	bool _menuOpen;                       ///< The menu is up over the running game
	bool _menuLayout;                     ///< Options or Help (layout 49738, 50460) is up over the paused game
	bool _menuReopen;                     ///< ReturnMainMenu or ReturnFromHelp ran: the menu reopens next frame
	bool _paused;
	uint _pauseDepth;                     ///< Pauses asked for (menu, GMM, dialogs)
	Graphics::ManagedSurface _menuShot;   ///< The screen when the menu opened (the saves' thumbnail)
	uint32 _pauseStart;                   ///< getMillis() when the game was paused
	uint32 _pausedTotal;                  ///< Milliseconds the game spent paused before
	uint32 _prefVariables[kPrefCount];  ///< From RegPreferences
	uint32 _uberVariables[3];           ///< iUberLoss, iUberParanoia, iUberXTrack, from RegUberVars

	Movie *_video;                       ///< The play-once video on screen
	uint32 _videoStarted;                ///< When it started (getMillis)
	Common::Array<uint32> _viewAssets;   ///< What the view layers show
	int32 _pendingClipTime;              ///< A restored save's clip time for the first view played; -1 if none
	uint32 _viewId;
	Common::Array<Movie *> _layers;      ///< Background and overlays of the view
	Interface *_interface;
	Cursors *_cursors;
	uint32 _lastClickTime;               ///< For double-clicks
	Common::Point _lastClickPos;
	Interface::Hit _lastClickHit;
	Interface::Hit _hover;               ///< What the mouse is over, for Roll Into and Roll Out Of
	Common::HashMap<uint, TimerState> _timerStates;
	bool _timersPaused;                  ///< The PDA paused the timers (0x6b1680)
	struct PressedItem {
		uint32 id;
		uint32 since;
	};
	static const uint32 kButtonUpDelay = 100;
	struct DelayedPlay {
		uint32 asset;
		uint32 due; ///< gameMillis()
	};
	Common::Array<DelayedPlay> _delayedPlays;     ///< A tool's reaction clip, 2 s after the click (Script::toolReaction)
	// The hidden bar's reveal for a new item (0x4a1fb0): its icon joins
	// after 500 ms, the bar hides again 1000 ms after that
	static const uint32 kIconJoinDelay = 500;
	static const uint32 kRevealHideDelay = 1000;
	Common::Array<DelayedPlay> _pendingIcons;     ///< Items whose icon joins the bar at `due` (asset: the VCInventory)
	bool _barRevealed;                            ///< The reveal shows the hidden bar until _barRevealEnd
	uint32 _barRevealEnd;                         ///< gameMillis()
	/** The pending icons join the bar now (before a save), or are dropped (clear: a new game, a load). */
	void flushPendingIcons(bool clear);
	Common::Array<PressedItem> _pressedItems;     ///< Phone buttons shown pressed (Script::takePressedItems)
	SoundPlayer _sound;                           ///< The sounds and placed clips (sound.cpp)

	// Comparison with reference runs of the original (the research repo's
	// docs/reference-runs.md, "Screenshots"): the xfiles_shots setting names a
	// folder for screenshots taken at the events xfref.dll takes them at,
	// xfiles_steps a step list (as Script::step takes) played from the start (refrun.cpp)
	void scheduleShot(const char *reason, uint32 id, uint32 delay);
	void updateShots();
	void saveShot();
	void runSteps();
	Common::Path _shotsDir;
	uint _shots;
	uint32 _shotDue;                      ///< getMillis() of the pending shot; 0: none
	Common::String _shotReason;
	uint32 _shotId;
	uint32 _shotView;                     ///< The view the last view shot was for
	bool _shotBox;                        ///< The dialog box was on screen at the last frame
	Common::StringArray _steps;
	bool _loadedSave;                     ///< xfiles_save: the game continues from an original .X save
	VCSave _save;
	/** A load read by loadGameStream, waiting for the frame boundary. */
	struct PendingLoad {
		PendingLoad() : valid(false) {}
		bool valid;
		Script::SavedValues values;
		VCSave save;
	};
	PendingLoad _pendingLoad;
	Common::StringArray _fastForward;     ///< xfiles_fastforward: steps run headless before the game starts (cleared after)
	uint _nextStep;
	uint32 _waitVideo;                    ///< A wait step waits for this video to end
	int _waitTimer;                       ///< A timer step waits for this timer to expire; -1: none
};

extern XFilesEngine *g_engine;

} // End of namespace XFiles

#endif // XFILES_XFILES_H
