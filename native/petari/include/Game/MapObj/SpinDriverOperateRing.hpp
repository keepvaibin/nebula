#pragma once

#include "Game/LiveActor/LiveActor.hpp"

class SpinDriverOperateRing : public LiveActor {
public:
    SpinDriverOperateRing(const char*);

    virtual void init(const TableIter&);
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
    void calcOperatePowerByPointer(Vec3f*) const;

    void exeWait();

    Vec3f mCenter;
    Vec3f mOffset;
    Vec3f mAccelerate;
    Vec3f mDirection;
    Vec3f mPointerPos;
    f32 mRadiusRate;
    f32 mOffsetLength;
    f32 mOffsetRate;
    s32 mOperateState;
};
