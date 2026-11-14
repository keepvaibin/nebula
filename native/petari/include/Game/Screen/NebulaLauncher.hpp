#pragma once

// Launcher mode's home screen (the host's NEBULA_HOME_MENU_LAUNCHER_*): Play, Settings, Quit, and an update's notice.
// The host keeps it (its keys and controllers' buttons) and serves its view as the HOME Menu's while that is closed; the
// settings menu's screen draws it as it draws the HOME Menu (NebulaSettingsMenu); the boot's title waits on it (BootScene), then goes on after Play. Off, nothing here runs but
// the one request at the hold's first frame.
namespace NebulaLauncher {
    // The title's hold, its first frame: the host opens the home screen when launcher mode is on.
    void open();
    // Each frame of the hold: the sounds of what changed, and the choice (Quit leaves the game as the HOME Menu's does).
    void update();
    // The title waits: the home screen shows, or the game is leaving from it.
    bool isHolding();
    // The notice's texts (0 the update's version, 1 its notes) into `pText`, at most `capacity` units with the end; the length.
    s32 readText(u32 which, char16_t* pText, s32 capacity);
};  // namespace NebulaLauncher
