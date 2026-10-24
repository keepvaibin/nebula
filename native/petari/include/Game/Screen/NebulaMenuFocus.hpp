#pragma once

#include <revolution/types.h>

class ButtonPaneController;

// Nebula's menu focus: the D-pad and the Nunchuk stick move the pointer's highlight between the buttons a menu takes
// input on (ButtonPaneController::trySelect), to the nearest one in the pressed direction by where the buttons are on
// screen; A decides as it does for a pointed button. Moving the pointer gives the menu back to it. The highlight is the
// button's own (its SelectIn and SelectOut animations, cursor sound and effects), as when the pointer is on it.
namespace NebulaMenuFocus {
    void update();                                        // GameSystem::update, once a frame, before the menus run
    void offer(ButtonPaneController* pButton);            // a button its menu takes input on this frame
    bool isActive();                                      // the D-pad or stick moved the highlight after the pointer
    bool isFocused(const ButtonPaneController* pButton);  // the button the D-pad or stick highlights
};  // namespace NebulaMenuFocus
