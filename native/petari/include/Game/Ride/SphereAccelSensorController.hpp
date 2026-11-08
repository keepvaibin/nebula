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
    virtual void clacXY(f32*, f32*);

    void getPadAcceleration(Vec3f*) const;
    bool testBrake() const;

    bool isMainHand() const {
        return mPad == Pad_Core;
    }

    Pad mPad;  // determines whether wiimote or nunchuck is used for control
};
