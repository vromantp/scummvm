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
#include "engines/util.h"
#include "graphics/cursorman.h"
#include "graphics/pixelformat.h"
#include "graphics/wincursor.h"

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
	_gameDescription(gameDesc), _randomSource("xfiles"), _data(nullptr), _script(nullptr), _video(nullptr), _videoStarted(0), _viewId(0), _soundView(0), _dialogBoxView(0),
	_texts(nullptr), _interface(nullptr), _inventoryVariable(0), _inventoryNone(0), _lastClickTime(0) {
	g_engine = this;
	memset(_prefVariables, 0, sizeof(_prefVariables));
	_lastClickHit.kind = Interface::kHitNone;
	_lastClickHit.id = 0;
	_hover = _lastClickHit;
}

XFilesEngine::~XFilesEngine() {
	stopAllSounds();
	closeView();
	delete _interface;
	delete _video;
	delete _texts;
	delete _script;
	delete _data;
	_screen.free();
}

bool XFilesEngine::loadDatabases() {
	Common::File hdb, gam;
	if (!hdb.open("XFiles.hdb") || !gam.open("XFiles.gam"))
		return false;
	_data = new VCData();
	if (!_data->load(hdb, gam))
		return false;
	debugC(1, kDebugDatabase, "Loaded %u classes from XFiles.hdb, %u from XFiles.gam",
		_data->getHdb().getClasses().size(), _data->getGam().getClasses().size());
	return true;
}

Common::Error XFilesEngine::run() {
	_discFiles.init(Common::FSNode(ConfMan.getPath("path")));
	if (!loadDatabases())
		return Common::Error(Common::kReadingFailed, "XFiles.hdb / XFiles.gam");

	Graphics::PixelFormat format(2, 5, 6, 5, 0, 11, 5, 0, 0);
	initGraphics(kScreenWidth, kScreenHeight, &format);
	_screen.create(kScreenWidth, kScreenHeight, g_system->getScreenFormat());
	setDebugger(new Console());

	Graphics::Cursor *cursor = Graphics::makeDefaultWinCursor();
	CursorMan.replaceCursor(cursor);
	delete cursor;
	CursorMan.showMouse(true);
	_captions.loadFonts();
	_interface = new Interface(*_data, _discFiles);
	_interface->setFormat(_screen.format);
	if (!_dialogBox.load(*_data, _discFiles, _screen.format))
		warning("Cannot load the dialog box pictures from x.pff");
	// The language DLL with the topic texts (the exe loads it with LoadLibrary)
	Common::SeekableReadStream *texts = _discFiles.openFile(0, "XFilesT.dll");
	if (!texts)
		texts = _discFiles.openFile(1, "XFilesT.dll");
	if (texts)
		_texts = Common::WinResources::createFromEXE(texts);
	if (!_texts)
		warning("Cannot load XFilesT.dll; topics show their authoring names");
	_interface->init(_texts);
	syncSoundSettings();

	_video = new Movie();
	if (playIntro() && runMenu())
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
		if (event.type == Common::EVENT_LBUTTONDOWN) {
			input.click = true;
			input.pos = event.mouse;
		} else if (event.type == Common::EVENT_MOUSEMOVE) {
			_mouse = event.mouse;
		} else if (event.type == Common::EVENT_KEYDOWN && event.kbd.keycode == Common::KEYCODE_ESCAPE) {
			input.skip = true;
		}
	}
	return input;
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
	_script->setRandomSeed(_randomSource.getRandomNumber(0xFFFFFFFF));
	_script->start();
	while (!shouldQuit()) {
		Input input = pollInput();
		updateTimers();
		updateSounds();
		_captions.update(g_system->getMillis());
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
			drawView();
			_dialogBox.draw(_screen, _mouse);
		}
		_captions.drawCard(_screen);
		present();
	}
}

// -- C++ functions ------------------------------------------------------------

