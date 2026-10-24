#pragma once

#ifdef TARGET_PC
#include <revolution/types.h>

class LayoutActor;

// The game's button icons (its picture font's A, B, C, Z, 1, 2, Plus and Minus) drawn as the player's controller's
// buttons: the host names the style (the controller in use, or the one chosen) and the button each stands for
// (NebulaHostServices::button_icons). Every text and layout that shows a button through the picture font shows them.
namespace NebulaIcons {
    // Each frame: the icons again when the host's choice changed.
    void update();
    // The A button over people and signs (IconAButton's own pictures) as the icons have it, in `pActor`'s layout, when
    // the icons changed since `*pShown` (the layout's own count, 0 at first).
    void applyToLayout(LayoutActor* pActor, u32* pShown);
    // A real Wii Remote is in the player's hand (the host's NebulaButtonIcons::wii_remote): the game's pictures of one
    // (how to shake it to spin, tilt it to roll the Star Ball or steer the Ray) show only then.
    bool wiiRemoteInUse();
};  // namespace NebulaIcons
#endif
