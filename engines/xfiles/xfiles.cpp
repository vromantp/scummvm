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
#include "image/png.h"
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
	kDoubleClickDistance = 4,
	// The main menu: the X movie at the left, the entries movie at the right
	kMenuEntriesLeft = 472,
	kMenuEntriesShown = 75   ///< Frame of the entries movie where all entries are lit and no help text shows
};

// Logos and title, played by XFiles.exe itself (no list or action refers to them)
static const uint32 kIntroAssets[] = { 56003, 56002, 19668, 56001 };

// Main menu movies (category 37 Game, also played by the exe)
static const uint32 kMenuXAsset = 49583;
static const uint32 kMenuEntriesAsset = 49587;

// Main menu entries: vertical extent of New and Quit in the entries movie
static const int kMenuNewTop = 80, kMenuNewBottom = 118;
static const int kMenuQuitTop = 385, kMenuQuitBottom = 420;

XFilesEngine::XFilesEngine(OSystem *syst, const ADGameDescription *gameDesc) : Engine(syst),
	_gameDescription(gameDesc), _randomSource("xfiles"), _data(nullptr), _script(nullptr), _video(nullptr), _videoStarted(0), _viewId(0), _soundView(0), _shots(0), _shotDue(0), _shotId(0), _shotView(0), _shotBox(false), _nextStep(0), _waitVideo(0), _waitTimer(-1), _dialogBoxView(0),
	_textsDll(nullptr), _texts(nullptr), _interface(nullptr), _cursors(nullptr), _lastClickTime(0) {
	g_engine = this;
	_loadedSave = false;
	_pendingClipTime = -1;
	memset(_prefVariables, 0, sizeof(_prefVariables));
	memset(_uberVariables, 0, sizeof(_uberVariables));
	_lastClickHit.kind = Interface::kHitNone;
	_lastClickHit.id = 0;
	_hover = _lastClickHit;
}

