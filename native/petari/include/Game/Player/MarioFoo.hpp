#pragma once

#include "Game/Player/MarioState.hpp"
#include <JSystem/JGeometry/TMatrix.hpp>

class MarioActor;
class AreaObj;
class JUTTexture;

class MarioFoo : public MarioState {
public:
    MarioFoo(MarioActor*);

    virtual f32 getStickY() const;
    virtual void init();
    virtual bool start();
    virtual bool close();
    virtual bool update();
    virtual bool notice();
    virtual void hitWall(const TVec3f&, HitSensor*);
    virtual bool passRing(const HitSensor*);
    virtual f32 getBlurOffset() const;
    virtual void draw3D() const;
    virtual const TVec3f& getGravityVec() const;

    void setStartSpeed(f32 speed) {
        _28 = speed;
        _AC = 20;
    }

    void jet();
    void updateTilt();
    void spin();
    f32 calcRingAcc();

    u8 _11;
    u8 _12;
    u32 _14;
    u16 _18;
    u16 _1A;
    u16 _1C;
    u16 _1E;
    f32 _20;
    f32 _24;
    f32 _28;
    f32 _2C;
    TVec3f _30;
    TVec3f _3C;
    AreaObj* _48;
    u16 _4C;
    u16 _4E;
    u16 _50;
    f32 _54;
    u8 _58;
    u8 _59;
    u16 _5A;
    f32 _5C;
    u16 _60;
    TVec3f _64;
    f32 _70;
    f32 _74;
    TMtx34f _78;
    u16 _A8;
    u16 _AA;
    u16 _AC;
    u8 _AE;
    TVec3f _B0[64];
    TVec3f _3B0[64];
    u32 _6B0;
    u32 _6B4;
    JUTTexture* _6B8;
};
