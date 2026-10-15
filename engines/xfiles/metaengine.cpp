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

#include "common/translation.h"
#include "graphics/managed_surface.h"
#include "graphics/scaler.h"

#include "xfiles/metaengine.h"
#include "xfiles/detection.h"
#include "xfiles/xfiles.h"

static const ADExtraGuiOptionsMap optionsList[] = {
	{
		GAMEOPTION_SUBLIMINAL_MESSAGES,
		{
			_s("Subliminal messages"),
			_s("Flash a word on the screen now and then, as the original's preference does"),
			"subliminal_messages",
			false,
			0,
			0
		}
	},
	AD_EXTRA_GUI_OPTIONS_TERMINATOR
};

const char *XFilesMetaEngine::getName() const {
	return "xfiles";
}

Common::Error XFilesMetaEngine::createInstance(OSystem *syst, Engine **engine, const ADGameDescription *desc) const {
	*engine = new XFiles::XFilesEngine(syst, desc);
	return Common::kNoError;
}

const ADExtraGuiOptionsMap *XFilesMetaEngine::getAdvancedExtraGuiOptions() const {
	return optionsList;
}

bool XFilesMetaEngine::hasFeature(MetaEngineFeature f) const {
	return checkExtendedSaves(f) || f == kSupportsLoadingDuringStartup;
}

void XFilesMetaEngine::getSavegameThumbnail(Graphics::Surface &thumb) {
	// The menu covers the game: its picture is the copy made when it opened
	Graphics::ManagedSurface *shot = XFiles::g_engine ? XFiles::g_engine->getMenuShot() : nullptr;
	if (!shot || !createThumbnail(&thumb, shot))
		MetaEngine::getSavegameThumbnail(thumb);
}

#if PLUGIN_ENABLED_DYNAMIC(XFILES)
REGISTER_PLUGIN_DYNAMIC(XFILES, PLUGIN_TYPE_ENGINE, XFilesMetaEngine);
#else
REGISTER_PLUGIN_STATIC(XFILES, PLUGIN_TYPE_ENGINE, XFilesMetaEngine);
#endif
