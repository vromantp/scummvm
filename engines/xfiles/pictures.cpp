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

#include "common/ptr.h"
#include "common/stream.h"
#include "common/substream.h"
#include "common/textconsole.h"
#include "graphics/surface.h"
#include "image/pict.h"

#include "xfiles/discfiles.h"
#include "xfiles/pictures.h"
#include "xfiles/vcdata.h"

namespace XFiles {

// A .pff archive: "PFF ", the entry count n, n + 1 entry offsets (the last
// is the file size), then n frame durations in ms (0 in archives that are
// not animations). The asset's media file gives the first and last entry.
static Common::SeekableReadStream *openArchive(const VCData &data, const DiscFiles &files, uint32 assetId, VCMediaFile &file, uint32 &count) {
	VCAsset asset;
	if (!data.getAsset(assetId, asset) || asset.files.empty()) {
		warning("Pictures: #%u is not a picture asset", assetId);
		return nullptr;
	}
	file = asset.files[0];
	Common::SeekableReadStream *stream = files.openFile(file.disc, file.file);
	if (!stream) {
		warning("Pictures: cannot open %s for #%u", file.file.c_str(), assetId);
		return nullptr;
	}
	if (stream->readUint32BE() != MKTAG('P', 'F', 'F', ' ')) {
		warning("Pictures: %s is not a .pff archive", file.file.c_str());
		delete stream;
		return nullptr;
	}
	count = stream->readUint32LE();
	return stream;
}

static Graphics::Surface *decodeEntry(Common::SeekableReadStream &stream, uint32 count, uint32 index, const Graphics::PixelFormat &format) {
	if (index >= count)
		return nullptr;
	stream.seek(8 + index * 4);
	uint32 start = stream.readUint32LE();
	uint32 end = stream.readUint32LE();
	if (stream.err() || start >= end || end > stream.size())
		return nullptr;
	Common::SeekableSubReadStream entry(&stream, start, end);
	Image::PICTDecoder decoder;
	if (!decoder.loadStream(entry) || !decoder.getSurface())
		return nullptr;
	const Graphics::Palette &palette = decoder.getPalette();
	return decoder.getSurface()->convertTo(format, palette.data(), palette.size());
}

Graphics::Surface *loadPicture(const VCData &data, const DiscFiles &files, uint32 assetId, const Graphics::PixelFormat &format) {
	VCMediaFile file;
	uint32 count;
	Common::ScopedPtr<Common::SeekableReadStream> stream(openArchive(data, files, assetId, file, count));
	if (!stream)
		return nullptr;
	Graphics::Surface *picture = decodeEntry(*stream, count, file.start, format);
	if (!picture)
		warning("Pictures: cannot decode entry %u of %s for #%u", file.start, file.file.c_str(), assetId);
	return picture;
}

bool loadFrames(const VCData &data, const DiscFiles &files, uint32 assetId, const Graphics::PixelFormat &format,
		Common::Array<Graphics::Surface *> &frames, Common::Array<uint32> &durations) {
	VCMediaFile file;
	uint32 count;
	Common::ScopedPtr<Common::SeekableReadStream> stream(openArchive(data, files, assetId, file, count));
	if (!stream)
		return false;
	for (uint32 i = file.start; i <= file.end && i < count; i++) {
		Graphics::Surface *frame = decodeEntry(*stream, count, i, format);
		if (!frame) {
			warning("Pictures: cannot decode entry %u of %s for #%u", i, file.file.c_str(), assetId);
			break;
		}
		stream->seek(12 + count * 4 + i * 4);
		frames.push_back(frame);
		durations.push_back(stream->readUint32LE());
	}
	return !frames.empty();
}

void drawKeyed(Graphics::Surface &dst, const Graphics::Surface &src, int x, int y) {
	uint32 key = src.format.RGBToColor(0, 0, 255);
	for (int sy = 0; sy < src.h; sy++) {
		int dy = y + sy;
		if (dy < 0 || dy >= dst.h)
			continue;
		for (int sx = 0; sx < src.w; sx++) {
			int dx = x + sx;
			if (dx < 0 || dx >= dst.w)
				continue;
			uint32 color = src.getPixel(sx, sy);
			if (color != key)
				dst.setPixel(dx, dy, color);
		}
	}
}

} // End of namespace XFiles
