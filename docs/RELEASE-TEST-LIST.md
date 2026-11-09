# Release test list

What the owner checks by hand before a beta. Each line is one check and what should happen. The automated gates
(`tools/verify`, the unit tests, `installer/tests`) run first; this list covers what only a person on a real machine
sees. Use a clean Windows user, a phone, and a disc image of your own game. Mark each line pass, fail (with a note)
or n/a.

## Install

1. Run `Nebula-Setup.exe` on a machine with no Nebula: it asks for an install folder and a disc image (ISO, RVZ or an extracted folder), and shows progress with a Cancel button.
2. Pick a disc image: Setup imports it, installs, and offers a desktop shortcut; Nebula starts from the shortcut and reaches the title screen.
3. Cancel part way: Setup asks to confirm, stops, and leaves no half install (starting Setup again begins clean).
4. Choose a folder that is not a Super Mario Galaxy disc: Setup says so and installs nothing.
5. Run Setup again on an installed copy (repair): the saves and settings are unchanged and the game still starts.
6. Start Nebula while it is already running: it says it is running and does not open a second game.
7. Uninstall from Apps: Nebula is removed; with the "remove user data" choice off, the saves stay; with it on, they go.

## Update

8. With an older version installed, Nebula checks for updates at start (or Check for updates in the launcher): it shows the new version, the notes, and a Download and install button.
9. Install the update: it downloads, verifies, installs, and starts as the new version; the saves and settings are unchanged; the old version stays available until the new one is ready.
10. Cut the network during the download: it reports the failure and the installed version still starts.
11. Turn on Beta releases: a prerelease is offered; turn it off: only releases are.
12. Roll back to the previous version: the previous version is active again and starts; the saves set aside for it return, and updating again brings the newer saves back.

## Saves

13. Play, save in the game, quit: the launcher's Open save folder shows the save files.
14. Export saves: a `.zip` is written to the chosen folder; it holds the game's save files.
15. Import saves from that `.zip` into a fresh install: File Select shows the same files, stars and Miis.
16. Import a `Nebula.sav` from another machine: it loads, and the saves in place are kept in the save backups folder.
17. Import a `GameData.bin` from a Wii or Dolphin save folder: it loads in File Select.
18. Import a damaged or encrypted save (a Wii SD export): the launcher says what is wrong and changes nothing.
19. Import with the game running: the launcher asks to close the game first.

## Settings and the launcher

20. Open the launcher: General, Graphics, Display, Audio, Controls, Mods and Advanced pages open; every control responds.
21. Change a setting, close, reopen: it is kept. Defaults restores it.
22. Change the language: the launcher and the game's own menus switch (check each language the build ships).
23. In the game, F1 (or View + Menu, or Minus + Plus) opens the settings menu; the same settings are there and a change takes effect.
24. Record diagnostics (lightweight, then detailed): the next run writes a log and Open diagnostics opens it.

## Display and renderers

25. Windowed, borderless fullscreen and a chosen monitor: each starts at the right size and place; resizing the window keeps the picture's shape.
26. Internal resolution 1x, 2x, 4x and anti-aliasing options: the picture changes and the game stays at 60 fps on a mid-range machine.
27. Frame rate limit and Match refresh rate on a 60, a 120 and a variable-refresh display: the game runs at its speed and the picture is smooth.
28. Interpolation on: motion is smooth and the HUD and cuts do not smear; off: the game steps at its own rate.
29. Direct3D 12 (`NebulaNative.exe`, the Windows default): the game plays from the title to a galaxy; alt-tab and back, minimise and restore keep the picture.
30. Vulkan (`NebulaNativeVk.exe` on Windows, and on Linux): the same run as 29 works and the pictures match Direct3D 12's; pick the other graphics card in Graphics and it starts on it.
31. DLSS, FSR and XeSS options (where the machine supports them): each loads, the downloads ask for the licence first, and turning them off returns to the plain picture.
32. Force a graphics device loss (driver reset): the game recovers or shows NEB-G01 with a report, and does not hang.
33. Play through the first galaxies, a boss, the Observatory and the ending cutscene: no missing textures, no black screens, sound and video in sync.

## Sound

34. Music, effects, voice, ambience and cutscene volumes: each changes only its sound; Mono, Stereo and Surround switch the output.
35. Unplug and replug headphones in play: sound continues on the new device without a restart.

## Controllers (docs/CONTROLLER_CHECKLIST.md has the full per-family list)

36. Xbox, PlayStation, Switch Pro, Joy-Con pair, Steam Deck or Steam Controller, and a generic pad: each connects and its name shows in Controls within a second.
37. The button icons in the game and the menus match the controller in use; switching controllers switches the icons; Button icons set to a family shows that family.
38. Rebind a button in Controls: the new binding works, a shared binding shows red, Defaults restores.
39. Pointer by right stick and by gyro; shake spins; vibration on a Star Bit and a hit.
40. Unplug a controller in play: the game goes on; plug it back: it works within two seconds.
41. A real Wii Remote (Bluetooth, and a DolphinBar on Android): connects, points at the sensor bar and by motion (docs/WII_REMOTE.md), the speaker sounds.
42. Keyboard and mouse: the default map plays the whole game (docs/KEYBOARD_CONTROLS.md); the mouse points.
43. Input mode Controller with no controller: the missing-controller screen shows and goes within 2 seconds of one arriving.

## Mods

44. Import a mod from a folder, a `.zip` and a `.7z`: it appears in the Mods page.
45. Enable it, start the game: its files, texture or movie show; disable it: the original returns.
46. Two mods that change the same file: the one higher in the list wins; Move up and Move down change that.
47. A mod made for another region is marked, and a mod that crashes the game gives NEB-M01 or NEB-M02 with the mod named in the report.
48. Remove a mod: it goes to the Recycle Bin and the list updates.
49. A texture pack with a damaged image gives NEB-T01, and a bad replacement video gives NEB-V01; neither hangs.

## Android

50. Install the APK on a phone (and update over an older one signed with the same key): the launcher opens.
51. Setup from a disc image on the device: it imports, shows progress, and the game is ready; Cancel and retry works.
52. Start the game: it reaches the title; touch controls play it (floating stick, buttons, pointer); with a controller attached the touch controls hide.
53. Back or the HOME button opens the HOME Menu; leaving the app and returning keeps the game and the sound.
54. Rotate the phone and fold or unfold a foldable: the picture stays the right way up and the right shape.
55. Export and import saves on the device (as 14 to 17); the saves of an older build are kept after an update.
56. A Bluetooth controller and a Wii Remote through a DolphinBar: both play.
57. Uninstall: Android asks whether to keep the data; Keep, then reinstall: the saves are still there.

## Crash and report

58. Force a crash (a mod that breaks the game): the report dialog shows a NEB code, advice, and the report file; the report holds no user name or personal path.
59. After the crash, Nebula starts again normally.

## Privacy and release check

60. The installer and the game send nothing except the update check and the downloads the player asked for; the Discord status follows its setting.
61. The release folder holds no test hooks (no `GALAXY_` test variables act in the release build), no disc content, and its files are signed with the release key.
62. The version shown in Setup, the launcher and the update notes matches the release.
