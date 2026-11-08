#pragma once

class LiveActorFlag {
public:
    LiveActorFlag();

    bool mIsDead;
    bool mIsStoppedAnim;
    bool mIsNoCalcAnim;
    bool mIsNoCalcView;
    bool mIsHiddenModel;
    bool mIsNoBind;
    bool mIsCalcGravity;
    bool mIsClipped;
    bool mIsInvalidClipping;
    bool mIsDrawnClipped;  // stopped by the level's distance, its model still drawn because the camera sees it
    bool mHasPose;         // its own code has placed its model at least once
};
