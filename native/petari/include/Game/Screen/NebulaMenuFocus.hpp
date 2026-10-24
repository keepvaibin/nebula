#pragma once

#include <JSystem/JGeometry/TVec.hpp>
#include <revolution/types.h>

class ButtonPaneController;

// Nebula's menu focus: the D-pad and the Nunchuk stick move the pointer's highlight between the buttons a menu takes
// input on (ButtonPaneController::trySelect) and the other things it offers (the file select's 3D files), to the
// nearest one in the pressed direction by where they are on screen; A decides as it does for a pointed one. Moving the
// pointer gives the menu back to it. The highlight is the button's own (its SelectIn and SelectOut animations, cursor
// sound and effects), as when the pointer is on it.
//
// Every Nebula menu reads the D-pad and stick the same way (readPress): a press, then repeats while it is held.
namespace NebulaMenuFocus {
    enum Direction {
        Direction_None,
        Direction_Up,
        Direction_Down,
        Direction_Left,
        Direction_Right,
    };

    // One menu's held direction (zero is nothing held).
    struct Repeat {
        s32 mHeld;
        s32 mHeldFrames;
    };

    Direction readDirection();             // held this frame: the D-pad, else the stick pushed far enough
    Direction readPress(Repeat* pRepeat);  // a press this frame, or a repeat of a held one
    void holdCurrent(Repeat* pRepeat);     // what is held now is not a press (the stick that was walking as a menu opens)

    void update();                                        // GameSystem::update, once a frame, before the menus run
    void offer(ButtonPaneController* pButton);            // a button its menu takes input on this frame
    void offerTarget(const void* pKey, const TVec2f& rScreenPos);  // anything else, where it is on screen this frame
    bool isActive();                                      // the D-pad or stick moved the highlight after the pointer
    bool isFocused(const ButtonPaneController* pButton);  // the button the D-pad or stick highlights
    bool isTargetFocused(const void* pKey);               // the target the D-pad or stick highlights
    Direction getPress();                                 // this frame's press (or repeat) in a menu
    Direction getUnmovedPress();                          // the same, when nothing lay that way (the highlight stayed)
    void setFocus(const ButtonPaneController* pButton);   // a menu's own move: the highlight to a button it offers
};  // namespace NebulaMenuFocus
