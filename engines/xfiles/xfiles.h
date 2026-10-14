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
#include "graphics/surface.h"

#include "xfiles/captions.h"
#include "xfiles/cursors.h"
#include "xfiles/detection.h"
#include "xfiles/dialogbox.h"
#include "xfiles/discfiles.h"
#include "xfiles/headcredits.h"
#include "xfiles/interface.h"
#include "xfiles/script.h"
#include "xfiles/subliminal.h"

namespace Common {
class WinResources;
}

namespace XFiles {

class Movie;
class VCData;

class XFilesEngine : public Engine, public ScriptHooks {
public:
	XFilesEngine(OSystem *syst, const ADGameDescription *gameDesc);
	~XFilesEngine() override;

	bool hasFeature(EngineFeature f) const override {
		return f == kSupportsReturnToLauncher || f == kSupportsSubtitleOptions;
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
	 * camera is ported.
	 */
	void fillSnapshot(VCSave &save);

	// Saves (saveload.cpp): the original's save content in ScummVM's stream
	Common::Error saveGameStream(Common::WriteStream *stream, bool isAutosave = false) override;
	/** Parses the save and queues the load, which runs at the start of the next frame. */
	Common::Error loadGameStream(Common::SeekableReadStream *stream) override;

	// ScriptHooks
	bool callFunction(const VCAction &action, const Common::Array<int32> &args) override;
	void playAsset(uint32 id, byte mode, bool positional, int16 x, int16 y) override;
	void restoreSound(uint32 id, bool loop, uint32 startMs) override;
	bool readAssetFile(uint32 id, Common::Array<byte> &data) override;
	int measureText(const Common::U32String &text, int size, bool bold, bool sans) const override;
	bool queryInventoryIcon(uint32 id, bool &hasIcon) const override;
	void heldItemChanged(uint32 id) override;
	bool queryPreference(uint index, int32 &value) const override;
	void stopAllSounds() override;

protected:
	Common::Error run() override;

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
	};

	/** Load XFiles.hdb and the game state; fromSave: xfiles_save takes the place of XFiles.gam. */
	bool loadDatabases(bool fromSave);
	Input pollInput();
	/** The Windows virtual-key code of a key, 0 for keys the game does not take. */
	static uint getVirtualKey(const Common::KeyState &kbd);
	void present();
	void blit(const Graphics::Surface &src, int x, int y);
	Common::Point getBandPosition(const Movie &movie) const;
	bool openMovie(Movie &movie, uint32 assetId);

	bool playIntro();
	bool runMenu();
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

	// The main menu, also the one the X corner opens during play
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

	/** The game's own clock: getMillis() without the time spent paused. */
	uint32 gameMillis() const;
	/** Freeze or resume the game's time, its videos and sounds. */
	void pauseGame(bool pause);

	void playQueuedVideo(const Input &input);
	void updateTimers();
	void syncView();
	void closeView();
	void updateInterface();
	void drawView();
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
	bool callInventoryFunction(const VCAction &action, const Common::Array<int32> &args);
	void openDialogBox(const VCCharView &charView);
	/** Close the box if its character left the view, and update its topics. */
	void updateDialogBox();
	void askTopic(uint32 conversation);
	void drawCaptions(const Movie &movie, const Common::Point &pos);
	void startVideoText(const VCAction &action, const Common::Array<int32> &args);
	void registerPreferences(const VCAction &action);
	/** A preference's value, 0 before RegPreferences. */
	bool getPreference(uint index) const;
	void registerUberVariables(const VCAction &action);
	/** Loss (0), paranoia (1) or X-track (2), 0 before RegUberVars. */
	int32 getUberVariable(uint index) const;

	/** A playing sound asset (XS): environments, effects, music. */
	struct Sound {
		Movie *movie;
		bool positional;  ///< Played by a 3D Sound action
		int16 x, y;       ///< On the viewpoint grid
	};

	void stopSound(uint32 id);
	void updateSounds();
	/** Start a sound; startMs != 0 is its in point (where a loop goes back to). */
	void startSound(uint32 id, bool loop, bool positional, int16 x, int16 y, uint32 startMs);
	/** Volume and balance of a 3D sound for the current view. */
	void placeSound(Sound &sound);
	/** The target of a view's first nav whose hotspot has the given one's cursor; 0 if none. */
	uint32 getNavTarget(uint32 view, uint32 hotSpot) const;
	/** Position of the viewpoint of the current location that holds a view. */
	bool getViewPosition(uint32 view, Common::Point &pos) const;
	/** Where a view looks, from its navigation (XFiles.exe 0x46d7a0). */
	bool getFacing(uint32 view, const Common::Point &from, Common::Point &facing) const;

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
	Movie *_menuX;                        ///< The main menu's movies (49583 the X, 49587 the entries)
	Movie *_menuEntries;
	Movie *_menuClick;                    ///< The click sound of an entry
	bool _menuOpen;                       ///< The menu is up over the running game
	bool _paused;
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
	struct PressedItem {
		uint32 id;
		uint32 since;
	};
	static const uint32 kButtonUpDelay = 100;
	Common::Array<PressedItem> _pressedItems;     ///< Phone buttons shown pressed (Script::takePressedItems)
	Common::HashMap<uint32, Sound> _sounds;
	Common::Array<uint32> _soundOrder;    ///< The playing sounds in the order they started (a save lists them so)
	uint32 _soundView;                    ///< The view the 3D sounds were placed for
	Common::Point _soundFacing;           ///< The last facing found, kept for views without one

	// Comparison with reference runs of the original (the research repo's
	// docs/reference-runs.md, "Screenshots"): the xfiles_shots setting names a
	// folder for screenshots taken at the events xfref.dll takes them at,
	// xfiles_steps a step list (as Script::step takes) played from the start
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
	Common::StringArray _fastForward;     ///< xfiles_fastforward: steps run headless before the game starts
	uint _nextStep;
	uint32 _waitVideo;                    ///< A wait step waits for this video to end
	int _waitTimer;                       ///< A timer step waits for this timer to expire; -1: none
};

extern XFilesEngine *g_engine;

} // End of namespace XFiles

#endif // XFILES_XFILES_H
