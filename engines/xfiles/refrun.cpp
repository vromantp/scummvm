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

#include "common/file.h"
#include "common/system.h"
#include "image/png.h"

#include "xfiles/movie.h"
#include "xfiles/vcdata.h"
#include "xfiles/vcdump.h"
#include "xfiles/xfiles.h"

namespace XFiles {

// Comparison with reference runs: the xfiles_shots screenshots and the xfiles_steps step list

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
		// Done: quit once the videos have played (and a tool's delayed clip has started)
		if (queue.empty() && _delayedPlays.empty())
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
		if (step == "xclick:hot" || step == "xclick:hotlate") {
			// Engine only (B-177): wait until a .HOT entry of the view's clip is live (hot) or its window has
			// passed (hotlate), then click the middle of its rectangle
			bool late = step == "xclick:hotlate";
			if (_hotMovie && !_hotMovie->hasEnded() && queue.empty()) {
				uint32 now = _hotMovie->getTime();
				const Common::Array<HotEntry> &entries = _hot.getEntries();
				for (uint i = 0; i < entries.size(); i++) {
					const HotEntry &e = entries[i];
					if (late ? now >= e.endMs : (now >= e.startMs && now < e.endMs)) {
						Common::Point band = getBandPosition(*_hotMovie);
						Common::Point pos(band.x + (e.rect.left + e.rect.right) / 2, band.y + (e.rect.top + e.rect.bottom) / 2);
						debugC(1, kDebugScript, "-- xclick %s at %d,%d (clip time %u)", late ? "after the window" : "in the window", pos.x, pos.y, now);
						_mouse = pos;
						handleClick(pos);
						return;
					}
				}
			}
			_nextStep--; // not yet: try again next frame
			return;
		}
		if (step.hasPrefix("xclick:")) {
			// Engine only: a click at a screen position (the menu's while it is up)
			int x = 0, y = 0;
			if (sscanf(step.c_str() + 7, "%d,%d", &x, &y) == 2) {
				Common::Point pos(x, y);
				_mouse = pos;
				if (_menuOpen)
					menuChoice(clickMenu(pos));
				else if (queue.empty())
					handleClick(pos);
			}
			return;
		}
		if (step.hasPrefix("state:")) {
			// Engine only: write the state as `state json FILE` does
			VCSave snapshot;
			fillSnapshot(snapshot);
			Common::String json = dumpState(*_data, *_script, &snapshot);
			Common::DumpFile file;
			if (!file.open(Common::Path(step.substr(6), '/'), true) || file.write(json.c_str(), json.size()) != json.size())
				warning("Cannot write the state to %s", step.c_str() + 6);
			continue;
		}
		if (step.hasPrefix("savegame:") || step.hasPrefix("loadgame:")) {
			// Engine only: save to or load from a ScummVM slot (the load runs at the start of the next frame)
			int slot = atoi(step.c_str() + 9);
			Common::Error error = step[0] == 's' ? saveGameState(slot, "step") : loadGameState(slot);
			if (error.getCode() != Common::kNoError)
				warning("%s failed: %s", step.c_str(), error.getDesc().c_str());
			return;
		}
		if (step == "escape") {
			// Engine only: the Escape key
			if (_menuLayout)
				escapeMenuLayout();
			else if (!_menuOpen && queue.empty())
				openGameMenu();
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
			if (_script->opensBoxOnClick())
				openDialogBox(cv);
		} else if (step.hasPrefix("ask:")) {
			askTopic(atoi(step.c_str() + 4));
		} else {
			_script->step(step);
		}
		return;
	}
}

} // End of namespace XFiles
