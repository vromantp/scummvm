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

#include "common/formats/winexe.h"

#include "xfiles/script.h"

namespace XFiles {

// The text of interface items: the layout of the exe's 0x4be2c0 (Courier New or Arial, wrapped, a line per
// newline) for the phone, the PDA and the computers, and the reading of text assets and text files

/** Pixels from line to line (font.metrics of pda-usage-run1: 12 gives 15, 14 gives 17; the phone's 15 gives 17). */
static int lineHeightOf(int size) {
	return size <= 12 ? 15 : 17;
}

/** Courier New's advance: 7 px at 12, 8 px at 14. */
static int charWidthOf(int size) {
	return (size * 6 + 5) / 10;
}

/** The width of a text: Courier New's fixed advance, or Arial through the hook (an estimate without one). */
struct TextMeter {
	const ScriptHooks *hooks;
	int size;
	bool bold, sans;

	int width(const Common::U32String &text) const {
		if (!sans)
			return text.size() * charWidthOf(size);
		int w = hooks ? hooks->measureText(text, size, bold, sans) : -1;
		// Without fonts (vcdump): Arial 12 averages 5 px per character
		return w >= 0 ? w : text.size() * size * 5 / 12;
	}
};

/** Word-wrap a line (DT_WORDBREAK): break after the last space that fits, or inside a word longer than a line. */
static void wrapLine(const Common::U32String &line, int limit, const TextMeter &meter, Common::U32StringArray &out) {
	Common::U32String rest = line;
	while (meter.width(rest) > limit) {
		int at = -1;
		uint fit = 0; // the characters that fit
		for (uint i = 0; i < rest.size(); i++) {
			if (meter.width(rest.substr(0, i + 1)) > limit)
				break;
			fit = i + 1;
		}
		for (uint i = 0; i <= fit && i < rest.size(); i++) {
			if (rest[i] == ' ')
				at = i;
		}
		if (at > 0) {
			out.push_back(rest.substr(0, at));
			while ((uint)at < rest.size() && rest[at] == ' ')
				at++;
			rest = rest.substr(at);
		} else {
			fit = MAX<uint>(fit, 1);
			out.push_back(rest.substr(0, fit));
			rest = rest.substr(fit);
		}
	}
	out.push_back(rest);
}

void Script::setItemText(uint32 item, const Common::U32String &text, int size, bool bold, uint32 color, TextAlign align,
		bool shadow, int wrapWidth, bool sans) {
	ItemText &t = _itemTexts[item];
	t.lines.clear();
	t.color = color;
	t.bold = bold;
	t.sans = sans;
	t.size = size;
	t.lineHeight = lineHeightOf(size);
	t.align = align;
	t.shadow = shadow;
	t.firstLine = 0;
	// Courier New wraps at whole characters; Arial at the pixel width
	TextMeter meter = { _hooks, size, bold, sans };
	int limit = sans ? wrapWidth : MAX(1, wrapWidth / charWidthOf(size)) * charWidthOf(size);
	Common::U32String line;
	for (uint i = 0; i <= text.size(); i++) {
		if (i < text.size() && text[i] != '\n') {
			line += text[i];
			continue;
		}
		if (wrapWidth > 0)
			wrapLine(line, limit, meter, t.lines);
		else
			t.lines.push_back(line);
		line.clear();
	}
}

void Script::setPDAText(uint32 item, const Common::U32String &text, int size, bool doubleWidth, uint32 color, bool bold,
		bool sans) {
	// 0x4be2c0: Courier New 400, black, left, no shadow; laid out at twice the item's width unless
	// +0x194 is set (it is 0 for every PDA text, which the callers pass as doubleWidth)
	VCInterfaceItem data;
	int width = 0;
	if (_data.getInterfaceItem(item, data))
		width = (data.rect.right - data.rect.left) * (doubleWidth ? 2 : 1);
	setItemText(item, text, size, bold, color, kAlignLeft, false, width, sans);
}

bool Script::getTextAsset(uint32 entry, Common::U32String &text) {
	// A text asset, or one that names a string of XFilesT.dll
	uint32 type, resource;
	if (_data.getResourceRef(entry, type, resource)) {
		if (!_texts || type != 6)
			return false;
		text = _texts->loadString(resource).decode(Common::kWindows1252);
		return true;
	}
	return _data.getText(entry, text);
}

bool Script::readTextFile(uint32 asset, Common::U32String &text) {
	Common::Array<byte> data;
	if (!_hooks || !_hooks->readAssetFile(asset, data))
		return false;
	Common::String raw;
	for (uint i = 0; i < data.size(); i++) {
		if (data[i] != '\r')
			raw += (char)data[i];
	}
	text = Common::U32String(raw, Common::kWindows1252);
	return true;
}

} // End of namespace XFiles
