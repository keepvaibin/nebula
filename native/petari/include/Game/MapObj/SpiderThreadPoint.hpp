#pragma once

#include <nebula_geometry.hpp>

class SpiderThreadPoint {
public:
    SpiderThreadPoint(const Vec3f&, f32);

    bool updateSpring();
    void updateDrawVec(const Vec3f*, const Vec3f*);
    void updateWind(f32);
    void updatePos();
    void restrict(const Vec3f*, f32);
    void updateHang(const Vec3f&);
    bool tryTouch(f32, const Vec3f&, s32);
    bool tryPush(const Vec3f&, f32);
    void startThreadLevelSound();

    inline void setPos(const Vec3f& rPos) {
        mPosition.set(rPos);
        mVelocity.zero();
    }

    inline f32 getPosZDiff(const Vec3f& rPos) const {
        return rPos.z - mPosition.z;
    }

    // Two levels as one sound parameter: a's whole part in the high half, b's in the low.
    inline s32 systemLevelParam(f32 a, f32 b) {
        return static_cast< s16 >((s32)(a)) << 16 | (static_cast< s16 >((s32)(b)) & 0xFFFF);
    }

    Vec3f mBasePos;
    Vec3f mPosition;
    Vec3f mVelocity;
    f32 mFriction;
    Vec3f mUp;
    Vec3f mFront;
    s32 mWindStartTime;
    s32 mWindTime;
    f32 mPrevStretchDist;
};
