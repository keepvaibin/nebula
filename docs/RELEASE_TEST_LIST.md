# Release candidate test list

The owner's hand test of a release candidate. Every step says what to do and what should be seen. The automated gates run
first (`tools/verify`, the unit tests, `installer/tests`, `tools/release_check.ps1`); this list is what only a person on a
real machine sees. Use a Windows user who has never run Nebula, a phone, and a disc image of your own game. Mark each
step pass, fail (with a note) or n/a. A failure with a NEB code: note the code and keep the report file.

## 0. Before anything

| # | Do | Should see |
|---|----|------------|
| 0.1 | Run `tools/release_check.ps1 -Path <release folder> -Installer <Setup exe>` (add `-RequireSigned` when signing is set up) | `release_check: N files passed`; notes list unsigned third-party files only; no failure line |
| 0.2 | Check the version in the Setup file's properties, the launcher's About and the release notes | All three show the release's version |
| 0.3 | Copy the Setup file to the test machine by the route players use (download, not a shared folder) | Windows shows no malware warning; a SmartScreen "unrecognised app" notice for an unsigned file is noted, not a failure |

## 1. Install

| # | Do | Should see |
|---|----|------------|
| 1.1 | Run `Nebula-Setup.exe` on a machine with no Nebula | It asks for the install folder and the game disc image (ISO, RVZ or an extracted folder) and shows a progress bar with Cancel |
| 1.2 | Choose a folder that is not a Super Mario Galaxy disc | Setup says so and installs nothing |
| 1.3 | Choose a disc image of a language other than your own | It imports; the launcher's Language lists that disc's languages |
| 1.4 | Cancel part way | Setup asks to confirm, stops, and leaves no half install; running Setup again starts clean |
| 1.5 | Let the import finish | Setup offers a desktop shortcut and finishes; no console window opens |
| 1.6 | Run Setup again on the installed copy (repair) | Saves and settings are unchanged and the game still starts |
| 1.7 | Start Nebula while it is already running | It says it is running and does not open a second game |

## 2. Import and first boot

| # | Do | Should see |
|---|----|------------|
| 2.1 | Start Nebula from the shortcut | The launcher opens on the Display page in the system language |
| 2.2 | Press Play | The game window opens and reaches the title screen; no black frames; the title music plays |
| 2.3 | Press A+B (or the keyboard equivalent) at the title to reach File Select | Empty save files; choosing one starts a new game and opens the Mii picker |
| 2.4 | Open the install folder and the data folder in Explorer | The game data is there as Nebula's own store (`game.pak`); no copy of the disc image is kept |
| 2.5 | Close the game | The launcher is still open and no `NebulaNative.exe` remains in Task Manager |

## 3. Every settings page

Open the launcher pages, then in the game open the settings menu (`F1`, View+Menu, Share+Options or Minus+Plus) and check its pages: Display, Graphics, Controls, Audio, General, Mods, System, Advanced.

| # | Do | Should see |
|---|----|------------|
| 3.1 | Launcher Display: change window mode, monitor, refresh handling | Each choice is kept after Close and reopen; the next game start uses it |
| 3.2 | Launcher Graphics: internal resolution, anti-aliasing, graphics card, renderer, upscaler and frame generation options | Each control responds; options the machine cannot run are greyed with a reason; the card chosen is the one the game starts on |
| 3.3 | Launcher Audio: each volume, output mode (Mono, Stereo, Surround) and the device | Each volume changes only its own sound; the mode switches the output |
| 3.4 | Launcher Controls: device, rebinding, deadzones, pointer, gyro, vibration | A rebind works in the game; a shared binding shows red; Defaults restores |
| 3.5 | Launcher General: language, text speed, update and diagnostics options | Language switches the launcher and the game's menus; the choices are kept |
| 3.6 | Launcher Mods: import, enable, disable, reorder, remove | See section 9 |
| 3.7 | Launcher Advanced: its options, Open save folder; record diagnostics (lightweight, then detailed) and play | Each option applies; the next run writes a log and Open diagnostics opens it |
| 3.8 | Press Defaults on a page | Only that page returns to its default values |
| 3.9 | In the game, open the settings menu and change a Display and a Graphics row | The change takes effect at once or says it applies at the next start |
| 3.10 | In the game, System and Advanced pages | System shows the graphics card and the diagnostics record rows; Advanced rows apply |
| 3.11 | Change a setting in the game, quit, open the launcher | The launcher shows the same value |

