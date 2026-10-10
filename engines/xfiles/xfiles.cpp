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

#include "common/config-manager.h"
#include "common/debug-channels.h"
#include "common/events.h"
#include "common/file.h"
#include "common/system.h"
#include "engines/util.h"
#include "graphics/framelimiter.h"
#include "graphics/pixelformat.h"

#include "xfiles/xfiles.h"
#include "xfiles/console.h"
#include "xfiles/vcdata.h"

namespace XFiles {

XFilesEngine *g_engine;

XFilesEngine::XFilesEngine(OSystem *syst, const ADGameDescription *gameDesc) : Engine(syst),
	_gameDescription(gameDesc), _data(nullptr) {
	g_engine = this;
}

XFilesEngine::~XFilesEngine() {
	delete _data;
}

bool XFilesEngine::loadDatabases() {
	Common::File hdb, gam;
	if (!hdb.open("XFiles.hdb") || !gam.open("XFiles.gam"))
		return false;
	_data = new VCData();
	if (!_data->load(hdb, gam))
		return false;
	debugC(1, kDebugDatabase, "Loaded %u classes from XFiles.hdb, %u from XFiles.gam",
		_data->getHdb().getClasses().size(), _data->getGam().getClasses().size());
	return true;
}

Common::Error XFilesEngine::run() {
	_discFiles.init(Common::FSNode(ConfMan.getPath("path")));
	if (!loadDatabases())
		return Common::Error(Common::kReadingFailed, "XFiles.hdb / XFiles.gam");

	Graphics::PixelFormat format(2, 5, 6, 5, 0, 11, 5, 0, 0);
	initGraphics(640, 480, &format);
	setDebugger(new Console());

	// Nothing to play yet: the game itself is the next step. The debugger
	// (Ctrl+Alt+D) can dump the databases and look up media files.
	Graphics::FrameLimiter limiter(g_system, 60);
	while (!shouldQuit()) {
		Common::Event event;
		while (g_system->getEventManager()->pollEvent(event)) {
		}
		limiter.delayBeforeSwap();
		g_system->updateScreen();
		limiter.startFrame();
	}

	return Common::kNoError;
}

} // End of namespace XFiles
