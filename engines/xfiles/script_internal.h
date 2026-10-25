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

#ifndef XFILES_SCRIPT_INTERNAL_H
#define XFILES_SCRIPT_INTERNAL_H

#include "common/str.h"

#include "xfiles/vcdata.h"

namespace XFiles {

/** First media file of an asset as the log shows it: "XV:19812.xmv", with " @7s" if it starts later. */
inline Common::String formatFile(const VCMediaFile &f) {
	Common::String text = f.file;
	if (f.start)
		text += Common::String::format(" @%gs", f.start / 1000.0);
	return text;
}

inline bool isVideoAsset(const VCAsset &a) {
	bool video = false;
	for (uint i = 0; i < a.files.size(); i++)
		video |= a.files[i].file.hasPrefix("XV:") || a.files[i].file.hasPrefix("XN:") || a.files[i].file.hasSuffixIgnoreCase(".nmv");
	return video;
}

} // End of namespace XFiles

#endif
