#pragma once

#ifdef TARGET_PC
#include <revolution/types.h>

class LayoutActor;

// The game's button icons (its picture font's A, B, C, Z, 1, 2, Plus and Minus) drawn as the player's controller's
// buttons: the host names the style (the controller in use, or the one chosen) and the button each stands for
// (NebulaHostServices::button_icons). Every text and layout that shows a button through the picture font shows them.
namespace NebulaIcons {
    // The stationed GDDR heap's room for our sheets, widths and code map (HeapMemoryWatcher); a font that needs more
    // keeps its own icons.
    const u32 cStationedBytes = 0x100000;
    // Once the stationed resources are loaded (the picture font among them), before the stationed heaps shrink to fit:
    // the font's sheets, widths and code map of ours.
    void init();
    // Each frame: the icons again when the host's choice changed.
    void update();
    // `pActor` (an IconAButton) drawn with the A button over people and signs as the icons have it: ours in place of
    // its own two pictures while it draws, its own after.
    void drawWithIcons(const LayoutActor* pActor);
    // A real Wii Remote is in the player's hand (the host's NebulaButtonIcons::wii_remote): the game's pictures of one
    // (how to shake it to spin, tilt it to roll the Star Ball or steer the Ray) show only then.
    bool wiiRemoteInUse();
};  // namespace NebulaIcons
#endif
