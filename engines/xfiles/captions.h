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

#ifndef XFILES_CAPTIONS_H
#define XFILES_CAPTIONS_H

#include "common/ustr.h"

namespace Graphics {
class Font;
struct Surface;
}

namespace XFiles {

/**
 * The two kinds of text the game puts over its videos:
 *
 * - the location card of StartVideoText ("FBI FIELD OFFICE, SEATTLE",
 *   date, time), typed in at the bottom left of the view band;
 * - the videos' closed captions (QuickTime text tracks), in a strip below
 *   the video.
 */
class Captions {
public:
	Captions();
	~Captions();

	/** Load the fonts; false if neither is available. */
	bool loadFonts();

	/**
	 * The text of StartVideoText: the location name, the date of day
	 * dayNight (above 10 for the night) and, if withTime, the time of
	 * currentTime (hours * 100 + minutes).
	 */
	static Common::U32String formatCard(const Common::U32String &location, int32 dayNight, int32 currentTime, bool withTime);

	/** Start typing a location card; it replaces the one on screen. */
	void startCard(const Common::U32String &text, uint32 now);
	/** Type the card on and take it away after its time; now in ms. */
	void update(uint32 now);
	bool hasCard() const { return _cardState != kCardNone; }
	/** Take the card away (a new game). */
	void reset() { _cardState = kCardNone; }

	/** Draw the card, with its bottom left corner in the view band. */
	void drawCard(Graphics::Surface &dst) const;
	/** Draw a caption in the strip below a video at (left, top) with the given width. */
	void drawCaption(Graphics::Surface &dst, const Common::U32String &text, int left, int top, int width) const;

private:
	enum CardState {
		kCardNone,
		kCardDelay,   ///< Waiting before the first characters
		kCardTyping,
		kCardHolding  ///< All of it shows
	};

	Graphics::Font *_cardFont;
	Graphics::Font *_captionFont;
	Common::U32String _cardText;
	uint _cardShown;     ///< Characters typed so far
	CardState _cardState;
	uint32 _cardNext;    ///< When the state changes or the next characters are typed
};

} // End of namespace XFiles

#endif // XFILES_CAPTIONS_H
