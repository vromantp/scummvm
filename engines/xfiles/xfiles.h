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
#include "common/hashmap.h"
#include "common/rect.h"
#include "common/scummsys.h"
#include "common/error.h"
#include "engines/engine.h"
#include "graphics/surface.h"

#include "xfiles/captions.h"
#include "xfiles/detection.h"
#include "xfiles/discfiles.h"
#include "xfiles/script.h"

namespace XFiles {

class Movie;
class VCData;

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

	// ScriptHooks
	void callFunction(const VCAction &action, const Common::Array<int32> &args) override;
	void playAsset(uint32 id, byte mode, bool positional, int16 x, int16 y) override;

protected:
	Common::Error run() override;

private:
	/** Input gathered by pollInput() for one frame. */
	struct Input {
		bool click;
		Common::Point pos;
		bool skip;
	};

	struct TimerState {
		uint32 serial;
		uint32 started;
	};

	bool loadDatabases();
	Input pollInput();
	void present();
	void blit(const Graphics::Surface &src, int x, int y);
	Common::Point getBandPosition(const Movie &movie) const;
	bool openMovie(Movie &movie, uint32 assetId);

	bool playIntro();
	bool runMenu();
	void runGame();

	void playQueuedVideo(const Input &input);
	void updateTimers();
	void syncView();
	void closeView();
	void updateIcons();
	void drawView();
	void handleClick(const Common::Point &pos);
	void drawCaptions(const Movie &movie, const Common::Point &pos);
	void startVideoText(const VCAction &action, const Common::Array<int32> &args);
	void registerPreferences(const VCAction &action);
	/** A preference's value, 0 before RegPreferences. */
	bool getPreference(uint index) const;
	Common::Rect getIconRect(uint index) const;

	const ADGameDescription *_gameDescription;
	VCData *_data;
	DiscFiles _discFiles;
	Script *_script;
	Graphics::Surface _screen;
	Captions _captions;
	uint32 _prefVariables[kPrefCount];  ///< From RegPreferences

	Movie *_video;                       ///< The play-once video on screen
	Common::Array<uint32> _viewAssets;   ///< What the view layers show
	uint32 _viewId;
	Common::Array<Movie *> _layers;      ///< Background and overlays of the view
	Common::Array<uint32> _emotionIcons; ///< Every VCEmotionIcon
	Common::HashMap<uint32, bool> _spentIcons;
	Common::Array<uint32> _shownIcons;
	Common::HashMap<uint32, Movie *> _iconMovies;
	Common::HashMap<uint, TimerState> _timerStates;
};

extern XFilesEngine *g_engine;

} // End of namespace XFiles

#endif // XFILES_XFILES_H
