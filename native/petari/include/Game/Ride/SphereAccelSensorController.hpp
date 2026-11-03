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
    virtual void update(const TVec3f&);
    virtual void clacXY(f32*, f32*);

    void getPadAcceleration(TVec3f*) const;
    bool testBrake() const;

    bool isMainHand() const {
        return mPad == Pad_Core;
    }

    // everything up to 0x58 seems like it may be inhereted memory
    // however none of the inhereted classes use anything near
    // this much memory. Unsure of what exactly this memory is...
    u32 _08, _0C;
    u32 _10, _14, _18, _1C;
    u32 _20, _24, _28, _2C;
    u32 _30, _34, _38, _3C;
    u32 _40, _44, _48, _4C;
    u32 _50, _54;

    u32 _58;
    u32 _5C;

    u32 _60;
    u32 _64;
    u32 _68;
    u32 _6C;
    u32 _70;

    u32 _74;
    u32 _78;

    f32 _7C;
    f32 _80;
    f32 _84;
    f32 _88;

    u32 _8C;
    u32 _90;

    f32 _94;
    f32 _98;
    f32 _9C;
    f32 _A0;
    f32 _A4;
    f32 _A8;
    f32 _AC;

    u16 _B0;
    f32 _B4;
    Pad mPad;  // determines whether wiimote or nunchuck is used for control
};
