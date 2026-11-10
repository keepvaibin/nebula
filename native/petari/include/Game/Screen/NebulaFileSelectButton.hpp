#pragma once

#include "Game/Screen/LayoutActor.hpp"

class ButtonPaneController;

// Nebula's buttons on the file select: Quit to desktop in the top-left corner and Settings in the top-right. Each is
// the file select's own Start button (its art, animations and pointer behaviour) at the size of the file select's
// secondary buttons, its pill as wide as its label needs, growing toward the screen's middle when pointed.
// FileSelector shows them while a file is being chosen and nothing else holds the top of the screen.
class NebulaFileSelectButton : public LayoutActor {
public:
    enum Role {
        Role_Quit,      // asks, then quits the game
        Role_Settings,  // opens the settings menu
    };

    NebulaFileSelectButton(const char* pName, Role role);

    virtual void init(const TableIter& rIter) override;
    virtual void control() override;

    // Shown while true (after it has held a few frames); it finishes an animation it has started before reversing.
    void setShown(bool shown);
    bool isDecided() const;
    // After the question or the menu: the button waits to be chosen again.
    void resume();
    bool isPointing() const;
    // Nebula's menu focus: the D-pad or stick's highlight to it, once it waits to be chosen. True when it took it.
    bool takeMenuFocus();

    static const char16_t* getQuitQuestion();

private:
    void layOut();

    ButtonPaneController* mButton;
    Role mRole;
    s32 mShownFrames;
    bool mIsDecided;
};
