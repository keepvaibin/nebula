#pragma once

#include "Game/Ride/SpherePadController.hpp"

class SphereAccelSensorController : public SpherePadController {
public:
    enum Pad {
        Pad_Core = 0,
        Pad_Sub = 1,
    };

    SphereAccelSensorController();

    virtual f32 calcJumpPower() const;
    virtual bool doBrake() const;
    virtual void update(const Vec3f&);
    virtual void calcXY(f32* pX, f32* pY);

    void getPadAcceleration(Vec3f* pAccel) const;
    bool testBrake() const;

    bool isMainHand() const {
        return mPad == Pad_Core;
    }

    // Which hand's tilt steers: the main controller or the sub one.
    Pad mPad;
};
