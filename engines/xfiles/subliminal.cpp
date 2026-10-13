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

#include "common/debug.h"
#include "common/formats/winexe.h"
#include "common/textconsole.h"
#include "graphics/font.h"
#include "graphics/fonts/ttf.h"
#include "graphics/surface.h"

#include "xfiles/detection.h"
#include "xfiles/discfiles.h"
#include "xfiles/script.h"
#include "xfiles/subliminal.h"

namespace XFiles {

enum {
	// The periodic timer of 0x45be60 (slot +0x10); there is no second timer
	// to take the word away (slot +0x14 gives 0)
	kPeriod = 6000,
	// Where XFiles.exe builds the two generators of a period (Random_Seed
	// return addresses): the word (0x468cf0), then whether it shows (0x468a70)
	kWordSite = 0x468d6b,
	kShowSite = 0x468a9c,
	// The word lists of XFilesS.dll, one per RegUberVars variable
	kXTrackFirst = 10000, kXTrackLast = 10077,
	kParanoiaFirst = 1, kParanoiaLast = 107,
	kLossFirst = 20000, kLossLast = 20065,
	// Courier New with lfHeight -20, regular: GDI gives lines of 22 with an
	// ascent of 17 and 12 pixels per character (font.metrics in the
	// reference run). The pane is centred on the screen rectangle
	// 0x6b9d10 (0, 0)-(640, 480) horizontally, its bottom on the centre line.
	kFontSize = 20,
	kAscent = 17,
	kLineHeight = 22,
	kCentreX = 320,
	kBottom = 240,
	// The colour is grey prefs +0xc8 (0x32, set by the prefs object's
	// constructor 0x4872d0 and never changed)
	kGrey = 0x32
};

Subliminal::Subliminal() : _dll(nullptr), _strings(nullptr), _font(nullptr), _running(false), _next(0) {
}

Subliminal::~Subliminal() {
	delete _strings;
	delete _dll;
	delete _font;
}

bool Subliminal::load(const DiscFiles &files) {
	// Slot +0xc names the DLL, which 0x45be60 loads with LoadLibrary
	_dll = files.openFile(0, "XFilesS.dll");
	if (_dll)
		_strings = Common::WinResources::createFromEXE(_dll);
#ifdef USE_FREETYPE2
	// Courier New's metrics
	_font = Graphics::loadTTFFontFromArchive("LiberationMono-Regular.ttf", kFontSize, Graphics::kTTFSizeModeCharacter);
#endif
	if (!_strings || !_font)
		warning("Subliminal: cannot load XFilesS.dll or the font");
	return _strings && _font;
}

void Subliminal::start(uint32 now) {
	if (_running || !_strings)
		return;
	_running = true;
	_next = now + kPeriod;
	_text.clear();
	debugC(1, kDebugScript, "-- subliminal start at %u", now);
}

void Subliminal::update(uint32 now, Script &script, int32 loss, int32 paranoia, int32 xTrack, bool canShow) {
	// 0x401ee0, every main loop pass (0x402a20)
	if (!_running || (int32)(now - _next) < 0)
		return;
	// A late pass gets one word however many periods went by (the timer
	// only sets a flag, +0x60)
	while ((int32)(now - _next) >= 0)
		_next += kPeriod;

	// Slot +0x1c (0x468cf0): a word of the list of the largest variable,
	// X-track first on a tie, then paranoia
	int first, last;
	if (xTrack >= paranoia && xTrack >= loss) {
		first = kXTrackFirst;
		last = kXTrackLast;
	} else if (paranoia >= loss) {
		first = kParanoiaFirst;
		last = kParanoiaLast;
	} else {
		first = kLossFirst;
		last = kLossLast;
	}
	Script::Dice dice = script.newDice(kWordSite);
	int id = script.roll(dice, first, last);
	// A missing string ends the period's work (0x430d50)
	Common::String text = _strings->loadString(id);
	if (text.empty())
		return;
	// Slot +0x24 (0x468a70): three times in four a text pane is made and
	// queued for showing
	dice = script.newDice(kShowSite);
	bool skipped = script.roll(dice, 1, 4) == 4;
	// The pane's show handler (0x42c3c0, 0x42b940) draws it only while the
	// screen composes its picture in the back buffer (screen +0x950, from
	// 0x510c30): not while a video plays in the band on its own
	debugC(1, kDebugScript, "-- subliminal %d at %u%s", id, now, skipped ? ", skipped" : canShow ? "" : ", not shown");
	if (skipped || !canShow)
		return;
	_text = Common::U32String(text, Common::kWindows1252);
}

void Subliminal::draw(Graphics::Surface &dst) {
	// 0x42b940 draws the pane into the back buffer, copies it to the
	// screen, waits prefs +0xcc / 2 ms (10 / 2) and puts the background
	// back: the word is on screen for one frame
	if (_text.empty() || !_font)
		return;
	// DrawTextA with DT_CENTER in a rectangle as wide as the text; in the
	// reference run a word of 7 characters is drawn at (278, 218)-(362, 240)
	int width = _font->getStringWidth(_text);
	int left = kCentreX - width / 2;
	int top = kBottom - kLineHeight + kAscent - _font->getFontAscent();
	_font->drawString(&dst, _text, left, top, width, dst.format.RGBToColor(kGrey, kGrey, kGrey));
	_text.clear();
}

} // End of namespace XFiles
