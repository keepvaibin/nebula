#pragma once

#include "Game/LiveActor/LiveActor.hpp"

// The pointer-steered offset of a super spin driver's flight: a spring in the plane across the shot direction.
class SpinDriverOperateRing : public LiveActor {
public:
    SpinDriverOperateRing(const char*);

    virtual void init(const TableIter&) override;

    // 0 turns steering off; 1 is the full ring radius.
    void setRadiusRate(f32);
    void reset();
    void update(const Vec3f& rCenter, const Vec3f& rShootDir);
    void updatePosition();
    void updateControlPoint(const Vec3f& rShootDir);
    void updateDirection(const Vec3f&);
    void resetVelocityAndTrans();
    void addAccelToOperatePlane(const Vec3f& rShootDir);
    void addAccelOperate(Vec3f* pAccel);
    void addAccelToCenter();
    void attenuateVelocity();
    void calcOperatePowerByPointer(Vec3f*) const;

    Vec3f mCenter;
    Vec3f mOffset;
    Vec3f mAccelerate;
    // Smoothed steering direction (SuperSpinDriver bends the shot by it).
    Vec3f mDirection;
    Vec3f mPointerPos;
    f32 mRadiusRate;
    f32 mOffsetLength;
    f32 mOffsetRate;
    s32 mOperateState;
};
