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
#include "common/ustr.h"
#include "graphics/pixelformat.h"

#include "xfiles/script.h"

namespace Common {
class WinResources;
}

namespace Graphics {
class Font;
struct Surface;
}

namespace XFiles {

class DiscFiles;
class Movie;
class PictureAsset;
class Script;
class VCData;

/**
 * The interface around the view: the inventory bar with its icons, the
 * emotion and action icons, the item in hand, and the
 * shown layouts (VCIFaceLayout) with their items.
 *
 * Icons follow the enable flags: an enabled icon is shown (emotion and
 * action icons until one of their kind is clicked). Each kind is placed
 * in a pane with a start point, a spacing and a direction (XFiles.exe
 * 0x4373e0, 0x487060). Evidence shows in the evidence kit: a column of
 * icons above the kit's inventory icon while the kit is open
 * (EvKit_Open, 0x5188e0).
 */
class Interface {
public:
	enum HitKind {
		kHitNone,
		kHitInventory,   ///< VCInventory icon
		kHitEmotion,     ///< VCEmotionIcon
		kHitAction,      ///< VCActionIcon
		kHitEvidence,    ///< VCEvidenceIcon in the open evidence kit
		kHitItem,        ///< VCInterfaceItem of a shown layout
		kHitAIIcon,      ///< The AI icon (its click is not ported, B-118)
		kHitIdea         ///< VCIdeaIcon of the open dialog box
	};

	struct Hit {
		HitKind kind;
		uint32 id;
	};

	Interface(const VCData &data, const DiscFiles &files);
	~Interface();

	void setFormat(const Graphics::PixelFormat &format) { _format = format; }
	/** Load the rollover text font; the texts come from XFilesT.dll (may be nullptr). */
	void init(Common::WinResources *texts);

	/**
	 * Follow the script: icons of enabled objects, the shown layouts' items.
	 * captions: the closed-captioning preference (moves the panes);
	 * hiddenBar: the inventory preference is "hide", so the bar only opens
	 * while the mouse is over it; rolloverText: the preference that shows
	 * an icon's text below it while the mouse is over it.
	 */
	void update(const Script &script, const Common::Point &mouse, bool captions, bool hiddenBar, bool rolloverText);
	void draw(Graphics::Surface &dst) const;

	/** What is under the mouse: layout items first, then icons. */
	Hit hitTest(const Common::Point &pos) const;
	/** A shown layout takes the input from the view and the icons (0x42b250). */
	bool isModal() const;
	/**
	 * FadeBack(b) (Scene_SuspendObjects 0x40be40): a flag, not a count. While set the view, icons and bar take
	 * no input, as under a modal layout. The grey tint of a suspended object is not drawn.
	 */
	void setSuspended(bool suspended) { _suspended = suspended; }
	/** The mouse moved into the inventory bar during the last update(). */
	bool hasEnteredBar() const { return _enteredBar; }
	/** The bar was shown at the last update() (Obj_IsShown of the bar). */
	bool isBarOpen() const { return _barOpen; }
	/** Whether an icon of a VCInventory is in the bar. */
	bool hasInventoryIcon(uint32 id) const { return getInventoryRect(id, nullptr); }
	/** Where the icon of a VCInventory is; false if it has none. */
	bool getInventoryRect(uint32 id, Common::Rect *rect) const;

	/** Open the evidence kit above an inventory icon. */
	void openKit(const Common::Rect &from);
	void closeKit();
	bool isKitOpen() const { return _kitOpen; }
	/** The mouse is over the open kit or the icon it was opened from. */
	bool isMouseInKit() const;
	/** DispEvIcons: while the evidence row is shown, the kit shows no icons. */
	void setEvidenceRowShown(bool shown);
	/**
	 * The AI icon (AIIconObj, kind 34): Prefs_ApplyAIIcon (0x452780) makes it with Artificial Intuition on and
	 * removes it with Artificial Intuition off.
	 * Returns true if it was made now (the glow starts off; the caller sets it, AI_SetGlow).
	 */
	bool showAIIcon(bool shown);
	/** AI_SetGlow (0x480670) on the icon: Icon_SetRollover on the frame animation, so AI.pff loops while it glows. */
	void setAIGlow(bool glow);
	/** The AI icon's suspension (slot +0x20, 0x4a04f0): it is drawn under the grey tint and takes no hit. */
	void setAISuspended(bool suspended) { _aiSuspended = suspended; }
	/** Close the bar until the mouse leaves it (EvKitIconClick in hidden-bar mode). */
	void closeBar() { _barClosed = true; }

	/** The VCInventory in hand (0 for none). */
	void setHeld(uint32 inventory) { _held = inventory; }
	uint32 getHeld() const { return _held; }
	/** The VCCursor of the item in hand; 0 if none. */
	uint32 getHeldCursor() const;

	/** Layouts that do not take the input: the exe's list (AddIFLExcept, ClearIFLExcept). */
	void addModalException(uint32 layout);
	void clearModalExceptions() { _modalExceptions.clear(); }

	/** An idea icon being dragged (IdeaIcon_DragEnd 0x4d15f0): its picture centred on the mouse. */
	void drawDragged(Graphics::Surface &dst, uint32 ideaId, const Common::Point &pos) const;

