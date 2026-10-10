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


#ifndef IMAGE_CODECS_MJPEGA_H
#define IMAGE_CODECS_MJPEGA_H

#include "image/codecs/codec.h"
#include "graphics/pixelformat.h"
#include "graphics/surface.h"

namespace Common {
class SeekableReadStream;
}

namespace Image {

/**
 * Motion JPEG format A decoder.
 *
 * Each sample holds one JPEG image per field; an APP1 'mjpg' marker
 * gives the offset of the next field. Two fields are interleaved into
 * one frame, the first one on the even lines.
 *
 * Used by QuickTime videos in:
 *  - (none yet)
 */
class MJPEGADecoder : public Codec {
public:
	MJPEGADecoder();
	~MJPEGADecoder() override;

	const Graphics::Surface *decodeFrame(Common::SeekableReadStream &stream) override;
	void setCodecAccuracy(CodecAccuracy accuracy) override { _accuracy = accuracy; }
	Graphics::PixelFormat getPixelFormat() const override { return _pixelFormat; }
	bool setOutputPixelFormat(const Graphics::PixelFormat &format) override {
		if (format.isCLUT8())
			return false;
		_pixelFormat = format;
		return true;
	}

private:
	Graphics::PixelFormat _pixelFormat;
	Graphics::Surface _surface;
	CodecAccuracy _accuracy;
};

} // End of namespace Image

#endif
