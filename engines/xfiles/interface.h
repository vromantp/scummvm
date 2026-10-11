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

#ifndef XFILES_INTERFACE_H
#define XFILES_INTERFACE_H

#include "common/array.h"
#include "common/hashmap.h"
#include "common/rect.h"
#include "graphics/pixelformat.h"

namespace Graphics {
struct Surface;
}

namespace XFiles {

class DiscFiles;
class Movie;
class Script;
class VCData;

/**
 * The interface around the view: the inventory bar with its icons, the
 * emotion and action icons, the item in hand with its cursor, and the
 * shown layouts (VCIFaceLayout) with their items.
 *
 * Icons follow the enable flags: an enabled icon is shown (emotion and
 * action icons until one of their kind is clicked). Each kind is placed
 * in a pane with a start point, a spacing and a direction (XFiles.exe
 * 0x4373e0, 0x487060).
 */
class Interface {
public:
	enum HitKind {
		kHitNone,
		kHitInventory,   ///< VCInventory icon
		kHitEmotion,     ///< VCEmotionIcon
		kHitAction,      ///< VCActionIcon
		kHitItem         ///< VCInterfaceItem of a shown layout
	};

	struct Hit {
		HitKind kind;
		uint32 id;
	};

	Interface(const VCData &data, const DiscFiles &files);
	~Interface();

	void setFormat(const Graphics::PixelFormat &format) { _format = format; }

	/**
	 * Follow the script: icons of enabled objects, the shown layouts' items.
	 * captions: the closed-captioning preference (moves the panes);
	 * hiddenBar: the inventory preference is "hide", so the bar only opens
	 * while the mouse is over it.
	 */
	void update(const Script &script, const Common::Point &mouse, bool captions, bool hiddenBar);
	void draw(Graphics::Surface &dst) const;

	/** What is under the mouse: layout items first, then icons. */
	Hit hitTest(const Common::Point &pos) const;
	/** A shown layout takes the input from the view and the icons (0x42b250). */
	bool isModal() const;
	/** The mouse moved into the inventory bar during the last update(). */
	bool hasEnteredBar() const { return _enteredBar; }
	/** Whether an icon of a VCInventory is in the bar. */
	bool hasInventoryIcon(uint32 id) const;

	/** Emotion icons go away until disabled and enabled again (a click on one, or a timer). */
	void spendEmotionIcons();
	/** The same for action icons. */
	void spendActionIcons();

	/** The VCInventory in hand (0 for none); its cursor replaces the default one. */
	void setHeld(uint32 inventory);
	uint32 getHeld() const { return _held; }

	/** Layouts that do not take the input: the exe's list (AddIFLExcept, ClearIFLExcept). */
	void addModalException(uint32 layout);
	void clearModalExceptions() { _modalExceptions.clear(); }

private:
	/** A picture (an entry of a .pff archive) or a movie, looped. */
	class Graphic {
	public:
		Graphic();
		~Graphic();
		bool open(const VCData &data, const DiscFiles &files, uint32 assetId, const Graphics::PixelFormat &format);
		void update();
		/** Empty if nothing could be opened. */
		const Graphics::Surface *getSurface() const;
		/** Draw at (x, y); pictures have pure blue as the transparent colour. */
		void draw(Graphics::Surface &dst, int x, int y) const;

	private:
		Graphics::Surface *_picture;
		Movie *_movie;
	};

	enum Pane {
		kPaneAction,
		kPaneInventory,
		kPaneEmotion,
		kPaneCount
	};

	struct Icon {
		uint32 id;
		Graphic *graphic;
		Graphic *rollover;   ///< Shown while the mouse is over the icon, or nullptr
		Common::Rect rect;
	};

	struct Item {
		uint32 id;
		uint16 layer;
		Common::Rect rect;
		Graphic *graphic;    ///< nullptr if the item shows nothing (or text, not drawn yet)
	};

	/** Load the graphic of the first asset of a VCAssetRefList, nullptr if none opens. */
	Graphic *loadGraphic(uint32 assetList) const;
	void syncIcons(const Script &script, uint32 classId, Common::Array<Icon> &icons, Common::HashMap<uint32, bool> *spent);
	void freeIcons(Common::Array<Icon> &icons);
	void placeIcons(Common::Array<Icon> &icons, Pane pane, bool captions);
	void syncItems(const Script &script);
	void freeItems();
	void drawIcons(Graphics::Surface &dst, const Common::Array<Icon> &icons) const;
	static void blit(Graphics::Surface &dst, const Graphics::Surface &src, int x, int y);

	const VCData &_data;
	const DiscFiles &_files;
	Graphics::PixelFormat _format;
	Common::Array<uint32> _classOrder[3];         ///< Every icon of a class, in database order
	Common::Array<Icon> _inventory, _emotions, _actions;
	Common::HashMap<uint32, bool> _spentEmotions, _spentActions;
	Common::Array<uint32> _layouts;               ///< Shown layouts whose items are loaded
	Common::Array<uint32> _itemIds;               ///< Their items, in script order
	Common::Array<Item> _items;                   ///< The same, in drawing order
	Common::Array<uint32> _modalExceptions;
	Common::Point _mouse;
	bool _barOpen;
	bool _mouseInBar;
	bool _enteredBar;
	uint32 _held;
	bool _cursorPushed;
};

} // End of namespace XFiles

#endif // XFILES_INTERFACE_H
