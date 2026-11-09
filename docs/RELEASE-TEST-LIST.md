# Release test list

What the owner checks by hand on a release candidate, per platform. Run the automated gates first (`tools/verify`, the unit
tests, `installer/tests`, `tools/release_check.ps1`). Controllers have their own list: `CONTROLLER_CHECKLIST.md`. Mark each
row pass, fail (with a note) or n/a. A failure with a NEB code: keep the code and the report file.

## Windows (Direct3D 12 and Vulkan)

Use a Windows user that never ran Nebula, a mid-range PC and a low-end one, and a disc image of your own game.

### Install and update

| # | Do | Expected |
|---|----|----------|
| W1 | `tools/release_check.ps1 -Path <release folder> -Installer <Setup>` (add `-RequireSigned` when signing is set up) | `release_check: N files passed`; unsigned files are listed as notes |
| W2 | Run Setup on a machine with no Nebula; choose a folder that is not the game | Setup refuses it and installs nothing |
| W3 | Choose a disc image (ISO, RVZ or extracted folder); cancel half way, then run it again | Cancel stops cleanly; the second run starts clean and finishes; no console window |
| W4 | Run Setup again on the installed copy | Saves and settings unchanged; the game starts |
| W5 | Start Nebula twice | The second start says it is running; one game window |
| W6 | With the previous release and saves in place: Download and install the update | New version starts; saves and settings unchanged |
| W7 | Cut the network during the download | A failure message; the installed version still starts |
| W8 | Update from an older install with the `game.pak` layout or a `nand` save folder | The files arrive in File Select; a second run changes nothing |
| W9 | Roll back, then update again; try to roll back while Nebula runs | Rollback keeps the saves; the second is refused with a message |
| W10 | Uninstall with "remove user data" off, then on | Off: saves stay. On: saves and settings go. No leftovers in the folders or Start menu |

### Boot, settings, saves

| # | Do | Expected |
|---|----|----------|
| W11 | Play: reach the title, File Select, a new game | Title music plays; no black frames; the Mii picker opens |
| W11b | Install from a European (RMGP01) and a Japanese (RMGJ01) disc image as well as the American one; in each, choose every language the disc has (Language row), then play the prologue and a galaxy | Each is accepted; title, menus, talk, star names and voices are in the language chosen, with no missing letters; the speed is the game's own on every disc |
| W12 | Every launcher page and every in-game settings page (`F1`): change a row, quit, reopen | The value is kept and shown the same in both; Defaults resets only its own page |
| W12b | Settings, ADVANCED: travel to a galaxy and star from the galaxy select; then give the Grand Stars | The game goes there through its own stage change; with the Grand Stars the Observatory's domes are built |
| W13 | Save in a galaxy, quit, start | The file shows its stars and Mii |
| W14 | Export saves; import the `.zip` into a fresh install | Same files, stars and Miis |
| W15 | Import a `Nebula.sav`, a `GameData.bin` from a Wii or Dolphin save folder, a damaged file, and an import while the game runs | The first two load (the saves in place go to the backups folder); the damaged file is explained and nothing changes; the last asks to close the game |
| W16 | End the process while the game saves, then start | The last complete save loads; no NEB code |

### Display, sound, play

| # | Do | Expected |
|---|----|----------|
| W17 | Windowed, borderless, a chosen monitor; resize; alt-tab; minimise and restore | Right size and place, shape kept, picture kept |
| W17b | Drag the window to a monitor with another refresh rate, scaling or HDR; change Windows display scaling while it runs; cover it fully with another window, then show it | Sharp at the new scale; pacing follows the new monitor's rate; the picture returns at once when shown; sound never stops |
| W18 | Internal resolution, anti-aliasing, frame rate limit and Match refresh rate on 60, 120 and variable-refresh displays | The picture changes; speed is the game's own; motion smooth |
| W18b | A laptop on battery: play ten minutes at a 60 frame rate limit, then minimise for five; watch Task Manager's Power usage column | Playing: the limit holds and power usage is no higher than with Nebula's previous release; minimised: very low |
| W19 | Interpolation on and off | On: smooth, HUD and cuts clean. Off: the game's own rate |
| W20 | Run `NebulaNative.exe` (Direct3D 12) and `NebulaNativeVk.exe` (Vulkan) through title to a galaxy; pick the other graphics card | Same picture on both; the card chosen is the card used |
| W21 | DLSS, FSR, XeSS where the machine has them | Each loads; the licence is asked before a download; off returns the plain picture |
| W22 | Force a driver reset | Recovers or shows NEB-G01 with a report; no hang |
| W23 | Volumes, Mono / Stereo / Surround, unplug and replug headphones | Each volume moves only its sound; output follows without a restart |
| W24 | Play the first galaxies, a boss, a power-up tutorial, a Luma talk, a movie and its skip | No missing textures or black screens; text boxes work; voices and movies in sync |
| W25 | Play to the final battle, the ending and credits, then as Luigi | Ending in sync to its save prompt; the title returns; Luigi plays |

### Mods, crash report, privacy

| # | Do | Expected |
|---|----|----------|
| W26 | Import a mod from a folder, a `.zip` and a `.7z`; enable, disable, reorder, remove | Appears, applies and reverts; the higher mod wins a shared file; removal goes to the Recycle Bin |
| W26b | A mod with code (a plugin): enable it; allow its code, then replace its library with another build | Its code runs only once allowed; a changed library is asked about again |
| W27 | A mod for another region; a mod that crashes the game; a damaged texture pack image; a bad video | Marked; NEB-M01 or NEB-M02 naming the mod; NEB-T01; NEB-V01; no hang |
| W28 | Force a crash; open the report | A NEB code, advice, the report file; no user name, path, e-mail or MAC address in it; the next start is normal |
| W29 | Watch the network while the launcher and game run | Only the update check and downloads asked for; the Discord status follows its setting |

## Android

Use a phone and a foldable or tablet, with and without a Bluetooth controller.

| # | Do | Expected |
|---|----|----------|
| A1 | Install the APK; install a newer one over it | Launcher opens; the update keeps the saves |
| A2 | Setup from a disc image on the device; cancel and retry | Imports with progress; cancel and retry work |
| A3 | Start the game; use the touch controls, then attach a controller | Title reached; stick, buttons and pointer play; touch controls hide with a controller |
| A4 | Back or HOME; leave the app and return | The HOME Menu opens; game and sound continue |
| A5 | Rotate; fold and unfold | Upright and the right shape |
| A6 | Frame rate limit, interpolation and upscaler rows; play five minutes | Rows apply; no flicker; the device does not overheat to a stop |
| A7 | Export and import saves (as W14, W15) | Same results |
| A8 | A Bluetooth controller; a Wii Remote through a DolphinBar | Both play |
| A9 | Uninstall keeping the data, then reinstall | Saves still there |
