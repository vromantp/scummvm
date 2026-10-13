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

#ifndef XFILES_HEADCREDITS_H
#define XFILES_HEADCREDITS_H

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

/**
 * The head credits of RollHdCredits(): the strings of XFilesC.dll, one
 * every 4.5 seconds for 3 seconds, centred at the bottom of the view band
 * (XFiles.exe: the object at app +0x104, vtable 0x653ff8).
 */
class HeadCredits {
public:
	HeadCredits();
	~HeadCredits();

	/** Load XFilesC.dll and the Typist font (Hcd.ttr); false if either is missing. */
	bool load(const DiscFiles &files);

	/** A new game allows the credits; a restored one only if they were rolling. */
	void setEnabled(bool enabled) { _enabled = enabled; }
	/** RollHdCredits(): start unless disabled or rolling; skipIntro is the preference. */
	void roll(uint32 now, bool skipIntro);
	/** Take the credit on screen away now; the next one still comes. */
	void hide();
	/**
	 * Show and take away the credits; now in ms. canShow is false while a
	 * play-once clip plays or the dialog box is open.
	 */
	void update(uint32 now, bool canShow);
	void draw(Graphics::Surface &dst) const;

private:
	Common::SeekableReadStream *_dll;  ///< XFilesC.dll; the resources read it lazily, so it lives as long as _strings
	Common::WinResources *_strings;
	Graphics::Font *_font;
	bool _enabled;
	bool _rolling;
	bool _shownAny;            ///< A credit came since the start, shown or not (exe: +0x68)
	int _index;                ///< String of the last credit shown; -1 before the first
	uint32 _next;              ///< When the next credit comes
	uint32 _hideAt;            ///< When the credit on screen goes; 0 if none
	Common::U32String _text;   ///< The credit on screen
};

} // End of namespace XFiles

#endif // XFILES_HEADCREDITS_H