## 4. Save and load

| # | Do | Should see |
|---|----|------------|
| 4.1 | Start a new file, play the first galaxy, save in the game, quit | The next start shows the file with its stars and Mii |
| 4.2 | Launcher: Open save folder | The folder opens and holds the save files |
| 4.3 | Export saves | A `.zip` is written to the chosen folder and holds the game's save files |
| 4.4 | Import that `.zip` into a fresh install | File Select shows the same files, stars and Miis |
| 4.5 | Import a `Nebula.sav` from another machine | It loads; the saves in place are kept in the save backups folder |
| 4.6 | Import a `GameData.bin` from a Wii or Dolphin save folder | It loads in File Select |
| 4.7 | Import a damaged or encrypted save (a Wii SD export) | The launcher says what is wrong and changes nothing |
| 4.8 | Import while the game runs | The launcher asks to close the game first |
| 4.9 | Pull the plug (end the process in Task Manager) while saving, then start | The game starts and loads the last complete save; no NEB code for a half-written file |
| 4.10 | Delete a file in File Select, copy a file, then quit and start | The change is kept |

## 5. Controllers

(`docs/CONTROLLER_CHECKLIST.md` has the full list per family.)

| # | Do | Should see |
|---|----|------------|
| 5.1 | Connect Xbox, PlayStation, Switch Pro, a Joy-Con pair, a Steam Controller or Deck, and a generic pad, one at a time | Each is named in Controls within a second |
| 5.2 | Play with each | The button icons in the game and menus match the controller in use; switching controller switches the icons; Button icons set to a family shows that family |
| 5.3 | Pointer by right stick and by gyro; shake spins; vibration on a Star Bit and on a hit | Each works; vibration only on a pad that has it |
| 5.4 | Unplug a pad in play, plug it back | The game goes on; the pad works again within two seconds |
| 5.5 | A Wii Remote (Bluetooth, and a DolphinBar on Android) | Connects, points at the sensor bar and by motion (`docs/WII_REMOTE.md`), the speaker sounds |
| 5.6 | Keyboard and mouse (`docs/KEYBOARD_CONTROLS.md`) | The default map plays the whole game; the mouse points |
| 5.7 | Input mode Controller with no controller attached | The missing-controller screen shows and goes within 2 seconds of one arriving |

## 6. Display and renderers

| # | Do | Should see |
|---|----|------------|
| 6.1 | Windowed, borderless fullscreen, a chosen monitor | Each starts at the right size and place; resizing keeps the picture's shape |
| 6.2 | Internal resolution 1x, 2x, 4x and the anti-aliasing options | The picture changes; the game holds 60 fps on a mid-range machine |
| 6.3 | Frame rate limit and Match refresh rate on a 60, a 120 and a variable-refresh display | The game runs at its speed and the picture is smooth |
| 6.4 | Interpolation on and off | On: motion is smooth and the HUD and cuts do not smear. Off: the game steps at its own rate |
| 6.5 | Direct3D 12 (`NebulaNative.exe`, the Windows default): title to a galaxy; alt-tab; minimise and restore | The picture is kept each time |
| 6.6 | Vulkan (`NebulaNativeVk.exe` on Windows, and on Linux): the same run; pick the other graphics card in Graphics | It works, the pictures match Direct3D 12's, and it starts on the card chosen |
| 6.7 | DLSS, FSR and XeSS (where the machine supports them) | Each loads; the downloads ask for the licence first; turning them off returns to the plain picture |
| 6.8 | Force a graphics device loss (a driver reset) | The game recovers, or shows NEB-G01 with a report; it does not hang |

## 7. Play through, including the ending

| # | Do | Should see |
|---|----|------------|
| 7.1 | Play the first galaxies, a boss, the Observatory | No missing textures, no black screens, music and voices in sync |
| 7.2 | Reach a power-up tutorial and a Luma talk | The text boxes show, advance and close; voices play |
| 7.3 | Watch a movie scene and skip it | It plays in sync; the skip prompt works |
| 7.4 | Play to the final battle and the ending movie and credits | The ending plays in sync to its save prompt; the game returns to the title |
| 7.5 | After the ending, play as Luigi | Luigi plays; a death reloads the stage |

