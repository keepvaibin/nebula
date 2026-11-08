#pragma once

#include <nebula_geometry.hpp>
#include <NebulaGpu.h>
#include <nebula_math.h>

class Swinger {
public:
    Swinger(const Vec3f* pPivot, MtxPtr pBaseMtx, f32 length, f32 gravityAccel, f32 damping, const Vec3f* pGravity);

    void update();
    void accel(const Vec3f& rAccel);
    void updateSwingMtx(const Vec3f& rPivot);

    const Vec3f* mPivot;
    Vec3f mTipPos;
    Vec3f mAcceleration;
    Vec3f mSwingDir;
    f32 mLength;
    f32 mGravityAccel;
    f32 mDamping;
    const Vec3f* mGravity;
    Vec3f mSide;
    Vec3f mUp;
    Vec3f mFront;
    Pos3f mSwingMtx;
};
