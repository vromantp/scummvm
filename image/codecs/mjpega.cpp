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


#include "common/endian.h"
#include "common/memstream.h"
#include "common/textconsole.h"
#include "image/jpeg.h"

#include "image/codecs/mjpega.h"

namespace Image {

MJPEGADecoder::MJPEGADecoder() : Codec() {
	_pixelFormat = getDefaultYUVFormat();
	_accuracy = CodecAccuracy::Default;
}

MJPEGADecoder::~MJPEGADecoder() {
	_surface.free();
}

/**
 * Find the APP1 'mjpg' marker of the field starting at data and read the
 * field size and the offset of the next field (0 for the last field).
 */
static bool readFieldHeader(const byte *data, uint32 size, uint32 &fieldSize, uint32 &nextField) {
	if (size < 4 || READ_BE_UINT16(data) != 0xFFD8)
		return false;

	uint32 pos = 2;
	while (pos + 4 <= size && data[pos] == 0xFF) {
		byte marker = data[pos + 1];
		uint16 length = READ_BE_UINT16(data + pos + 2);

		if (marker == 0xDA) // Start of scan: no header found
			return false;

		if (marker == 0xE1 && length >= 18 && pos + 2 + length <= size && READ_BE_UINT32(data + pos + 8) == MKTAG('m', 'j', 'p', 'g')) {
			fieldSize = READ_BE_UINT32(data + pos + 12);
			nextField = READ_BE_UINT32(data + pos + 20);
			return true;
		}

		pos += 2 + length;
	}

	return false;
}

const Graphics::Surface *MJPEGADecoder::decodeFrame(Common::SeekableReadStream &stream) {
	uint32 size = stream.size();
	byte *data = (byte *)malloc(size);
	if (!data || stream.read(data, size) != size) {
		free(data);
		warning("MJPEGADecoder: Failed to read frame");
		return nullptr;
	}

	JPEGDecoder fields[2];
	uint fieldCount = 0;
	uint32 fieldStart = 0;

	while (fieldCount < 2 && fieldStart < size) {
		uint32 fieldSize = 0, nextField = 0;
		if (!readFieldHeader(data + fieldStart, size - fieldStart, fieldSize, nextField)) {
			// No field information: treat the remainder as a single image
			fieldSize = size - fieldStart;
			nextField = 0;
		}
		if (fieldSize == 0 || fieldSize > size - fieldStart)
			fieldSize = size - fieldStart;

		Common::MemoryReadStream fieldStream(data + fieldStart, fieldSize);
		fields[fieldCount].setCodecAccuracy(_accuracy);
		fields[fieldCount].setOutputPixelFormat(_pixelFormat);
		if (!fields[fieldCount].loadStream(fieldStream)) {
			free(data);
			warning("MJPEGADecoder: Failed to decode field %d", fieldCount);
			return nullptr;
		}
		fieldCount++;

		if (nextField == 0)
			break;
		fieldStart += nextField;
	}

	free(data);

	const Graphics::Surface *first = fields[0].getSurface();
	if (fieldCount == 1) {
		_surface.copyFrom(*first);
		return &_surface;
	}

	const Graphics::Surface *second = fields[1].getSurface();
	if (first->w != second->w || first->format != second->format) {
		warning("MJPEGADecoder: Fields have different formats");
		return nullptr;
	}

	if (_surface.w != first->w || _surface.h != first->h + second->h || _surface.format != first->format) {
		_surface.free();
		_surface.create(first->w, first->h + second->h, first->format);
	}

	// Interleave the fields, the first one on the even lines
	for (int y = 0; y < _surface.h; y++) {
		const Graphics::Surface *field = (y & 1) ? second : first;
		int fieldY = MIN<int>(y / 2, field->h - 1);
		memcpy(_surface.getBasePtr(0, y), field->getBasePtr(0, fieldY), _surface.w * _surface.format.bytesPerPixel);
	}

	return &_surface;
}

} // End of namespace Image
