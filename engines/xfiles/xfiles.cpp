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

#include "common/config-manager.h"
#include "common/debug-channels.h"
#include "common/events.h"
#include "common/file.h"
#include "common/formats/winexe.h"
#include "common/system.h"
#include "common/tokenizer.h"
#include "engines/util.h"
#include "graphics/pixelformat.h"

#include "xfiles/xfiles.h"
#include "xfiles/console.h"
#include "xfiles/movie.h"
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
	_gameDescription(gameDesc), _randomSource("xfiles"), _data(nullptr), _script(nullptr), _menuX(nullptr), _menuEntries(nullptr), _menuClick(nullptr), _menuLoop(nullptr), _menuLoopSound(nullptr), _menuTop(nullptr), _menuReturn(nullptr), _menuIntroDone(false), _menuFading(false), _menuOpen(false), _paused(false), _pauseDepth(0), _pauseStart(0), _pausedTotal(0), _video(nullptr), _videoStarted(0), _viewId(0), _shots(0), _shotDue(0), _shotId(0), _shotView(0), _shotBox(false), _nextStep(0), _waitVideo(0), _waitTimer(-1), _dialogBoxView(0),
	_textsDll(nullptr), _texts(nullptr), _interface(nullptr), _cursors(nullptr), _lastClickTime(0), _sound(_discFiles) {
	g_engine = this;
	addHooks();
	_loadedSave = false;
	_pendingClipTime = -1;
	memset(_prefVariables, 0, sizeof(_prefVariables));
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
	ConfMan.registerDefault("subliminal_messages", false);
	_discFiles.init(Common::FSNode(ConfMan.getPath("path")));
	if (!loadDatabases(true))
		return Common::Error(Common::kReadingFailed, "XFiles.hdb / XFiles.gam");

	Graphics::PixelFormat format(2, 5, 6, 5, 0, 11, 5, 0, 0);
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
	syncSoundSettings();

	_shotsDir = ConfMan.getPath("xfiles_shots");
	_steps = Common::StringTokenizer(ConfMan.get("xfiles_steps")).split();
	if (!_steps.empty())
		_mouse = Common::Point(320, 240); // tools/ref/run.ps1 Start-Game: move 320 240
	_fastForward = Common::StringTokenizer(ConfMan.get("xfiles_fastforward")).split();

	_video = new Movie();
	// -x / --save-slot: load that save before the intro, and skip the intro and the menu
	int saveSlot = ConfMan.getInt("save_slot");
	if (saveSlot >= 0) {
		Common::Error status = loadGameState(saveSlot);
		if (status.getCode() != Common::kNoError)
			warning("Cannot load save slot %d: %s", saveSlot, status.getDesc().c_str());
	}
	// A step list starts the game at once, without the intro and the menu
	if (_pendingLoad.valid || !_steps.empty() || !_fastForward.empty() || _loadedSave || (playIntro() && runMenu()))
		runGame();
	return Common::kNoError;
}

// -- Screen and input ---------------------------------------------------------

XFilesEngine::Input XFilesEngine::pollInput() {
	Input input;
	input.click = false;
	input.skip = false;
	input.escape = false;
	Common::Event event;
	while (g_system->getEventManager()->pollEvent(event)) {
		if (event.type == Common::EVENT_LBUTTONDOWN && _steps.empty()) {
			input.click = true;
			input.pos = event.mouse;
		} else if (event.type == Common::EVENT_LBUTTONUP && _steps.empty()) {
			if (_script)
				_script->release();
		} else if (event.type == Common::EVENT_MOUSEMOVE && _steps.empty()) {
			// A step list (xfiles_steps) keeps the mouse where a reference
			// run puts it, whatever the real mouse does
			_mouse = event.mouse;
		} else if (event.type == Common::EVENT_RBUTTONDOWN) {
			// The exe ends the playing video on a right-click as on a natural end
			input.skip = true;
		} else if (event.type == Common::EVENT_KEYDOWN && event.kbd.keycode == Common::KEYCODE_ESCAPE) {
			input.skip = true;
			input.escape = true;
		} else if (event.type == Common::EVENT_KEYDOWN && _script && _steps.empty()) {
			uint vk = getVirtualKey(event.kbd);
			if (vk)
				_script->keyPress(vk);
		}
	}
	return input;
}

