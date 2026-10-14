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

#include "common/debug.h"
#include "common/system.h"
#include "common/textconsole.h"
#include "video/qt_decoder.h"

#include "xfiles/detection.h"
#include "xfiles/discfiles.h"
#include "xfiles/movie.h"
#include "xfiles/vcdata.h"

namespace XFiles {

Audio::Mixer::SoundType Movie::getSoundType(const Common::String &file) {
	// XFiles.exe gives each file type a sound type (table at 0x65c380) and
	// sets the volume from the matching preference (0x469fb0): .amv sound
	// effects, .mus and .mmv music, everything else (.dmv, the videos) dialogue.
	if (file.hasSuffixIgnoreCase(".amv"))
		return Audio::Mixer::kSFXSoundType;
	if (file.hasSuffixIgnoreCase(".mus") || file.hasSuffixIgnoreCase(".mmv"))
		return Audio::Mixer::kMusicSoundType;
	return Audio::Mixer::kSpeechSoundType;
}

Movie::Movie() : _decoder(nullptr), _assetId(0), _startMs(0), _endMs(0), _loop(false), _ended(false), _paused(false) {
	_format = g_system->getScreenFormat();
}

Movie::~Movie() {
	close();
}

bool Movie::open(const DiscFiles &files, const VCAsset &asset) {
	close();
	for (uint i = 0; i < asset.files.size(); i++) {
		const VCMediaFile &f = asset.files[i];
		Common::SeekableReadStream *stream = files.openFile(f.disc, f.file);
		if (!stream)
			continue;
		_decoder = new Video::QuickTimeDecoder();
		_decoder->setSoundType(getSoundType(f.file));
		if (!_decoder->loadStream(stream)) {
			warning("Movie: cannot decode %s (asset #%u)", f.file.c_str(), asset.id);
			delete _decoder;
			_decoder = nullptr;
			return false;
		}
		_decoder->setOutputPixelFormat(_format);
		_assetId = asset.id;
		_startMs = f.start;
		_endMs = f.end;
		debugC(2, kDebugFiles, "Movie: #%u is disc %u %s, %u-%u ms", asset.id, f.disc, f.file.c_str(), f.start, f.end);
		return true;
	}
	warning("Movie: no file of asset #%u found", asset.id);
	return false;
}

void Movie::close() {
	delete _decoder;
	_decoder = nullptr;
	_frame.free();
	_assetId = 0;
	_ended = false;
	_paused = false;
}

void Movie::play(bool loop, int32 fromMs) {
	// fromMs >= 0: this play starts there (a restored view's clip time,
	// 0x51d200); a loop still goes back to the start time
	if (!_decoder)
		return;
	_loop = loop;
	_ended = false;
	if (fromMs >= 0)
		_decoder->seek(Audio::Timestamp(fromMs, 1000));
	else if (_startMs)
		_decoder->seek(Audio::Timestamp(_startMs, 1000));
	if (_endMs > _startMs)
		_decoder->setEndTime(Audio::Timestamp(_endMs, 1000));
	_decoder->start();
}

bool Movie::showStill() {
	if (!_decoder)
		return false;
	if (_startMs)
		_decoder->seek(Audio::Timestamp(_startMs, 1000));
	const Graphics::Surface *frame = _decoder->decodeNextFrame();
	if (!frame) {
		warning("Movie: no frame at %u ms in asset #%u", _startMs, _assetId);
		return false;
	}
	copyFrame(frame);
	_ended = true;
	return true;
}

void Movie::update() {
	if (!_decoder || _ended)
		return;
	if (_decoder->needsUpdate()) {
		const Graphics::Surface *frame = _decoder->decodeNextFrame();
		if (frame)
			copyFrame(frame);
	}
	if (_decoder->endOfVideo()) {
		if (_loop) {
			_decoder->seek(Audio::Timestamp(_startMs, 1000));
			if (!_decoder->isPlaying())
				_decoder->start();
		} else {
			stop();
		}
	}
}

void Movie::stop() {
	if (_decoder && _decoder->isPlaying())
		_decoder->pauseVideo(true);
	_ended = true;
}

void Movie::pause(bool pause) {
	if (!_decoder || pause == _paused)
		return;
	// A stopped movie or a still is not playing
	if (pause && (_ended || !_decoder->isPlaying()))
		return;
	_paused = pause;
	_decoder->pauseVideo(pause);
}

int Movie::getCurFrame() const {
	return _decoder ? _decoder->getCurFrame() : -1;
}

void Movie::setVolume(byte volume) {
	if (_decoder)
		_decoder->setVolume(volume);
}

void Movie::setBalance(int8 balance) {
	if (_decoder)
		_decoder->setBalance(balance);
}

uint16 Movie::getWidth() const {
	return _decoder ? _decoder->getWidth() : 0;
}

uint16 Movie::getHeight() const {
	return _decoder ? _decoder->getHeight() : 0;
}

Common::U32String Movie::getCaption() const {
	if (!_decoder || _ended)
		return Common::U32String();
	uint32 time = _decoder->getTime();
	for (int i = (int)_decoder->getTextTrackCount() - 1; i >= 0; i--) {
		const Common::Array<Video::QuickTimeDecoder::TextCue> &cues = _decoder->getTextCues(i);
		for (uint j = 0; j < cues.size(); j++) {
			if (time < cues[j].startTime || time >= cues[j].endTime)
				continue;
			Common::String text = cues[j].text;
			for (uint k = 0; k < text.size(); k++) {
				if (text[k] == '\r' || text[k] == '\n')
					text.setChar(' ', k);
			}
			text.trim();
			return Common::U32String(text, Common::kMacRoman);
		}
	}
	return Common::U32String();
}

void Movie::copyFrame(const Graphics::Surface *frame) {
	_frame.free();
	if (frame->format == _format) {
		_frame.copyFrom(*frame);
		return;
	}
	Graphics::Surface *converted = frame->convertTo(_format, _decoder->getPalette());
	_frame.copyFrom(*converted);
	converted->free();
	delete converted;
}

} // End of namespace XFiles
