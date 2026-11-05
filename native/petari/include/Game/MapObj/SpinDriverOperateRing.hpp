#pragma once

#include "Game/LiveActor/LiveActor.hpp"

class SpinDriverOperateRing : public LiveActor {
public:
    SpinDriverOperateRing(const char*);

    virtual void init(const JMapInfoIter&);
    virtual void control();

    void setRadiusRate(f32);
    void reset();
    void update(const Vec3f&, const Vec3f&);
    void updatePosition();
    void updateControlPoint(const Vec3f&);
    void updateDirection(const Vec3f&);
    void resetVelocityAndTrans();
    void addAccelToOperatePlane(const Vec3f&);
    void addAccelOperate(Vec3f*, const Vec3f&);
    void addAccelToCenter();
    void attenuateVelocity();
    void calcOperatePowerByDPD(Vec3f*) const;

    void exeWait();

    Vec3f _8C;
    Vec3f _98;
    Vec3f _A4;
    Vec3f mAccelerate;
    Vec3f mDirection;
    Vec3f _C8;
    f32 mRadiusRate;
    f32 _D8;
    f32 _DC;
    s32 _E0;
};
