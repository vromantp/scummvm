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
#include "common/formats/winexe.h"
#include "graphics/font.h"
#include "graphics/fonts/ttf.h"
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

enum {
	// Evidence kit (EvKit_Open 0x5188e0, Screen_PlaceEvIcon 0x50ee90): icons
	// stack upwards from the top of the kit's inventory icon, 5 px apart;
	// the kit area starts 2 px above that icon and reaches 5 px above the
	// highest kit icon
	kKitSpacing = 5,
	kKitAreaBelow = 2,
	kKitAreaAbove = 5,
	// Rollover text (Screen_RolloverText 0x437600): Arial 9 (12 px), 2 px
	// below the icon, centred on it and kept on the screen
	kTextFontSize = 12,
	kTextGap = 2,
	kTextMargin = 3,
	kScreenWidth = 640
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
	delete _picture;
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
		return &_picture->getSurface();
	if (_movie && _movie->getFrame().getPixels())
		return &_movie->getFrame();
	return nullptr;
}

void Interface::Graphic::draw(Graphics::Surface &dst, int x, int y) const {
	if (_picture)
		_picture->draw(dst, x, y);
	else if (_movie && _movie->getFrame().getPixels())
		blit(dst, _movie->getFrame(), x, y);
}

// -- Interface ----------------------------------------------------------------

Interface::Interface(const VCData &data, const DiscFiles &files) : _data(data), _files(files), _kitOpen(false),
	_evidenceRowShown(false), _inventorySeeded(false), _texts(nullptr), _font(nullptr), _rolloverText(false), _barClosed(false), _barOpen(false),
	_mouseInBar(false), _enteredBar(false), _held(0) {
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
	freeIcons(_kit);
	freeItems();
	delete _font;
	for (Common::HashMap<uint, Graphics::Font *>::iterator it = _itemFonts.begin(); it != _itemFonts.end(); ++it)
		delete it->_value;
}

void Interface::init(Common::WinResources *texts) {
	_texts = texts;
#ifdef USE_FREETYPE2
	// Arial is not available: Liberation Sans has its metrics
	_font = Graphics::loadTTFFontFromArchive("LiberationSans-Regular.ttf", kTextFontSize, Graphics::kTTFSizeModeCharacter);
#endif
	if (!_font)
		warning("Interface: cannot load the rollover text font from fonts.dat");
}

Interface::Graphic *Interface::loadGraphic(uint32 assetList) const {
	Common::Array<uint32> assets;
	if (!assetList || !_data.getMembers(assetList, assets) || assets.empty())
		return nullptr;
	return loadAsset(assets[0]);
}

Interface::Graphic *Interface::loadAsset(uint32 assetId) const {
	if (!assetId)
		return nullptr;
	Graphic *graphic = new Graphic();
	if (!graphic->open(_data, _files, assetId, _format)) {
		delete graphic;
		return nullptr;
	}
	return graphic;
}

void Interface::update(const Script &script, const Common::Point &mouse, bool captions, bool hiddenBar, bool rolloverText) {
	_mouse = mouse;
	_rolloverText = rolloverText;
	syncItems(script);
	syncIcons(script, kClassActionIcon, _actions);
	syncIcons(script, kClassEmotionIcon, _emotions);
	syncIcons(script, kClassInventory, _inventory);
	syncKit(script);
	placeIcons(_actions, kPaneAction, captions);
	placeIcons(_emotions, kPaneEmotion, captions);
	placeIcons(_inventory, kPaneInventory, captions);
	placeKit();

	// The kit closes when the mouse leaves it and the icon it opened from
	// (EvKitArea_OnRoll 0x518b20, RollOnEvKit), or when a layout takes the
	// input (kit icons are suspended)
	if (_kitOpen && (!isMouseInKit() || isModal()))
		closeKit();

	// The bar opens while the mouse is over it, or always if the inventory
	// preference is "visible" (InvBar_OnRoll 0x468330); the open kit holds
	// it open; a modal layout closes it
	bool inBar = Common::Rect(kBarLeft, kBarTop, kBarRight, kBarBottom).contains(mouse) && !isModal();
	_enteredBar = inBar && !_mouseInBar;
	_mouseInBar = inBar;
	if (!inBar)
		_barClosed = false;
	_barOpen = !isModal() && (!hiddenBar || ((inBar || _kitOpen) && !_barClosed));
}