void XFilesEngine::callFunction(const VCAction &action, const Common::Array<int32> &args) {
	if (action.function == "StartVideoText")
		startVideoText(action, args);
	else if (action.function == "RegPreferences")
		registerPreferences(action);
	else if (action.function == "StopEnvSounds")
		stopAllSounds(); // XFiles.exe 0x4cc9c0: stops every playing asset without a picture
	else
		callInventoryFunction(action, args);
}

void XFilesEngine::callInventoryFunction(const VCAction &action, const Common::Array<int32> &args) {
	const Common::String &name = action.function;
	if (name == "RegCurrInvVar" && action.args.size() >= 2 && action.args[0].kind == kOperandVariable) {
		// RegCurrInvVar(iCurrInventory, Inventory_None), 0x458e60: the
		// variable starts as "nothing in hand"
		_inventoryVariable = action.args[0].value;
		_inventoryNone = args[1];
		_script->setVariable(_inventoryVariable, _inventoryNone);
	} else if (name == "OnInvClick" && args.size() >= 1) {
		// Inv_Select (0x49b410): the item's value goes into the variable
		if (_inventoryVariable && _script->getVariable(_inventoryVariable) != args[0])
			_script->setVariable(_inventoryVariable, args[0]);
	} else if (name == "OnIntInvClick") {
		// Interface items (PDA, phone) are not held (0x467fb0)
		_interface->setHeld(0);
	} else if (name == "SelectInv" && args.size() >= 1) {
		// 0x50a390: put back what is in hand and take the item, as a click
		// on its icon would
		if (_interface->hasInventoryIcon(args[0])) {
			putBack();
			clickInventory(args[0]);
		}
	} else if (name == "DeselectInv" && args.size() >= 1) {
		// 0x50a570: 0 puts back any item
		if (args[0] == 0 || (uint32)args[0] == _interface->getHeld())
			putBack();
	} else if (name == "RollOnEvKit" && args.size() >= 2) {
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
		return;
	}
	debugC(1, kDebugScript, "-- sound #%u %s%s", id, asset.files[0].file.c_str(), mode == kModePlayLoop ? ", looped" : "");
	if (positional)
		placeSound(sound);
	sound.movie->play(mode == kModePlayLoop);
	_sounds[id] = sound;
}

void XFilesEngine::stopSound(uint32 id) {
	if (!_sounds.contains(id))
		return;
	delete _sounds[id].movie;
	_sounds.erase(id);
}

void XFilesEngine::stopAllSounds() {
	for (Common::HashMap<uint32, Sound>::iterator it = _sounds.begin(); it != _sounds.end(); ++it)
		delete it->_value.movie;
	_sounds.clear();
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
	for (uint i = 0; i < ended.size(); i++)
		stopSound(ended[i]);
}