uint XFilesEngine::getVirtualKey(const Common::KeyState &kbd) {
	// Key Press events carry a Windows virtual-key code
	if (kbd.keycode >= Common::KEYCODE_0 && kbd.keycode <= Common::KEYCODE_9)
		return '0' + (kbd.keycode - Common::KEYCODE_0);
	if (kbd.keycode >= Common::KEYCODE_KP0 && kbd.keycode <= Common::KEYCODE_KP9)
		return 0x60 + (kbd.keycode - Common::KEYCODE_KP0); // VK_NUMPAD0
	if (kbd.keycode >= Common::KEYCODE_a && kbd.keycode <= Common::KEYCODE_z)
		return 'A' + (kbd.keycode - Common::KEYCODE_a);
	if (kbd.keycode == Common::KEYCODE_BACKSPACE)
		return 0x08;
	if (kbd.keycode == Common::KEYCODE_TAB)
		return 0x09;
	if (kbd.keycode == Common::KEYCODE_SPACE)
		return 0x20;
	if (kbd.keycode == Common::KEYCODE_DELETE)
		return 0x2e;
	if (kbd.keycode == Common::KEYCODE_RETURN || kbd.keycode == Common::KEYCODE_KP_ENTER)
		return 0x0d;
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
	// Scene_SuspendAll: the media pause (Media_PauseAll) and the game's
	// timed objects stop, so everything goes on where it was. The menu and
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
	_script->setHooks(this);
	_sound.attach(_data, _script);
	_script->setTexts(_texts);
	_script->setRandomSeed(_randomSource.getRandomNumber(0xFFFFFFFF));
}

void XFilesEngine::startNewGame() {
	// A new game starts from XFiles.gam, which allows the head credits
	// (XFiles.exe Game_Start 0x48cd70)
	_headCredits.setEnabled(true);
	_script->start();
}

