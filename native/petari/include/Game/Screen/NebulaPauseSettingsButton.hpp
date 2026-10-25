#pragma once

#ifdef TARGET_PC
#include "Game/Screen/LayoutActor.hpp"

class ButtonPaneController;

// The pause menu's Settings button: the pause menu's own lower button (its art, animations and pointer behaviour) at a
// smaller size in the middle of the menu's lower band, between the counters. PauseMenu shows and draws it with its own
// buttons and opens Nebula's settings menu (NebulaSettingsMenu) when it is chosen.
class NebulaPauseSettingsButton : public LayoutActor {
public:
    NebulaPauseSettingsButton();

    virtual void init(const JMapInfoIter& rIter);
    virtual void control();

    void show();
    void hide();
    ButtonPaneController* getButton() const { return mButton; }

private:
    ButtonPaneController* mButton;
};
#endif
