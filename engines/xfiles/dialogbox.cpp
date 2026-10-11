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

#include "common/textconsole.h"
#include "graphics/font.h"
#include "graphics/fonts/ttf.h"
#include "graphics/surface.h"

#include "xfiles/dialogbox.h"
#include "xfiles/pictures.h"

namespace XFiles {

// The box's pictures (category 21, in x.pff), loaded by XFiles.exe 0x482f20
static const uint32 kPictureAssets[] = { 34761, 34760, 34762, 34763, 34759, 34764, 85383 };

enum {
	// Placement (0x4819b0): centred on the character's rectangle, inside
	// the view band, at this height
	kBoxTop = 285,
	kBandLeft = 20,
	kBandRight = 620,
	// Parts of the folder, relative to its top left corner (static
	// rectangles initialised at 0x50de80 ... 0x50e140)
	kListX = 11, kListY = 20, kListW = 212, kListH = 90,
	kTalkTabX = 10, kTabY = 0, kTabW = 92, kTabH = 12,
	kHistoryTabX = 110,
	kScrollX = 232, kScrollY = 41,
	kArrowH = 10,
	// The small square at the top right; assumed to close the box
	kCloseX = 233, kCloseY = 16, kCloseW = 13, kCloseH = 14,
	// Not found in the exe yet: the topics' font, colours and spacing
	kFontSize = 12,
	kRowHeight = 15
};

DialogBox::DialogBox() : _font(nullptr), _charView(0), _historyTab(false), _scroll(0) {
	for (uint i = 0; i < kPicCount; i++)
		_pictures[i] = nullptr;
}

DialogBox::~DialogBox() {
	for (uint i = 0; i < kPicCount; i++) {
		if (_pictures[i])
			_pictures[i]->free();
		delete _pictures[i];
	}
	delete _font;
}

bool DialogBox::load(const VCData &data, const DiscFiles &files, const Graphics::PixelFormat &format) {
	for (uint i = 0; i < kPicCount; i++) {
		_pictures[i] = loadPicture(data, files, kPictureAssets[i], format);
		if (!_pictures[i])
			return false;
	}
#ifdef USE_FREETYPE2
	// Arial's metrics, as for the captions; the exe's font for the topics is not known yet
	_font = Graphics::loadTTFFontFromArchive("LiberationSans-Regular.ttf", kFontSize, Graphics::kTTFSizeModeCharacter);
#endif
	if (!_font)
		warning("DialogBox: cannot load the font from fonts.dat");
	return true;
}

void DialogBox::open(uint32 charView, const Common::Rect &charRect) {
	int width = _pictures[kPicTalk] ? _pictures[kPicTalk]->w : 0;
	int x = charRect.left + (charRect.width() - width) / 2;
	if (x + width > kBandRight)
		x = kBandRight - width;
	if (x < kBandLeft)
		x = kBandLeft;
	_pos = Common::Point(x, kBoxTop);
	_charView = charView;
	_historyTab = false;
	_scroll = 0;
}

void DialogBox::close() {
	_charView = 0;
	_talk.clear();
	_history.clear();
}

void DialogBox::setTopics(const Common::Array<Topic> &talk, const Common::Array<Topic> &history) {
	_talk = talk;
	_history = history;
	if (_history.empty())
		_historyTab = false;
	_scroll = MIN(_scroll, getMaxScroll());
}

uint DialogBox::getVisibleRows() const {
	return kListH / kRowHeight;
}

uint DialogBox::getMaxScroll() const {
	uint rows = getTopics().size();
	return rows > getVisibleRows() ? rows - getVisibleRows() : 0;
}

Common::Rect DialogBox::getRect(int x, int y, int w, int h) const {
	return Common::Rect(_pos.x + x, _pos.y + y, _pos.x + x + w, _pos.y + y + h);
}

Common::Rect DialogBox::getThumbRect() const {
	const Graphics::Surface *bar = _pictures[kPicScrollBar], *thumb = _pictures[kPicThumb];
	int range = bar->h - thumb->h;
	uint max = getMaxScroll();
	int y = max ? range * (int)_scroll / (int)max : 0;
	return getRect(kScrollX, kScrollY + y, thumb->w, thumb->h);
}

DialogBox::Hit DialogBox::click(const Common::Point &pos, uint32 &topic) {
	const Graphics::Surface *folder = _pictures[kPicTalk];
	if (!isOpen() || !getRect(0, 0, folder->w, folder->h).contains(pos))
		return kHitOutside;
	if (getRect(kCloseX, kCloseY, kCloseW, kCloseH).contains(pos))
		return kHitClose;
	if (getRect(kTalkTabX, kTabY, kTabW, kTabH).contains(pos)) {
		_historyTab = false;
		_scroll = 0;
		return kHitInside;
	}
	if (!_history.empty() && getRect(kHistoryTabX, kTabY, kTabW, kTabH).contains(pos)) {
		_historyTab = true;
		_scroll = 0;
		return kHitInside;
	}
	const Graphics::Surface *bar = _pictures[kPicScrollBar];
	if (getRect(kScrollX, kScrollY - kArrowH, bar->w, kArrowH).contains(pos)) {
		if (_scroll > 0)
			_scroll--;
		return kHitInside;
	}
	if (getRect(kScrollX, kScrollY + bar->h, bar->w, kArrowH).contains(pos)) {
		if (_scroll < getMaxScroll())
			_scroll++;
		return kHitInside;
	}
	if (getRect(kScrollX, kScrollY, bar->w, bar->h).contains(pos)) {
		// Page up or down from the thumb
		uint page = getVisibleRows();
		if (pos.y < getThumbRect().top)
			_scroll = _scroll > page ? _scroll - page : 0;
		else if (pos.y >= getThumbRect().bottom)
			_scroll = MIN(_scroll + page, getMaxScroll());
		return kHitInside;
	}
	if (getRect(kListX, kListY, kListW, kListH).contains(pos)) {
		uint row = _scroll + (pos.y - _pos.y - kListY) / kRowHeight;
		if (row < getTopics().size() && row < _scroll + getVisibleRows()) {
			topic = getTopics()[row].id;
			return kHitTopic;
		}
	}
	return kHitInside;
}

void DialogBox::draw(Graphics::Surface &dst, const Common::Point &mouse) const {
	if (!isOpen())
		return;
	drawKeyed(dst, *_pictures[_historyTab ? kPicHistory : kPicTalk], _pos.x, _pos.y);
	if (_history.empty())
		drawKeyed(dst, *_pictures[kPicNoHistory], _pos.x + kHistoryTabX, _pos.y + kTabY);

	if (getMaxScroll()) {
		const Graphics::Surface *bar = _pictures[kPicScrollBar];
		drawKeyed(dst, *_pictures[kPicUp], _pos.x + kScrollX, _pos.y + kScrollY - kArrowH);
		drawKeyed(dst, *bar, _pos.x + kScrollX, _pos.y + kScrollY);
		drawKeyed(dst, *_pictures[kPicDown], _pos.x + kScrollX, _pos.y + kScrollY + bar->h);
		Common::Rect thumb = getThumbRect();
		drawKeyed(dst, *_pictures[kPicThumb], thumb.left, thumb.top);
	}

	if (!_font)
		return;
	const Common::Array<Topic> &topics = getTopics();
	uint32 normal = dst.format.RGBToColor(191, 191, 191);
	uint32 hover = dst.format.RGBToColor(255, 255, 255);
	for (uint i = _scroll; i < topics.size() && i < _scroll + getVisibleRows(); i++) {
		Common::Rect row = getRect(kListX, kListY + (i - _scroll) * kRowHeight, kListW, kRowHeight);
		int y = row.top + (kRowHeight - _font->getFontHeight()) / 2;
		_font->drawString(&dst, topics[i].label, row.left, y, row.width(), row.contains(mouse) ? hover : normal,
			Graphics::kTextAlignLeft, 0, true);
	}
}

} // End of namespace XFiles
