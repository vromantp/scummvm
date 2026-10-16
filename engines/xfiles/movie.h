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

#ifndef XFILES_MOVIE_H
#define XFILES_MOVIE_H

#include "audio/mixer.h"
#include "common/ustr.h"
#include "graphics/pixelformat.h"
#include "graphics/surface.h"

namespace Video {
class QuickTimeDecoder;
}

namespace XFiles {

class DiscFiles;
struct VCAsset;

/**
 * A QuickTime media asset (XV, XN, XG movies and the NAVn.NMV navigation
 * movies), played between the start and end time of its file entry, or
 * shown as a single frame (backgrounds). The last decoded frame stays
 * available after the end, converted to the screen format.
 */
class Movie {
public:
	Movie();
	~Movie();

	/**
	 * Open the first file entry of the asset that exists on the discs;
	 * false if none does or the file does not decode.
	 */
	bool open(const DiscFiles &files, const VCAsset &asset);
	void close();
	bool isOpen() const { return _decoder != nullptr; }
	uint32 getAssetId() const { return _assetId; }

	/** Play from the start time to the end time (or the end of the file). */
	void play(bool loop, int32 fromMs = -1);
	/**
	 * Move the in point before play(): the first play and a loop both start
	 * at startMs (a restored sound, 0x4a88d0), the end time stays.
	 */
	void setStart(uint32 startMs) { _startMs = startMs; }
	/** Decode the single frame at the start time. */
	bool showStill();
	/** Decode the single frame at ms (from the start of the file). */
	bool showStillAt(uint32 ms);
	/** Decode the last frame of the asset's range (the whole file if it has none). */
	bool showStillAtEnd();
	/** Decode a frame if one is due; handle looping and the end. */
	void update();
	/** Stop at once, keeping the last frame. */
	void stop();
	/** Pause or resume a playing movie (the game's pause); the time it was paused does not count. */
	void pause(bool pause);
	bool hasEnded() const { return _ended; }
	/** Where the movie is, in ms from the start of its file (not of the asset's range); 0 if it is not open. */
	uint32 getTime() const;
	bool isPlaying() const;
	int getCurFrame() const;

	/** Volume of the soundtrack, 0-255 (Audio::Mixer::kMaxChannelVolume). */
	void setVolume(byte volume);
	/** Balance of the soundtrack, -127 (left) to 127 (right). */
	void setBalance(int8 balance);

	uint16 getWidth() const;
	uint16 getHeight() const;

	/**
	 * The caption at the current time: the cue of the last text track that
	 * has one (QuickTime draws later tracks on top). Line breaks become
	 * spaces. Empty if nothing shows.
	 */
	Common::U32String getCaption() const;
	/** The last decoded frame in the screen format; empty before the first one. */
	const Graphics::Surface &getFrame() const { return _frame; }

private:
	static Audio::Mixer::SoundType getSoundType(const Common::String &file);
	void copyFrame(const Graphics::Surface *frame);

	Video::QuickTimeDecoder *_decoder;
	Graphics::PixelFormat _format;
	Graphics::Surface _frame;
	uint32 _assetId;
	uint32 _startMs, _endMs;
	bool _loop;
	bool _ended;
	bool _paused;         ///< pause() paused the decoder
};

} // End of namespace XFiles

#endif // XFILES_MOVIE_H
