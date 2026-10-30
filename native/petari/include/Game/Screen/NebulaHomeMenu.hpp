#pragma once

// The HOME Menu: the host keeps it (its rows close, reset and leave the game, the last two after "are you sure?"; its
// keys and controller buttons) and the game drives and draws it (HomeButtonLayout, NebulaSettingsMenu). On the console
// it was Nintendo's HOME button library, a module on the disc the game linked at run time.

#include <revolution/types.h>

namespace NebulaHomeMenu {
    enum Choice {
        cChoiceNone = -1,
        cChoiceClose = 0,
        cChoiceLeaveGame = 1,
        cChoiceReset = 2,
    };

    // Opens the menu, with the HOME button's sound.
    void open(bool widescreen);
    // Each frame while open: the choice so far, with the sounds of what changed since the last frame (a row selected,
    // "are you sure?" asked or left, the choice made). Once made, the choice stays until the menu is opened again.
    Choice update();
    // Closes the menu at once (a reset or power event): the choice becomes cChoiceClose.
    void close();

    // For drawing: -1 while closed, else the selected answer in bits 0-7 and in bits 8-15 the choice "are you sure?"
    // asks about (0: the list of rows).
    s32 view();
    // The pointer is over an answer, or clicked one.
    void point(s32 answer);
    void choose(s32 answer);
}  // namespace NebulaHomeMenu
