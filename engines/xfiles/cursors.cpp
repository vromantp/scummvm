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

#include "common/system.h"
#include "common/textconsole.h"
#include "common/formats/winexe.h"
#include "graphics/cursorman.h"
#include "graphics/surface.h"
#include "graphics/wincursor.h"

#include "xfiles/cursors.h"
#include "xfiles/discfiles.h"
#include "xfiles/pictures.h"
#include "xfiles/vcdata.h"

namespace XFiles {

// The cursor group XFiles.exe loads for the screen (0x4946b0) and shows
// when no VCCursor is shown. The screen also loads 132, shown instead
// while inventory value 9 is in hand (a tool mode, Cursors::kToolCursorId).
static const uint16 kDefaultCursorId = 129;
static const uint16 kToolCursorGroup = 132;

Cursors::Cursors(const VCData &data, const DiscFiles &files) : _data(data), _files(files), _default(nullptr), _tool(nullptr),
	_shown(0), _cursor(nullptr), _frame(0), _frameStart(0) {
}

Cursors::~Cursors() {
	for (Common::HashMap<uint32, Cursor *>::iterator it = _cursors.begin(); it != _cursors.end(); ++it) {
		if (!it->_value)
			continue;
		for (uint i = 0; i < it->_value->frames.size(); i++) {
			it->_value->frames[i]->free();
			delete it->_value->frames[i];
		}
		delete it->_value;
	}
	delete _default;
	delete _tool;
}

void Cursors::init(const Graphics::PixelFormat &format) {
	_format = format;
	Common::SeekableReadStream *stream = _files.openFile(0, "XFiles.exe");
	Common::WinResources *exe = stream ? Common::WinResources::createFromEXE(stream) : nullptr;
	if (exe)
		_default = Graphics::WinCursorGroup::createCursorGroup(exe, Common::WinResourceID(kDefaultCursorId));
	if (exe)
		_tool = Graphics::WinCursorGroup::createCursorGroup(exe, Common::WinResourceID(kToolCursorGroup));
	delete exe;
	delete stream;
	if (!_default || _default->cursors.empty())
		warning("Cursors: cannot load cursor %u of XFiles.exe; using the Windows arrow", kDefaultCursorId);
	showDefault();
	CursorMan.showMouse(true);
}

void Cursors::show(uint32 cursorId) {
	if (cursorId == _shown)
		return;
	_shown = cursorId;
	if (cursorId == kToolCursorId && _tool && !_tool->cursors.empty()) {
		_cursor = nullptr;
		CursorMan.replaceCursor(_tool->cursors[0].cursor);
		return;
	}
	_cursor = cursorId && cursorId != kToolCursorId ? getCursor(cursorId) : nullptr;
	if (!_cursor) {
		showDefault();
		return;
	}
	_frame = 0;
	_frameStart = g_system->getMillis();
	showFrame();
}

void Cursors::update() {
	if (!_cursor || _cursor->frames.size() < 2)
		return;
	uint32 now = g_system->getMillis();
	uint frame = _frame;
	// A frame without a duration stays
	while (_cursor->durations[frame] && now - _frameStart >= _cursor->durations[frame]) {
		_frameStart += _cursor->durations[frame];
		frame = (frame + 1) % _cursor->frames.size();
	}
	if (frame != _frame) {
		_frame = frame;
		showFrame();
	}
}

const Cursors::Cursor *Cursors::getCursor(uint32 cursorId) {
	if (_cursors.contains(cursorId))
		return _cursors[cursorId];
	Cursor *cursor = nullptr;
	VCCursor vc;
	Common::Array<uint32> assets;
	if (_data.getCursor(cursorId, vc) && _data.getMembers(vc.assets, assets) && !assets.empty()) {
		cursor = new Cursor();
		cursor->hotX = vc.hotX;
		cursor->hotY = vc.hotY;
		if (!loadFrames(_data, _files, assets[0], _format, cursor->frames, cursor->durations)) {
			delete cursor;
			cursor = nullptr;
		}
	}
	if (!cursor)
		warning("Cursors: VCCursor #%u has no picture", cursorId);
	_cursors[cursorId] = cursor;
	return cursor;
}

void Cursors::showDefault() {
	if (_default && !_default->cursors.empty()) {
		CursorMan.replaceCursor(_default->cursors[0].cursor);
		return;
	}
	Graphics::Cursor *cursor = Graphics::makeDefaultWinCursor();
	CursorMan.replaceCursor(cursor);
	delete cursor;
}

void Cursors::showFrame() {
	// Pure blue is transparent, as in the interface pictures
	CursorMan.replaceCursor(*_cursor->frames[_frame], _cursor->hotX, _cursor->hotY, _format.RGBToColor(0, 0, 255));
}

} // End of namespace XFiles