void XFilesEngine::restoreGame(const VCSave &save) {
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
	while (!shouldQuit()) {
		// A load queued by loadGameStream (from the global main menu, from the
		// steps) runs here, between two frames
		if (_pendingLoad.valid)
			applyPendingLoad();
		Input input = pollInput();
		// The X corner and Escape open the menu, not during a video
		if (input.escape && !_menuOpen && _script->getVideoQueue().empty())
			openGameMenu();
		if (_menuOpen) {
			updateMenu();
			if (input.click)
				menuChoice(clickMenu(input.pos));
			if (_menuOpen) {
				_cursors->show(0);
				_cursors->update();
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
		if (!_script->getVideoQueue().empty()) {
			playQueuedVideo(input);
		} else {
			if (_video->isOpen())
				_video->close(); // Stopped by the script
			syncView();
			updateInterface();
			updateDialogBox();
			if (input.click)
				handleClick(input.pos);
			updateCursor();
			drawView();
			_dialogBox.draw(_screen, _mouse);
			_sound.drawClips(_screen);
		}
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
	if (!teardownGame())
		return;
	createScript();
	_script->applySaved(load.values);
	restoreGame(load.save);
}

bool XFilesEngine::teardownGame() {
	// Game_Start (0x48cd70) destroys every timer, stops the head credits and
	// the subliminal words, removes the sounds and videos and opens the state
	// database again, so the game starts from the unchanged XFiles.gam
	closeMenu();
	_menuOpen = false;
	_timerStates.clear();
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
	_lastClickTime = 0;
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
	t.add("StartVideoText", [](XFilesEngine &e, const HookCall &c) {
		e.startVideoText(c.action, c.args);
		return true;
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
		if (!e.getPreference(kPrefSubliminalMessages))
			return false;
		e._subliminal.start(e.gameMillis()); // Game_BuildObjects 0x477940 calls Prefs_Apply 0x452b20
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
		if (e._prefVariables[kPrefInventory] && e._script->getVariable(e._prefVariables[kPrefInventory]) == 'H')
			e._interface->closeBar();
		return true;
	});
	t.add("DispEvIcons", [](XFilesEngine &e, const HookCall &c) {
		if (!c.has(1))
			return false;
		e._interface->setEvidenceRowShown(c.args[0] != 0);
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
}

void XFilesEngine::registerPreferences(const VCAction &action) {
	// RegPreferences(24 variables), run by Script before a game starts or
	// continues (Script::loadPreferences) and by the title. XFiles.exe keeps
	// the preferences in the registry and writes them into these variables;
	// Script does the writes, asking queryPreference for the registry's value.
	for (uint i = 0; i < action.args.size() && i < kPrefCount; i++)
		_prefVariables[i] = action.args[i].kind == kOperandVariable ? action.args[i].value : 0;
}

bool XFilesEngine::queryPreference(uint index, int32 &value) const {
	// The others have no ScummVM setting and keep their .gam values
	if (index == kPrefClosedCaptioning)
		value = ConfMan.getBool("subtitles");
	else if (index == kPrefSubliminalMessages)
		value = ConfMan.getBool("subliminal_messages");
	else
		return false;
	return true;
}

void XFilesEngine::registerUberVariables(const VCAction &action) {
	// RegUberVars(iUberLoss, iUberParanoia, iUberXTrack), 0x48ba50: the
	// subliminal words read them (0x468cf0)
	for (uint i = 0; i < action.args.size() && i < ARRAYSIZE(_uberVariables); i++)
		_uberVariables[i] = action.args[i].kind == kOperandVariable ? action.args[i].value : 0;
}

int32 XFilesEngine::getUberVariable(uint index) const {
	return _uberVariables[index] ? _script->getVariable(_uberVariables[index]) : 0;
}

bool XFilesEngine::getPreference(uint index) const {
	return _prefVariables[index] && _script->getVariable(_prefVariables[index]);
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

void XFilesEngine::restoreSound(uint32 id, bool loop, uint32 startMs) {
	_sound.restore(id, loop, startMs);
}

void XFilesEngine::playClip(uint32 id, int16 x, int16 y) {
	_sound.playClip(id, x, y);
}

void XFilesEngine::stopAllSounds() {
	_sound.stopAll();
}

void XFilesEngine::drawCaptions(const Movie &movie, const Common::Point &pos) {
	if (!getPreference(kPrefClosedCaptioning))
		return;
	// Text tracks sit below the picture (track matrix y 240), as wide as the video
	_captions.drawCaption(_screen, movie.getCaption(), pos.x, pos.y + movie.getHeight(), movie.getWidth());
}

void XFilesEngine::playQueuedVideo(const Input &input) {
	uint32 id = _script->getVideoQueue()[0];
	if (!_video->isOpen() || _video->getAssetId() != id) {
		closeView();
		if (!openMovie(*_video, id)) {
			_script->finishVideo();
			return;
		}
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

	_screen.fillRect(Common::Rect(_screen.w, _screen.h), 0);
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
	const Common::HashMap<uint, Script::Timer> &timers = _script->getTimers();
	Common::Array<uint> expired, stopped;
	for (Common::HashMap<uint, Script::Timer>::const_iterator it = timers.begin(); it != timers.end(); ++it) {
		if (!_timerStates.contains(it->_key) || _timerStates[it->_key].serial != it->_value.serial) {
			TimerState state = { it->_value.serial, now };
			_timerStates[it->_key] = state;
		} else if (now - _timerStates[it->_key].started >= it->_value.ms) {
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
			debugC(2, kDebugScript, "-- view clip #%u from %d ms", ids[i], _pendingClipTime >= 0 ? _pendingClipTime : (int)asset.files[0].start);
			_pendingClipTime = -1;
		}
		_layers.push_back(movie);
	}
	_pendingClipTime = -1;
}

void XFilesEngine::closeView() {
	for (uint i = 0; i < _layers.size(); i++)
		delete _layers[i];
	_layers.clear();
	_viewAssets.clear();
	_viewId = 0;
}

void XFilesEngine::updateInterface() {
	bool hiddenBar = _prefVariables[kPrefInventory] && _script->getVariable(_prefVariables[kPrefInventory]) == 'H';
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

void XFilesEngine::clickInventory(uint32 inventory) {
	// InvIcon_OnMouseClick (0x49c180); Script does it, heldItemChanged
	// closes the box and takes the cursor
	if (inventory == _interface->getHeld())
		return;
	_script->selectInventory(inventory);
}

void XFilesEngine::drawView() {
	_screen.fillRect(Common::Rect(_screen.w, _screen.h), 0);
	for (uint i = 0; i < _layers.size(); i++) {
		Common::Point pos = getBandPosition(*_layers[i]);
		blit(_layers[i]->getFrame(), pos.x, pos.y);
	}
	for (uint i = 0; i < _layers.size(); i++)
		drawCaptions(*_layers[i], getBandPosition(*_layers[i]));
	_interface->draw(_screen);
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
		_dialogBox.close();
		_script->closeBox();
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
	default:
		break;
	}
	if (_interface->isModal())
		return; // A layout has the input

	ViewHit viewHit = hitView(pos);
	switch (viewHit.kind) {
	case ViewHit::kExplorable:
		debugC(1, kDebugScript, "-- click:%u (explorable)", viewHit.id);
		_script->click(viewHit.id);
		break;
	case ViewHit::kCharView: {
		debugC(1, kDebugScript, "-- click:%u (character view)", viewHit.id);
		// The view's triggers handle inventory items held over the
		// character; with nothing in hand the exe opens the dialog box
		_script->click(viewHit.id);
		VCCharView cv;
		if (!isHolding() && _data->getCharView(viewHit.id, cv))
			openDialogBox(cv);
		break;
	}
	case ViewHit::kNav:
		_script->step(Common::String::format("nav:%u", viewHit.id));
		break;
	default:
		break;
	}
}

XFilesEngine::ViewHit XFilesEngine::hitView(const Common::Point &pos) const {
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
	_cursors->show(getCursorAt(_mouse));
	_cursors->update();
}

uint32 XFilesEngine::getCursorAt(const Common::Point &pos) const {
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
	ViewHit viewHit = hitView(pos);
	switch (viewHit.kind) {
	case ViewHit::kExplorable:
	case ViewHit::kCharView:
		// With an item in hand, an object that takes it keeps the item's
		// cursor (0x46e050, 0x418520), any other shows its own
		if (held && _script->isTestedOnClick(viewHit.id, _script->getHeldVariable()))
			return held;
		return viewHit.cursor;
	case ViewHit::kNav:
		return viewHit.cursor; // 0x446aa0: the hotspot's cursor
	default:
		return held;
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

// -- Dialog box ---------------------------------------------------------------

void XFilesEngine::openDialogBox(const VCCharView &charView) {
	// DisplayDBox / XFiles.exe 0x481660: only for a character with topics
	if (!charView.conversations)
		return;
	_headCredits.hide(); // DBox_OnActivate 0x4767a0
	_dialogBox.open(charView.id, Common::Rect(charView.rect.left, charView.rect.top, charView.rect.right, charView.rect.bottom));
	_dialogBoxView = _script->getView();
	debugC(1, kDebugScript, "-- dialog box of #%u", charView.id);
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
