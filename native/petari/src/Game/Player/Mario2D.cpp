
#include "Game/Player/Mario.hpp"
#include "Game/Player/MarioActor.hpp"
#include "Game/Util/MathUtil.hpp"
#include "Game/Util/MtxUtil.hpp"
#include "math_types.hpp"

void Mario::stick2DadjustGround(f32& rX, f32& rY) {
    if (isStageCameraRotate2D()) {
        return;
    }

    if (mActionStates.gravityTurned && mMovementStates.onGround && !mMovementStates.jumping) {
        mActionStates.gravityTurned = false;
        clear2DStick();
        m2DStickHold = false;
    }

    mSide2D.set(1.0f, 0.0f, 0.0f);
    mUp2D.set(0.0f, 1.0f, 0.0f);
    mNormal2D.set(0.0f, 0.0f, -1.0f);

    NebulaMtxMultVecSR(m2DRotateMtx, mSide2D, mSide2D);
    NebulaMtxMultVecSR(m2DRotateMtx, mNormal2D, mNormal2D);
    NebulaMtxMultVecSR(m2DRotateMtx, mUp2D, mUp2D);

    m2DPlaneNormal = mNormal2D;

    MR::vecKillElement(mGroundNorm, m2DPlaneNormal, &m2DGroundNorm);

    if (MR::normalizeOrZero(&m2DGroundNorm)) {
        m2DGroundNorm = mGroundNorm;
    }

    f32 dot = m2DGroundNorm.dot(mUp2D);

    if (!MR::isInRange(dot, -0.707f, 0.707f)) {
        set2Dmode(dot >= 0.0f);
    }

    Vec3f stickDir2D(rX, rY, 0.0f);
    MR::normalizeOrZero(&stickDir2D);

    if (m2DStickHold) {
        if (MR::isNearZero(mStickPos.z)) {
            m2DStickHold = false;
            return;
        }

        if (MR::diffAngleAbs(stickDir2D, m2DPrevStick) > 0.3f) {
            m2DStickHold = false;
            return;
        }

        NebulaMtxMultVecSR(MR::tmpMtxRotZRad(-MR::diffAngleSignedHorizontal(m2DHeldStick, m2DGroundNorm, m2DPlaneNormal)), stickDir2D, stickDir2D);
        rX = stickDir2D.x;
        rY = stickDir2D.y;
        return;
    }

    if (MR::isNearZero(mStickPos.z)) {
        return;
    }

    Vec3f vec2;
    vec2.x = m2DGroundNorm.dot(getCamDirX());
    vec2.y = m2DGroundNorm.dot(getCamDirY());
    vec2.z = 0.0f;

    f32 diff = MR::diffAngleAbs(stickDir2D, vec2);

    // explicit conversions to radian instead of calling MR::toRadian()
    if (diff < 30.0f * MR::pi() / 180.0f || diff > 150.0f * MR::pi() / 180.0f) {
        rX = 0.0f;
        rY = 0.0f;
        mStickPos.z = 0.0f;
        return;
    }

    m2DPrevStick = stickDir2D;
    m2DHeldStick = m2DGroundNorm;
    m2DStickHold = true;
}

void Mario::calcDir2D(f32 x, f32 y, Vec3f* pOut) {
    Vec3f xDir;
    Vec3f yDir;
    MR::vecKillElement(getCamDirX(), m2DPlaneNormal, &xDir);
    MR::vecKillElement(getCamDirY(), m2DPlaneNormal, &yDir);

    MR::normalizeOrZero(&xDir);
    MR::normalizeOrZero(&yDir);

    pOut->set(xDir * x);
    pOut->add(yDir * y);
}