/** Unit vector of a view facing (1 North ... 8 NorthWest); North is +y, East +x. */
static bool getFacingVector(uint32 facing, int &x, int &y) {
	static const int kVectors[8][2] = {
		{ 0, 1 }, { 1, 1 }, { 1, 0 }, { 1, -1 }, { 0, -1 }, { -1, -1 }, { -1, 0 }, { -1, 1 }
	};
	if (facing < 1 || facing > 8)
		return false;
	x = kVectors[facing - 1][0];
	y = kVectors[facing - 1][1];
	return true;
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
	// ahead or behind and 85% to the side. The original takes the facing from
	// the neighbouring viewpoints (0x46d7a0); the view's direction gives the
	// same vector for views that face a compass direction.
	VCView view;
	int fx, fy;
	if (!_data->getView(_script->getView(), view) || !getFacingVector(view.facing, fx, fy) || (dx == 0 && dy == 0)) {
		sound.movie->setBalance(0);
		return;
	}
	double angle = acos((fx * dx + fy * dy) / (sqrt((double)(fx * fx + fy * fy)) * distance)) * 2 / M_PI;
	if (angle > 1)
		angle = 2 - angle;
	int balance = (int)(angle * 100);
	if (fx * dy - dx * fy > 0)
		balance = -balance; // To the left
	balance = CLIP((int)(balance * 0.85), -100, 100);
	sound.movie->setBalance(balance * 127 / 100);
	debugC(1, kDebugScript, "-- sound #%u at (%d, %d), viewpoint (%d, %d) facing %u: volume %d, balance %d",
		sound.movie->getAssetId(), sound.x, sound.y, viewPoint.x, viewPoint.y, view.facing, volume, balance);
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

	for (uint i = 0; i < expired.size(); i++) {
		debugC(1, kDebugScript, "-- timer %u expired", expired[i]);
		_timerStates.erase(expired[i]);
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
		// The background goes below the overlays, wherever it is in the list
		if (ids[i] == background) {
			movie->showStill();
			_layers.insert_at(0, movie);
		} else {
			movie->play(asset.flags & kAssetLoop);
			_layers.push_back(movie);
		}
	}
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
	// (InvBar_OnRoll 0x468330) or when it is disabled (0x436c20)
	if (isHolding() && (_interface->hasEnteredBar() || !_script->isEnabled(_interface->getHeld())))
		putBack();
}

bool XFilesEngine::isHolding() const {
	return _inventoryVariable && _script->getVariable(_inventoryVariable) != _inventoryNone;
}

void XFilesEngine::putBack() {
	// The held-item object exists only while something is in hand; its
	// deactivation (0x437c20) resets the variable
	if (!isHolding())
		return;
	debugC(1, kDebugScript, "-- #%u put back", _interface->getHeld());
	_script->setVariable(_inventoryVariable, _inventoryNone);
	_interface->setHeld(0);
}

void XFilesEngine::clickInventory(uint32 inventory) {
	// InvIcon_OnMouseClick (0x49c180): close the box, put back what is in
	// hand, take the item's cursor, then the icon's Mouse Click triggers
	// (which call OnInvClick)
	if (inventory == _interface->getHeld())
		return;
	if (_dialogBox.isOpen()) {
		_dialogBox.close();
		_script->closeBox();
	}
	putBack();
	_interface->setHeld(inventory);
	debugC(1, kDebugScript, "-- click:%u (inventory)", inventory);
	_script->click(inventory);
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
		_script->click(hit.id);
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

	VCView view;
	if (!_data->getView(_script->getView(), view))
		return;
	Common::Array<uint32> ids;
	_data->getMembers(view.explorables, ids);
	for (uint i = 0; i < ids.size(); i++) {
		VCExplorable e;
		if (_script->isEnabled(ids[i]) && _data->getExplorable(ids[i], e) && containsPoint(e.rect, pos)) {
			debugC(1, kDebugScript, "-- click:%u (explorable)", ids[i]);
			_script->click(ids[i]);
			return;
		}
	}
	ids.clear();
	_data->getMembers(view.characters, ids);
	for (uint i = 0; i < ids.size(); i++) {
		VCCharView cv;
		if (_script->isEnabled(ids[i]) && _data->getCharView(ids[i], cv) && containsPoint(cv.rect, pos)) {
			debugC(1, kDebugScript, "-- click:%u (character view)", ids[i]);
			// The view's triggers handle inventory items held over the
			// character; with nothing in hand the exe opens the dialog box
			_script->click(ids[i]);
			if (!isHolding())
				openDialogBox(cv);
			return;
		}
	}
	ids.clear();
	_data->getMembers(view.navs, ids);
	for (uint i = 0; i < ids.size(); i++) {
		VCNav nav;
		VCHotSpot hotSpot;
		if (_script->isEnabled(ids[i]) && _data->getNav(ids[i], nav) && _data->getHotSpot(nav.hotSpot, hotSpot) &&
				containsPoint(hotSpot.rect, pos)) {
			_script->step(Common::String::format("nav:%u", ids[i]));
			return;
		}
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