XFilesEngine::~XFilesEngine() {
	for (Common::HashMap<uint32, Sound>::iterator it = _sounds.begin(); it != _sounds.end(); ++it)
		delete it->_value.movie;
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

bool XFilesEngine::loadDatabases() {
	Common::File hdb, gam;
	if (!hdb.open("XFiles.hdb"))
		return false;
	// xfiles_save names an original .X save: it takes the place of XFiles.gam
	// (it is a copy of it) and the game continues from it
	Common::Path savePath = ConfMan.getPath("xfiles_save");
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
	if (!loadDatabases())
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
	// A step list starts the game at once, without the intro and the menu
	if (!_steps.empty() || !_fastForward.empty() || _loadedSave || (playIntro() && runMenu()))
		runGame();
	return Common::kNoError;
}

// -- Screen and input ---------------------------------------------------------

XFilesEngine::Input XFilesEngine::pollInput() {
	Input input;
	input.click = false;
	input.skip = false;
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

// -- Intro and main menu ------------------------------------------------------

bool XFilesEngine::playIntro() {
	for (uint i = 0; i < ARRAYSIZE(kIntroAssets) && !shouldQuit(); i++) {
		if (!openMovie(*_video, kIntroAssets[i]))
			continue;
		_video->play(false);
		while (!shouldQuit() && !_video->hasEnded()) {
			Input input = pollInput();
			if (input.click || input.skip)
				_video->stop();
			_video->update();
			_screen.fillRect(Common::Rect(_screen.w, _screen.h), 0);
			const Graphics::Surface &frame = _video->getFrame();
			blit(frame, (_screen.w - frame.w) / 2, (_screen.h - frame.h) / 2);
			present();
		}
		_video->close();
	}
	return !shouldQuit();
}

bool XFilesEngine::runMenu() {
	// Only New and Quit work; the X loop (49585) and name (49589) movies and
	// the entries' help texts are not shown.
	Movie x, entries;
	if (openMovie(x, kMenuXAsset))
		x.play(false);
	if (openMovie(entries, kMenuEntriesAsset))
		entries.play(false);

	while (!shouldQuit()) {
		Input input = pollInput();
		if (input.click && input.pos.x >= kMenuEntriesLeft) {
			if (input.pos.y >= kMenuNewTop && input.pos.y < kMenuNewBottom)
				return true;
			if (input.pos.y >= kMenuQuitTop && input.pos.y < kMenuQuitBottom) {
				quitGame();
				return false;
			}
		}
		x.update();
		entries.update();
		if (!entries.hasEnded() && entries.getCurFrame() >= kMenuEntriesShown)
			entries.stop();

		_screen.fillRect(Common::Rect(_screen.w, _screen.h), 0);
		blit(x.getFrame(), 0, 0);
		blit(entries.getFrame(), kMenuEntriesLeft, 0);
		present();
	}
	return false;
}

// -- The game -----------------------------------------------------------------

void XFilesEngine::runGame() {
	_script = new Script(*_data);
	_script->setHooks(this);
	_script->setTexts(_texts);
	_script->setRandomSeed(_randomSource.getRandomNumber(0xFFFFFFFF));
	// A new game starts from XFiles.gam, which allows the head credits
	// (XFiles.exe Game_Start 0x48cd70)
	if (_loadedSave) {
		_script->restore(_save);
		// The view's clip plays from the saved time (0x51d200), the head
		// credits roll again if they were rolling, from the last credit
		// shown (0x4a80a0); the dialog box that was open is created again
		if (_save.clip)
			_pendingClipTime = _save.clipTime;
		if (_save.credits != -1) {
			_headCredits.setEnabled(true);
			_headCredits.roll(g_system->getMillis(), getPreference(kPrefSkipIntro), _save.credits);
		}
		VCCharView boxView;
		if (_save.dialogBox && _data->getCharView(_save.dialogBox, boxView))
			openDialogBox(boxView);
	} else {
		_headCredits.setEnabled(true);
		_script->start();
	}
	// Fast-forward (xfiles_fastforward): the steps run in the script alone,
	// as vc_sim.py runs them, so nothing plays and no time passes; the
	// game goes on from where they end
	for (uint i = 0; i < _fastForward.size(); i++)
		_script->step(_fastForward[i]);
	while (!shouldQuit()) {
		Input input = pollInput();
		updateTimers();
		updateSounds();
		_captions.update(g_system->getMillis());
		_headCredits.update(g_system->getMillis(), _script->getVideoQueue().empty() && !_dialogBox.isOpen());
		// The words show over a view, the dialog box or the location card,
		// not over a video alone
		_subliminal.update(g_system->getMillis(), *_script, getUberVariable(0), getUberVariable(1), getUberVariable(2),
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
		}
		_captions.drawCard(_screen);
		_headCredits.draw(_screen);
		_subliminal.draw(_screen);
		present();
		updateShots();
		runSteps();
	}
}

// -- Comparison with reference runs -------------------------------------------

// The delays xfref.dll waits before its shots: after a view is activated
// and no video plays, and after the dialog box is drawn
static const uint32 kShotViewDelay = 1500;
static const uint32 kShotBoxDelay = 600;

void XFilesEngine::scheduleShot(const char *reason, uint32 id, uint32 delay) {
	// A later event replaces a pending shot, as in xfref.dll
	_shotReason = reason;
	_shotId = id;
	_shotDue = g_system->getMillis() + delay;
	if (!_shotDue)
		_shotDue = 1;
}

void XFilesEngine::updateShots() {
	if (_shotsDir.empty())
		return;
	// A view counts once it is activated (xfref.dll: obj.activate kind=4),
	// the box whenever it comes on screen (dbox.show on=1)
	if (_script->isViewActive() && _script->getView() != _shotView) {
		_shotView = _script->getView();
		scheduleShot("view", _shotView, kShotViewDelay);
	}
	bool box = _dialogBox.isOpen() && _script->getVideoQueue().empty();
	if (box && !_shotBox)
		scheduleShot("dbox", _dialogBox.getCharView(), kShotBoxDelay);
	_shotBox = box;
	if (_shotDue && _shotReason == "view" && !_script->getVideoQueue().empty())
		_shotDue = g_system->getMillis() + kShotViewDelay; // After the video
	if (_shotDue && (int32)(g_system->getMillis() - _shotDue) >= 0) {
		saveShot();
		_shotDue = 0;
	}
}

void XFilesEngine::saveShot() {
	Common::String name = Common::String::format("%04u-%s-%u.png", ++_shots, _shotReason.c_str(), _shotId);
	Common::DumpFile file;
	if (!file.open(_shotsDir.join(name), true) || !Image::writePNG(file, _screen))
		warning("Cannot write the screenshot %s", name.c_str());
	debugC(1, kDebugScript, "-- shot %s", name.c_str());
}

void XFilesEngine::runSteps() {
	// xfref.dll holds input commands while a shot is pending (not while
	// it waits for a video)
	const Common::Array<uint32> &queue = _script->getVideoQueue();
	if (_steps.empty() || (_shotDue && queue.empty()) || _waitTimer >= 0)
		return;
	if (_nextStep >= _steps.size()) {
		// Done: quit once the videos have played
		if (queue.empty())
			quitGame();
		return;
	}
	if (_waitVideo) {
		if (!queue.empty() && queue[0] == _waitVideo)
			return;
		_waitVideo = 0;
	}
	while (_nextStep < _steps.size()) {
		const Common::String &step = _steps[_nextStep++];
		if (step == "wait") {
			// The end of the playing video, in real time
			if (!queue.empty()) {
				_waitVideo = queue[0];
				return;
			}
			continue;
		}
		if (step == "shot") {
			// The input command shot of xfref.dll: a screenshot now
			scheduleShot("shot", 0, 100);
			return;
		}
		if (step.hasPrefix("timer:")) {
			// The engine's timers run in real time: wait for this one to
			// expire, if it runs
			uint timer = atoi(step.c_str() + 6);
			if (_script->getTimers().contains(timer)) {
				_waitTimer = timer;
				return;
			}
			continue;
		}
		// A character view clicked opens the dialog box, as handleClick does
		VCCharView cv;
		if (step.hasPrefix("click:") && _data->getCharView(atoi(step.c_str() + 6), cv)) {
			_script->click(cv.id);
			if (!isHolding())
				openDialogBox(cv);
		} else if (step.hasPrefix("ask:")) {
			askTopic(atoi(step.c_str() + 4));
		} else {
			_script->step(step);
		}
		return;
	}
}

// -- C++ functions ------------------------------------------------------------

void XFilesEngine::callFunction(const VCAction &action, const Common::Array<int32> &args) {
	if (action.function == "StartVideoText")
		startVideoText(action, args);
	else if (action.function == "RegPreferences")
		registerPreferences(action);
	else if (action.function == "RollHdCredits")
		_headCredits.roll(g_system->getMillis(), getPreference(kPrefSkipIntro));
	else if (action.function == "SetStartView" && getPreference(kPrefSubliminalMessages))
		_subliminal.start(g_system->getMillis()); // Game_BuildObjects 0x477940 calls Prefs_Apply 0x452b20
	else if (action.function == "RegUberVars")
		registerUberVariables(action);
	else if (action.function == "StopEnvSounds")
		stopAllSounds(); // XFiles.exe 0x4cc9c0: stops every playing asset without a picture
	else
		callInventoryFunction(action, args);
}

void XFilesEngine::callInventoryFunction(const VCAction &action, const Common::Array<int32> &args) {
	const Common::String &name = action.function;
	// RegCurrInvVar, OnInvClick and OnIntInvClick are handled by Script (iCurrInventory)
	if (name == "RollOnEvKit" && args.size() >= 2) {
		// 0x4efda0: nothing while that item is in hand; open the kit above
		// the item's icon, or close it once the mouse has left it
		Common::Rect from;
		if ((uint32)args[0] == _interface->getHeld())
			return;
		if (args[1] && _interface->getInventoryRect(args[0], &from))
			_interface->openKit(from);
		else if (!args[1] && !_interface->isMouseInKit())
			_interface->closeKit();
	} else if (name == "EvKitIconClick") {
		// 0x4efc90: the kit is taken in hand; the hidden bar closes
		_interface->closeKit();
		if (_prefVariables[kPrefInventory] && _script->getVariable(_prefVariables[kPrefInventory]) == 'H')
			_interface->closeBar();
	} else if (name == "DispEvIcons" && args.size() >= 1) {
		_interface->setEvidenceRowShown(args[0] != 0);
	} else if (name == "AddIFLExcept" && args.size() >= 1) {
		_interface->addModalException(args[0]);
	} else if (name == "ClearIFLExcept") {
		_interface->clearModalExceptions();
	}
}

void XFilesEngine::registerPreferences(const VCAction &action) {
	// RegPreferences(24 variables), called once by the title. XFiles.exe
	// keeps the preferences in the registry and writes them into these
	// variables; here ScummVM's settings take the registry's place.
	for (uint i = 0; i < action.args.size() && i < kPrefCount; i++)
		_prefVariables[i] = action.args[i].kind == kOperandVariable ? action.args[i].value : 0;
	if (_prefVariables[kPrefClosedCaptioning])
		_script->setVariable(_prefVariables[kPrefClosedCaptioning], ConfMan.getBool("subtitles"));
	if (_prefVariables[kPrefSubliminalMessages])
		_script->setVariable(_prefVariables[kPrefSubliminalMessages], ConfMan.getBool("subliminal_messages"));
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
	_captions.startCard(Captions::formatCard(location, args[1], args[2], args[3] == 1), g_system->getMillis());
}

// -- Sound --------------------------------------------------------------------

void XFilesEngine::playAsset(uint32 id, byte mode, bool positional, int16 x, int16 y) {
	if (mode == kModeStop) {
		stopSound(id);
		return;
	}
	startSound(id, mode == kModePlayLoop, positional, x, y, 0);
}

void XFilesEngine::restoreSound(uint32 id, bool loop, uint32 startMs) {
	startSound(id, loop, false, 0, 0, startMs);
}

void XFilesEngine::startSound(uint32 id, bool loop, bool positional, int16 x, int16 y, uint32 startMs) {
	// Overlays and backgrounds come from the view's asset list; this only
	// handles sounds
	VCAsset asset;
	if (!_data->getAsset(id, asset) || asset.files.empty() || !asset.files[0].file.hasPrefix("XS:"))
		return;
	stopSound(id);
	Sound sound;
	sound.movie = new Movie();
	sound.positional = positional;
	sound.x = x;
	sound.y = y;
	if (!sound.movie->open(_discFiles, asset)) {
		delete sound.movie;
		_script->finishSound(id);
		return;
	}
	// A restored sound's time is its in point (0x4a88d0)
	if (startMs)
		sound.movie->setStart(startMs);
	debugC(1, kDebugScript, "-- sound #%u %s%s from %u ms", id, asset.files[0].file.c_str(), loop ? ", looped" : "", startMs ? startMs : asset.files[0].start);
	if (positional)
		placeSound(sound);
	sound.movie->play(loop);
	_sounds[id] = sound;
}

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

void XFilesEngine::stopSound(uint32 id) {
	if (!_sounds.contains(id))
		return;
	delete _sounds[id].movie;
	_sounds.erase(id);
}

void XFilesEngine::stopAllSounds() {
	Common::Array<uint32> stopped;
	for (Common::HashMap<uint32, Sound>::iterator it = _sounds.begin(); it != _sounds.end(); ++it) {
		delete it->_value.movie;
		stopped.push_back(it->_key);
	}
	_sounds.clear();
	for (uint i = 0; i < stopped.size(); i++)
		_script->finishSound(stopped[i]);
}

void XFilesEngine::updateSounds() {
	// 3D sounds are placed again when the player moves
	bool moved = _script->getView() != _soundView;
	_soundView = _script->getView();
	Common::Array<uint32> ended;
	for (Common::HashMap<uint32, Sound>::iterator it = _sounds.begin(); it != _sounds.end(); ++it) {
		if (moved && it->_value.positional)
			placeSound(it->_value);
		it->_value.movie->update();
		if (it->_value.movie->hasEnded())
			ended.push_back(it->_key);
	}
	for (uint i = 0; i < ended.size(); i++) {
		stopSound(ended[i]);
		_script->finishSound(ended[i]);
	}
}

uint32 XFilesEngine::getNavTarget(uint32 view, uint32 hotSpot) const {
	// XFiles.exe matches navs by their hotspot's cursor: a nav whose own
	// hotspot shows the Forward cursor counts as the forward one
	VCView v;
	VCHotSpot wanted;
	Common::Array<uint32> navs;
	if (!_data->getView(view, v) || !_data->getHotSpot(hotSpot, wanted) || !_data->getMembers(v.navs, navs))
		return 0;
	for (uint i = 0; i < navs.size(); i++) {
		VCNav nav;
		VCHotSpot h;
		if (_data->getNav(navs[i], nav) && _data->getHotSpot(nav.hotSpot, h) && h.cursor == wanted.cursor)
			return nav.target;
	}
	return 0;
}

bool XFilesEngine::getViewPosition(uint32 view, Common::Point &pos) const {
	VCLocation location;
	if (!_data->getLocation(_script->getLocation(), location))
		return false;
	for (uint i = 0; i < location.viewPoints.size(); i++) {
		VCViewPoint vp;
		if (!_data->getViewPoint(location.viewPoints[i], vp))
			continue;
		for (uint j = 0; j < vp.views.size(); j++) {
			if (vp.views[j] == view) {
				pos = Common::Point(vp.x, vp.y);
				return true;
			}
		}
	}
	return false;
}

bool XFilesEngine::getFacing(uint32 view, const Common::Point &from, Common::Point &facing) const {
	// XFiles.exe 0x46d7a0, as Sound3D_Place 0x4a8be0 uses it: towards the
	// viewpoint of the view's forward nav; else the forward one of the view
	// its left nav leads to, turned right by 90 degrees; else that of its
	// right nav's view, turned left. North is +y, East +x.
	uint32 hotSpots[3]; // Left, right, forward
	Common::Point to;
	if (!_data->getDefaultHotSpots(hotSpots))
		return false;
	uint32 target = getNavTarget(view, hotSpots[2]);
	if (target && getViewPosition(target, to)) {
		facing = to - from;
		return true;
	}
	for (int side = 0; side < 2; side++) {
		uint32 sideView = getNavTarget(view, hotSpots[side]);
		target = sideView ? getNavTarget(sideView, hotSpots[2]) : 0;
		if (target && getViewPosition(target, to)) {
			Common::Point d = to - from;
			facing = side == 0 ? Common::Point(d.y, -d.x) : Common::Point(-d.y, d.x);
			return true;
		}
	}
	return false;
}

void XFilesEngine::placeSound(Sound &sound) {
	// XFiles.exe 0x4a8be0. Without the 3D sound preference the sound plays
	// at the volume of its type, as any other.
	VCViewPoint viewPoint;
	if (!getPreference(kPref3DSound) || !_data->getViewPoint(_script->getViewPoint(), viewPoint)) {
		sound.movie->setVolume(Audio::Mixer::kMaxChannelVolume);
		sound.movie->setBalance(0);
		return;
	}
	// Placed before its view is activated, it is silent until the player
	// moves on (reference run, 2026-10-12: the Field Office ambience at the
	// start of the game)
	if (!_script->isViewActive()) {
		sound.movie->setVolume(0);
		return;
	}

	// Volume: 100 within 3 units of the viewpoint, 90 up to 5, then 5 less
	// per unit, 10 up to 21 and 5 beyond
	double dx = sound.x - viewPoint.x, dy = sound.y - viewPoint.y;
	double distance = sqrt(dx * dx + dy * dy);
	int volume;
	if (distance < 3)
		volume = 100;
	else if (distance <= 5)
		volume = 90;
	else if (distance <= 21)
		volume = 85 - 5 * (int)ceil(distance - 6);
	else
		volume = 5;
	sound.movie->setVolume(volume * Audio::Mixer::kMaxChannelVolume / 100);
	if (volume < 11)
		return; // The original leaves the balance alone

	// Balance: the angle between the view's facing and the sound, 0 straight
	// ahead or behind and 85% to the side. A view without a facing keeps the
	// last one found; with no facing or the sound on the viewpoint, the
	// balance is left alone.
	getFacing(_script->getView(), Common::Point(viewPoint.x, viewPoint.y), _soundFacing);
	int fx = _soundFacing.x, fy = _soundFacing.y;
	if ((fx == 0 && fy == 0) || (dx == 0 && dy == 0))
		return;
	double angle = acos((fx * dx + fy * dy) / (sqrt((double)(fx * fx + fy * fy)) * distance)) * 2 / M_PI;
	if (angle > 1)
		angle = 2 - angle;
	int balance = (int)(angle * 100);
	// The original takes 85% in extended precision, which truncates exact
	// products one lower (100 gives 84)
	int scaled = ABS(balance) * 85;
	scaled = scaled / 100 - (scaled && scaled % 100 == 0 ? 1 : 0);
	balance = fx * dy - dx * fy > 0 ? -scaled : scaled; // Negative: to the left
	sound.movie->setBalance(balance * 127 / 100);
	debugC(1, kDebugScript, "-- sound #%u at (%d, %d), viewpoint (%d, %d) facing (%d, %d): volume %d, balance %d",
		sound.movie->getAssetId(), sound.x, sound.y, viewPoint.x, viewPoint.y, fx, fy, volume, balance);
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
		_videoStarted = g_system->getMillis();
	}
	// A piece of the view's loop between conversation clips plays for a
	// fixed time (Script::queueViewLoop); a looping clip plays until a Stop
	// action stops it
	uint32 limit = _script->getVideoLimit();
	if ((input.skip && !_script->isVideoLooped()) || (limit && g_system->getMillis() - _videoStarted >= limit))
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
	uint32 now = g_system->getMillis();
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

static bool containsPoint(const VCRect &r, const Common::Point &pos) {
	return pos.x >= r.left && pos.x < r.right && pos.y >= r.top && pos.y < r.bottom;
}

void XFilesEngine::handleClick(const Common::Point &pos) {
	Interface::Hit hit = _interface->hitTest(pos);
	uint32 now = g_system->getMillis();
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
