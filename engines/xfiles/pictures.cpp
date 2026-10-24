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

static bool isBlue(const Graphics::Surface &surface, const Graphics::Palette &palette, int x, int y) {
	byte r, g, b;
	uint32 color = surface.getPixel(x, y);
	if (surface.format.isCLUT8())
		palette.get(color, r, g, b);
	else
		surface.format.colorToRGB(color, r, g, b);
	return r == 0 && g == 0 && b == 255;
}

// With alpha, the decoder keeps the alpha plane of 32-bit pictures and the
// surface is ARGB32
static Graphics::Surface *decodeEntry(Common::SeekableReadStream &stream, uint32 count, uint32 index, const Graphics::PixelFormat &format,
		byte **alphaOut = nullptr, byte **keyOut = nullptr) {
	if (index >= count)
		return nullptr;
	stream.seek(8 + index * 4);
	uint32 start = stream.readUint32LE();
	uint32 end = stream.readUint32LE();
	if (stream.err() || start >= end || end > stream.size())
		return nullptr;
	Common::SeekableSubReadStream entry(&stream, start, end);
	Image::PICTDecoder decoder;
	decoder.setKeepAlpha(alphaOut != nullptr);
	if (!decoder.loadStream(entry) || !decoder.getSurface())
		return nullptr;
	const Graphics::Surface &src = *decoder.getSurface();
	const Graphics::Palette &palette = decoder.getPalette();
	if (alphaOut) {
		*alphaOut = nullptr;
		if (keyOut)
			*keyOut = nullptr;
		byte *alpha = nullptr;
		byte *key = nullptr;
		if (src.format.aBits()) {
			alpha = new byte[src.w * src.h];
			for (int y = 0; y < src.h; y++) {
				for (int x = 0; x < src.w; x++) {
					byte a, r, g, b;
					src.format.colorToARGB(src.getPixel(x, y), a, r, g, b);
					alpha[y * src.w + x] = a;
				}
			}
		} else if (src.w && src.h) {
			const int xs[2] = { 0, src.w - 1 }, ys[2] = { 0, src.h - 1 };
			bool keyed = false;
			for (int i = 0; i < 4 && !keyed; i++)
				keyed = isBlue(src, palette, xs[i & 1], ys[i >> 1]);
			if (keyed) {
				alpha = new byte[src.w * src.h];
				for (int y = 0; y < src.h; y++) {
					for (int x = 0; x < src.w; x++)
						alpha[y * src.w + x] = isBlue(src, palette, x, y) ? 0 : 255;
				}
			}
		}
		// The blue key (translucency off): pure blue pixels are transparent. Where the rule already is that key
		// (no alpha plane, a blue corner) the two arrays are the same
		if (keyOut && src.w && src.h) {
			bool any = false;
			byte *k = new byte[src.w * src.h];
			for (int y = 0; y < src.h; y++) {
				for (int x = 0; x < src.w; x++) {
					bool blue = isBlue(src, palette, x, y);
					any |= blue;
					k[y * src.w + x] = blue ? 0 : 255;
				}
			}
			if (!any)
				delete[] k;
			else if (alpha && !src.format.aBits()) {
				delete[] k;
				key = alpha;
			} else
				key = k;
		}
		*alphaOut = alpha;
		if (keyOut)
			*keyOut = key;
	}
	return src.convertTo(format, palette.data(), palette.size());
}

bool PictureAsset::_translucency = true;

PictureAsset::PictureAsset(Graphics::Surface *surface, byte *alpha, byte *keyAlpha)
		: _surface(surface), _alpha(alpha), _keyAlpha(keyAlpha) {
}

PictureAsset::~PictureAsset() {
	_surface->free();
	delete _surface;
	if (_keyAlpha != _alpha)
		delete[] _keyAlpha;
	delete[] _alpha;
}

int PictureAsset::getWidth() const {
	return _surface->w;
}

int PictureAsset::getHeight() const {
	return _surface->h;
}

void PictureAsset::draw(Graphics::Surface &dst, int x, int y) const {
	const byte *alpha = _translucency ? _alpha : _keyAlpha;
	for (int sy = 0; sy < _surface->h; sy++) {
		int dy = y + sy;
		if (dy < 0 || dy >= dst.h)
			continue;
		for (int sx = 0; sx < _surface->w; sx++) {
			int dx = x + sx;
			if (dx < 0 || dx >= dst.w)
				continue;
			uint32 color = _surface->getPixel(sx, sy);
			byte a = alpha ? alpha[sy * _surface->w + sx] : 255;
			if (a == 255) {
				dst.setPixel(dx, dy, color);
			} else if (a) {
				byte sr, sg, sb, dr, dg, db;
				_surface->format.colorToRGB(color, sr, sg, sb);
				dst.format.colorToRGB(dst.getPixel(dx, dy), dr, dg, db);
				dst.setPixel(dx, dy, dst.format.RGBToColor((sr * a + dr * (255 - a)) / 255, (sg * a + dg * (255 - a)) / 255,
					(sb * a + db * (255 - a)) / 255));
			}
		}
	}
}

PictureAsset *loadPicture(const VCData &data, const DiscFiles &files, uint32 assetId, const Graphics::PixelFormat &format) {
	VCMediaFile file;
	uint32 count;
	Common::ScopedPtr<Common::SeekableReadStream> stream(openArchive(data, files, assetId, file, count));
	if (!stream)
		return nullptr;
	byte *alpha, *key;
	Graphics::Surface *surface = decodeEntry(*stream, count, file.start, format, &alpha, &key);
	if (!surface) {
		warning("Pictures: cannot decode entry %u of %s for #%u", file.start, file.file.c_str(), assetId);
		return nullptr;
	}
	return new PictureAsset(surface, alpha, key);
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

bool loadPictureFrames(const VCData &data, const DiscFiles &files, uint32 assetId, const Graphics::PixelFormat &format,
		Common::Array<PictureAsset *> &frames, Common::Array<uint32> &durations) {
	VCMediaFile file;
	uint32 count;
	Common::ScopedPtr<Common::SeekableReadStream> stream(openArchive(data, files, assetId, file, count));
	if (!stream)
		return false;
	for (uint32 i = file.start; i <= file.end && i < count; i++) {
		byte *alpha, *key;
		Graphics::Surface *surface = decodeEntry(*stream, count, i, format, &alpha, &key);
		if (!surface) {
			warning("Pictures: cannot decode entry %u of %s for #%u", i, file.file.c_str(), assetId);
			break;
		}
		frames.push_back(new PictureAsset(surface, alpha, key));
		stream->seek(12 + count * 4 + i * 4);
		durations.push_back(stream->readUint32LE());
	}
	return !frames.empty();
}

} // End of namespace XFiles