Interface::Icon Interface::makeIcon(uint32 id) const {
	VCIcon data;
	Icon icon;
	icon.id = id;
	icon.graphic = nullptr;
	icon.rollover = nullptr;
	if (!_data.getIcon(id, data))
		return icon;
	icon.graphic = loadGraphic(data.graphic);
	icon.rollover = loadGraphic(data.rollover);
	// The rollover text is an asset without media naming a string of
	// XFilesT.dll (RT_STRING = 6), as the topic labels
	Common::Array<uint32> texts;
	uint32 type, string;
	if (_texts && data.rolloverText && _data.getMembers(data.rolloverText, texts) && !texts.empty() &&
			_data.getResourceRef(texts[0], type, string) && type == 6) {
		Common::String text = _texts->loadString(string);
		icon.text = Common::U32String(text, Common::kWindows1252);
	}
	return icon;
}

void Interface::syncKit(const Script &script) {
	// The kit's icons are the script's, in order, while enabled
	const Common::Array<uint32> &ids = script.getKitIcons();
	for (uint i = 0; i < _kit.size();) {
		if (Common::find(ids.begin(), ids.end(), _kit[i].id) == ids.end() || !script.isEnabled(_kit[i].id)) {
			delete _kit[i].graphic;
			delete _kit[i].rollover;
			_kit.remove_at(i);
		} else {
			i++;
		}
	}
	for (uint i = 0; i < ids.size(); i++) {
		bool found = false;
		for (uint j = 0; j < _kit.size() && !found; j++)
			found = _kit[j].id == ids[i];
		if (!found && script.isEnabled(ids[i])) {
			debugC(1, kDebugScript, "-- kit icon #%u", ids[i]);
			_kit.push_back(makeIcon(ids[i]));
		}
	}
	for (uint i = 0; i < _kit.size(); i++) {
		if (_kit[i].graphic)
			_kit[i].graphic->update();
	}
}

void Interface::placeKit() {
	int x = _kitFrom.left, bottom = _kitFrom.top;
	for (uint i = 0; i < _kit.size(); i++) {
		const Graphics::Surface *surface = _kit[i].graphic ? _kit[i].graphic->getSurface() : nullptr;
		int w = surface ? surface->w : 0, h = surface ? surface->h : 0;
		_kit[i].rect = Common::Rect(x, bottom - h, x + w, bottom);
		bottom -= h + kKitSpacing;
	}
}

Common::Rect Interface::getKitArea() const {
	Common::Rect area(_kitFrom.left, _kitFrom.top - kKitAreaBelow, _kitFrom.right, _kitFrom.top);
	if (_evidenceRowShown)
		return area;
	for (uint i = 0; i < _kit.size(); i++) {
		Common::Rect r = _kit[i].rect;
		r.top -= kKitAreaAbove;
		area.extend(r);
	}
	return area;
}

void Interface::openKit(const Common::Rect &from) {
	if (_kitOpen && _kitFrom == from)
		return;
	debugC(1, kDebugScript, "-- evidence kit opened");
	_kitOpen = true;
	_kitFrom = from;
	placeKit();
}

void Interface::closeKit() {
	if (!_kitOpen)
		return;
	debugC(1, kDebugScript, "-- evidence kit closed");
	_kitOpen = false;
}

bool Interface::isMouseInKit() const {
	return _kitOpen && (getKitArea().contains(_mouse) || _kitFrom.contains(_mouse));
}

void Interface::setEvidenceRowShown(bool shown) {
	// DispEvIcons (0x4efb20): the row of collected evidence (pane 4) and the
	// kit exclude each other. The engine does not show the row: in a new game
	// nothing shows it (the data only calls DispEvIcons(0)).
	_evidenceRowShown = shown;
}

