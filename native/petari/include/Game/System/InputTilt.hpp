#pragma once

#include <nebula_geometry.hpp>

class PlayerInput;

// How a hand's controller is tilted, in g, as the host keeps it.
class InputTilt {
public:
    InputTilt(const PlayerInput* pPad, u32 hand);

    bool getAcceleration(Vec3f* pOut) const;
    void update();

    const PlayerInput* mPad;
    u32 mHand;
    Vec3f mTilt;
    bool mValid;
};
