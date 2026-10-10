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


#ifndef AUDIO_QDMC_H
#define AUDIO_QDMC_H

#include "common/scummsys.h"

#ifdef USE_QDMC

#include "common/types.h"

namespace Common {
class SeekableReadStream;
}

namespace Audio {

class Codec;

/**
 * Create a new Codec for QDesign Music 1 (QDMC) audio.
 *
 * @param extraData           the QuickTime extra data stream (contents of the 'wave' atom)
 * @param disposeExtraData    whether to delete the extra data stream
 * @return   a new Codec, or nullptr if the extra data is invalid
 */
Codec *makeQDMCDecoder(Common::SeekableReadStream *extraData,
					   DisposeAfterUse::Flag disposeExtraData = DisposeAfterUse::NO);

} // End of namespace Audio

#endif // USE_QDMC
#endif // AUDIO_QDMC_H