void Interface::syncIcons(const Script &script, uint32 classId, Common::Array<Icon> &icons) {
	// Disabled icons go; newly enabled ones join at the end, as
	// the exe creates their objects when they are enabled (0x4201a0)
	for (uint i = 0; i < icons.size();) {
		if (!script.isEnabled(icons[i].id)) {
			delete icons[i].graphic;
			delete icons[i].rollover;
			icons.remove_at(i);
		} else {
			i++;
		}
	}
	// A restored game starts with the inventory in the order of the save,
	// without the enable check (Game_BuildObjects 0x477940); icons enabled
	// later join at the end as usual
	if (classId == kClassInventory && !_inventorySeeded && script.isRestored()) {
		const Common::Array<uint32> &saved = script.getInventory();
		for (uint i = 0; i < saved.size(); i++) {
			debugC(1, kDebugScript, "-- icon #%u shown (saved order)", saved[i]);
			icons.push_back(makeIcon(saved[i]));
		}
	}
	if (classId == kClassInventory)
		_inventorySeeded = true;
	// The inventory's order is Script's (a new game's class order, then the order items were enabled in),
	// which is also what a save keeps
	const Common::Array<uint32> &order = classId == kClassInventory ? script.getInventory() : _classOrder[classId == kClassActionIcon ? 0 : 2];
	for (uint i = 0; i < order.size(); i++) {
		uint32 id = order[i];
		if (!script.isEnabled(id))
			continue;
		bool found = false;
		for (uint j = 0; j < icons.size() && !found; j++)
			found = icons[j].id == id;
		if (found)
			continue;
		debugC(1, kDebugScript, "-- icon #%u shown", id);
		icons.push_back(makeIcon(id));
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
		syncItemTexts(script);
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
			// The picture is the first enabled asset of the list (0x41fe60)
			item.graphic = loadAsset(script.firstEnabled(data.assets));
			item.hasText = false;
			// Lower layers are on top: higher ones are drawn first. The phone
			// shows it: its body is layer 200, the pressed buttons 198-199,
			// the display and the buttons' hot areas 100, and the full-screen
			// item that closes it on a click outside 201. A later layout's
			// items go over an earlier one's.
			uint pos = _items.size();
			while (pos > 0 && _items[pos - 1].layer < item.layer)
				pos--;
			_items.insert_at(pos, item);
		}
		debugC(1, kDebugScript, "-- layout #%u shown with %u items", layouts[i].id, layouts[i].items.size());
	}
	syncItemTexts(script);
}

