#pragma once

#include "Game/NameObj/NameObj.hpp"

class AudBgmConductor : public NameObj {
public:
    /// @brief Creates a new `AudBgmConductor`.
    AudBgmConductor();

    virtual void init(const JMapInfoIter& rIter);
    virtual void movement();

    void playPinchEventBgmLevel(bool);
    void setStateBit(u32);
    void stopPinchEventBgmSoon();
    void movementPinchEventBgm();

    // TODO: clarify StateBit_1
    enum StateBits {
        StateBit_1 = 0b010,
        StateBit_Delay = 0b100,
    };

    u32 mStateBits;
    s32 mState;
    s32 mStartDelay;
    s32 mFadeInDelay;
    bool mStopImmediately;
};
