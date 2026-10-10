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

#include "common/array.h"
#include "common/str.h"
#include "common/textconsole.h"
#include "graphics/font.h"
#include "graphics/fonts/ttf.h"
#include "graphics/surface.h"

#include "xfiles/captions.h"

namespace XFiles {

enum {
	// The view band, where the location card goes (XFiles.exe: rectangle at 0x6bb978)
	kBandLeft = 20,
	kBandBottom = 330,
	kCardMargin = 15,
	// Typing: after a delay, three characters every 50 ms; the whole text
	// then stays four seconds (XFiles.exe 0x484340 and 0x42bd40)
	kCardDelay = 250,
	kCardStep = 3,
	kCardTick = 50,
	kCardHold = 4000,
	// Fonts: the card is Courier New 12 (character height), the text tracks
	// ask for Geneva 12. Both are replaced by metric-compatible fonts from
	// ScummVM's fonts.dat.
	kCardFontSize = 12,
	kCaptionFontSize = 12,
	kCaptionHeight = 20
};

// The days of the story: XFiles.exe's table at 0x6581c0. Day 0 is in March,
// the others in April, all in 1996.
static const int kStoryDays[8] = { 29, 2, 3, 4, 5, 6, 7, 8 };
static const int kStoryYear = 1996;

Captions::Captions() : _cardFont(nullptr), _captionFont(nullptr), _cardShown(0), _cardState(kCardNone), _cardNext(0) {
}

Captions::~Captions() {
	delete _cardFont;
	delete _captionFont;
}

bool Captions::loadFonts() {
#ifdef USE_FREETYPE2
	_cardFont = Graphics::loadTTFFontFromArchive("LiberationMono-Regular.ttf", kCardFontSize, Graphics::kTTFSizeModeCharacter);
	_captionFont = Graphics::loadTTFFontFromArchive("LiberationSans-Regular.ttf", kCaptionFontSize, Graphics::kTTFSizeModeCharacter);
#endif
	if (!_cardFont || !_captionFont)
		warning("Captions: cannot load the fonts from fonts.dat");
	return _cardFont || _captionFont;
}

Common::U32String Captions::formatCard(const Common::U32String &location, int32 dayNight, int32 currentTime, bool withTime) {
	// XFiles.exe 0x4e1490: "<location>\n<Month> <day>, <year>[\n<h>:<mm> AM]"
	int day = dayNight > 10 ? dayNight - 10 : dayNight;
	if (day < 0 || day > 7)
		day = 7;
	Common::U32String text = location;
	text += Common::U32String(Common::String::format("\n%s %d, %d", day ? "April" : "March", kStoryDays[day], kStoryYear));
	if (withTime) {
		// The time is read as a four-digit HHMM number
		int hours = (currentTime / 100) % 24;
		int minutes = currentTime % 100;
		int hours12 = hours % 12 ? hours % 12 : 12;
		text += Common::U32String(Common::String::format("\n%d:%02d %s", hours12, minutes, hours < 12 ? "AM" : "PM"));
	}
	return text;
}

void Captions::startCard(const Common::U32String &text, uint32 now) {
	_cardText = text;
	_cardShown = 0;
	_cardState = kCardDelay;
	_cardNext = now + kCardDelay;
}

void Captions::update(uint32 now) {
	while (_cardState != kCardNone && (int32)(now - _cardNext) >= 0) {
		switch (_cardState) {
		case kCardDelay:
		case kCardTyping:
			// As the exe: a tick that finds the text complete starts the hold
			if (_cardText.size() > _cardShown) {
				_cardShown = MIN<uint>(_cardShown + kCardStep, _cardText.size());
				_cardState = kCardTyping;
				_cardNext += kCardTick;
			} else {
				_cardState = kCardHolding;
				_cardNext += kCardHold;
			}
			break;
		case kCardHolding:
			_cardState = kCardNone;
			break;
		default:
			break;
		}
	}
}

/** Split at explicit line breaks. */
static void splitLines(const Common::U32String &text, Common::Array<Common::U32String> &lines) {
	Common::U32String line;
	for (uint i = 0; i < text.size(); i++) {
		if (text[i] == '\n') {
			lines.push_back(line);
			line.clear();
		} else {
			line += text[i];
		}
	}
	lines.push_back(line);
}

void Captions::drawCard(Graphics::Surface &dst) const {
	if (_cardState == kCardNone || !_cardFont)
		return;
	// The card is placed for the whole text, then typed into place
	Common::Array<Common::U32String> all, shown;
	splitLines(_cardText, all);
	splitLines(Common::U32String(_cardText.c_str(), _cardShown), shown);
	int lineHeight = _cardFont->getFontHeight();
	int top = kBandBottom - kCardMargin - (int)all.size() * lineHeight;
	int left = kBandLeft + kCardMargin;
	uint32 white = dst.format.RGBToColor(255, 255, 255);
	uint32 black = dst.format.RGBToColor(0, 0, 0);
	for (uint i = 0; i < shown.size(); i++) {
		int y = top + i * lineHeight;
		_cardFont->drawString(&dst, shown[i], left + 1, y + 1, dst.w - left - 1, black);
		_cardFont->drawString(&dst, shown[i], left, y, dst.w - left, white);
	}
}

void Captions::drawCaption(Graphics::Surface &dst, const Common::U32String &text, int left, int top, int width) const {
	if (text.empty() || !_captionFont)
		return;
	Common::Rect strip(left, top, left + width, top + kCaptionHeight);
	strip.clip(Common::Rect(dst.w, dst.h));
	dst.fillRect(strip, dst.format.RGBToColor(0, 0, 0));
	int y = top + (kCaptionHeight - _captionFont->getFontHeight()) / 2;
	_captionFont->drawString(&dst, text, left, y, width, dst.format.RGBToColor(255, 255, 255), Graphics::kTextAlignCenter);
}

} // End of namespace XFiles
