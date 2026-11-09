#pragma once

class LayoutActorFlag {
public:
    LayoutActorFlag();

    bool mIsDead;
    bool mIsStopAnimFrame;
    bool mIsHidden;
    bool mIsOffCalcAnim;
    u8 mHudPart;  // NEBULA_HUD_*: the HUD part it is, which plugins can hide; 0 for the rest
};
