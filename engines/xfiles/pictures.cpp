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

Graphics::Surface *loadPicture(const VCData &data, const DiscFiles &files, uint32 assetId, const Graphics::PixelFormat &format) {
	// A .pff archive: "PFF ", the entry count n, n + 1 entry offsets (the
	// last is the file size). The asset's media file gives the entry index.
	VCAsset asset;
	if (!data.getAsset(assetId, asset) || asset.files.empty()) {
		warning("Pictures: #%u is not a picture asset", assetId);
		return nullptr;
	}
	const VCMediaFile &file = asset.files[0];
	Common::ScopedPtr<Common::SeekableReadStream> stream(files.openFile(file.disc, file.file));
	if (!stream) {
		warning("Pictures: cannot open %s for #%u", file.file.c_str(), assetId);
		return nullptr;
	}
	uint32 index = file.start;
	if (stream->readUint32BE() != MKTAG('P', 'F', 'F', ' ') || index >= stream->readUint32LE()) {
		warning("Pictures: %s has no entry %u", file.file.c_str(), index);
		return nullptr;
	}
	stream->seek(8 + index * 4);
	uint32 start = stream->readUint32LE();
	uint32 end = stream->readUint32LE();
	if (stream->err() || start >= end || end > stream->size()) {
		warning("Pictures: bad entry %u in %s", index, file.file.c_str());
		return nullptr;
	}
	Common::SeekableSubReadStream entry(stream.get(), start, end);
	Image::PICTDecoder decoder;
	if (!decoder.loadStream(entry) || !decoder.getSurface()) {
		warning("Pictures: cannot decode #%u", assetId);
		return nullptr;
	}
	const Graphics::Palette &palette = decoder.getPalette();
	return decoder.getSurface()->convertTo(format, palette.data(), palette.size());
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