## 8. Sound

| # | Do | Should see |
|---|----|------------|
| 8.1 | Music, effects, voice, ambience and cutscene volumes | Each changes only its own sound |
| 8.2 | Mono, Stereo and Surround | The output switches without a restart |
| 8.3 | Unplug and replug headphones in play | Sound continues on the new device without a restart |

## 9. Mods

| # | Do | Should see |
|---|----|------------|
| 9.1 | Import a mod from a folder, a `.zip` and a `.7z` | Each appears in the Mods page |
| 9.2 | Enable it and start the game | Its files, texture or movie show; disabling returns the original |
| 9.3 | Two mods that change the same file | The one higher in the list wins; Move up and Move down change that |
| 9.4 | A mod made for another region; a mod that crashes the game | The first is marked; the second gives NEB-M01 or NEB-M02 with the mod named in the report |
| 9.5 | Remove a mod | It goes to the Recycle Bin and the list updates |
| 9.6 | A texture pack with a damaged image; a bad replacement video | NEB-T01 and NEB-V01; neither hangs |

## 10. Update over an older install

| # | Do | Should see |
|---|----|------------|
| 10.1 | With the previous release installed and saves in place, start Nebula | It checks for updates and shows the new version, the notes and a Download and install button |
| 10.2 | Install the update | It downloads, verifies, installs and starts as the new version; saves and settings are unchanged; the old version stays until the new one is ready |
| 10.3 | Cut the network during the download | It reports the failure and the installed version still starts |
| 10.4 | Install an older install that still has the game.pak layout or a `nand` save folder | Setup moves it to the native store and `saves`; the files arrive in File Select; running Setup again changes nothing |
| 10.5 | Turn Beta releases on and off | A prerelease is offered with it on; only releases with it off |
| 10.6 | Roll back to the previous version | The previous version starts with its own game files; the files in the saves folder are unchanged (note their sizes first); updating again works |
| 10.7 | Try to roll back while Nebula runs | It is refused with a message |

## 11. Uninstall

| # | Do | Should see |
|---|----|------------|
| 11.1 | Uninstall from Apps, with "remove user data" off | Nebula is gone from the install folder and Apps; the saves stay |
| 11.2 | Install again, then uninstall with "remove user data" on | The saves and settings go too |
| 11.3 | Look for leftovers: install folder, the data folder, shortcuts, the Start menu | Nothing of Nebula is left after step 11.2 |

## 12. Android

| # | Do | Should see |
|---|----|------------|
| 12.1 | Install the APK on a phone; update over an older one signed with the same key | The launcher opens; the update keeps the saves |
| 12.2 | Setup from a disc image on the device | It imports with progress; Cancel and retry work |
| 12.3 | Start the game | It reaches the title; touch controls play it (floating stick, buttons, pointer); with a controller attached the touch controls hide |
| 12.4 | Back or the HOME button | The HOME Menu opens; leaving the app and returning keeps the game and the sound |
| 12.5 | Rotate the phone; fold or unfold a foldable | The picture stays upright and the right shape |
| 12.6 | Export and import saves on the device (as 4.3 to 4.6) | The same results |
| 12.7 | A Bluetooth controller and a Wii Remote through a DolphinBar | Both play |
| 12.8 | Uninstall: choose to keep the data, then reinstall | The saves are still there |

## 13. Crash report

| # | Do | Should see |
|---|----|------------|
| 13.1 | Force a crash (a mod that breaks the game) | A report dialog with a NEB code, advice and the report file |
| 13.2 | Open the report | No user name, personal path, e-mail address or MAC address in it |
| 13.3 | Start Nebula again | It starts normally |

## 14. Privacy

| # | Do | Should see |
|---|----|------------|
| 14.1 | Watch the network (Resource Monitor) while the game and launcher run | Only the update check and the downloads the player asked for; the Discord status follows its setting |
| 14.2 | Search the install folder for `GALAXY_` | No file holds a test switch name (this is what `release_check.ps1` checks) |
