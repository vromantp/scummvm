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

#include "common/ptr.h"
#include "common/savefile.h"
#include "common/system.h"
#include "common/translation.h"
#include "engines/metaengine.h"
#include "gui/message.h"

#include "xfiles/xfiles.h"
#include "xfiles/movie.h"
#include "xfiles/pictures.h"

namespace XFiles {

enum {
	// The main menu: the X movie at the left, the entries movie at the right
	kMenuEntriesLeft = 472,
	kMenuEntriesShown = 75,     ///< Frame of the entries movie where all entries are lit and no help text shows
	kMenuEntriesShownMs = 5000, ///< The same moment in ms (0x6bae84)
	kMenuLoopLeft = 61,
	kMenuLoopTop = 103,
	kMenuTopLeft = 57,
	kMenuReturnTop = 215
};

// Logos and title, played by XFiles.exe itself (no list or action refers to them)
static const uint32 kIntroAssets[] = {56003, 56002, 19668, 56001};

// Main menu movies (category 37 Game, also played by the exe)
static const uint32 kMenuXAsset = 49583;
static const uint32 kMenuEntriesAsset = 49587;
static const uint32 kMenuLoopAsset = 49585;      // The X loop, MIDloop.mov
static const uint32 kMenuLoopSoundAsset = 94320; // Its sound, MIDloop.amv
static const uint32 kMenuTopAsset = 49589;       // The top strip, TOPro.mov
static const uint32 kMenuReturnAsset = 72895;    // return.pic, over Previous when the menu opens from the game

// The layouts of the Options and Help entries, and the buttons that lead back (Game_KeyDown, Escape)
static const uint32 kOptionsLayout = 49738;
static const uint32 kHelpLayout = 50460;
static const uint32 kOptionsBackItem = 49841;
static const uint32 kHelpBackItem = 50464;

// Main menu: the click sound (PrefClick.amv)
static const uint32 kMenuClickAsset = 67518;

// Main menu entries (MainMenu_OnMouseClick, XFiles.exe): all from x 472 to 638
// (exclusive); the vertical extent of each, in the order of enum MenuEntry
static const int kMenuEntriesRight = 638;
static const int kMenuEntryRows[][2] = {
	{78, 114}, {126, 162}, {175, 211}, {225, 261}, {275, 311}, {330, 366}, {385, 421}};
// The entries movie's help-text segment of each entry (start, end in ms; 0x46ba00), in the same order
static const uint32 kMenuEntryHelpMs[][2] = {
	{6200, 6700}, {7000, 7500}, {7800, 8300}, {8600, 9100}, {9400, 9900}, {10200, 10700}, {11000, 11500}};
static const uint32 kMenuHelpHold = 150; ///< The segment plays to its end less this (0x6bac8c), then the still of the end
// The top strip's rectangle (0x6baca8): over it TOPro plays
static const int kMenuTopRect[] = {57, 0, 409, 60}; // left, top, right, bottom (exclusive)
static const char *const kMenuEntryNames[] = {"new", "load", "save", "previous", "options", "help", "quit"};

// The intro, the start-up menu and the in-game menu the X corner opens

bool XFilesEngine::playIntro() {
	for (uint i = 0; i < ARRAYSIZE(kIntroAssets) && !shouldQuit(); i++) {
		if (!openMovie(*_video, kIntroAssets[i]))
			continue;
		_video->play(false);
		// A load chosen in the GMM ends the intro at once
		while (!shouldQuit() && !_video->hasEnded() && !_pendingLoad.valid) {
			Input input = pollInput();
			if (input.click || input.skip)
				_video->stop();
			_video->update();
			_screen.fillRect(Common::Rect(_screen.w, _screen.h), 0);
			const Graphics::Surface &frame = _video->getFrame();
			blit(frame, (_screen.w - frame.w) / 2, (_screen.h - frame.h) / 2);
			present();
		}
		_video->close();
		if (_pendingLoad.valid)
			break;
	}
	return !shouldQuit();
}

bool XFilesEngine::runMenu() {
	// The start-up menu: Options and Help show their layout over a black screen
	startMenu();
	_cursors->setVisible(true); // A game may have ended during a movie
	while (!shouldQuit()) {
		if (_pendingLoad.valid) {
			// A load chosen in the GMM: the game starts from it, as after the menu's Load
			closeMenu();
			return true;
		}
		Input input = pollInput();
		if (input.click) {
			MenuEntry entry = clickMenu(input.pos);
			if (entry == kMenuNew) {
				closeMenu();
				return true;
			}
			if ((entry == kMenuLoad && loadGameDialog()) || (entry == kMenuPrevious && loadMostRecentSave())) {
				// The load is queued; the game starts from it
				closeMenu();
				return true;
			}
			if (entry == kMenuOptions || entry == kMenuHelp) {
				runMenuLayout(entry == kMenuOptions ? kOptionsLayout : kHelpLayout);
				continue;
			}
			if (entry == kMenuQuit) {
				quitGame();
				break;
			}
		}
		updateMenu();
		hoverMenu(_mouse);
		drawMenu();
		present();
	}
	closeMenu();
	return false;
}

void XFilesEngine::startMenu() {
	// MainMenu_LoadMedia and Screen_ShowMenu (0x44e060): the X fades in alone, then
	// the entries and the loop start, but only at the first menu of a run and never
	// with Pref_bSkipIntro. Every later open shows the X's last frame and the lit
	// entries at once, with the loop running.
	closeMenu();
	_menuX = new Movie();
	_menuEntries = new Movie();
	_menuLoop = new Movie();
	_menuLoopSound = new Movie();
	_menuTop = new Movie();
	_menuIntroPlay = false;
	_menuHelpEnd = kMenuEntriesShownMs;
	_menuHelpPlaying = false;
	_menuTopHot = false;
	bool fade = !_menuIntroDone && !getPreference(kPrefSkipIntro);
	bool xOpen = openMovie(*_menuX, kMenuXAsset);
	bool entriesOpen = openMovie(*_menuEntries, kMenuEntriesAsset);
	bool loopOpen = openMovie(*_menuLoop, kMenuLoopAsset);
	bool soundOpen = openMovie(*_menuLoopSound, kMenuLoopSoundAsset);
	if (openMovie(*_menuTop, kMenuTopAsset))
		_menuTop->showStill();
	// Opened from the game, the entries are drawn with return.pic over Previous
	if (_menuOpen)
		_menuReturn = loadPicture(*_data, _discFiles, kMenuReturnAsset, _screen.format);
	_menuFading = fade && xOpen;
	if (_menuFading) {
		_menuX->play(false);
		return;
	}
	if (xOpen)
		_menuX->showStillAtEnd();
	if (entriesOpen) {
		_menuEntries->showStillAt(kMenuEntriesShownMs);
		debugC(1, kDebugScript, "-- menu entries movie audio=%d", _menuEntries->hasAudio());
	}
	if (loopOpen)
		_menuLoop->play(true);
	if (soundOpen)
		_menuLoopSound->play(true);
}

void XFilesEngine::closeMenu() {
	delete _menuX;
	delete _menuEntries;
	delete _menuClick;
	delete _menuLoop;
	delete _menuLoopSound;
	delete _menuTop;
	delete _menuReturn;
	_menuX = _menuEntries = _menuClick = _menuLoop = _menuLoopSound = _menuTop = nullptr;
	_menuReturn = nullptr;
	_menuFading = false;
}

void XFilesEngine::updateMenu() {
	if (_menuFading) {
		_menuX->update();
		if (!_menuX->hasEnded())
			return;
		// The X is in: the entries run to 5000 ms while the loop and its sound start
		_menuFading = false;
		_menuIntroPlay = true;
		_menuEntries->play(false);
		_menuLoop->play(true);
		_menuLoopSound->play(true);
	}
	_menuEntries->update();
	_menuLoop->update();
	_menuLoopSound->update();
	_menuTop->update();
	if (_menuIntroPlay && !_menuEntries->hasEnded() && _menuEntries->getCurFrame() >= kMenuEntriesShown) {
		_menuEntries->stop();
		_menuIntroPlay = false;
		_menuIntroDone = true; // 0x4812d0
	}
	if (_menuHelpPlaying && _menuEntries->hasEnded()) {
		// 0x46c0b0: the segment is over, the still of its end holds the help text
		_menuHelpPlaying = false;
		_menuEntries->showStillAt(_menuHelpEnd);
	}
	if (_menuClick) {
		_menuClick->update();
		if (_menuClick->hasEnded()) {
			delete _menuClick;
			_menuClick = nullptr;
		}
	}
}

void XFilesEngine::drawMenu() {
	_screen.fillRect(Common::Rect(_screen.w, _screen.h), 0);
	blit(_menuX->getFrame(), 0, 0);
	if (_menuFading)
		return;
	blit(_menuLoop->getFrame(), kMenuLoopLeft, kMenuLoopTop);
	blit(_menuTop->getFrame(), kMenuTopLeft, 0);
	blit(_menuEntries->getFrame(), kMenuEntriesLeft, 0);
	// Hovering Previous takes return.pic off, so its help text shows
	if (_menuReturn && _menuHelpEnd != kMenuEntryHelpMs[kMenuPrevious][1])
		_menuReturn->draw(_screen, kMenuEntriesLeft, kMenuReturnTop);
}

XFilesEngine::MenuEntry XFilesEngine::hitMenu(const Common::Point &pos) const {
	if (pos.x < kMenuEntriesLeft || pos.x >= kMenuEntriesRight)
		return kMenuNone;
	for (int i = 0; i < kMenuEntryCount; i++) {
		if (pos.y >= kMenuEntryRows[i][0] && pos.y < kMenuEntryRows[i][1])
			return (MenuEntry)i;
	}
	return kMenuNone;
}

void XFilesEngine::hoverMenu(const Common::Point &pos) {
	// Nothing reacts while the X fades in and the entries run to frame 75
	if (_menuFading || _menuIntroPlay || !_menuEntries)
		return;
	// Save counts only while a game is loaded, which is the menu opened from the game
	MenuEntry entry = hitMenu(pos);
	if (entry == kMenuSave && !_menuOpen)
		entry = kMenuNone;
	uint32 end = entry == kMenuNone ? kMenuEntriesShownMs : kMenuEntryHelpMs[entry][1];
	// 0x46ba00: the segment already shown is not started again
	if (end != _menuHelpEnd) {
		_menuHelpEnd = end;
		_menuEntries->halt();
		if (entry == kMenuNone) {
			_menuHelpPlaying = false;
			_menuEntries->showStillAt(kMenuEntriesShownMs);
		} else {
			_menuEntries->setStart(kMenuEntryHelpMs[entry][0]);
			_menuEntries->setEnd(end - kMenuHelpHold);
			_menuEntries->play(false);
			_menuHelpPlaying = true;
		}
	}
	// The top strip plays while the cursor is over it
	if (pos.x >= kMenuTopRect[0] && pos.x < kMenuTopRect[2] && pos.y >= kMenuTopRect[1] && pos.y < kMenuTopRect[3]) {
		_menuTopHot = true;
		if (!_menuTop->isPlaying())
			_menuTop->play(false);
	} else if (_menuTopHot) {
		_menuTopHot = false;
		_menuTop->halt();
		_menuTop->showStill();
	}
}

XFilesEngine::MenuEntry XFilesEngine::clickMenu(const Common::Point &pos) {
	MenuEntry entry = hitMenu(pos);
	if (entry == kMenuNone)
		return entry; // A click outside the entries does nothing
	debugC(1, kDebugScript, "-- menu %s", kMenuEntryNames[entry]);
	// Every entry plays PrefClick.amv (the exe also plays the entry's highlight)
	delete _menuClick;
	_menuClick = new Movie();
	if (openMovie(*_menuClick, kMenuClickAsset)) {
		_menuClick->play(false);
	} else {
		delete _menuClick;
		_menuClick = nullptr;
	}
	return entry;
}

void XFilesEngine::menuChoice(MenuEntry entry) {
	switch (entry) {
	case kMenuPrevious:
		closeMenu();
		_menuOpen = false;
		pauseGame(false);
		pauseTimedText(false);
		// 0x4978a0: the preferences changed on the Game Preferences screens take effect now
		applyPreferences(true);
		break;
	case kMenuQuit:
		if (saveOnExit())
			quitGame();
		break;
	case kMenuNew:
		if (saveOnExit())
			restartGame();
		break;
	case kMenuSave:
		// The menu stays up, as after the original's save
		commitCheckpoint(); // Menu_AskSave snapshots even when the save screen is left
		saveGameDialog();
		break;
	case kMenuLoad:
		// A chosen save is queued; the next frame closes the menu and loads it
		loadGameDialog();
		break;
	case kMenuOptions:
		openMenuLayout(kOptionsLayout);
		break;
	case kMenuHelp:
		openMenuLayout(kHelpLayout);
		break;
	default:
		break;
	}
}

bool XFilesEngine::saveOnExit() {
	// A scripted run (xfiles_steps) has nobody to answer
	if (!_script || !_steps.empty())
		return true;
	// As the X-corner menu does, put the held item back before it saves
	if (isHolding())
		putBack();
	commitCheckpoint(); // Menu_AskSave snapshots, whatever the answer
	int32 mode = _preferences.variable(kPrefSaveGameOnExit) ? _script->getVariable(_preferences.variable(kPrefSaveGameOnExit)) : 'A';
	if (mode == 'Y') {
		// The original saves to the current save without asking; the autosave slot never overwrites a player's slot
		if (canSaveGameStateCurrently() &&
				saveGameState(getAutosaveSlot(), Common::convertFromU32String(_("Autosave")), true).getCode() != Common::kNoError)
			g_system->displayMessageOnOSD(_("Error occurred making autosave"));
		return true;
	}
	if (mode != 'A')
		return true;
	GUI::MessageDialog question(_("Do you want to save your game first?"), _("Yes"), _("No"));
	bool yes = question.runModal() == GUI::kMessageOK;
	// A load chosen in the GMM meanwhile wins: New or Quit is dropped
	if (_pendingLoad.valid)
		return false;
	if (!yes)
		return true;
	// A game that cannot be saved just now does not hold up New or Quit
	return !canSaveGameStateCurrently() || saveGameDialog();
}

bool XFilesEngine::loadMostRecentSave() {
	int best = -1;
	uint64 bestTime = 0;
	SaveStateList saves = getMetaEngine()->listSaves(_targetName.c_str());
	for (SaveStateList::const_iterator it = saves.begin(); it != saves.end(); ++it) {
		Common::ScopedPtr<Common::InSaveFile> in(_saveFileMan->openForLoading(getSaveStateName(it->getSaveSlot())));
		ExtendedSavegameHeader header;
		if (!in || !MetaEngine::readSavegameHeader(in.get(), &header))
			continue;
		uint16 year;
		uint8 month, day, hour, minute;
		MetaEngine::decodeSavegameDate(&header, year, month, day);
		MetaEngine::decodeSavegameTime(&header, hour, minute);
		uint64 time = ((((uint64)year * 12 + month) * 31 + day) * 24 + hour) * 60 + minute;
		// The date has no seconds: of two saves in the same minute the later slot counts
		if (best < 0 || time > bestTime || (time == bestTime && it->getSaveSlot() > best)) {
			best = it->getSaveSlot();
			bestTime = time;
		}
	}
	if (best < 0) {
		g_system->displayMessageOnOSD(_("There is no saved game"));
		return false;
	}
	Common::Error status = loadGameState(best);
	if (status.getCode() != Common::kNoError) {
		GUI::MessageDialog errorDialog(status.getDesc());
		errorDialog.runModal();
		return false;
	}
	return true;
}

void XFilesEngine::openGameMenu() {
	// XCorner_OpenMenu: the held item goes back, the scene is suspended and
	// the menu shows; the dialog box stays open behind it
	if (_menuOpen)
		return;
	if (isHolding())
		putBack();
	// The saves made while the menu is up show the game, not the menu
	_menuShot.copyFrom(_screen);
	pauseGame(true);
	// Scene_SuspendAll (0x46c9b0) stops the head credits and the subliminal object
	pauseTimedText(true);
	_menuOpen = true;
	startMenu();
	debugC(1, kDebugScript, "-- menu open");
}

void XFilesEngine::openMenuLayout(uint32 layout) {
	// MainMenu_OnMouseClick: hide the menu (the scene stays suspended) and display the layout
	if (!_script)
		return;
	closeMenu();
	_menuOpen = false;
	_menuLayout = true;
	_script->displayLayout(layout);
}

void XFilesEngine::runMenuLayout(uint32 layout) {
	// There is no game at the start-up menu: a Script that is not started holds the layout and the preferences, and
	// is dropped when the layout is unshown (the game creates its own). What the exe shows behind the layout here is
	// not read: nothing is drawn but the layout
	closeMenu();
	createScript();
	_script->loadPreferences(); // Prefs hooks need the registered variables
	_menuReopen = false;
	_menuLayout = true;
	_script->displayLayout(layout);
	while (!shouldQuit() && !_menuReopen && !_pendingLoad.valid) {
		Input input = pollInput();
		if (input.escape)
			escapeMenuLayout();
		if (input.click) {
			Interface::Hit hit = _interface->hitTest(input.pos);
			if (hit.kind == Interface::kHitItem)
				_script->press(hit.id);
		}
		_sound.update(getPreference(kPref3DSound));
		updateInterface();
		Interface::Hit hover = _interface->hitTest(_mouse);
		VCInterfaceItem item;
		_cursors->setVisible(true);
		_cursors->show(hover.kind == Interface::kHitItem && _data->getInterfaceItem(hover.id, item) ? item.cursor : 0);
		_cursors->update(getPreference(kPrefTranslucency));
		_screen.fillRect(Common::Rect(_screen.w, _screen.h), 0);
		_interface->drawBar(_screen);
		_interface->drawItems(_screen);
		present();
	}
	// Back to the menu: the layout's state goes with the script and the interface
	_menuReopen = false;
	_menuLayout = false;
	_sound.clear();
	_sound.attach(nullptr, nullptr);
	delete _script;
	_script = nullptr;
	_preferences.setScript(nullptr);
	delete _interface;
	_interface = new Interface(*_data, _discFiles);
	_interface->setFormat(_screen.format);
	_interface->init(_texts);
	_pressedItems.clear();
	_hover.kind = Interface::kHitNone;
	_hover.id = 0;
	_wasModal = false;
	// The volumes the screen changed (ReturnMainMenu stored them in the settings)
	syncSoundSettings();
	if (!shouldQuit())
		startMenu();
}

void XFilesEngine::reopenMenu() {
	// The menu object is created again; the media show as stills (the fade-in plays once per run)
	_menuReopen = false;
	if (!_menuLayout)
		return;
	_menuLayout = false;
	_menuOpen = true;
	startMenu();
	debugC(1, kDebugScript, "-- menu open");
}

void XFilesEngine::escapeMenuLayout() {
	// Game_KeyDown: a shown item 50464 (Help) or 49841 (Preferences' main menu button) gets its Mouse Click
	const Common::Array<Script::Layout> &layouts = _script->getLayouts();
	for (uint i = 0; i < layouts.size(); i++) {
		for (uint j = 0; j < layouts[i].items.size(); j++) {
			uint32 item = layouts[i].items[j];
			if (item == kHelpBackItem || item == kOptionsBackItem) {
				_script->press(item);
				_script->release();
				return;
			}
		}
	}
}

} // End of namespace XFiles
