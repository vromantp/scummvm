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
	// Emotion icons: placed in a row below the band (position not from the game)
	kIconTop = 360,
	kIconWidth = 80,
	kIconHeight = 60,
	kIconGap = 20,
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
	_gameDescription(gameDesc), _data(nullptr), _script(nullptr), _video(nullptr), _viewId(0), _soundView(0) {
	g_engine = this;
	memset(_prefVariables, 0, sizeof(_prefVariables));
}

XFilesEngine::~XFilesEngine() {
	stopAllSounds();
	closeView();
	for (Common::HashMap<uint32, Movie *>::iterator it = _iconMovies.begin(); it != _iconMovies.end(); ++it)
		delete it->_value;
	delete _video;
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
	Common::Array<NeoEntry> icons;
	_data->getHdb().getObjects(kClassEmotionIcon, icons);
	for (uint i = 0; i < icons.size(); i++)
		_emotionIcons.push_back(icons[i].key);

	_script->setHooks(this);
	_script->start();
	while (!shouldQuit()) {
		Input input = pollInput();
		updateTimers();
		updateSounds();
		_captions.update(g_system->getMillis());
		if (!_script->getVideoQueue().empty()) {
			playQueuedVideo(input);
		} else {
			syncView();
			updateIcons();
			if (input.click)
				handleClick(input.pos);
			drawView();
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
		_video->play(false);
	}
	if (input.skip)
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
		// The choice is over: the emotion icons on screen go away (an assumption, see updateIcons)
		for (uint j = 0; j < _shownIcons.size(); j++)
			_spentIcons[_shownIcons[j]] = true;
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

void XFilesEngine::updateIcons() {
	// The exe decides when emotion icons show (they stay enabled after a
	// choice). Here an enabled icon shows until one of them is clicked or a
	// timer expires; disabling it makes it available again.
	Common::Array<uint32> shown;
	for (uint i = 0; i < _emotionIcons.size(); i++) {
		uint32 id = _emotionIcons[i];
		if (!_script->isEnabled(id))
			_spentIcons.erase(id);
		else if (!_spentIcons.contains(id))
			shown.push_back(id);
	}

	for (uint i = 0; i < shown.size(); i++) {
		if (_iconMovies.contains(shown[i]))
			continue;
		VCIcon icon;
		Common::Array<uint32> graphics;
		Movie *movie = new Movie();
		if (_data->getIcon(shown[i], icon) && _data->getMembers(icon.graphic, graphics) && !graphics.empty() &&
				openMovie(*movie, graphics[0]))
			movie->play(true);
		_iconMovies[shown[i]] = movie;
	}
	for (uint i = 0; i < _shownIcons.size(); i++) {
		if (Common::find(shown.begin(), shown.end(), _shownIcons[i]) == shown.end()) {
			delete _iconMovies[_shownIcons[i]];
			_iconMovies.erase(_shownIcons[i]);
		}
	}
	_shownIcons = shown;
	for (uint i = 0; i < _shownIcons.size(); i++)
		_iconMovies[_shownIcons[i]]->update();
}

Common::Rect XFilesEngine::getIconRect(uint index) const {
	int width = _shownIcons.size() * (kIconWidth + kIconGap) - kIconGap;
	int left = (kScreenWidth - width) / 2 + index * (kIconWidth + kIconGap);
	return Common::Rect(left, kIconTop, left + kIconWidth, kIconTop + kIconHeight);
}

void XFilesEngine::drawView() {
	_screen.fillRect(Common::Rect(_screen.w, _screen.h), 0);
	for (uint i = 0; i < _layers.size(); i++) {
		Common::Point pos = getBandPosition(*_layers[i]);
		blit(_layers[i]->getFrame(), pos.x, pos.y);
	}
	for (uint i = 0; i < _layers.size(); i++)
		drawCaptions(*_layers[i], getBandPosition(*_layers[i]));
	for (uint i = 0; i < _shownIcons.size(); i++) {
		Common::Rect r = getIconRect(i);
		blit(_iconMovies[_shownIcons[i]]->getFrame(), r.left, r.top);
	}
}

static bool containsPoint(const VCRect &r, const Common::Point &pos) {
	return pos.x >= r.left && pos.x < r.right && pos.y >= r.top && pos.y < r.bottom;
}

void XFilesEngine::handleClick(const Common::Point &pos) {
	for (uint i = 0; i < _shownIcons.size(); i++) {
		if (getIconRect(i).contains(pos)) {
			uint32 id = _shownIcons[i];
			debugC(1, kDebugScript, "-- click:%u (emotion icon)", id);
			for (uint j = 0; j < _shownIcons.size(); j++)
				_spentIcons[_shownIcons[j]] = true;
			_script->click(id);
			return;
		}
	}

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
			_script->click(ids[i]);
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

} // End of namespace XFiles
