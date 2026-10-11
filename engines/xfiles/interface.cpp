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

#include "common/algorithm.h"
#include "common/debug.h"
#include "graphics/cursorman.h"
#include "graphics/surface.h"

#include "xfiles/detection.h"
#include "xfiles/interface.h"
#include "xfiles/movie.h"
#include "xfiles/pictures.h"
#include "xfiles/script.h"
#include "xfiles/vcdata.h"

namespace XFiles {

// The inventory bar: (0, 420)-(640, 480) (Game_Start, XFiles.exe 0x48cd70)
enum {
	kBarLeft = 0,
	kBarTop = 420,
	kBarRight = 640,
	kBarBottom = 480
};

// Panes (0x487060, 0x45b160): start without and with closed captions,
// spacing and direction; icons are placed by their top left corner
static const struct {
	int16 x, y, captionsY;
	int16 spacing;
	int8 direction;
} kPanes[] = {
	{ 520, 340, 351, 20, -1 },  // Action icons
	{ 10, 420, 425, 5, 1 },     // Inventory
	{ 180, 340, 351, 20, 1 }    // Emotion icons
};

// Layouts that leave the view and the icons working (the IFL-except list,
// registered by XFiles.exe at start, 0x41b060): e-mail popups, the Game
// Preferences screens, Help, the Field Office letter, drawer and cabinets...
static const uint32 kNonModalLayouts[] = {
	37197, 40403, 40440, 49738, 49781, 49783, 49785, 50460, 52242, 53263, 53268, 57255,
	60115, 62952, 81530, 72504, 57082, 84328, 84370, 58553, 32600, 32660, 61792
};

// Layouts the activation itself exempts (0x42b250), whatever the list holds
static const uint32 kAlwaysNonModalLayouts[] = { 49738, 49781, 49783, 49785, 72933, 50460 };

// -- Graphic ------------------------------------------------------------------

Interface::Graphic::Graphic() : _picture(nullptr), _movie(nullptr) {
}

Interface::Graphic::~Graphic() {
	if (_picture) {
		_picture->free();
		delete _picture;
	}
	delete _movie;
}

bool Interface::Graphic::open(const VCData &data, const DiscFiles &files, uint32 assetId, const Graphics::PixelFormat &format) {
	VCAsset asset;
	if (!data.getAsset(assetId, asset) || asset.files.empty())
		return false;
	if (asset.files[0].file.hasSuffixIgnoreCase(".pff")) {
		_picture = loadPicture(data, files, assetId, format);
		return _picture != nullptr;
	}
	_movie = new Movie();
	if (!_movie->open(files, asset)) {
		delete _movie;
		_movie = nullptr;
		return false;
	}
	_movie->play(true);
	return true;
}

void Interface::Graphic::update() {
	if (_movie)
		_movie->update();
}

const Graphics::Surface *Interface::Graphic::getSurface() const {
	if (_picture)
		return _picture;
	if (_movie && _movie->getFrame().getPixels())
		return &_movie->getFrame();
	return nullptr;
}

void Interface::Graphic::draw(Graphics::Surface &dst, int x, int y) const {
	if (_picture)
		drawKeyed(dst, *_picture, x, y);
	else if (_movie && _movie->getFrame().getPixels())
		blit(dst, _movie->getFrame(), x, y);
}

// -- Interface ----------------------------------------------------------------

Interface::Interface(const VCData &data, const DiscFiles &files) : _data(data), _files(files), _barOpen(false),
	_mouseInBar(false), _enteredBar(false), _held(0), _cursorPushed(false) {
	static const uint32 kClasses[] = { kClassActionIcon, kClassInventory, kClassEmotionIcon };
	for (uint i = 0; i < ARRAYSIZE(kClasses); i++) {
		Common::Array<NeoEntry> entries;
		data.getHdb().getObjects(kClasses[i], entries);
		for (uint j = 0; j < entries.size(); j++)
			_classOrder[i].push_back(entries[j].key);
	}
	for (uint i = 0; i < ARRAYSIZE(kNonModalLayouts); i++)
		_modalExceptions.push_back(kNonModalLayouts[i]);
}

Interface::~Interface() {
	setHeld(0);
	freeIcons(_inventory);
	freeIcons(_emotions);
	freeIcons(_actions);
	freeItems();
}

Interface::Graphic *Interface::loadGraphic(uint32 assetList) const {
	Common::Array<uint32> assets;
	if (!assetList || !_data.getMembers(assetList, assets) || assets.empty())
		return nullptr;
	Graphic *graphic = new Graphic();
	if (!graphic->open(_data, _files, assets[0], _format)) {
		delete graphic;
		return nullptr;
	}
	return graphic;
}

void Interface::update(const Script &script, const Common::Point &mouse, bool captions, bool hiddenBar) {
	_mouse = mouse;
	syncItems(script);
	syncIcons(script, kClassActionIcon, _actions, &_spentActions);
	syncIcons(script, kClassEmotionIcon, _emotions, &_spentEmotions);
	syncIcons(script, kClassInventory, _inventory, nullptr);
	placeIcons(_actions, kPaneAction, captions);
	placeIcons(_emotions, kPaneEmotion, captions);
	placeIcons(_inventory, kPaneInventory, captions);

	// The bar opens while the mouse is over it, or always if the inventory
	// preference is "visible" (InvBar_OnRoll 0x468330); a modal layout
	// closes it
	bool inBar = Common::Rect(kBarLeft, kBarTop, kBarRight, kBarBottom).contains(mouse) && !isModal();
	_enteredBar = inBar && !_mouseInBar;
	_mouseInBar = inBar;
	_barOpen = !isModal() && (!hiddenBar || inBar);
}

void Interface::syncIcons(const Script &script, uint32 classId, Common::Array<Icon> &icons, Common::HashMap<uint32, bool> *spent) {
	// Disabled and spent icons go; newly enabled ones join at the end, as
	// the exe creates their objects when they are enabled (0x4201a0). A
	// spent icon comes back once it is disabled and enabled again.
	if (spent) {
		Common::Array<uint32> disabled;
		for (Common::HashMap<uint32, bool>::iterator it = spent->begin(); it != spent->end(); ++it) {
			if (!script.isEnabled(it->_key))
				disabled.push_back(it->_key);
		}
		for (uint i = 0; i < disabled.size(); i++)
			spent->erase(disabled[i]);
	}
	for (uint i = 0; i < icons.size();) {
		if (!script.isEnabled(icons[i].id) || (spent && spent->contains(icons[i].id))) {
			delete icons[i].graphic;
			delete icons[i].rollover;
			icons.remove_at(i);
		} else {
			i++;
		}
	}
	const Common::Array<uint32> &order = _classOrder[classId == kClassActionIcon ? 0 : classId == kClassInventory ? 1 : 2];
	for (uint i = 0; i < order.size(); i++) {
		uint32 id = order[i];
		if (!script.isEnabled(id) || (spent && spent->contains(id)))
			continue;
		bool found = false;
		for (uint j = 0; j < icons.size() && !found; j++)
			found = icons[j].id == id;
		if (found)
			continue;
		VCIcon data;
		Icon icon;
		icon.id = id;
		icon.graphic = nullptr;
		icon.rollover = nullptr;
		if (_data.getIcon(id, data)) {
			icon.graphic = loadGraphic(data.graphic);
			icon.rollover = loadGraphic(data.rollover);
		}
		debugC(1, kDebugScript, "-- icon #%u shown", id);
		icons.push_back(icon);
	}
	for (uint i = 0; i < icons.size(); i++) {
		if (icons[i].graphic)
			icons[i].graphic->update();
	}
}

void Interface::freeIcons(Common::Array<Icon> &icons) {
	for (uint i = 0; i < icons.size(); i++) {
		delete icons[i].graphic;
		delete icons[i].rollover;
	}
	icons.clear();
}

void Interface::placeIcons(Common::Array<Icon> &icons, Pane pane, bool captions) {
	// Each icon takes the next slot; the exe keeps the slots of icons that
	// were hidden until the pane is empty, here the icons close up
	int x = kPanes[pane].x;
	int y = captions ? kPanes[pane].captionsY : kPanes[pane].y;
	for (uint i = 0; i < icons.size(); i++) {
		const Graphics::Surface *surface = icons[i].graphic ? icons[i].graphic->getSurface() : nullptr;
		int w = surface ? surface->w : 0, h = surface ? surface->h : 0;
		icons[i].rect = Common::Rect(x, y, x + w, y + h);
		x += (w + kPanes[pane].spacing) * kPanes[pane].direction;
	}
}

void Interface::syncItems(const Script &script) {
	const Common::Array<Script::Layout> &layouts = script.getLayouts();
	Common::Array<uint32> layoutIds, itemIds;
	for (uint i = 0; i < layouts.size(); i++) {
		layoutIds.push_back(layouts[i].id);
		for (uint j = 0; j < layouts[i].items.size(); j++)
			itemIds.push_back(layouts[i].items[j]);
	}
	if (layoutIds == _layouts && itemIds == _itemIds) {
		for (uint i = 0; i < _items.size(); i++) {
			if (_items[i].graphic)
				_items[i].graphic->update();
		}
		return;
	}

	freeItems();
	_layouts = layoutIds;
	_itemIds = itemIds;
	for (uint i = 0; i < layouts.size(); i++) {
		for (uint j = 0; j < layouts[i].items.size(); j++) {
			VCInterfaceItem data;
			if (!_data.getInterfaceItem(layouts[i].items[j], data))
				continue;
			Item item;
			item.id = data.id;
			item.layer = data.layer & 0xff; // only the low byte is the drawing layer
			item.rect = Common::Rect(data.rect.left, data.rect.top, data.rect.right, data.rect.bottom);
			item.graphic = loadGraphic(data.assets);
			// Lower layers first; a later layout's items over an earlier one's
			uint pos = _items.size();
			while (pos > 0 && _items[pos - 1].layer > item.layer)
				pos--;
			_items.insert_at(pos, item);
		}
		debugC(1, kDebugScript, "-- layout #%u shown with %u items", layouts[i].id, layouts[i].items.size());
	}
}

void Interface::freeItems() {
	for (uint i = 0; i < _items.size(); i++)
		delete _items[i].graphic;
	_items.clear();
	_layouts.clear();
	_itemIds.clear();
}

bool Interface::isModal() const {
	for (uint i = 0; i < _layouts.size(); i++) {
		const uint32 *always = kAlwaysNonModalLayouts + ARRAYSIZE(kAlwaysNonModalLayouts);
		if (Common::find(_modalExceptions.begin(), _modalExceptions.end(), _layouts[i]) == _modalExceptions.end() &&
				Common::find(kAlwaysNonModalLayouts, always, _layouts[i]) == always)
			return true;
	}
	return false;
}

void Interface::addModalException(uint32 layout) {
	if (Common::find(_modalExceptions.begin(), _modalExceptions.end(), layout) == _modalExceptions.end())
		_modalExceptions.push_back(layout);
}

bool Interface::hasInventoryIcon(uint32 id) const {
	for (uint i = 0; i < _inventory.size(); i++) {
		if (_inventory[i].id == id)
			return true;
	}
	return false;
}

void Interface::spendEmotionIcons() {
	for (uint i = 0; i < _emotions.size(); i++)
		_spentEmotions[_emotions[i].id] = true;
}

void Interface::spendActionIcons() {
	for (uint i = 0; i < _actions.size(); i++)
		_spentActions[_actions[i].id] = true;
}

Interface::Hit Interface::hitTest(const Common::Point &pos) const {
	Hit hit;
	hit.kind = kHitNone;
	hit.id = 0;
	for (uint i = _items.size(); i-- > 0;) {
		if (_items[i].rect.contains(pos)) {
			hit.kind = kHitItem;
			hit.id = _items[i].id;
			return hit;
		}
	}
	if (isModal())
		return hit;
	if (_barOpen) {
		for (uint i = 0; i < _inventory.size(); i++) {
			if (_inventory[i].rect.contains(pos)) {
				hit.kind = kHitInventory;
				hit.id = _inventory[i].id;
				return hit;
			}
		}
	}
	for (uint i = 0; i < _emotions.size(); i++) {
		if (_emotions[i].rect.contains(pos)) {
			hit.kind = kHitEmotion;
			hit.id = _emotions[i].id;
			return hit;
		}
	}
	for (uint i = 0; i < _actions.size(); i++) {
		if (_actions[i].rect.contains(pos)) {
			hit.kind = kHitAction;
			hit.id = _actions[i].id;
			return hit;
		}
	}
	return hit;
}

void Interface::setHeld(uint32 inventory) {
	// Inv_SetHeldCursor (0x4b0710): the item's VCCursor replaces the default
	// cursor; items without one keep it
	if (inventory == _held)
		return;
	_held = inventory;
	if (_cursorPushed) {
		CursorMan.popCursor();
		_cursorPushed = false;
	}
	VCIcon icon;
	VCCursor cursor;
	Common::Array<uint32> assets;
	if (!inventory || !_data.getIcon(inventory, icon) || !icon.cursor || !_data.getCursor(icon.cursor, cursor) ||
			!_data.getMembers(cursor.assets, assets) || assets.empty())
		return;
	Graphics::Surface *picture = loadPicture(_data, _files, assets[0], _format);
	if (!picture)
		return;
	// Pure blue is transparent, as in the dialog box pictures
	CursorMan.pushCursor(*picture, cursor.hotX, cursor.hotY, _format.RGBToColor(0, 0, 255));
	_cursorPushed = true;
	picture->free();
	delete picture;
}

void Interface::blit(Graphics::Surface &dst, const Graphics::Surface &src, int x, int y) {
	Common::Rect r(x, y, x + src.w, y + src.h);
	r.clip(Common::Rect(dst.w, dst.h));
	if (r.isEmpty())
		return;
	dst.copyRectToSurface(src, r.left, r.top, Common::Rect(r.left - x, r.top - y, r.right - x, r.bottom - y));
}

void Interface::drawIcons(Graphics::Surface &dst, const Common::Array<Icon> &icons) const {
	for (uint i = 0; i < icons.size(); i++) {
		const Graphic *graphic = icons[i].graphic;
		if (icons[i].rollover && icons[i].rect.contains(_mouse) && !isModal())
			graphic = icons[i].rollover;
		if (graphic)
			graphic->draw(dst, icons[i].rect.left, icons[i].rect.top);
	}
}

void Interface::draw(Graphics::Surface &dst) const {
	drawIcons(dst, _actions);
	drawIcons(dst, _emotions);
	if (_barOpen)
		drawIcons(dst, _inventory);
	for (uint i = 0; i < _items.size(); i++) {
		if (_items[i].graphic)
			_items[i].graphic->draw(dst, _items[i].rect.left, _items[i].rect.top);
	}
}

} // End of namespace XFiles
