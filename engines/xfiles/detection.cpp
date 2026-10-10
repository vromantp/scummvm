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

#include "base/plugins.h"

#include "xfiles/detection.h"
#include "xfiles/detection_tables.h"

const DebugChannelDef XFilesMetaEngineDetection::debugFlagList[] = {
	{ XFiles::kDebugDatabase, "Database", "Game database (NeoAccess) loading" },
	{ XFiles::kDebugFiles, "Files", "Media file lookup per disc" },
	{ XFiles::kDebugScript, "Script", "Triggers and actions" },
	DEBUG_CHANNEL_END
};

XFilesMetaEngineDetection::XFilesMetaEngineDetection() : AdvancedMetaEngineDetection(
	XFiles::gameDescriptions, XFiles::xfilesGames) {
}

REGISTER_PLUGIN_STATIC(XFILES_DETECTION, PLUGIN_TYPE_ENGINE_DETECTION, XFilesMetaEngineDetection);
