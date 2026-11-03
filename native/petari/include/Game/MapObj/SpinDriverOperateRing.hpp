#pragma once

#include "Game/LiveActor/LiveActor.hpp"

class SpinDriverOperateRing : public LiveActor {
public:
    SpinDriverOperateRing(const char*);

    virtual void init(const JMapInfoIter&);
    virtual void control();

    void setRadiusRate(f32);
    void reset();
    void update(const TVec3f&, const TVec3f&);
    void updatePosition();
    void updateControlPoint(const TVec3f&);
    void updateDirection(const TVec3f&);
    void resetVelocityAndTrans();
    void addAccelToOperatePlane(const TVec3f&);
    void addAccelOperate(TVec3f*, const TVec3f&);
    void addAccelToCenter();
    void attenuateVelocity();
    void calcOperatePowerByDPD(TVec3f*) const;

    void exeWait();

    TVec3f _8C;
    TVec3f _98;
    TVec3f _A4;
    TVec3f mAccelerate;
    TVec3f mDirection;
    TVec3f _C8;
    f32 mRadiusRate;
    f32 _D8;
    f32 _DC;
    s32 _E0;
};
