#pragma once

#ifdef TARGET_PC
#include "Game/Screen/LayoutActor.hpp"

class ButtonPaneController;

// Nebula's Quit to desktop button on the file select: the file select's own Start button (its art, animations and
// pointer behaviour) in the opposite corner, at the size of the file select's secondary buttons, its pill as wide as
// its label needs. FileSelector shows it while a file is being chosen and asks before quitting.
class NebulaQuitButton : public LayoutActor {
public:
    NebulaQuitButton(const char* pName);

    virtual void init(const JMapInfoIter& rIter);
    virtual void control();

    // Shown while true (after it has held a few frames); it finishes an animation it has started before reversing.
    void setShown(bool shown);
    bool isDecided() const;
    // After the question: the button waits to be chosen again.
    void resume();
    bool isPointing() const;
    // Nebula's menu focus: the D-pad or stick's highlight to it, once it waits to be chosen. True when it took it.
    bool takeMenuFocus();

    static const wchar_t* getQuestion();

private:
    void layOut();

    /* 0x20 */ ButtonPaneController* mButton;
    /* 0x24 */ s32 mShownFrames;
    /* 0x28 */ bool mIsDecided;
};
#endif