	/** The width of a text in an item font (Script wraps by it), -1 if the font is missing. */
	int measureItemText(const Common::U32String &text, int size, bool bold, bool sans) const;

private:
	/** A picture (an entry of a .pff archive) or a movie, looped. */
	class Graphic {
	public:
		Graphic();
		~Graphic();
		bool open(const VCData &data, const DiscFiles &files, uint32 assetId, const Graphics::PixelFormat &format);
		void update();
		/**
		 * Replace the picture by every frame of a multi-frame .pff asset (the frame player the exe builds, 0x459730);
		 * it shows frame 0 until play(true). A single frame keeps the picture.
		 */
		bool loadFrames(const VCData &data, const DiscFiles &files, uint32 assetId, const Graphics::PixelFormat &format);
		/** Icon_SetRollover (0x43bcb0) of a frame animation: true rewinds and loops by the durations, false stops on frame 0. */
		void play(bool on);
		/** Empty if nothing could be opened. */
		const Graphics::Surface *getSurface() const;
		/** Draw at (x, y); pictures follow the exe's transparency rule (PictureAsset). */
		void draw(Graphics::Surface &dst, int x, int y) const;

	private:
		const PictureAsset *current() const;

		PictureAsset *_picture;
		Movie *_movie;
		Common::Array<PictureAsset *> _frames;
		Common::Array<uint32> _durations;
		uint _frame;
		uint32 _frameStart;
		bool _playing;
	};

	enum Pane {
		kPaneAction,
		kPaneInventory,
		kPaneEmotion,
		kPaneAIIcon,
		kPaneIdea,
		kPaneCount
	};

	struct Icon {
		uint32 id;
		Graphic *graphic;
		Graphic *rollover;   ///< Shown while the mouse is over the icon, or nullptr
		Common::U32String text;  ///< Rollover text
		Common::Rect rect;
	};

	struct Item {
		uint32 id;
		uint16 layer;
		Common::Rect rect;
		Graphic *graphic;    ///< nullptr if the item shows no picture
		bool hasText;
		Script::ItemText text;   ///< Text drawn into the item (the phone's display, the PDA's texts)
	};

	/** Load the graphic of the first asset of a VCAssetRefList, nullptr if none opens. */
	/** The graphic of the first asset of a list, or nullptr. */
	Graphic *loadGraphic(uint32 assetList) const;
	/** The graphic of an asset, or nullptr (also for asset 0). */
	Graphic *loadAsset(uint32 assetId) const;
	void syncIcons(const Script &script, uint32 classId, Common::Array<Icon> &icons);
	void syncKit(const Script &script);
	Icon makeIcon(uint32 id) const;
	void placeKit();
	Common::Rect getKitArea() const;
	void freeIcons(Common::Array<Icon> &icons);
	void placeIcons(Common::Array<Icon> &icons, Pane pane, bool captions);
	void syncIdeas(const Script &script);
	void syncItems(const Script &script);
	void freeItems();
	void syncItemTexts(const Script &script);
	void drawItemText(Graphics::Surface &dst, const Item &item) const;
	/** Courier New 400 or 700 at a character height, from Liberation Mono (kept); nullptr if unavailable. */
	Graphics::Font *getItemFont(int size, bool bold, bool sans) const;
	void drawIcons(Graphics::Surface &dst, const Common::Array<Icon> &icons) const;
	void drawRolloverText(Graphics::Surface &dst, const Common::Array<Icon> &icons) const;
	static void blit(Graphics::Surface &dst, const Graphics::Surface &src, int x, int y);

	const VCData &_data;
	const DiscFiles &_files;
	Graphics::PixelFormat _format;
	Common::Array<uint32> _classOrder[3];         ///< Every icon of a class, in database order
	Common::Array<Icon> _inventory, _emotions, _actions;
	Common::Array<Icon> _kit;                     ///< Evidence icons in the kit
	Common::Array<Icon> _ideas;                   ///< The idea icons of the open dialog box
	bool _aiSuspended = false;                    ///< The AI icon is suspended: tinted, no hit
	bool _aiGlow;                                 ///< The glow the script asks for, kept while the icon is removed
	Common::Array<Icon> _aiIcon;                  ///< The AI icon while it is shown, else empty
	bool _kitOpen;
	Common::Rect _kitFrom;                        ///< The inventory icon the kit opened from
	bool _evidenceRowShown;
	bool _inventorySeeded;                         ///< The restored inventory order has been used
	Common::WinResources *_texts;
	Graphics::Font *_font;
	mutable Common::HashMap<uint, Graphics::Font *> _itemFonts;  ///< Text of items per (size, bold)
	bool _rolloverText;
	bool _barClosed;
	Common::Array<uint32> _layouts;               ///< Shown layouts whose items are loaded
	Common::Array<uint32> _itemIds;               ///< Their items, in script order
	Common::Array<Item> _items;                   ///< The same, in drawing order
	Common::Array<uint32> _modalExceptions;
	Common::Point _mouse;
	bool _barOpen;
	bool _mouseInBar;
	bool _enteredBar;
	bool _suspended;                              ///< FadeBack(1) came and FadeBack(0) has not
	uint32 _held;
};

} // End of namespace XFiles

#endif // XFILES_INTERFACE_H