void Interface::syncItemTexts(const Script &script) {
	for (uint i = 0; i < _items.size(); i++) {
		const Script::ItemText *text = script.getItemText(_items[i].id);
		_items[i].hasText = text != nullptr;
		if (text)
			_items[i].text = *text;
		Common::Point origin;
		if (script.getItemOrigin(_items[i].id, origin))
			_items[i].rect.moveTo(origin);
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

bool Interface::getInventoryRect(uint32 id, Common::Rect *rect) const {
	for (uint i = 0; i < _inventory.size(); i++) {
		if (_inventory[i].id == id) {
			if (rect)
				*rect = _inventory[i].rect;
			return true;
		}
	}
	return false;
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
	if (_kitOpen && !_evidenceRowShown) {
		for (uint i = 0; i < _kit.size(); i++) {
			if (_kit[i].rect.contains(pos)) {
				hit.kind = kHitEvidence;
				hit.id = _kit[i].id;
				return hit;
			}
		}
	}
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

uint32 Interface::getHeldCursor() const {
	// Inv_SetHeldCursor (0x4b0710): the item's VCCursor (VCIcon +30)
	VCIcon icon;
	return _held && _data.getIcon(_held, icon) ? icon.cursor : 0;
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
	bool kitShown = _kitOpen && !_evidenceRowShown;
	if (kitShown)
		drawIcons(dst, _kit);
	// Kit icons have no rollover text (0x437600 leaves out group 1)
	drawRolloverText(dst, _actions);
	drawRolloverText(dst, _emotions);
	if (_barOpen)
		drawRolloverText(dst, _inventory);
	for (uint i = 0; i < _items.size(); i++) {
		if (_items[i].graphic)
			_items[i].graphic->draw(dst, _items[i].rect.left, _items[i].rect.top);
		if (_items[i].hasText)
			drawItemText(dst, _items[i]);
	}
}

Graphics::Font *Interface::getItemFont(int size, bool bold, bool sans) const {
	uint key = size << 2 | (sans ? 2 : 0) | (bold ? 1 : 0);
	if (_itemFonts.contains(key))
		return _itemFonts[key];
	Graphics::Font *font = nullptr;
#ifdef USE_FREETYPE2
	if (sans) {
		// Arial: Liberation Sans has its metrics
		font = Graphics::loadTTFFontFromArchive(bold ? "LiberationSans-Bold.ttf" : "LiberationSans-Regular.ttf", size,
			Graphics::kTTFSizeModeCharacter);
	} else {
		// Courier New: Liberation Mono has its metrics; GDI draws it without smoothing
		font = Graphics::loadTTFFontFromArchive(bold ? "LiberationMono-Bold.ttf" : "LiberationMono-Regular.ttf", size,
			Graphics::kTTFSizeModeCharacter, 0, 0, Graphics::kTTFRenderModeMonochrome);
	}
#endif
	_itemFonts[key] = font;
	return font;
}

int Interface::measureItemText(const Common::U32String &text, int size, bool bold, bool sans) const {
	Graphics::Font *font = getItemFont(size, bold, sans);
	return font ? font->getStringWidth(text) : -1;
}

void Interface::drawItemText(Graphics::Surface &dst, const Item &item) const {
	// Item text (the phone's display 0x4d6250, the PDA's texts 0x4be2c0): laid out by the
	// script, drawn from the item's top left, a line per line height, clipped to the item.
	// A shadow is the same in black, 1 px right and down, drawn first.
	const Script::ItemText &text = item.text;
	Graphics::Font *font = getItemFont(text.size, text.bold, text.sans);
	if (!font)
		return;
	Common::Rect clip = item.rect;
	clip.clip(Common::Rect(dst.w, dst.h));
	if (clip.isEmpty())
		return;
	Graphics::Surface area = dst.getSubArea(clip);
	Graphics::TextAlign align = text.align == Script::kAlignCenter ? Graphics::kTextAlignCenter :
		text.align == Script::kAlignRight ? Graphics::kTextAlignRight : Graphics::kTextAlignLeft;
	uint32 colors[2] = { dst.format.RGBToColor(0, 0, 0),
		dst.format.RGBToColor((text.color >> 16) & 0xff, (text.color >> 8) & 0xff, text.color & 0xff) };
	for (int pass = text.shadow ? 0 : 1; pass < 2; pass++) {
		int offset = pass == 0 ? 1 : 0;
		for (uint i = text.firstLine; i < text.lines.size(); i++) {
			int y = (int)(i - text.firstLine) * text.lineHeight;
			if (y >= item.rect.height())
				break;
			font->drawString(&area, text.lines[i], item.rect.left - clip.left + offset,
				item.rect.top - clip.top + y + offset, item.rect.width(), colors[pass], align);
		}
	}
}

void Interface::drawRolloverText(Graphics::Surface &dst, const Common::Array<Icon> &icons) const {
	if (!_rolloverText || !_font || isModal())
		return;
	for (uint i = 0; i < icons.size(); i++) {
		const Icon &icon = icons[i];
		if (icon.text.empty() || !icon.rect.contains(_mouse))
			continue;
		// White is assumed; the exe draws the text asset in its own style
		int w = _font->getStringWidth(icon.text);
		int x = CLIP<int>((icon.rect.left + icon.rect.right - w) / 2, kTextMargin, kScreenWidth - w);
		_font->drawString(&dst, icon.text, x, icon.rect.bottom + kTextGap, w, dst.format.RGBToColor(255, 255, 255));
	}
}

} // End of namespace XFiles
