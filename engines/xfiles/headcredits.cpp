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
#include "common/debug.h"
#include "common/formats/winexe.h"
#include "common/textconsole.h"
#include "graphics/font.h"
#include "graphics/fonts/ttf.h"
#include "graphics/surface.h"

#include "xfiles/detection.h"
#include "xfiles/discfiles.h"
#include "xfiles/headcredits.h"

namespace XFiles {

enum {
	// Timers of XFiles.exe 0x45be60: a periodic one brings the next credit,
	// a one-shot one started with each credit takes it away
	kCreditPeriod = 4500,
	kCreditShown = 3000,
	// The first roll starts at string 4 unless the intro is skipped, any
	// later one at 1 (0x44ec40); presumably the intro has the first three
	kFirstCredit = 1,
	kFirstCreditAfterIntro = 4,
	// The font is Typist (Hcd.ttr), 18 px character height, regular. GDI
	// gives it lines of 20 with an ascent of 16 (font.metrics in the
	// reference run); FreeType's are smaller, so lines are placed by these
	kFontSize = 18,
	kLineHeight = 20,
	kAscent = 16,
	// The text is centred on the screen, its bottom 5 px above the bottom
	// of the view band and its shadow below that (0x459390, rectangles
	// 0x6b7c10 and 0x6b7c40)
	kCentreX = 320,
	kBottom = 330 - 5
};

HeadCredits::HeadCredits() : _dll(nullptr), _strings(nullptr), _font(nullptr), _enabled(false), _rolling(false),
	_shownAny(false), _index(-1), _next(0), _hideAt(0) {
}

HeadCredits::~HeadCredits() {
	delete _strings;
	delete _dll;
	delete _font;
}

bool HeadCredits::load(const DiscFiles &files) {
	// The exe loads XFilesC.dll with LoadLibrary and registers Hcd.ttr with
	// AddFontResource
	_dll = files.openFile(0, "XFilesC.dll");
	if (_dll)
		_strings = Common::WinResources::createFromEXE(_dll);
#ifdef USE_FREETYPE2
	Common::SeekableReadStream *font = files.openFile(0, "Hcd.ttr");
	if (font)
		_font = Graphics::loadTTFFont(font, DisposeAfterUse::YES, kFontSize, Graphics::kTTFSizeModeCharacter, 0, 0,
			Graphics::kTTFRenderModeMonochrome);
#endif
	if (!_strings || !_font)
		warning("HeadCredits: cannot load XFilesC.dll or Hcd.ttr");
	return _strings && _font;
}

void HeadCredits::roll(uint32 now, bool skipIntro, int first) {
	// RollHdCredits (0x45aab0) and its start (0x45be60)
	if (!_enabled || _rolling || !_strings)
		return;
	_rolling = true;
	bool fromStart = _shownAny || skipIntro;
	_index = first >= 1 ? first - 1 : (fromStart ? kFirstCredit : kFirstCreditAfterIntro) - 1;
	_next = now + kCreditPeriod;
	_hideAt = 0;
	_text.clear();
	debugC(1, kDebugScript, "-- head credits roll at %u", now);
}

void HeadCredits::hide() {
	// 0x45c390, when the dialog box opens
	_text.clear();
	_hideAt = 0;
}

void HeadCredits::reset() {
	// Game_Start stops the credits; the new game's roll starts as at first
	_rolling = false;
	_shownAny = false;
	_index = -1;
	hide();
}

void HeadCredits::update(uint32 now, bool canShow) {
	// 0x401ee0, every frame
	if (!_rolling)
		return;
	bool due = (int32)(now - _next) >= 0;
	// A clip or the dialog box coming up takes the credit away (0x441190,
	// 0x42b272, 0x4767a0)
	if (due || !canShow || (_hideAt && (int32)(now - _hideAt) >= 0))
		hide();
	if (!due)
		return;
	_next += kCreditPeriod;
	// The credits stop at the first string missing
	Common::String text = _strings->loadString(_index + 1);
	if (text.empty()) {
		_rolling = false;
		debugC(1, kDebugScript, "-- head credits end at %u", now);
		return;
	}
	// A credit that cannot be shown comes again with the next period: the
	// show handler (0x44f170) asks 0x442b50 and steps the index back
	_shownAny = true;
	debugC(1, kDebugScript, "-- head credit %d at %u%s", _index + 1, now, canShow ? "" : ", not shown");
	if (!canShow)
		return;
	_index++;
	_text = Common::U32String(text, Common::kWindows1252);
	_hideAt = now + kCreditShown;
}

void HeadCredits::draw(Graphics::Surface &dst) const {
	if (_text.empty() || !_font)
		return;
	Common::Array<Common::U32String> lines;
	Common::U32String line;
	for (uint i = 0; i < _text.size(); i++) {
		if (_text[i] == '\n') {
			lines.push_back(line);
			line.clear();
		} else if (_text[i] != '\r') {
			line += _text[i];
		}
	}
	lines.push_back(line);
	int width = 0;
	for (uint i = 0; i < lines.size(); i++)
		width = MAX(width, _font->getStringWidth(lines[i]));
	// DrawTextA with DT_CENTER and a black copy one pixel down and right;
	// in the reference run string 4 (two lines of 20, 228 x 40) is drawn at
	// (205, 285) and its shadow at (206, 286)
	int left = kCentreX - width / 2 - 1;
	int top = kBottom - (int)lines.size() * kLineHeight + kAscent - _font->getFontAscent();
	uint32 white = dst.format.RGBToColor(255, 255, 255);
	uint32 black = dst.format.RGBToColor(0, 0, 0);
	for (uint i = 0; i < lines.size(); i++) {
		int y = top + i * kLineHeight;
		_font->drawString(&dst, lines[i], left + 1, y + 1, width, black, Graphics::kTextAlignCenter);
		_font->drawString(&dst, lines[i], left, y, width, white, Graphics::kTextAlignCenter);
	}
}

} // End of namespace XFiles
