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

#include "common/scummsys.h"

namespace Graphics {
struct PixelFormat;
struct Surface;
}

namespace XFiles {

class DiscFiles;
class VCData;

/**
 * Load a picture asset: an entry of a .pff archive (QuickDraw PICTs
 * without the 512-byte file header), converted to the given format.
 * Returns nullptr if it cannot be read; the caller frees the surface.
 */
Graphics::Surface *loadPicture(const VCData &data, const DiscFiles &files, uint32 assetId, const Graphics::PixelFormat &format);

/**
 * Draw a picture with pure blue as the transparent colour (as the dialog
 * box pictures use it). Both surfaces have the same format.
 */
void drawKeyed(Graphics::Surface &dst, const Graphics::Surface &src, int x, int y);

} // End of namespace XFiles

#endif // XFILES_PICTURES_H
