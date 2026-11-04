#pragma once

#include <JSystem/JGeometry/TVec.hpp>

class PlayerInput;

// How a hand's controller is tilted, in g, as the host keeps it.
class InputTilt {
public:
    InputTilt(const PlayerInput* pPad, u32 hand);

    bool getAcceleration(TVec3f* pOut) const;
    void update();

    const PlayerInput* mPad;
    u32 mHand;
    TVec3f mTilt;
    bool mValid;
};
