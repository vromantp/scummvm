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

#ifndef XFILES_SUBLIMINAL_H
#define XFILES_SUBLIMINAL_H

#include "common/ustr.h"

namespace Common {
class WinResources;
}

namespace Graphics {
class Font;
struct Surface;
}

namespace XFiles {

class DiscFiles;
class Script;

/**
 * The subliminal messages of Pref_bSubliminalMessages: every 6 seconds a
 * word of XFilesS.dll flashes in dark grey in the middle of the screen
 * (XFiles.exe: the object at app +0x108, vtable 0x655e30).
 */
class Subliminal {
public:
	Subliminal();
	~Subliminal();

	/** Load XFilesS.dll and the font; false if either is missing. */
	bool load(const DiscFiles &files);

	/** Start the period (Prefs_Apply 0x452b20 with the preference set); nothing if running. */
	void start(uint32 now);
	void stop() { _running = _paused = false; }
	/** TextOn(0) stops the period (0x46c970); TextOn(1) starts it again from now, if it was running. */
	void pause();
	void resume(uint32 now);
	bool isRunning() const { return _running; }
	/**
	 * Pick and flash the words; now in ms. The three values are those of
	 * RegUberVars (loss, paranoia, X-track), which choose the word list.
	 * canShow is false while a video plays in the band without anything
	 * drawn over it.
	 */
	void update(uint32 now, Script &script, int32 loss, int32 paranoia, int32 xTrack, bool canShow);
	/** Draw the word picked this frame, once: it is gone from the next frame. */
	void draw(Graphics::Surface &dst);

private:
	Common::SeekableReadStream *_dll;  ///< XFilesS.dll; the resources read it lazily, so it lives as long as _strings
	Common::WinResources *_strings;
	Graphics::Font *_font;
	bool _running;
	bool _paused;              ///< Running when TextOn(0) stopped it
	uint32 _next;              ///< When the next word comes
	Common::U32String _text;   ///< The word to flash this frame
};

} // End of namespace XFiles

#endif // XFILES_SUBLIMINAL_H
