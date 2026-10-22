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

#ifndef XFILES_DIALOGBOX_H
#define XFILES_DIALOGBOX_H

#include "common/array.h"
#include "common/rect.h"
#include "common/ustr.h"

namespace Graphics {
class Font;
struct PixelFormat;
struct Surface;
}

namespace XFiles {

class DiscFiles;
class PictureAsset;
class VCData;

/**
 * The conversation box that opens below a character clicked with nothing
 * in hand (XFiles.exe 0x481660): a folder with a Talk tab (the topics that
 * can be asked) and a History tab (those asked before), with a scroll bar.
 */
class DialogBox {
public:
	struct Topic {
		uint32 id;                ///< VCConversation
		Common::U32String label;
	};

	enum Hit {
		kHitOutside,
		kHitInside,   ///< Handled by the box (tabs, scroll bar, empty space)
		kHitTopic,
		kHitClose
	};

	DialogBox();
	~DialogBox();

	/** Load the pictures and the font; false if the pictures are missing. */
	bool load(const VCData &data, const DiscFiles &files, const Graphics::PixelFormat &format);

	/** Open the box for a character view, centred below its rectangle. */
	void open(uint32 charView, const Common::Rect &charRect);
	void close();
	bool isOpen() const { return _charView != 0; }
	uint32 getCharView() const { return _charView; }

	/** The topics of both tabs, in order; the scroll position is kept. */
	void setTopics(const Common::Array<Topic> &talk, const Common::Array<Topic> &history);

	/** A click at pos; topic is set for kHitTopic. */
	Hit click(const Common::Point &pos, uint32 &topic);
	/** Whether a point is on the open box. */
	bool contains(const Common::Point &pos) const;
	void draw(Graphics::Surface &dst, const Common::Point &mouse) const;

private:
	enum Picture {
		kPicTalk,
		kPicHistory,
		kPicScrollBar,
		kPicThumb,
		kPicUp,
		kPicDown,
		kPicNoHistory,
		kPicCount
	};

	const Common::Array<Topic> &getTopics() const { return _historyTab ? _history : _talk; }
	/** Height of a topic's text: its lines as DrawTextA's word break makes them. */
	int getTextHeight(const Topic &topic) const;
	/** The topic shown at a height in the list (from its top), or -1. */
	int getTopicAt(int y) const;
	uint getVisibleRows() const;
	uint getMaxScroll() const;
	Common::Rect getRect(int x, int y, int w, int h) const;
	Common::Rect getThumbRect() const;

	PictureAsset *_pictures[kPicCount];
	Graphics::Font *_font;
	Common::Point _pos;
	uint32 _charView;
	bool _historyTab;
	bool _tabAuto;                     ///< Just opened: the first topic list picks the tab
	uint _scroll;            ///< First row shown
	Common::Array<Topic> _talk, _history;
};

} // End of namespace XFiles

#endif // XFILES_DIALOGBOX_H
