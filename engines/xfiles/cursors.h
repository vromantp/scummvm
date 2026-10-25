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

#ifndef XFILES_CURSORS_H
#define XFILES_CURSORS_H

#include "common/array.h"
#include "common/hashmap.h"
#include "graphics/pixelformat.h"

namespace Graphics {
struct Surface;
struct WinCursorGroup;
}

namespace XFiles {

class DiscFiles;
class PictureAsset;
class VCData;

/**
 * The mouse cursor. XFiles.exe shows the VCCursor of the object under the
 * mouse as a sprite, with the Windows cursor hidden; with no VCCursor it
 * shows the Windows cursor 129 of XFiles.exe (0x4463b0). Cursors of
 * several .pff entries are animations, stepped with the archive's frame
 * durations (0x448770).
 */
class Cursors {
public:
	Cursors(const VCData &data, const DiscFiles &files);
	~Cursors();

	/** Load the default cursor from XFiles.exe and show it. */
	void init(const Graphics::PixelFormat &format);
	/** Passed to show(): the tool cursor, group 132 of XFiles.exe (Screen_SetToolCursor, shown with the gun in hand). */
	static const uint32 kToolCursorId = 0x7fffff84;
	/** Show a VCCursor; 0 shows the default cursor. */
	void show(uint32 cursorId);
	/**
	 * Step the frames of an animated cursor. Pref_bTranslucency (translucent): on, a cursor picture is drawn through its
	 * alpha plane; off, keyed on pure blue (Prefs_Apply 0x452d70). A change shows the cursor again.
	 */
	void update(bool translucent);

private:
	struct Cursor {
		int16 hotX, hotY;
		Common::Array<PictureAsset *> frames;
		Common::Array<Graphics::Surface *> argb; ///< Per frame, made when first shown with translucency
		Common::Array<uint32> durations;   ///< ms per frame
	};

	/** Load a VCCursor (once); nullptr if it has no picture. */
	Cursor *getCursor(uint32 cursorId);
	void showDefault();
	void showFrame();
	/** The frame with its alpha plane in an ARGB surface (made once). */
	const Graphics::Surface &getArgbFrame(Cursor &cursor, uint frame);

	const VCData &_data;
	const DiscFiles &_files;
	Graphics::PixelFormat _format;
	Graphics::WinCursorGroup *_default;
	Graphics::WinCursorGroup *_tool;
	Common::HashMap<uint32, Cursor *> _cursors;   ///< Loaded cursors; nullptr for those without a picture
	uint32 _shown;                ///< The VCCursor shown; 0: the default
	Cursor *_cursor;              ///< Its pictures; nullptr: the default
	bool _translucent;            ///< The preference the shown frame was made for
	uint _frame;
	uint32 _frameStart;           ///< getMillis() when the frame was shown
};

} // End of namespace XFiles

#endif // XFILES_CURSORS_H
