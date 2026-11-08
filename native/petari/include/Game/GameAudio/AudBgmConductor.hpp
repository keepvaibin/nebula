#pragma once

#include "Game/NameObj/NameObj.hpp"

class AudBgmConductor : public NameObj {
public:
    /// @brief Creates a new `AudBgmConductor`.
    AudBgmConductor();

    virtual void init(const TableIter& rIter);
    virtual void movement();

    void playPinchEventBgmLevel(bool);
    void setStateBit(u32);
    void stopPinchEventBgmSoon();
    void movementPinchEventBgm();

    // Set each frame by AudCameraWatcher (camera under water) and MarioSwim (a swim dash); cleared by movement.
    enum StateBits {
        StateBit_CameraInWater = 0b010,
        StateBit_SwimJet = 0b100,
    };

    u32 mStateBits;
    s32 mState;
    s32 mStartDelay;
    s32 mFadeInDelay;
    bool mStopImmediately;
};
