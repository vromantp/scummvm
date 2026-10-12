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

namespace XFiles {

const PlainGameDescriptor xfilesGames[] = {
	{ "xfiles", "The X-Files Game" },
	{ 0, 0 }
};

const ADGameDescription gameDescriptions[] = {
	// English, Windows, 7 CDs. The CDs go in subdirectories cd1 ... cd7 of the installation.
	{
		"xfiles",
		nullptr,
		AD_ENTRY2s("XFiles.hdb", "a87bbe0f3b51856303386db083c0321b", 6042472,
				   "XFiles.gam", "1de1e4d2a05c5185605232caee341661", 154656),
		Common::EN_ANY,
		Common::kPlatformWindows,
		ADGF_UNSTABLE,
		GUIO2(GUIO_NOMIDI, GAMEOPTION_SUBLIMINAL_MESSAGES)
	},

	AD_TABLE_END_MARKER
};

} // End of namespace XFiles
