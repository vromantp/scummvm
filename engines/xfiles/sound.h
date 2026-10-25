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

#ifndef XFILES_SOUND_H
#define XFILES_SOUND_H

#include "common/array.h"
#include "common/hashmap.h"
#include "common/rect.h"
#include "graphics/surface.h"

namespace XFiles {

class DiscFiles;
class Movie;
class Script;
class VCData;

/**
 * The sounds an Asset action plays (environments, effects, music) with their 3D placement, and the graphic
 * clips PlayAssetAt places over the screen. Owned by XFilesEngine, which forwards the ScriptHooks calls here.
 * attach() binds the running game's data and Script (both are rebuilt by New Game and a load); the time
 * and the 3D preference come from the engine as arguments, so this class keeps no clock.
 */
class SoundPlayer {
public:
	explicit SoundPlayer(const DiscFiles &files);
	~SoundPlayer();

	/** The running game's data and interpreter; nullptr when it is torn down. */
	void attach(const VCData *data, Script *script);

	/** An Asset action (AssetMode): start a sound, or stop the sound and the clip with the ID. */
	void play(uint32 id, byte mode, bool positional, int16 x, int16 y, bool sound3D);
	/** A restored save's sound; startMs is its in point (where a loop goes back to). */
	void restore(uint32 id, bool loop, uint32 startMs);
	/** A phone key tone: stops the previous one and plays the digit's from its start; 0 only stops. It is no sound object. */
	void playTone(uint32 id, uint32 followUp = 0);
	/** PlayAssetAt: a graphic clip played once, drawn over the screen at (x, y). */
	void playClip(uint32 id, int16 x, int16 y);
	/** StopEnvSounds: the sounds go; Script ends their objects afterwards. */
	void stopAll();
	/** The game is torn down: sounds, clips and the 3D view go. */
	void clear();
	/** Once a frame: places the 3D sounds again after a move, ends the finished sounds and clips (Script::finishSound). */
	void update(bool sound3D);
	void drawClips(Graphics::Surface &dst) const;
	/** Freeze or resume every sound and clip. */
	void pause(bool pause);
	/** Appends the save's triples (sound, 1, position in ms) for the playing sounds in start order. */
	void getSaved(Common::Array<uint32> &triples) const;

private:
	/** A playing sound asset (XS). */
	struct Sound {
		Movie *movie;
		bool positional; ///< Played by a 3D Sound action
		bool loop;       ///< The sound object's +0x30, saved as the triple's 2nd value
		int16 x, y;      ///< On the viewpoint grid
	};

	/** A graphic clip PlayAssetAt placed: played once, drawn over the screen at (x, y). */
	struct Clip {
		Movie *movie;
		int16 x, y;
	};

	void stopTone();
	void stopSound(uint32 id);
	void stopClip(uint32 id);
	void clearClips();
	void updateSounds(bool sound3D);
	void updateClips();
	/** Start a sound; startMs != 0 is its in point. */
	void startSound(uint32 id, bool loop, bool positional, int16 x, int16 y, uint32 startMs, bool sound3D);
	/** Volume and balance of a 3D sound for the current view. */
	void placeSound(Sound &sound, bool sound3D);
	/** The target of a view's first nav whose hotspot has the given one's cursor; 0 if none. */
	uint32 getNavTarget(uint32 view, uint32 hotSpot) const;
	/** Position of the viewpoint of the current location that holds a view. */
	bool getViewPosition(uint32 view, Common::Point &pos) const;
	/** Where a view looks, from its navigation (XFiles.exe 0x46d7a0). */
	bool getFacing(uint32 view, const Common::Point &from, Common::Point &facing) const;

	const DiscFiles &_files;
	const VCData *_data;
	Script *_script;
	Common::HashMap<uint32, Sound> _sounds;
	Common::HashMap<uint32, Clip> _clips;
	uint32 _toneFollowUp;              ///< A sound that starts when the tone has ended (the gun's remark), or 0
	Movie *_tone;                      ///< The phone key tone playing (a movie of its own in the exe), or nullptr
	Common::Array<uint32> _soundOrder; ///< The playing sounds in the order they started (a save lists them so)
	uint32 _soundView;                 ///< The view the 3D sounds were placed for
	Common::Point _soundFacing;        ///< The last facing found, kept for views without one
};

} // End of namespace XFiles

#endif // XFILES_SOUND_H
