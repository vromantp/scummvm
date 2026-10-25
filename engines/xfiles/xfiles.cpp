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

#include "audio/mixer.h"
#include "common/config-manager.h"
#include "common/debug-channels.h"
#include "common/events.h"
#include "common/file.h"
#include "common/algorithm.h"
#include "common/list.h"
#include "common/formats/winexe.h"
#include "common/system.h"
#include "common/tokenizer.h"
#include "common/translation.h"
#include "engines/util.h"
#include "graphics/font.h"
#include "graphics/fontman.h"
#include "graphics/fonts/ttf.h"
#include "graphics/pixelformat.h"
#include "gui/message.h"

#include "xfiles/xfiles.h"
#include "xfiles/console.h"
#include "xfiles/movie.h"
#include "xfiles/pictures.h"
#include "xfiles/script.h"
#include "xfiles/vcdata.h"

namespace XFiles {

XFilesEngine *g_engine;

enum {
	kScreenWidth = 640,
	kScreenHeight = 480,
	// The view band: navigation frames and videos are 600x240 at (20, 90);
	// hotspot rectangles are in screen coordinates
	kBandLeft = 20,
	kBandTop = 90,
	kBandWidth = 600,
	kBandHeight = 240,
	// Two clicks at most this far apart in time and space make a double-click
	// (the Windows defaults)
	kDoubleClickMs = 500,
	kDoubleClickDistance = 4
};

// The X corner, XCorner_ctor's rectangle (right and bottom exclusive)
static const int kMenuCornerLeft = 501, kMenuCornerBottom = 34;
// Its cursor, the VCCursor named "X"
static const uint32 kXCursor = 25541;

XFilesEngine::XFilesEngine(OSystem *syst, const ADGameDescription *gameDesc) : Engine(syst),
	_gameDescription(gameDesc), _randomSource("xfiles"), _data(nullptr), _script(nullptr), _menuX(nullptr), _menuEntries(nullptr), _menuClick(nullptr), _menuLoop(nullptr), _menuLoopSound(nullptr), _menuTop(nullptr), _viewfinder(nullptr), _counterFont(nullptr), _counterFontOwned(false), _menuReturn(nullptr), _menuIntroDone(false), _menuFading(false), _menuOpen(false), _menuLayout(false), _menuReopen(false), _paused(false), _pauseDepth(0), _pauseStart(0), _pausedTotal(0), _video(nullptr), _videoStarted(0), _viewId(0), _hotMovie(nullptr), _shots(0), _shotDue(0), _shotId(0), _shotView(0), _shotBox(false), _nextStep(0), _waitVideo(0), _waitTimer(-1), _dialogBoxView(0),
	_textsDll(nullptr), _texts(nullptr), _interface(nullptr), _cursors(nullptr), _lastClickTime(0), _dragIdea(0), _charHover(0), _dragMoved(false), _timersPaused(false), _barRevealed(false), _barRevealEnd(0), _sound(_discFiles) {
	g_engine = this;
	addHooks();
	_loadedSave = false;
	_checkpointNew = false;
	_gameEnded = false;
	_stepAnswer = -1;
	_pendingClipTime = -1;
	memset(_uberVariables, 0, sizeof(_uberVariables));
	_lastClickHit.kind = Interface::kHitNone;
	_lastClickHit.id = 0;
	_hover = _lastClickHit;
}

XFilesEngine::~XFilesEngine() {
	_sound.clear();
	closeMenu();
	closeView();
	delete _interface;
	delete _cursors;
	delete _video;
	delete _viewfinder;
	for (uint i = 0; i < _photoPictures.size(); i++) {
		if (_photoPictures[i].surface) {
			_photoPictures[i].surface->free();
			delete _photoPictures[i].surface;
		}
	}
	if (_counterFontOwned)
		delete _counterFont;
	delete _texts;
	delete _textsDll;
	delete _script;
	delete _data;
	_screen.free();
}

bool XFilesEngine::loadDatabases(bool fromSave) {
	Common::File hdb, gam;
	if (!hdb.open("XFiles.hdb"))
		return false;
	// xfiles_save names an original .X save: it takes the place of XFiles.gam
	// (it is a copy of it) and the game continues from it
	Common::Path savePath = fromSave ? ConfMan.getPath("xfiles_save") : Common::Path();
	Common::SeekableReadStream *saveStream = nullptr;
	if (!savePath.empty()) {
		saveStream = Common::FSNode(savePath).createReadStream();
		if (!saveStream) {
			warning("Cannot open the save %s", savePath.toString().c_str());
			return false;
		}
	} else if (!gam.open("XFiles.gam")) {
		return false;
	}
	_data = new VCData();
	bool loaded = _data->load(hdb, saveStream ? *saveStream : (Common::SeekableReadStream &)gam);
	delete saveStream;
	if (!loaded)
		return false;
	_loadedSave = !savePath.empty() && _data->getSave(_save);
	if (!savePath.empty() && !_loadedSave) {
		warning("%s is not a save", savePath.toString().c_str());
		return false;
	}
	debugC(1, kDebugDatabase, "Loaded %u classes from XFiles.hdb, %u from XFiles.gam",
		_data->getHdb().getClasses().size(), _data->getGam().getClasses().size());
	return true;
}

Common::Error XFilesEngine::run() {
	_discFiles.init(Common::FSNode(ConfMan.getPath("path")));
	if (!loadDatabases(true))
		return Common::Error(Common::kReadingFailed, "XFiles.hdb / XFiles.gam");

	// A scripted run's registry is fixed (B-207): the setting xfiles_prefs, not the player's settings. It is read
	// here because Auto Set Depth picks the screen format
	if (!ConfMan.get("xfiles_steps").empty())
		_preferences.setScripted(ConfMan.get("xfiles_prefs"));
	// The exe draws at the screen depth: 32 bits in its Quality configuration, 16 with Auto Set Depth L. ScummVM has
	// no desktop depth to keep, so H, N and A take the best depth the backend offers (B-217). The list overload of
	// initGraphics takes the backend's first format that the list holds, so the order here would not count: the
	// preferred format the backend lists is picked here. The OpenGL backend lists ARGB8888 but no XRGB8888
	Common::List<Graphics::PixelFormat> wanted;
	if (!_preferences.wantsReducedDepth()) {
		wanted.push_back(Graphics::PixelFormat(4, 8, 8, 8, 0, 16, 8, 0, 0));
		wanted.push_back(Graphics::PixelFormat::createFormatARGB32());
	}
	wanted.push_back(Graphics::PixelFormat(2, 5, 6, 5, 0, 11, 5, 0, 0));
	const Common::List<Graphics::PixelFormat> supported = g_system->getSupportedFormats();
	Graphics::PixelFormat format = wanted.back();
	for (const auto &candidate : wanted) {
		if (Common::find(supported.begin(), supported.end(), candidate) != supported.end()) {
			format = candidate;
			break;
		}
	}
	initGraphics(kScreenWidth, kScreenHeight, &format);
	_screen.create(kScreenWidth, kScreenHeight, g_system->getScreenFormat());
	setDebugger(new Console());

	_cursors = new Cursors(*_data, _discFiles);
	_cursors->init(_screen.format);
	_captions.loadFonts();
	_interface = new Interface(*_data, _discFiles);
	_interface->setFormat(_screen.format);
	if (!_dialogBox.load(*_data, _discFiles, _screen.format))
		warning("Cannot load the dialog box pictures from x.pff");
	// The language DLL with the topic texts (the exe loads it with LoadLibrary)
	_textsDll = _discFiles.openFile(0, "XFilesT.dll");
	if (!_textsDll)
		_textsDll = _discFiles.openFile(1, "XFilesT.dll");
	if (_textsDll)
		_texts = Common::WinResources::createFromEXE(_textsDll);
	if (!_texts)
		warning("Cannot load XFilesT.dll; topics show their authoring names");
	_interface->init(_texts);
	_headCredits.load(_discFiles);
	_subliminal.load(_discFiles);

	_shotsDir = ConfMan.getPath("xfiles_shots");
	_steps = Common::StringTokenizer(ConfMan.get("xfiles_steps")).split();
	if (!_steps.empty()) {
		_mouse = Common::Point(320, 240); // tools/ref/run.ps1 Start-Game: move 320 240
	}
	syncSoundSettings();
	_fastForward = Common::StringTokenizer(ConfMan.get("xfiles_fastforward")).split();

	_video = new Movie();
	// -x / --save-slot: load that save before the intro, and skip the intro and the menu
	int saveSlot = ConfMan.getInt("save_slot");
	if (saveSlot >= 0) {
		Common::Error status = loadGameState(saveSlot);
		if (status.getCode() != Common::kNoError)
			warning("Cannot load save slot %d: %s", saveSlot, status.getDesc().c_str());
	}
	// A step list starts the game at once, without the intro and the menu. The exe has shown the menu's fade-in by
	// then, so the X corner opens the still menu (g_menuIntroDone)
	bool play;
	if (_pendingLoad.valid || !_steps.empty() || !_fastForward.empty() || _loadedSave) {
		_menuIntroDone = true;
		play = true;
	} else {
		play = playIntro() && runMenu();
	}
	// EndGame without a Try again takes the game down to the start-up menu (B-176); a scripted run has nobody there
	while (play && !shouldQuit()) {
		_gameEnded = false;
		runGame();
		play = _gameEnded && _steps.empty() && !shouldQuit() && runMenu();
	}
	return Common::kNoError;
}

// -- Screen and input ---------------------------------------------------------

XFilesEngine::Input XFilesEngine::pollInput() {
	Input input;
	input.click = false;
	input.skip = false;
	input.escape = false;
	input.rightClick = false;
	input.release = false;
	Common::Event event;
	while (g_system->getEventManager()->pollEvent(event)) {
		if (event.type == Common::EVENT_LBUTTONDOWN && _steps.empty()) {
			input.click = true;
			input.pos = event.mouse;
		} else if (event.type == Common::EVENT_LBUTTONUP && _steps.empty()) {
			input.release = true;
			input.releasePos = event.mouse;
			if (_script)
				_script->release();
		} else if (event.type == Common::EVENT_MOUSEMOVE && _steps.empty()) {
			// A step list (xfiles_steps) keeps the mouse where a reference
			// run puts it, whatever the real mouse does
			_mouse = event.mouse;
			if (_dragIdea && (ABS(_mouse.x - _dragStart.x) > 3 || ABS(_mouse.y - _dragStart.y) > 3))
				_dragMoved = true;
		} else if (event.type == Common::EVENT_RBUTTONDOWN) {
			// The exe ends the playing video on a right-click as on a natural end
			input.skip = true;
			input.rightClick = _steps.empty();
			input.rightPos = event.mouse;
		} else if (event.type == Common::EVENT_KEYDOWN && event.kbd.keycode == Common::KEYCODE_ESCAPE) {
			input.skip = true;
			input.escape = true;
		} else if (event.type == Common::EVENT_KEYDOWN && _script && _steps.empty()) {
			uint key = getKeyValue(event.kbd);
			if (key)
				_script->keyPress(key);
		}
	}
	return input;
}

uint XFilesEngine::getKeyValue(const Common::KeyState &kbd) {
	// The exe posts a Key Press for the key-down of Backspace, Tab, Return and Delete (virtual-key codes), and
	// otherwise for the character of WM_CHAR (docs/subsystems/computers.md, "Text entry")
	switch (kbd.keycode) {
	case Common::KEYCODE_BACKSPACE:
		return 0x08;
	case Common::KEYCODE_TAB:
		return 0x09;
	case Common::KEYCODE_RETURN:
	case Common::KEYCODE_KP_ENTER:
		return 0x0d;
	case Common::KEYCODE_DELETE:
		return 0x2e;
	default:
		break;
	}
	if (kbd.ascii >= 0x20 && kbd.ascii < 0x7f)
		return kbd.ascii;
	// The numpad without an ascii value
	if (kbd.keycode >= Common::KEYCODE_KP0 && kbd.keycode <= Common::KEYCODE_KP9)
		return '0' + (kbd.keycode - Common::KEYCODE_KP0);
	if (kbd.keycode == Common::KEYCODE_KP_MINUS)
		return '-';
	return 0;
}

void XFilesEngine::present() {
	g_system->copyRectToScreen(_screen.getPixels(), _screen.pitch, 0, 0, _screen.w, _screen.h);
	g_system->updateScreen();
	g_system->delayMillis(10);
}

void XFilesEngine::blit(const Graphics::Surface &src, int x, int y) {
	if (!src.getPixels())
		return;
	Common::Rect r(x, y, x + src.w, y + src.h);
	r.clip(Common::Rect(_screen.w, _screen.h));
	if (r.isEmpty())
		return;
	_screen.copyRectToSurface(src, r.left, r.top, Common::Rect(r.left - x, r.top - y, r.right - x, r.bottom - y));
}

Common::Point XFilesEngine::getBandPosition(const Movie &movie) const {
	return Common::Point(kBandLeft + (kBandWidth - (int)movie.getWidth()) / 2, kBandTop + (kBandHeight - (int)movie.getHeight()) / 2);
}

uint32 XFilesEngine::hotKindAt(const Common::Point &pos) const {
	// Kind 29 objects test the point moved by the band origin (HitTestObject 0x519e40, 0x51ad80), before every
	// view object, and the first live one takes the click and the cursor even when nothing happens (B-031)
	if (!_hotMovie || _hotMovie->hasEnded())
		return 0;
	Common::Point band = getBandPosition(*_hotMovie);
	const HotEntry *e = _hot.find(_hotMovie->getTime(), Common::Point(pos.x - band.x, pos.y - band.y));
	return e ? e->kind : 0;
}

bool XFilesEngine::openMovie(Movie &movie, uint32 assetId) {
	VCAsset asset;
	if (!_data->getAsset(assetId, asset)) {
		warning("#%u is not a media asset", assetId);
		return false;
	}
	return movie.open(_discFiles, asset);
}

// -- Pause and the game clock -------------------------------------------------

uint32 XFilesEngine::gameMillis() const {
	return (_paused ? _pauseStart : g_system->getMillis()) - _pausedTotal;
}

void XFilesEngine::pauseGame(bool pause) {
	// Scene_SuspendAll: the timers (Timers_Pause), the scene's objects and
	// the game's timed objects stop, so everything goes on where it was. The menu and
	// the GMM (pauseEngineIntern) can both ask: the game runs again when
	// the last one resumes
	if (pause) {
		if (_pauseDepth++ > 0)
			return;
	} else {
		if (_pauseDepth == 0 || --_pauseDepth > 0)
			return;
	}
	if (pause) {
		_pauseStart = g_system->getMillis();
	} else {
		uint32 paused = g_system->getMillis() - _pauseStart;
		_pausedTotal += paused;
		debugC(1, kDebugScript, "-- game resumed after %u ms", paused);
	}
	_paused = pause;
	if (_video)
		_video->pause(pause);
	for (uint i = 0; i < _layers.size(); i++)
		_layers[i]->pause(pause);
	_sound.pause(pause);
}

void XFilesEngine::pauseEngineIntern(bool pause) {
	Engine::pauseEngineIntern(pause);
	if (pause)
		debugC(1, kDebugScript, "-- game paused by ScummVM");
	// Before a game runs there is nothing to pause
	pauseGame(pause);
}

// -- The game -----------------------------------------------------------------

void XFilesEngine::createScript() {
	_script = new Script(*_data);
	_preferences.setScript(_script);
	_script->setHooks(this);
	_sound.attach(_data, _script);
	_script->setTexts(_texts);
	_script->setRandomSeed(_randomSource.getRandomNumber(0xFFFFFFFF));
}

void XFilesEngine::startNewGame() {
	// A new game starts from XFiles.gam, which allows the head credits
	// (XFiles.exe Game_Start 0x48cd70)
	_headCredits.setEnabled(true);
	flushPendingIcons(true);
	// No commit yet: Try again restarts from XFiles.gam
	_checkpointNew = true;
	_checkpoint = PendingLoad();
	_script->start();
}

void XFilesEngine::restoreGame(const VCSave &save) {
	flushPendingIcons(true);
	_script->restore(save);
	// The view's clip plays from the saved time (0x51d200), the head
	// credits roll again if they were rolling, from the last credit
	// shown (0x4a80a0); the dialog box that was open is created again
	if (save.clip)
		_pendingClipTime = save.clipTime;
	if (save.credits != -1) {
		_headCredits.setEnabled(true);
		_headCredits.roll(gameMillis(), getPreference(kPrefSkipIntro), save.credits);
	}
	VCCharView boxView;
	if (save.dialogBox && _data->getCharView(save.dialogBox, boxView))
		openDialogBox(boxView);
}

void XFilesEngine::runGame() {
	if (_pendingLoad.valid) {
		// A save chosen before the game (the start-up menu, save_slot)
		applyPendingLoad();
		if (!_script)
			return;
	} else {
		createScript();
		if (_loadedSave)
			restoreGame(_save);
		else
			startNewGame();
	}
	// Fast-forward (xfiles_fastforward): the steps run in the script alone,
	// as vc_sim.py runs them, so nothing plays and no time passes; the
	// game goes on from where they end
	for (uint i = 0; i < _fastForward.size(); i++)
		_script->step(_fastForward[i]);
	_fastForward.clear();
	while (!shouldQuit()) {
		// A load queued by loadGameStream (from the global main menu, from the
		// steps) runs here, between two frames
		if (_pendingLoad.valid)
			applyPendingLoad();
		if (_script->isEndGamePending() && handleEndGame()) {
			if (_gameEnded)
				return;
			continue;
		}
		Input input = pollInput();
		if (_menuReopen)
			reopenMenu();
		// The X corner and Escape open the menu, not during a video
		if (input.escape && _menuLayout)
			escapeMenuLayout();
		else if (input.escape && !_menuOpen && _script->getVideoQueue().empty())
			openGameMenu();
		if (_menuOpen) {
			updateMenu();
			if (input.click)
				menuChoice(clickMenu(input.pos));
			if (_menuOpen) {
				_cursors->show(0);
				_cursors->update(getPreference(kPrefTranslucency));
				drawMenu();
			}
			present();
			updateShots();
			runSteps();
			continue;
		}
		updateTimers();
		_sound.update(getPreference(kPref3DSound));
		_captions.update(gameMillis());
		_headCredits.update(gameMillis(), _script->getVideoQueue().empty() && !_dialogBox.isOpen());
		// The words show over a view, the dialog box or the location card,
		// not over a video alone
		_subliminal.update(gameMillis(), *_script, getUberVariable(0), getUberVariable(1), getUberVariable(2),
			_script->getVideoQueue().empty() || _captions.hasCard());
		bool still = !_script->getVideoQueue().empty() && isStillVideo(_script->getVideoQueue()[0]);
		if (!_script->getVideoQueue().empty() && !still) {
			playQueuedVideo(input);
		} else {
			// A queued still is shown over the view, which stays live (asset-play.md)
			if (still)
				updateQueuedStill();
			else if (_video->isOpen())
				_video->close(); // Stopped by the script
			syncView();
			updateInterface();
			updateDialogBox();
			if (input.click)
				handleClick(input.pos);
			if (input.rightClick)
				handleRightClick(input.rightPos);
			if (input.release)
				handleRelease(input.releasePos);
			updateCursor();
			drawView();
			// The folders follow Pref_bTranslucency (Prefs_Apply 0x452d70)
			_dialogBox.draw(_screen, _mouse, !getPreference(kPrefTranslucency));
			_sound.drawClips(_screen);
		}
		// The location card is a timed overlay on the suspended scene: it would sit over Options and Help
		if (!_menuLayout)
			_captions.drawCard(_screen);
		_headCredits.draw(_screen);
		_subliminal.draw(_screen);
		present();
		updateShots();
		runSteps();
	}
}

void XFilesEngine::restartGame() {
	// New from the menu
	debugC(1, kDebugScript, "-- new game");
	if (!teardownGame())
		return;
	createScript();
	startNewGame();
}

void XFilesEngine::applyPendingLoad() {
	// Loading is Game_Start (0x48cd70) with a .X: the same teardown, then the
	// restore with the saved values over a fresh XFiles.gam
	debugC(1, kDebugScript, "-- load");
	PendingLoad load = _pendingLoad;
	_pendingLoad = PendingLoad();
	// A load is the commit Try again restarts from (the .X is copied to Temp.gam)
	_checkpoint = load;
	_checkpointNew = false;
	if (!teardownGame())
		return;
	createScript();
	_script->applySaved(load.values);
	restoreGame(load.save);
}

bool XFilesEngine::buildSave(PendingLoad &out) {
	if (!_script)
		return false;
	// An icon still waiting for the reveal's timer is saved in the bar
	flushPendingIcons(false);
	out.valid = false;
	fillSnapshot(out.save);
	_script->getSaved(out.values);
	out.valid = true;
	return true;
}

void XFilesEngine::commitCheckpoint() {
	// GameState_Save commits the working database to Temp.gam
	if (buildSave(_checkpoint))
		_checkpointNew = false;
}

bool XFilesEngine::handleEndGame() {
	// 0x4cc870: the dialog box closes; a non-zero bReplay with Pref_bAutoRewind asks "Try again?"
	bool ask = _script->getEndGameValue() != 0 && getPreference(kPrefAutoRewind);
	bool again = false;
	if (ask) {
		if (!_steps.empty()) {
			// A scripted run has nobody to answer: the answer: step does
			if (_stepAnswer < 0)
				return false;
			again = _stepAnswer != 0;
			_stepAnswer = -1;
		} else {
			_dialogBox.close();
			GUI::MessageDialog question(_("Try again?"), _("Yes"), _("No"));
			again = question.runModal() == GUI::kMessageOK;
		}
	}
	if (_dialogBox.isOpen())
		_dialogBox.close();
	_script->answerEndGame(ask);
	const char *what = !ask ? "no question" : (again ? "try again" : "no");
	debugC(1, kDebugScript, "-- end game: %s", what);
	if (again) {
		// Yes reopens Temp.gam: the last commit, or a new game when there is none
		if (_checkpoint.valid) {
			_pendingLoad = _checkpoint;
			applyPendingLoad();
		} else {
			restartGame();
		}
		return true;
	}
	// No: g_gameLoaded = 0, the start-up menu (Game_Start kind 1)
	if (teardownGame())
		_gameEnded = true;
	return true;
}

bool XFilesEngine::teardownGame() {
	// Game_Start (0x48cd70) destroys every timer, stops the head credits and
	// the subliminal words, removes the sounds and videos and opens the state
	// database again, so the game starts from the unchanged XFiles.gam
	closeMenu();
	_menuOpen = false;
	_menuLayout = false;
	_menuReopen = false;
	_timerStates.clear();
	_timersPaused = false;
	_pressedItems.clear();
	_sound.clear();
	_video->close();
	_videoStarted = 0;
	closeView();
	_dialogBox.close();
	_dialogBoxView = 0;
	_captions.reset();
	_headCredits.reset();
	_headCredits.setEnabled(false); // only a new game, or a restore with the credits rolling, allows them
	_subliminal.stop();
	_interface->setSuspended(false);
	_lastClickTime = 0;
	_dragIdea = 0;
	_lastClickHit.kind = Interface::kHitNone;
	_lastClickHit.id = 0;
	_hover = _lastClickHit;
	_pendingClipTime = -1;
	_shotDue = 0;
	_shotView = 0;
	_shotBox = false;
	_waitVideo = 0;
	_waitTimer = -1;
	// Whatever asked for the pause (the menu) is gone with the game
	_pauseDepth = _paused ? 1 : 0;
	pauseGame(false);

	_sound.attach(nullptr, nullptr);
	delete _script;
	_script = nullptr;
	_preferences.setScript(nullptr);
	delete _interface;
	_interface = nullptr;
	delete _cursors;
	_cursors = nullptr;
	if (_loadedSave) {
		// The save is a modified copy of XFiles.gam: read the original again
		delete _data;
		_data = nullptr;
		_loadedSave = false;
		if (!loadDatabases(false)) {
			warning("Cannot load XFiles.gam again");
			quitGame();
			return false;
		}
	}
	_cursors = new Cursors(*_data, _discFiles);
	_cursors->init(_screen.format);
	_interface = new Interface(*_data, _discFiles);
	_interface->setFormat(_screen.format);
	_interface->init(_texts);
	return true;
}

// -- C++ functions ------------------------------------------------------------

bool XFilesEngine::callFunction(const HookCall &call) {
	return _hookTable.run(*this, call) == kHookHandled;
}

void XFilesEngine::addHooks() {
	HookTable<XFilesEngine> &t = _hookTable;
	t.add("SaveGameState", [](XFilesEngine &e, const HookCall &c) {
		e.commitCheckpoint();
		return true;
	});
	t.add("StartVideoText", [](XFilesEngine &e, const HookCall &c) {
		e.startVideoText(c.action, c.args);
		return true;
	});
	// The layouts' buttons unshow the layout, then these create the menu object again (MainMenu_ctor)
	t.add("ReturnMainMenu", [](XFilesEngine &e, const HookCall &c) {
		e.savePreferences();
		e._menuReopen = e._menuLayout;
		return e._menuLayout;
	});
	t.add("ReturnFromHelp", [](XFilesEngine &e, const HookCall &c) {
		e._menuReopen = e._menuLayout;
		return e._menuLayout;
	});
	t.add("RegPreferences", [](XFilesEngine &e, const HookCall &c) {
		e.registerPreferences(c.action);
		return true;
	});
	t.add("RollHdCredits", [](XFilesEngine &e, const HookCall &c) {
		e._headCredits.roll(e.gameMillis(), e.getPreference(kPrefSkipIntro));
		return true;
	});
	t.add("SetStartView", [](XFilesEngine &e, const HookCall &c) {
		e.applyPreferences(false); // Game_BuildObjects 0x477940 calls Prefs_Apply 0x452b20
		return true;
	});
	t.add("RegUberVars", [](XFilesEngine &e, const HookCall &c) {
		e.registerUberVariables(c.action);
		return true;
	});

	// The inventory bar and the evidence kit; RegCurrInvVar, OnInvClick and OnIntInvClick are handled by Script
	// (iCurrInventory)
	t.add("RollOnEvKit", [](XFilesEngine &e, const HookCall &c) {
		if (!c.has(2))
			return false;
		// 0x4efda0: nothing while that item is in hand; open the kit above
		// the item's icon, or close it once the mouse has left it
		Common::Rect from;
		if ((uint32)c.args[0] == e._interface->getHeld())
			return true;
		if (c.args[1] && e._interface->getInventoryRect(c.args[0], &from))
			e._interface->openKit(from);
		else if (!c.args[1] && !e._interface->isMouseInKit())
			e._interface->closeKit();
		return true;
	});
	t.add("EvKitIconClick", [](XFilesEngine &e, const HookCall &c) {
		// 0x4efc90: the kit is taken in hand; the hidden bar closes
		e._interface->closeKit();
		if (e._preferences.variable(kPrefInventory) && e._script->getVariable(e._preferences.variable(kPrefInventory)) == 'H')
			e._interface->closeBar();
		return true;
	});
	t.add("DisplayDBox", [](XFilesEngine &e, const HookCall &c) {
		// The Script hook has opened or closed the box's script side
		if (!c.has(1))
			return false;
		VCCharView charView;
		if (c.args[0] == 0) {
			e._dialogBox.close();
			return true;
		}
		if (!e._data->getCharView(c.args[0], charView))
			return false;
		e.openDialogBox(charView, false);
		return true;
	});
	t.add("AddIFLExcept", [](XFilesEngine &e, const HookCall &c) {
		if (!c.has(1))
			return false;
		e._interface->addModalException(c.args[0]);
		return true;
	});
	t.add("ClearIFLExcept", [](XFilesEngine &e, const HookCall &c) {
		e._interface->clearModalExceptions();
		return true;
	});
	t.add("TextOn", [](XFilesEngine &e, const HookCall &c) {
		// 0x5078f0: for the head credits and the subliminal object, 0 pauses (0x46c970: stop the timers, hide),
		// else resumes (0x49fae0)
		if (!c.has(1))
			return false;
		if (c.args[0]) {
			e._headCredits.resume(e.gameMillis());
			e._subliminal.resume(e.gameMillis());
		} else {
			e._headCredits.pause();
			e._subliminal.pause();
		}
		return true;
	});
	t.add("FadeBack", [](XFilesEngine &e, const HookCall &c) {
		// 0x48a9f0 is Scene_SuspendObjects (0x40be40): a suspend flag on the view, icons, interface items and
		// inventory, which take no input meanwhile. The grey tint of a suspended object is not drawn (B-118)
		if (!c.has(1))
			return false;
		e._interface->setSuspended(c.args[0] != 0);
		return true;
	});
	t.add("FadeToBlack", [](XFilesEngine &e, const HookCall &c) {
		e.fadeToBlack();
		return true;
	});
}

void XFilesEngine::fadeToBlack() {
	// 0x4ccb50: 0x5103b0 fades the whole screen (interface bar too) to colour 0x12 (black; 500 ms, then 1000 ms in
	// 8 steps: read as start delay and length, unverified), then 2000 ms black, with the script held. The exe also
	// flags the object from 0x5196b0 meanwhile (0x4a0480, not identified): left out. Nothing undoes the black but
	// the next frame. A step list and a fast-forward have no time: the order only
	static const uint32 kStartDelay = 500;
	static const uint32 kFadeLength = 1000;
	static const uint32 kFadeSteps = 8;
	static const uint32 kBlackHold = 2000;
	debugC(1, kDebugScript, "-- fade to black");
	if (!_steps.empty() || !_fastForward.empty())
		return;
	Graphics::Surface shot;
	shot.copyFrom(_screen);
	uint32 start = g_system->getMillis();
	uint32 total = kStartDelay + kFadeLength + kBlackHold;
	uint32 lastStep = 0;
	while (!shouldQuit()) {
		uint32 elapsed = g_system->getMillis() - start;
		if (elapsed >= total)
			break;
		Common::Event event;
		while (g_system->getEventManager()->pollEvent(event)) {
		}
		uint32 step = 0; // 0: the picture as it was, kFadeSteps: black
		if (elapsed >= kStartDelay + kFadeLength)
			step = kFadeSteps;
		else if (elapsed > kStartDelay)
			step = (elapsed - kStartDelay) * kFadeSteps / kFadeLength;
		if (step != lastStep || elapsed == 0) {
			lastStep = step;
			uint32 scale = kFadeSteps - step;
			const Graphics::PixelFormat &fmt = _screen.format;
			for (int y = 0; y < shot.h; y++) {
				for (int x = 0; x < shot.w; x++) {
					byte r, g, b;
					fmt.colorToRGB(shot.getPixel(x, y), r, g, b);
					_screen.setPixel(x, y, fmt.RGBToColor(r * scale / kFadeSteps, g * scale / kFadeSteps, b * scale / kFadeSteps));
				}
			}
		}
		present();
	}
	shot.free();
}

void XFilesEngine::registerPreferences(const VCAction &action) {
	_preferences.registerVariables(action);
}

bool XFilesEngine::queryPreference(uint index, int32 &value) const {
	return _preferences.query(index, value);
}

void XFilesEngine::savePreferences() {
	_preferences.save();
}

void XFilesEngine::applyPreferences(bool resume) {
	// Prefs_Apply also sets the captions, the inventory bar and the translucency; this port reads those from the
	// preference variables where they are used.
	// Prefs_ApplyAIIcon (0x452780): Artificial Intuition on makes the AI icon if there is none; off removes it
	// and every action icon there is (their deactivation clears their enable flags, 0x4c0610). At SetStartView
	// no action icon exists yet: Game_BuildObjects (0x477940) makes them after Prefs_Apply, whatever the
	// preference
	bool intuition = _script->getPreferenceValue(kPrefArtificialIntuition) != 0;
	if (_interface->showAIIcon(intuition)) {
		_script->aiIconShown();
		_script->recheckAIIcon();
	}
	if (!intuition && resume)
		_script->removeActionIcons();
	syncSoundSettings();
	bool subliminals = getPreference(kPrefSubliminalMessages);
	if (subliminals && !_subliminal.isRunning())
		_subliminal.start(gameMillis());
	else if (!subliminals && _subliminal.isRunning())
		_subliminal.stop();
}

void XFilesEngine::applyGameSettings() {
	// ScummVM's menu closed (kSupportsChangingOptionsDuringRuntime): its captions and volumes into the game
	// (syncSoundSettings follows it)
	_preferences.syncFromScummVM();
}

void XFilesEngine::syncSoundSettings() {
	Engine::syncSoundSettings();
	// 0x469f50 sets every wave-out device to the game volume (0 to 100)
	int32 game = _preferences.gameVolume();
	static const Audio::Mixer::SoundType types[] = {
		Audio::Mixer::kPlainSoundType, Audio::Mixer::kMusicSoundType, Audio::Mixer::kSFXSoundType,
		Audio::Mixer::kSpeechSoundType
	};
	for (uint i = 0; i < ARRAYSIZE(types); i++)
		_mixer->setVolumeForSoundType(types[i], _mixer->getVolumeForSoundType(types[i]) * game / 100);
}

void XFilesEngine::registerUberVariables(const VCAction &action) {
	// RegUberVars(iUberLoss, iUberParanoia, iUberXTrack), 0x48ba50: the
	// subliminal words read them (0x468cf0)
	for (uint i = 0; i < action.args.size() && i < ARRAYSIZE(_uberVariables); i++)
		_uberVariables[i] = action.args[i].kind == kOperandVariable ? action.args[i].value : 0;
	if (_script)
		_script->registerUberParanoia(_uberVariables[1]);
}

int32 XFilesEngine::getUberVariable(uint index) const {
	return _uberVariables[index] ? _script->getVariable(_uberVariables[index]) : 0;
}

bool XFilesEngine::getPreference(uint index) const {
	return _preferences.isSet(index);
}

void XFilesEngine::startVideoText(const VCAction &action, const Common::Array<int32> &args) {
	// StartVideoText(asset, iDayNight, iCurrTime, bWithTime): the asset's
	// name is the location
	VCAsset asset;
	Common::U32String location;
	if (args.size() < 4 || !_data->getAsset(args[0], asset) || !_data->getName(asset.name, location)) {
		warning("StartVideoText: bad arguments in #%u", action.id);
		return;
	}
	_captions.startCard(Captions::formatCard(location, args[1], args[2], args[3] == 1), gameMillis());
}

// -- Sound --------------------------------------------------------------------

bool XFilesEngine::readAssetFile(uint32 id, Common::Array<byte> &data) {
	// A file asset on the hard disk or a disc (an e-mail's text, XT:<id>.xtx)
	VCAsset asset;
	if (!_data->getAsset(id, asset) || asset.files.empty())
		return false;
	Common::SeekableReadStream *file = _discFiles.openFile(asset.files[0].disc, asset.files[0].file);
	if (!file)
		return false;
	data.resize(file->size());
	bool ok = data.empty() || file->read(&data[0], data.size()) == data.size();
	delete file;
	return ok;
}

int XFilesEngine::measureText(const Common::U32String &text, int size, bool bold, bool sans) const {
	return _interface ? _interface->measureItemText(text, size, bold, sans) : -1;
}

void XFilesEngine::playAsset(uint32 id, byte mode, bool positional, int16 x, int16 y) {
	_sound.play(id, mode, positional, x, y, getPreference(kPref3DSound));
}

void XFilesEngine::stopViewAsset(uint32 id) {
	// 0x504c90: play once, then go to the segment end (else the movie end); the layer holds that frame
	for (uint i = 0; i < _layers.size(); i++) {
		if (_layers[i]->getAssetId() == id) {
			_layers[i]->stop();
			_layers[i]->showStillAtEnd();
		}
	}
}

void XFilesEngine::playPhoneTone(uint32 id) {
	_sound.playTone(id);
}

void XFilesEngine::playShutter(uint32 id) {
	// A bare movie of the camera's asset, stopped by the next shutter (no sound object)
	_sound.playTone(id);
}

uint32 XFilesEngine::getPhotoTime() const {
	if (_video && _video->isOpen())
		return _video->getTime();
	return !_layers.empty() ? _layers[0]->getTime() : 0;
}

void XFilesEngine::restoreSound(uint32 id, bool loop, uint32 startMs) {
	_sound.restore(id, loop, startMs);
}

void XFilesEngine::playClip(uint32 id, int16 x, int16 y) {
	_sound.playClip(id, x, y);
}

void XFilesEngine::stopAllSounds() {
	_sound.stopAll();
}

void XFilesEngine::pauseTimers() {
	// 0x48e640(1) on the timer list: each running timer is killed, and a
	// one-shot one keeps what was left of its delay (0x52f030; none left:
	// the delay stays). The PDA's own timers start after this and run
	uint32 now = gameMillis();
	const Common::HashMap<uint, Script::Timer> &timers = _script->getTimers();
	for (Common::HashMap<uint, Script::Timer>::const_iterator it = timers.begin(); it != timers.end(); ++it) {
		if (!_timerStates.contains(it->_key) || _timerStates[it->_key].serial != it->_value.serial)
			continue;
		TimerState &state = _timerStates[it->_key];
		uint32 elapsed = now - state.started;
		if (!state.paused && !it->_value.periodic && elapsed < state.ms)
			state.ms -= elapsed;
		state.paused = true;
	}
	_timersPaused = true;
}

void XFilesEngine::drawCaptions(const Movie &movie, const Common::Point &pos) {
	if (!getPreference(kPrefClosedCaptioning))
		return;
	// Text tracks sit below the picture (track matrix y 240), as wide as the video
	_captions.drawCaption(_screen, movie.getCaption(), pos.x, pos.y + movie.getHeight(), movie.getWidth());
}

bool XFilesEngine::isStillVideo(uint32 id) const {
	VCAsset asset;
	return _data->getAsset(id, asset) && (asset.flags & kAssetStill);
}

void XFilesEngine::updateQueuedStill() {
	// A still asset (flag 0x40) is the sound object showing the player's frame
	// until a one-shot 3000 ms timer hides it (SoundObj_OnShow 0x44f260); the
	// view and its clicks go on meanwhile, skip does nothing
	static const uint32 kStillMillis = 3000;
	uint32 id = _script->getVideoQueue()[0];
	if (!_video->isOpen() || _video->getAssetId() != id) {
		if (!openMovie(*_video, id) || !_video->showStill()) {
			_video->close();
			_script->finishVideo();
			return;
		}
		_videoStarted = gameMillis();
	}
	if (gameMillis() - _videoStarted >= kStillMillis) {
		_video->close();
		_script->finishVideo();
	}
}

void XFilesEngine::playQueuedVideo(const Input &input) {
	uint32 id = _script->getVideoQueue()[0];
	if (!_video->isOpen() || _video->getAssetId() != id) {
		closeView();
		if (!openMovie(*_video, id)) {
			_script->finishVideo();
			return;
		}
		if (uint32 end = _script->getVideoEnd())
			_video->setEnd(end);
		_video->play(_script->isVideoLooped());
		_videoStarted = gameMillis();
	}
	// A piece of the view's loop between conversation clips plays for a
	// fixed time (Script::queueViewLoop); a looping clip plays until a Stop
	// action stops it
	uint32 limit = _script->getVideoLimit();
	if ((input.skip && !_script->isVideoLooped()) || (limit && gameMillis() - _videoStarted >= limit))
		_video->stop();
	_video->update();

	clearScreen();
	Common::Point pos = getBandPosition(*_video);
	blit(_video->getFrame(), pos.x, pos.y);
	drawCaptions(*_video, pos);

	if (_video->hasEnded()) {
		_video->close();
		_script->finishVideo();
	}
}

void XFilesEngine::updateTimers() {
	// Timers run in real time; Script only knows they were started
	uint32 now = gameMillis();
	// 0x4a4fa0, after each pass of the event loop: once no layout is shown,
	// the timers the PDA paused start again (0x48e640(0))
	if (_timersPaused && _script->getLayouts().empty()) {
		for (Common::HashMap<uint, TimerState>::iterator it = _timerStates.begin(); it != _timerStates.end(); ++it) {
			if (it->_value.paused) {
				it->_value.started = now;
				it->_value.paused = false;
			}
		}
		_timersPaused = false;
	}
	const Common::HashMap<uint, Script::Timer> &timers = _script->getTimers();
	Common::Array<uint> expired, stopped;
	for (Common::HashMap<uint, Script::Timer>::const_iterator it = timers.begin(); it != timers.end(); ++it) {
		if (!_timerStates.contains(it->_key) || _timerStates[it->_key].serial != it->_value.serial) {
			TimerState state = { it->_value.serial, now, it->_value.ms, false };
			_timerStates[it->_key] = state;
		} else if (!_timerStates[it->_key].paused && now - _timerStates[it->_key].started >= _timerStates[it->_key].ms) {
			expired.push_back(it->_key);
		}
	}
	for (Common::HashMap<uint, TimerState>::const_iterator it = _timerStates.begin(); it != _timerStates.end(); ++it) {
		if (!timers.contains(it->_key))
			stopped.push_back(it->_key);
	}
	for (uint i = 0; i < stopped.size(); i++)
		_timerStates.erase(stopped[i]);

	// The phone's buttons go up 100 ms after they were pressed (0x4c8870)
	Common::Array<uint32> pressed;
	_script->takePressedItems(pressed);
	for (uint i = 0; i < pressed.size(); i++) {
		PressedItem item = { pressed[i], now };
		_pressedItems.push_back(item);
	}
	for (uint i = 0; i < _pressedItems.size(); ) {
		if (now - _pressedItems[i].since >= kButtonUpDelay) {
			uint32 id = _pressedItems[i].id;
			_pressedItems.remove_at(i);
			_script->releaseItem(id);
		} else {
			i++;
		}
	}

	for (uint i = 0; i < _delayedPlays.size(); ) {
		if ((int32)(now - _delayedPlays[i].due) >= 0) {
			uint32 asset = _delayedPlays[i].asset;
			_delayedPlays.remove_at(i);
			debugC(1, kDebugScript, "-- delayed play of #%u", asset);
			_script->playReaction(asset);
		} else {
			i++;
		}
	}

	// The reveal's timers (deferInventoryIcon): the icon joins, the bar hides
	for (uint i = 0; i < _pendingIcons.size();) {
		if ((int32)(now - _pendingIcons[i].due) >= 0) {
			uint32 id = _pendingIcons[i].asset;
			_pendingIcons.remove_at(i);
			// (an `icon:` step may have added it already)
			const Common::Array<uint32> &bar = _script->getInventory();
			if (Common::find(bar.begin(), bar.end(), id) == bar.end()) {
				debugC(1, kDebugScript, "-- icon #%u joins the bar", id);
				_script->addInventoryIcon(id);
			}
		} else {
			i++;
		}
	}
	if (_barRevealed && (int32)(now - _barRevealEnd) >= 0)
		_barRevealed = false;

	for (uint i = 0; i < expired.size(); i++) {
		debugC(1, kDebugScript, "-- timer %u expired", expired[i]);
		_timerStates.erase(expired[i]);
		if ((int)expired[i] == _waitTimer)
			_waitTimer = -1;
		// Icons are left alone: the data disables them when a choice times out
		_script->expireTimer(expired[i]);
	}
}

void XFilesEngine::syncView() {
	Common::Array<uint32> ids;
	uint32 background;
	_script->getViewAssets(ids, background);
	if (_viewId == _script->getView() && ids == _viewAssets) {
		for (uint i = 0; i < _layers.size(); i++)
			_layers[i]->update();
		return;
	}

	// A nav click's view change animates in the band (the Transitions preference)
	int mode = _script->getTransition();
	_script->clearTransition();
	bool animate = mode != Script::kTransitionCut && _viewId && _viewId != _script->getView() && canPlayTransition();
	Graphics::Surface oldBand;
	if (animate)
		oldBand.copyFrom(_screen.getSubArea(Common::Rect(kBandLeft, kBandTop, kBandLeft + kBandWidth, kBandTop + kBandHeight)));

	closeView();
	_viewId = _script->getView();
	_viewAssets = ids;
	for (uint i = 0; i < ids.size(); i++) {
		VCAsset asset;
		Movie *movie = new Movie();
		if (!_data->getAsset(ids[i], asset) || !movie->open(_discFiles, asset)) {
			delete movie;
			continue;
		}
		if (ids[i] == background)
			movie->showStill();
		else {
			// The restored clip time applies to the first player started
			movie->play(asset.flags & kAssetLoop, _pendingClipTime);
			// The clip's .HOT file: its entries open and close by the clip's time
			if (!_hotMovie && _hot.load(_discFiles, asset, _script->getPreferenceValue(kPrefActionSceneDifficulty)))
				_hotMovie = movie;
			debugC(2, kDebugScript, "-- view clip #%u from %d ms", ids[i], _pendingClipTime >= 0 ? _pendingClipTime : (int)asset.files[0].start);
			_pendingClipTime = -1;
		}
		_layers.push_back(movie);
	}
	_pendingClipTime = -1;
	if (animate) {
		for (uint i = 0; i < _layers.size(); i++)
			_layers[i]->update();
		// The new picture, drawn into the band the way drawView does
		Graphics::Surface keep;
		keep.copyFrom(_screen);
		clearScreen();
		for (uint i = 0; i < _layers.size(); i++) {
			Common::Point pos = getBandPosition(*_layers[i]);
			blit(_layers[i]->getFrame(), pos.x, pos.y);
		}
		Graphics::Surface newBand;
		newBand.copyFrom(_screen.getSubArea(Common::Rect(kBandLeft, kBandTop, kBandLeft + kBandWidth, kBandTop + kBandHeight)));
		_screen.copyFrom(keep);
		keep.free();
		playTransition(mode, oldBand, newBand);
		newBand.free();
	}
	oldBand.free();
}

bool XFilesEngine::canPlayTransition() const {
	// Screen_PlayView 0x464330: the preference is on and the held item is not a tool mode of type 3 (the camera). A
	// step list and a fast-forward have no time: they cut, so fixtures stay deterministic
	if (!_steps.empty() || !_fastForward.empty() || !getPreference(kPrefTransitions))
		return false;
	return !(_script->isHolding() && _script->getVariable(_script->getHeldVariable()) == 3);
}

/**
 * The slide (modes 1, 2; 0x5664b0) and the wipe (mode 3; 0x565fd0) in the band (docs/exe/view-transitions.md). The
 * slide moves the old picture, a 100 px edge and the new picture together by the step table (0x65ee40) per tick;
 * the wipe's two edges start at the hotspot centre and each tick move 100 px apart. The edge is a linear
 * cross-fade of old and new, the last tick's frame is the plain new view. The exe has no tick period (one tick per
 * compositor pass, about 33 ms in DOSBox), so one fixed period is used.
 */
void XFilesEngine::playTransition(int mode, const Graphics::Surface &oldBand, const Graphics::Surface &newBand) {
	static const uint32 kTransitionTick = 33;
	static const int kEdge = 100;
	static const int kSlideSteps[] = {25, 50, 100, 125, 125, 100, 50, 25};
	debugC(1, kDebugScript, "-- transition mode %d", mode);

	const Graphics::PixelFormat &fmt = _screen.format;
	Common::Point centre(kBandLeft + kBandWidth / 2, kBandTop + kBandHeight / 2);
	_script->getTransitionCentre(centre);
	int wipeX = centre.x; // the hotspot's screen x, taken as a band x (the exe does not subtract the band's left)

	uint32 start = g_system->getMillis();
	int slidePos = 0; // S, the old picture's offset
	for (int tick = 0;; tick++) {
		int left = 0, right = 0; // the wipe's edges
		bool done;
		if (mode == Script::kTransitionForward) {
			left = wipeX - kEdge - kEdge * tick;
			right = wipeX + kEdge * tick;
			done = left + kEdge <= 0 && right >= kBandWidth;
			debugC(1, kDebugScript, "-- transition wipe tick %d l=%d r=%d%s", tick, left, right, done ? " done" : "");
		} else {
			slidePos += kSlideSteps[MIN<int>(tick, ARRAYSIZE(kSlideSteps) - 1)];
			done = slidePos > kBandWidth - kEdge;
			debugC(1, kDebugScript, "-- transition slide tick %d S=%d%s", tick, slidePos, done ? " done" : "");
		}
		for (int y = 0; y < kBandHeight; y++) {
			for (int x = 0; x < kBandWidth; x++) {
				// the pixel is old[oi], new[ni], or the blend of both with the new share a (1 to 254 of 255)
				int oi = x, ni = x, a = 0;
				if (done) {
					a = 255;
				} else if (mode == Script::kTransitionForward) {
					if (x < left) {
						a = 0;
					} else if (x < left + kEdge) {
						a = 1 + (x - left) * 253 / (kEdge - 1); // old on its left, new on its right
					} else if (x < right) {
						a = 255;
					} else if (x < right + kEdge) {
						a = 1 + (kEdge - 1 - (x - right)) * 253 / (kEdge - 1);
					} else {
						a = 0;
					}
				} else {
					// Right (2): the old picture at x + S, the new one at x - (500 - S), the edge between; Left (1)
					// is its mirror image
					int sx = mode == Script::kTransitionRight ? x : kBandWidth - 1 - x;
					int nx = sx - (kBandWidth - kEdge - slidePos);
					int ox = sx + slidePos;
					if (nx < 0) {
						a = 0;
					} else if (nx < kEdge) {
						a = 1 + nx * 253 / (kEdge - 1);
					} else {
						a = 255;
					}
					if (mode != Script::kTransitionRight) {
						ox = kBandWidth - 1 - ox;
						nx = kBandWidth - 1 - nx;
					}
					oi = ox;
					ni = nx;
				}
				uint32 p;
				if (a == 0) {
					p = oldBand.getPixel(CLIP(oi, 0, kBandWidth - 1), y);
				} else if (a == 255) {
					p = newBand.getPixel(CLIP(ni, 0, kBandWidth - 1), y);
				} else {
					byte r1, g1, b1, r2, g2, b2;
					fmt.colorToRGB(oldBand.getPixel(CLIP(oi, 0, kBandWidth - 1), y), r1, g1, b1);
					fmt.colorToRGB(newBand.getPixel(CLIP(ni, 0, kBandWidth - 1), y), r2, g2, b2);
					p = fmt.RGBToColor((r2 * a + r1 * (255 - a)) / 255, (g2 * a + g1 * (255 - a)) / 255, (b2 * a + b1 * (255 - a)) / 255);
				}
				_screen.setPixel(kBandLeft + x, kBandTop + y, p);
			}
		}
		present();
		if (done)
			break;
		// The scene's sounds keep playing; the player's input is dropped
		Common::Event event;
		while (g_system->getEventManager()->pollEvent(event)) {
		}
		if (shouldQuit())
			break;
		_sound.update(getPreference(kPref3DSound));
		uint32 due = start + (tick + 1) * kTransitionTick;
		while (!shouldQuit() && (int32)(due - g_system->getMillis()) > 0)
			g_system->delayMillis(5);
	}
}

void XFilesEngine::closeView() {
	_hot.clear();
	_charHover = 0;
	_hotMovie = nullptr;
	for (uint i = 0; i < _layers.size(); i++)
		delete _layers[i];
	_layers.clear();
	_viewAssets.clear();
	_viewId = 0;
}

void XFilesEngine::updateInterface() {
	// A new item's reveal shows the hidden bar (deferInventoryIcon)
	bool hiddenBar = _preferences.variable(kPrefInventory) && _script->getVariable(_preferences.variable(kPrefInventory)) == 'H' && !_barRevealed;
	_interface->setAIGlow(_script->isHintGlowing());
	// A modal layout closing resumes the AI icon when a hint is available (0x509520); it never suspends it
	bool modal = _interface->isModal();
	if (_wasModal && !modal)
		_script->resumeAIIcon();
	_wasModal = modal;
	_script->aiSoundEndCheck();
	_interface->setAISuspended(_script->isAISuspended());
	_interface->update(*_script, _mouse, getPreference(kPrefClosedCaptioning), hiddenBar, getPreference(kPrefRolloverText));
	// Roll Into and Roll Out Of for the icons and layout items under the
	// mouse (the evidence kit icon opens the kit from these)
	Interface::Hit hover = _interface->hitTest(_mouse);
	if (hover.kind != _hover.kind || hover.id != _hover.id) {
		Interface::Hit old = _hover;
		_hover = hover;
		if (old.kind != Interface::kHitNone)
			_script->roll(old.id, false);
		if (hover.kind != Interface::kHitNone)
			_script->roll(hover.id, true);
	}
	// Roll Into and Roll Out Of for the character view under the mouse (CharView_OnRoll 0x443010: the gun,
	// handcuffs or stiletto in hand make a character with stock replies say "Don't do that")
	uint32 charHover = 0;
	if (hover.kind == Interface::kHitNone && !_interface->isModal()) {
		ViewHit viewHit = hitView(_mouse);
		if (viewHit.kind == ViewHit::kCharView)
			charHover = viewHit.id;
	}
	if (charHover != _charHover) {
		uint32 old = _charHover;
		_charHover = charHover;
		if (old)
			_script->roll(old, false);
		if (charHover)
			_script->roll(charHover, true);
	}
	// The item in hand goes back when the mouse moves into the bar
	// (InvBar_OnRoll 0x468330); a disabled one goes back in Script::enable
	if (isHolding() && _interface->hasEnteredBar())
		putBack();
}

bool XFilesEngine::isHolding() const {
	return _script->isHolding();
}

void XFilesEngine::putBack() {
	_script->putBack();
}

bool XFilesEngine::queryInventoryIcon(uint32 id, bool &hasIcon) const {
	hasIcon = _interface->hasInventoryIcon(id);
	return true;
}

void XFilesEngine::heldItemChanged(uint32 id) {
	if (id && _dialogBox.isOpen()) {
		_dialogBox.close();
		_script->closeBox();
	}
	_interface->setHeld(id);
}

bool XFilesEngine::delayPlay(uint32 asset, uint ms) {
	DelayedPlay play = { asset, gameMillis() + ms };
	debugC(1, kDebugScript, "-- play of #%u in %u ms", asset, ms);
	_delayedPlays.push_back(play);
	return true;
}

bool XFilesEngine::deferInventoryIcon(uint32 id) {
	// 0x4a1fb0: with the bar shown (the "visible" preference, the mouse
	// over it, an earlier item's reveal) the icon is appended at once;
	// else the bar is shown now, the icon is added by a 500 ms timer and
	// a second one hides the bar 1000 ms later. The save keeps the order
	// the icons joined in. A fast-forward has no time: the sim's way
	// (append, steps.py's `icon:` steps reorder)
	if (!_fastForward.empty())
		return false;
	bool hiddenBar = _preferences.variable(kPrefInventory) && _script->getVariable(_preferences.variable(kPrefInventory)) == 'H';
	if (!hiddenBar || _interface->isBarOpen())
		return false;
	uint32 now = gameMillis();
	DelayedPlay icon = { id, now + kIconJoinDelay };
	_pendingIcons.push_back(icon);
	_barRevealed = true;
	_barRevealEnd = now + kIconJoinDelay + kRevealHideDelay;
	debugC(1, kDebugScript, "-- icon #%u joins the bar in %u ms", id, kIconJoinDelay);
	return true;
}

void XFilesEngine::flushPendingIcons(bool clear) {
	const Common::Array<uint32> &bar = _script->getInventory();
	for (uint i = 0; i < _pendingIcons.size() && !clear; i++) {
		if (Common::find(bar.begin(), bar.end(), _pendingIcons[i].asset) == bar.end())
			_script->addInventoryIcon(_pendingIcons[i].asset);
	}
	_pendingIcons.clear();
	if (clear)
		_barRevealed = false;
}

void XFilesEngine::clickInventory(uint32 inventory) {
	// InvIcon_OnMouseClick (0x49c180); Script does it, heldItemChanged
	// closes the box and takes the cursor
	if (inventory == _interface->getHeld())
		return;
	_script->selectInventory(inventory);
}

/**
 * Clears the screen to black, then fills the letterbox band with the SetLBoxTint colour (the band's background:
 * it shows only where the picture or clip does not cover it). Not done: the tool modes' own band colours and the
 * caption-on band (0x6c0c40, down to y 350).
 */
void XFilesEngine::clearScreen() {
	_screen.fillRect(Common::Rect(_screen.w, _screen.h), 0);
	byte r, g, b;
	if (_script->getBandFill(r, g, b))
		_screen.fillRect(Common::Rect(kBandLeft, kBandTop, kBandLeft + kBandWidth, kBandTop + kBandHeight), _screen.format.RGBToColor(r, g, b));
}

void XFilesEngine::drawView() {
	clearScreen();
	for (uint i = 0; i < _layers.size(); i++) {
		Common::Point pos = getBandPosition(*_layers[i]);
		blit(_layers[i]->getFrame(), pos.x, pos.y);
	}
	// A queued still covers the view band; the bar and the box stay over it
	if (_video->isOpen() && !_script->getVideoQueue().empty() && isStillVideo(_script->getVideoQueue()[0])) {
		Common::Point pos = getBandPosition(*_video);
		blit(_video->getFrame(), pos.x, pos.y);
	}
	for (uint i = 0; i < _layers.size(); i++)
		drawCaptions(*_layers[i], getBandPosition(*_layers[i]));
	drawViewfinder();
	_interface->draw(_screen);
	if (_dragIdea && _dragMoved)
		_interface->drawDragged(_screen, _dragIdea, _mouse);
	drawPhotos();
}

const Graphics::Surface *XFilesEngine::getPhotoPicture(const VCPhoto &photo, int w, int h) {
	Common::String key = Common::String::format("%u/%u/%d,%d,%d,%d/%dx%d", photo.asset, photo.clipTime, photo.crop[0], photo.crop[1], photo.crop[2], photo.crop[3], w, h);
	for (uint i = 0; i < _photoPictures.size(); i++) {
		if (_photoPictures[i].key == key)
			return _photoPictures[i].surface;
	}
	PhotoPicture pic;
	pic.key = key;
	pic.surface = nullptr;
	Movie movie;
	if (openMovie(movie, photo.asset) && movie.showStillAt(photo.clipTime)) {
		const Graphics::Surface &frame = movie.getFrame();
		Common::Rect crop(photo.crop[0], photo.crop[1], photo.crop[2], photo.crop[3]);
		crop.clip(Common::Rect(frame.w, frame.h));
		if (crop.isEmpty())
			crop = Common::Rect(frame.w, frame.h);
		Graphics::Surface part = frame.getSubArea(crop);
		pic.surface = part.scale(w, h, true);
	}
	_photoPictures.push_back(pic);
	return pic.surface;
}

void XFilesEngine::drawPhotos() {
	const ComputerHooks &computer = _script->getComputerHooks();
	if (!computer.isPhotoViewerOpen())
		return;
	const Common::Array<VCPhoto> &photos = _script->getPhotos();
	const Common::Array<uint> &thumbs = computer.getPhotoThumbs();
	for (uint k = 0; k < thumbs.size(); k++) {
		VCInterfaceItem item;
		if (thumbs[k] >= photos.size() || !_data->getInterfaceItem(ComputerHooks::getPhotoSlotItem(k), item))
			continue;
		const Graphics::Surface *pic = getPhotoPicture(photos[thumbs[k]], 80, 60);
		if (pic)
			blit(*pic, item.rect.left, item.rect.top);
	}
	int enlarged = computer.getPhotoEnlarged();
	VCInterfaceItem item;
	if (enlarged >= 0 && (uint)enlarged < photos.size() && _data->getInterfaceItem(ComputerHooks::getPhotoEnlargedItem(), item)) {
		const Graphics::Surface *pic = getPhotoPicture(photos[enlarged], 160, 120);
		if (pic)
			blit(*pic, item.rect.left, item.rect.top);
	}
}

void XFilesEngine::drawViewfinder() {
	static const uint32 kViewfinderAsset = 34749;
	// System h=-13 for the font, (3, 5) inside the picture (0x4f2e50)
	static const int kCounterX = 3, kCounterY = 5, kCounterFontSize = 13;
	if (!_script || !_script->isCameraEnabled())
		return;
	if (!_viewfinder)
		_viewfinder = loadPicture(*_data, _discFiles, kViewfinderAsset, _screen.format);
	if (!_viewfinder)
		return;
	// Centred on the mouse (hotspot (160, 120) of the 320 x 241 frame), clipped to the band, not clamped (0x507e80)
	int x = _mouse.x - _viewfinder->getWidth() / 2;
	int y = _mouse.y - _viewfinder->getSurface().h / 2;
	// A sub-surface of the band clips the picture and the counter
	Graphics::Surface band = _screen.getSubArea(Common::Rect(kBandLeft, kBandTop, kBandLeft + kBandWidth, kBandTop + kBandHeight));
	x -= kBandLeft;
	y -= kBandTop;
	_viewfinder->draw(band, x, y);
	// The photos left, red, System bold 13 at the picture's corner
	if (!_counterFont) {
		_counterFont = Graphics::loadTTFFontFromArchive("LiberationSans-Bold.ttf", kCounterFontSize, Graphics::kTTFSizeModeCharacter);
		_counterFontOwned = _counterFont != nullptr;
		if (!_counterFont)
			_counterFont = FontMan.getFontByUsage(Graphics::FontManager::kBigGUIFont);
	}
	if (_counterFont) {
		Common::String left = Common::String::format("%d", 24 - _script->getPhotosOnCamera());
		_counterFont->drawString(&band, left, x + kCounterX, y + kCounterY, 16, band.format.RGBToColor(255, 0, 0));
	}
}

static bool inMenuCorner(const Common::Point &pos) {
	return pos.x >= kMenuCornerLeft && pos.x < kScreenWidth && pos.y >= 0 && pos.y < kMenuCornerBottom;
}

static bool containsPoint(const VCRect &r, const Common::Point &pos) {
	return pos.x >= r.left && pos.x < r.right && pos.y >= r.top && pos.y < r.bottom;
}

void XFilesEngine::handleClick(const Common::Point &pos) {
	Interface::Hit hit = _interface->hitTest(pos);
	uint32 now = gameMillis();
	bool doubleClick = now - _lastClickTime <= kDoubleClickMs && hit.kind != Interface::kHitNone &&
		hit.kind == _lastClickHit.kind && hit.id == _lastClickHit.id &&
		ABS(pos.x - _lastClickPos.x) <= kDoubleClickDistance && ABS(pos.y - _lastClickPos.y) <= kDoubleClickDistance;
	_lastClickTime = doubleClick ? 0 : now;
	_lastClickPos = pos;
	_lastClickHit = hit;
	if (doubleClick) {
		handleDoubleClick(hit);
		return;
	}

	if (hit.kind == Interface::kHitItem) {
		debugC(1, kDebugScript, "-- click:%u (interface item)", hit.id);
		_script->press(hit.id);
		return;
	}

	// Over Options and Help the scene stays suspended: only the layout's items take clicks
	if (_menuLayout)
		return;

	// The camera in hand takes a click in the band (kind 24 comes after the interface items, before the X corner and
	// the dialog box)
	if (hit.kind == Interface::kHitNone && !_interface->isModal() && _script->isCameraEnabled() && pos.x >= kBandLeft &&
		pos.x < kBandLeft + kBandWidth && pos.y >= kBandTop && pos.y < kBandTop + kBandHeight) {
		debugC(1, kDebugScript, "-- photo:%d,%d", pos.x, pos.y);
		_script->cameraClick(pos);
		return;
	}

	// The X corner (kind 26) comes after the interface items, before the
	// dialog box and the modal layouts
	if (hit.kind == Interface::kHitNone && !_dialogBox.contains(pos) && inMenuCorner(pos)) {
		openGameMenu();
		return;
	}

	if (_dialogBox.isOpen()) {
		// A click outside the box closes it (assumed; DisplayDBox(0) also
		// does); one on an icon goes on to the icon
		uint32 topic = 0;
		DialogBox::Hit boxHit = _dialogBox.click(pos, topic);
		if (boxHit == DialogBox::kHitTopic) {
			askTopic(topic);
			return;
		}
		if (boxHit == DialogBox::kHitInside)
			return;
		// An emotion or action icon leaves the box open (box-click-kinds); an inventory item or the kit closes it
		if (hit.kind != Interface::kHitEmotion && hit.kind != Interface::kHitAction &&
				hit.kind != Interface::kHitIdea) {
			_dialogBox.close();
			_script->closeBox();
		}
		if (hit.kind == Interface::kHitNone)
			return;
	}

	switch (hit.kind) {
	case Interface::kHitInventory:
		clickInventory(hit.id);
		return;
	case Interface::kHitEmotion:
		debugC(1, kDebugScript, "-- click:%u (emotion icon)", hit.id);
		_script->click(hit.id);
		return;
	case Interface::kHitAction:
		debugC(1, kDebugScript, "-- click:%u (action icon)", hit.id);
		_script->click(hit.id);
		return;
	case Interface::kHitEvidence:
		debugC(1, kDebugScript, "-- click:%u (evidence kit)", hit.id);
		_script->click(hit.id);
		return;
	case Interface::kHitEvidenceRow:
		debugC(1, kDebugScript, "-- click:%u (evidence row)", hit.id);
		_script->click(hit.id);
		return;
	case Interface::kHitIdea:
		// The icon is held until the button goes up: on a character it plays that character's topic, in place it plays
		// the open box's (IdeaIcon_DragEnd)
		_dragIdea = hit.id;
		_dragStart = pos;
		_dragMoved = false;
		return;
	case Interface::kHitAIIcon:
		// AIIconObj's click (0x5091d0): a glowing icon goes to its level's view (0x509aa0); without a glow the exe runs
		// the next hint-list action whose condition holds (0x5097e0)
		if (_script->isHintGlowing()) {
			debugC(1, kDebugScript, "-- click: AI icon glowing, level %d", _script->getHintGlowLevel());
			_script->clickAIIcon();
		} else {
			debugC(1, kDebugScript, "-- click: AI icon without a glow");
			_script->clickAIHint();
		}
		return;
	default:
		break;
	}
	if (_interface->isModal())
		return; // A layout has the input

	// A live .HOT entry takes the click (kind 29 objects come before the view's own), also when nothing happens
	if (uint32 kind = hotKindAt(pos)) {
		debugC(1, kDebugScript, "-- hot:%u", kind);
		_script->hotClick(kind);
		return;
	}

	ViewHit viewHit = hitView(pos);
	switch (viewHit.kind) {
	case ViewHit::kExplorable:
		debugC(1, kDebugScript, "-- click:%u (explorable)", viewHit.id);
		_script->click(viewHit.id);
		break;
	case ViewHit::kCharView: {
		debugC(1, kDebugScript, "-- click:%u (character view)", viewHit.id);
		// The view's triggers handle inventory items held over the
		// character; the exe opens the dialog box unless a reaction clip
		// plays for the item in hand
		_script->click(viewHit.id, &_mouse);
		VCCharView cv;
		if (_script->opensBoxOnClick() && _data->getCharView(viewHit.id, cv))
			openDialogBox(cv);
		break;
	}
	case ViewHit::kNav:
		_script->setClickPoint(_mouse);
		_script->step(Common::String::format("nav:%u", viewHit.id));
		break;
	default:
		break;
	}
}

XFilesEngine::ViewHit XFilesEngine::hitView(const Common::Point &pos, bool skipCharViews) const {
	ViewHit hit;
	hit.kind = ViewHit::kNone;
	hit.id = 0;
	hit.cursor = 0;
	VCView view;
	if (!_data->getView(_script->getView(), view))
		return hit;
	Common::Array<uint32> ids;
	_data->getMembers(view.explorables, ids);
	for (uint i = 0; i < ids.size(); i++) {
		VCExplorable e;
		if (_script->isEnabled(ids[i]) && _data->getExplorable(ids[i], e) && containsPoint(e.rect, pos)) {
			hit.kind = ViewHit::kExplorable;
			hit.id = ids[i];
			hit.cursor = e.cursor;
			return hit;
		}
	}
	ids.clear();
	if (!skipCharViews)
		_data->getMembers(view.characters, ids);
	for (uint i = 0; i < ids.size(); i++) {
		VCCharView cv;
		if (_script->isEnabled(ids[i]) && _data->getCharView(ids[i], cv) && containsPoint(cv.rect, pos)) {
			hit.kind = ViewHit::kCharView;
			hit.id = ids[i];
			hit.cursor = cv.cursor;
			return hit;
		}
	}
	ids.clear();
	_data->getMembers(view.navs, ids);
	for (uint i = 0; i < ids.size(); i++) {
		VCNav nav;
		VCHotSpot hotSpot;
		if (_script->isEnabled(ids[i]) && _data->getNav(ids[i], nav) && _data->getHotSpot(nav.hotSpot, hotSpot) &&
				containsPoint(hotSpot.rect, pos)) {
			hit.kind = ViewHit::kNav;
			hit.id = ids[i];
			hit.cursor = hotSpot.cursor;
			return hit;
		}
	}
	return hit;
}

void XFilesEngine::updateCursor() {
	// An object shows its VCCursor while the mouse is over it (vtable slot
	// +0x68, called on Roll Into and Roll Out Of); 0 is the default cursor
	// The firing cursor stays until the hit view is gone (the next view, see showCursorAnimation)
	if (_firingCursor && _script->getView() != _firingView)
		_firingCursor = 0;
	_cursors->show(getCursorAt(_mouse));
	_cursors->update(getPreference(kPrefTranslucency));
}

void XFilesEngine::reshowAIIcon() {
	// 0x493090: the show handler (0x509280) re-applies the glow from the level; the icon exists while Artificial
	// Intuition is on (Prefs_ApplyAIIcon); the suspension re-check (0x508e40) follows the show handler
	if (_script->getPreferenceValue(kPrefArtificialIntuition) != 0) {
		_script->aiIconShown();
		_script->recheckAIIcon();
	}
}

void XFilesEngine::showCursorAnimation(uint32 id) {
	// HotSpot_ClickAction (0x4b4a20) activates the VCCursor (slot +0x70): it is the cursor until an object under the
	// mouse shows one, in practice after the hit view is replaced
	_firingCursor = id;
	_firingView = _script->getView();
}

uint32 XFilesEngine::getCursorAt(const Common::Point &pos) const {
	if (_firingCursor)
		return _firingCursor;
	// An idea icon being dragged is the pointer: no character's lips over it
	if (_dragIdea && _dragMoved)
		return 0;
	uint32 held = isHolding() ? _interface->getHeldCursor() : 0;
	Interface::Hit hit = _interface->hitTest(pos);
	if (hit.kind == Interface::kHitItem) {
		// IfaceItem_ShowCursor (0x4469e0): the item's own, also with an item in hand
		VCInterfaceItem item;
		return _data->getInterfaceItem(hit.id, item) ? item.cursor : 0;
	}
	if (hit.kind != Interface::kHitNone)
		return 0; // Icons show the default cursor (0x46e1d0 reads a field that is always 0)
	if (_dialogBox.contains(pos))
		return 0; // Topics in the box show the default cursor (0x478940)
	if (inMenuCorner(pos))
		return kXCursor; // The X corner object (slot +0x68 looks up the VCCursor "X")
	if (_interface->isModal())
		return held;
	// With the camera in hand the viewfinder is the pointer: no object's hand over the band (seen in a window)
	if (_script->isCameraEnabled() && pos.x >= kBandLeft && pos.x < kBandLeft + kBandWidth && pos.y >= kBandTop &&
		pos.y < kBandTop + kBandHeight)
		return 0;
	// A live .HOT entry (HotSpotObj_Cursor 0x491b20, read from the exe only): the stiletto and fist cursors of some
	// kinds, else the tool cursor (Screen_SetToolCursor, group 132) with the gun in hand. With none of these the
	// exe shows no cursor of its own; what stays then is not read (B-031), so the lookup below goes on
	if (uint32 kind = hotKindAt(pos)) {
		const int32 value = _script->getHeldValue();
		const bool stiletto = value == 23;
		switch (kind) {
		case 433:
		case 461:
		case 470:
			if (stiletto)
				return 31931; // InventoryStiletto
			break;
		case 462:
			if (stiletto)
				return 58095; // ActionStiletto
			break;
		case 441:
		case 451:
		case 463:
			return 17492; // ActionFist
		default:
			break;
		}
		if (_script->isGunHeld())
			return Cursors::kToolCursorId;
	}
	ViewHit viewHit = hitView(pos);
	switch (viewHit.kind) {
	case ViewHit::kExplorable:
		// With an item in hand, an object that takes it keeps the item's
		// cursor (0x46e050), any other shows its own
		if (held && _script->isTestedOnClick(viewHit.id, _script->getHeldVariable()))
			return held;
		return viewHit.cursor;
	case ViewHit::kCharView:
		// CharView_ShowCursor (0x418520): the item's cursor is kept when the
		// character has a reaction slot for it, or a Roll Into or Mouse Click
		// trigger takes it
		if (held && (_script->hasHeldReactionSlot(viewHit.id) ||
					 _script->isTestedOnClick(viewHit.id, _script->getHeldVariable(), kEventRollInto) ||
					 _script->isTestedOnClick(viewHit.id, _script->getHeldVariable())))
			return held;
		// The view's own cursor (lips) only with an enabled topic or idea; else it sets none and the object
		// under it shows its cursor (the far Skinner shows the nav's hand, seen in the original)
		if (_script->hasTopicOrIdea(viewHit.id))
			return viewHit.cursor;
		// CharView_ShowCursor's CharTeleport lookup (0x407cd0): the cursor of the character view the click would lead
		// to, when it has a topic (the far Smolnikoff shows the lips, the far Skinner nothing)
		if (uint32 target = _script->getTeleportTarget(viewHit.id)) {
			VCCharView cv;
			if (_data->getCharView(target, cv))
				return cv.cursor;
		}
		return hitView(pos, true).cursor;
	case ViewHit::kNav:
		return viewHit.cursor; // 0x446aa0: the hotspot's cursor
	default:
		// The held item's cursor is only set by an object that takes the item (Obj_TakesHeldItem); empty
		// parts of the view show the default cursor
		return 0;
	}
}

void XFilesEngine::handleRelease(const Common::Point &pos) {
	if (!_dragIdea)
		return;
	uint32 icon = _dragIdea;
	_dragIdea = 0;
	if (!_dragMoved) {
		// A click: the box's character
		debugC(1, kDebugScript, "-- idea:%u", icon);
		_script->idea(icon);
		return;
	}
	// Dropped: the character view under the mouse takes it (a drop elsewhere does nothing)
	ViewHit viewHit = hitView(pos);
	if (viewHit.kind == ViewHit::kCharView) {
		debugC(1, kDebugScript, "-- idea:%u on character view #%u", icon, viewHit.id);
		_script->idea(icon, viewHit.id);
	}
}

void XFilesEngine::handleDoubleClick(const Interface::Hit &hit) {
	debugC(1, kDebugScript, "-- dclick:%u", hit.id);
	if (hit.kind == Interface::kHitInventory) {
		// InvIcon_OnDoubleClick (0x4a7ce0) also closes the box and the kit
		if (_dialogBox.isOpen()) {
			_dialogBox.close();
			_script->closeBox();
		}
		_interface->closeKit();
	}
	_script->doubleClick(hit.id);
}

void XFilesEngine::handleRightClick(const Common::Point &pos) {
	// A right button press on an inventory icon fires its Mouse Double-Click triggers, as the left double-click does
	// (the close-up of the book, the photos; the run logs show trigger event=1 right after msg=rdown)
	Interface::Hit hit = _interface->hitTest(pos);
	if (hit.kind == Interface::kHitInventory)
		handleDoubleClick(hit);
}

// -- Dialog box ---------------------------------------------------------------

void XFilesEngine::openDialogBox(const VCCharView &charView, bool openScript) {
	// DisplayDBox / XFiles.exe 0x481660: only for a character with topics or an enabled idea (0x436080)
	if (!_script->hasTopicOrIdea(charView.id))
		return;
	_headCredits.hide(); // DBox_OnActivate 0x4767a0
	_dialogBox.open(charView.id, Common::Rect(charView.rect.left, charView.rect.top, charView.rect.right, charView.rect.bottom));
	_dialogBoxView = _script->getView();
	debugC(1, kDebugScript, "-- dialog box of #%u", charView.id);
	if (openScript)
		_script->openBox(charView.id);
	updateDialogBox();
}

void XFilesEngine::updateDialogBox() {
	if (!_dialogBox.isOpen())
		return;
	VCCharView charView;
	if (_script->getView() != _dialogBoxView || !_script->isEnabled(_dialogBox.getCharView()) ||
			!_data->getCharView(_dialogBox.getCharView(), charView)) {
		_dialogBox.close();
		_script->closeBox();
		return;
	}
	// Enabled topics with a label: Talk lists those not asked yet, History
	// those asked (seen in a reference run of XFiles.exe)
	Common::Array<uint32> ids;
	_data->getMembers(charView.conversations, ids);
	Common::Array<DialogBox::Topic> talk, history;
	for (uint i = 0; i < ids.size(); i++) {
		VCConversation c;
		Common::Array<uint32> labels;
		VCAsset label;
		DialogBox::Topic topic;
		topic.id = ids[i];
		// The label is an asset without media naming a string of XFilesT.dll
		// (RT_STRING = 6); its authoring name is an older wording
		if (!_script->isEnabled(ids[i]) || !_data->getConversation(ids[i], c) || !c.label ||
				!_data->getMembers(c.label, labels) || labels.empty() || !_data->getAsset(labels[0], label) ||
				!_data->getName(label.name, topic.label))
			continue;
		uint32 type, string;
		if (_texts && _data->getResourceRef(labels[0], type, string) && type == 6) {
			Common::String text = _texts->loadString(string);
			if (!text.empty())
				topic.label = Common::U32String(text, Common::kWindows1252);
		}
		if (_script->getAskCount(ids[i]))
			history.push_back(topic);
		else
			talk.push_back(topic);
	}
	_dialogBox.setTopics(talk, history);
}

void XFilesEngine::askTopic(uint32 conversation) {
	debugC(1, kDebugScript, "-- ask:%u", conversation);
	_script->ask(conversation);
}

} // End of namespace XFiles
