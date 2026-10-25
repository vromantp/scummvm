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

#ifndef XFILES_PICTURES_H
#define XFILES_PICTURES_H

#include "common/array.h"
#include "common/scummsys.h"

namespace Graphics {
struct PixelFormat;
struct Surface;
}

namespace XFiles {

class DiscFiles;
class VCData;

/**
 * A picture asset in the screen format, with the exe's transparency rule
 * (XFiles.exe 0x429c70 with Pref_bTranslucency on) decided at load: a
 * picture with an alpha plane is drawn through it; one without is keyed on
 * pure blue if one of its four corner pixels is pure blue; otherwise it is
 * drawn opaque.
 */
class PictureAsset {
public:
	/** Takes the surface (screen format) and the alpha per pixel (w * h bytes, or nullptr for opaque). */
	PictureAsset(Graphics::Surface *surface, byte *alpha, byte *keyAlpha = nullptr);
	~PictureAsset();

	const Graphics::Surface &getSurface() const { return *_surface; }
	int getWidth() const;
	int getHeight() const;
	/** Draw at (x, y), clipped to dst: alpha 0 is skipped, 255 copied, between blended. */
	void draw(Graphics::Surface &dst, int x, int y) const;

	/**
	 * Pref_bTranslucency (0x42d790, 0x452d70): off, the pictures flagged with setPrefKeyed are keyed on pure blue
	 * (alpha 0 on the pixels of pure blue, else 255) instead of the alpha plane. Every other picture keeps the
	 * rule whatever the preference.
	 */
	static void setTranslucency(bool on) { _translucency = on; }
	/** Flag a picture whose keying depends on the preference: the two dialog-box folders (34761, 34760). */
	void setPrefKeyed(bool on) { _prefKeyed = on; }

private:
	static bool _translucency;
	Graphics::Surface *_surface;
	byte *_alpha;     ///< the translucency rule, or nullptr: opaque
	byte *_keyAlpha;  ///< the blue key, or nullptr: no pixel is pure blue (opaque); shares _alpha if equal
	bool _prefKeyed;  ///< drawn with _keyAlpha while the preference is off
};

/**
 * Load a picture asset: an entry of a .pff archive (QuickDraw PICTs
 * without the 512-byte file header), converted to the given format.
 * Returns nullptr if it cannot be read; the caller deletes the picture.
 */
PictureAsset *loadPicture(const VCData &data, const DiscFiles &files, uint32 assetId, const Graphics::PixelFormat &format);

/**
 * Load the frames of a picture asset that spans several entries of its
 * .pff archive (an animation, such as the Eye cursor), with the durations
 * the archive gives each frame in ms (0 if it gives none). The caller
 * frees the surfaces. Returns false if no frame can be read.
 */
bool loadFrames(const VCData &data, const DiscFiles &files, uint32 assetId, const Graphics::PixelFormat &format,
	Common::Array<Graphics::Surface *> &frames, Common::Array<uint32> &durations);

/**
 * Load every entry of a picture asset as a PictureAsset (the transparency rule applies to each), with the archive's
 * durations in ms. The caller deletes the pictures. Returns false if no frame can be read.
 */
bool loadPictureFrames(const VCData &data, const DiscFiles &files, uint32 assetId, const Graphics::PixelFormat &format,
	Common::Array<PictureAsset *> &frames, Common::Array<uint32> &durations);

} // End of namespace XFiles

#endif // XFILES_PICTURES_H
