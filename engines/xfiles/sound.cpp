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

#include "common/algorithm.h"
#include "common/debug.h"

#include "xfiles/detection.h"
#include "xfiles/discfiles.h"
#include "xfiles/movie.h"
#include "xfiles/script.h"
#include "xfiles/sound.h"
#include "xfiles/vcdata.h"

namespace XFiles {

SoundPlayer::SoundPlayer(const DiscFiles &files) : _files(files), _data(nullptr), _script(nullptr), _soundView(0) {
}

SoundPlayer::~SoundPlayer() {
	clear();
}

void SoundPlayer::attach(const VCData *data, Script *script) {
	_data = data;
	_script = script;
}

void SoundPlayer::play(uint32 id, byte mode, bool positional, int16 x, int16 y, bool sound3D) {
	if (mode == kModeStop) {
		stopSound(id);
		stopClip(id);
		return;
	}
	startSound(id, mode == kModePlayLoop, positional, x, y, 0, sound3D);
}

void SoundPlayer::restore(uint32 id, bool loop, uint32 startMs) {
	startSound(id, loop, false, 0, 0, startMs, false);
}

void SoundPlayer::startSound(uint32 id, bool loop, bool positional, int16 x, int16 y, uint32 startMs, bool sound3D) {
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
	if (!sound.movie->open(_files, asset)) {
		delete sound.movie;
		_script->finishSound(id);
		return;
	}
	// A restored sound's time is its in point (0x4a88d0)
	if (startMs)
		sound.movie->setStart(startMs);
	debugC(1, kDebugScript, "-- sound #%u %s%s from %u ms", id, asset.files[0].file.c_str(), loop ? ", looped" : "", startMs ? startMs : asset.files[0].start);
	if (positional)
		placeSound(sound, sound3D);
	sound.movie->play(loop);
	_sounds[id] = sound;
	Common::Array<uint32>::iterator it = Common::find(_soundOrder.begin(), _soundOrder.end(), id);
	if (it != _soundOrder.end())
		_soundOrder.erase(it);
	_soundOrder.push_back(id);
}

void SoundPlayer::playClip(uint32 id, int16 x, int16 y) {
	stopClip(id);
	Clip clip;
	clip.movie = new Movie();
	clip.x = x;
	clip.y = y;
	VCAsset asset;
	if (!_data->getAsset(id, asset)) {
		warning("#%u is not a media asset", id);
		delete clip.movie;
		_script->finishSound(id);
		return;
	}
	if (!clip.movie->open(_files, asset)) {
		delete clip.movie;
		_script->finishSound(id);
		return;
	}
	debugC(1, kDebugScript, "-- clip #%u at (%d, %d)", id, x, y);
	clip.movie->play(false);
	_clips[id] = clip;
}

void SoundPlayer::stopClip(uint32 id) {
	if (!_clips.contains(id))
		return;
	delete _clips[id].movie;
	_clips.erase(id);
}

void SoundPlayer::clearClips() {
	for (Common::HashMap<uint32, Clip>::iterator it = _clips.begin(); it != _clips.end(); ++it)
		delete it->_value.movie;
	_clips.clear();
}

void SoundPlayer::updateClips() {
	Common::Array<uint32> ended;
	for (Common::HashMap<uint32, Clip>::iterator it = _clips.begin(); it != _clips.end(); ++it) {
		it->_value.movie->update();
		if (it->_value.movie->hasEnded())
			ended.push_back(it->_key);
	}
	for (uint i = 0; i < ended.size(); i++) {
		stopClip(ended[i]);
		_script->finishSound(ended[i]);
	}
}

void SoundPlayer::drawClips(Graphics::Surface &dst) const {
	for (Common::HashMap<uint32, Clip>::const_iterator it = _clips.begin(); it != _clips.end(); ++it) {
		const Graphics::Surface &src = it->_value.movie->getFrame();
		if (!src.getPixels())
			continue;
		int x = it->_value.x, y = it->_value.y;
		Common::Rect r(x, y, x + src.w, y + src.h);
		r.clip(Common::Rect(dst.w, dst.h));
		if (r.isEmpty())
			continue;
		dst.copyRectToSurface(src, r.left, r.top, Common::Rect(r.left - x, r.top - y, r.right - x, r.bottom - y));
	}
}

void SoundPlayer::stopSound(uint32 id) {
	if (!_sounds.contains(id))
		return;
	delete _sounds[id].movie;
	_sounds.erase(id);
	Common::Array<uint32>::iterator it = Common::find(_soundOrder.begin(), _soundOrder.end(), id);
	if (it != _soundOrder.end())
		_soundOrder.erase(it);
}

void SoundPlayer::stopAll() {
	// StopEnvSounds: Script ends the sounds' objects afterwards
	for (Common::HashMap<uint32, Sound>::iterator it = _sounds.begin(); it != _sounds.end(); ++it)
		delete it->_value.movie;
	_sounds.clear();
	_soundOrder.clear();
}

void SoundPlayer::update(bool sound3D) {
	updateSounds(sound3D);
	updateClips();
}

void SoundPlayer::updateSounds(bool sound3D) {
	// 3D sounds are placed again when the player moves
	bool moved = _script->getView() != _soundView;
	_soundView = _script->getView();
	Common::Array<uint32> ended;
	for (Common::HashMap<uint32, Sound>::iterator it = _sounds.begin(); it != _sounds.end(); ++it) {
		if (moved && it->_value.positional)
			placeSound(it->_value, sound3D);
		it->_value.movie->update();
		if (it->_value.movie->hasEnded())
			ended.push_back(it->_key);
	}
	for (uint i = 0; i < ended.size(); i++) {
		stopSound(ended[i]);
		_script->finishSound(ended[i]);
	}
}

uint32 SoundPlayer::getNavTarget(uint32 view, uint32 hotSpot) const {
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

bool SoundPlayer::getViewPosition(uint32 view, Common::Point &pos) const {
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

bool SoundPlayer::getFacing(uint32 view, const Common::Point &from, Common::Point &facing) const {
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

void SoundPlayer::placeSound(Sound &sound, bool sound3D) {
	// XFiles.exe 0x4a8be0. Without the 3D sound preference the sound plays
	// at the volume of its type, as any other.
	VCViewPoint viewPoint;
	if (!sound3D || !_data->getViewPoint(_script->getViewPoint(), viewPoint)) {
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

void SoundPlayer::clear() {
	stopAll();
	clearClips();
	_soundView = 0;
}

void SoundPlayer::pause(bool pause) {
	for (Common::HashMap<uint32, Sound>::iterator it = _sounds.begin(); it != _sounds.end(); ++it)
		it->_value.movie->pause(pause);
	for (Common::HashMap<uint32, Clip>::iterator it = _clips.begin(); it != _clips.end(); ++it)
		it->_value.movie->pause(pause);
}

void SoundPlayer::getSaved(Common::Array<uint32> &triples) const {
	for (uint i = 0; i < _soundOrder.size(); i++) {
		triples.push_back(_soundOrder[i]);
		triples.push_back(1);
		triples.push_back(_sounds.getVal(_soundOrder[i]).movie->getTime());
	}
}

} // End of namespace XFiles
