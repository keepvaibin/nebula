#pragma once

#include <JSystem/JGeometry/TVec.hpp>

class SpiderThreadPoint {
public:
    SpiderThreadPoint(const TVec3f&, f32);

    bool updateSpring();
    void updateDrawVec(const TVec3f*, const TVec3f*);
    void updateWind(f32);
    void updatePos();
    void restrict(const TVec3f*, f32);
    void updateHang(const TVec3f&);
    bool tryTouch(f32, const TVec3f&, s32);
    bool tryPush(const TVec3f&, f32);
    void startThreadLevelSound();

    inline void setPos(const TVec3f& rPos) {
        mPosition.set(rPos);
        mVelocity.zero();
    }

    inline f32 getPosZDiff(const TVec3f& rPos) const {
        return rPos.z - mPosition.z;
    }

    // TODO: this should be removed when the appropriate inline in JAudio is made/found
    inline s32 systemLevelParam(f32 a, f32 b) {
        return static_cast< s16 >((s32)(a)) << 16 | (static_cast< s16 >((s32)(b)) & 0xFFFF);
    }

    TVec3f mBasePos;
    TVec3f mPosition;
    TVec3f mVelocity;
    f32 mFriction;
    TVec3f mUp;
    TVec3f mFront;
    s32 mWindStartTime;
    s32 mWindTime;
    f32 mPrevStretchDist;
};
