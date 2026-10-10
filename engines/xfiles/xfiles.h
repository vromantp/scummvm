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

#ifndef XFILES_XFILES_H
#define XFILES_XFILES_H

#include "common/scummsys.h"
#include "common/error.h"
#include "engines/engine.h"

#include "xfiles/detection.h"
#include "xfiles/discfiles.h"

namespace XFiles {

class VCData;

class XFilesEngine : public Engine {
public:
	XFilesEngine(OSystem *syst, const ADGameDescription *gameDesc);
	~XFilesEngine() override;

	bool hasFeature(EngineFeature f) const override {
		return f == kSupportsReturnToLauncher;
	}

	Common::Language getLanguage() const { return _gameDescription->language; }

	const VCData &getData() const { return *_data; }
	const DiscFiles &getDiscFiles() const { return _discFiles; }

protected:
	Common::Error run() override;

private:
	bool loadDatabases();

	const ADGameDescription *_gameDescription;
	VCData *_data;
	DiscFiles _discFiles;
};

extern XFilesEngine *g_engine;

} // End of namespace XFiles

#endif // XFILES_XFILES_H
