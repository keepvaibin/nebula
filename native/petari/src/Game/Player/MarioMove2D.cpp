#include "Game/LiveActor/Nerve.hpp"
#include "Game/Player/Mario.hpp"
#include "Game/Player/MarioActor.hpp"
#include "Game/Util/AreaObjUtil.hpp"
#include "Game/Util/MathUtil.hpp"
#include "Game/Util/MtxUtil.hpp"
#include <nebula_math.h>
#include "Game/Util/Tunable.hpp"

NEBULA_TUNABLE_NAMED(f32, cNum_calcMoveDir2D_f1, "calcMoveDir2D.f1", 0.1f);
NEBULA_TUNABLE_NAMED(f32, cNum_stick2Dadjust_f1, "stick2Dadjust.f1", 3.0415928f);
NEBULA_TUNABLE_NAMED(f32, cNum_stick2Dadjust_f2, "stick2Dadjust.f2", 3.0415928f);
NEBULA_TUNABLE_NAMED(f32, cNum_stick2Dadjust_f3, "stick2Dadjust.f3", 0.3f);
NEBULA_TUNABLE_NAMED(f32, cNum_stick2Dadjust_f4, "stick2Dadjust.f4", 0.5f);
NEBULA_TUNABLE_NAMED(f32, cNum_stick2Dadjust_f5, "stick2Dadjust.f5", 1.5f);
NEBULA_TUNABLE_NAMED(f32, cNum_stick2Dadjust_f6, "stick2Dadjust.f6", 3.1415927f);
NEBULA_TUNABLE_NAMED(f32, cNum_stick2Dadjust_f7, "stick2Dadjust.f7", 0.5235988f);
NEBULA_TUNABLE_NAMED(f32, cNum_stick2Dadjust_f8, "stick2Dadjust.f8", 2.617994f);
NEBULA_TUNABLE_NAMED(f32, cNum_stick2Dadjust_f9, "stick2Dadjust.f9", 0.3f);
NEBULA_TUNABLE_NAMED(f32, cNum_stick2Dadjust_f10, "stick2Dadjust.f10", 1.3962635f);
NEBULA_TUNABLE_NAMED(f32, cNum_stick2Dadjust_f11, "stick2Dadjust.f11", 1.7453294f);
NEBULA_TUNABLE_NAMED(f32, cNum_stick2Dadjust_f12, "stick2Dadjust.f12", 0.3f);
NEBULA_TUNABLE_NAMED(f32, cNum_stick2Dadjust_f13, "stick2Dadjust.f13", 0.19634955f);
NEBULA_TUNABLE_NAMED(f32, cNum_stick2Dadjust_f14, "stick2Dadjust.f14", 1.5707964f);
NEBULA_TUNABLE_NAMED(f32, cNum_stick2Dadjust_f15, "stick2Dadjust.f15", 1.5707964f);
NEBULA_TUNABLE_NAMED(f32, cNum_stick2Dadjust_f16, "stick2Dadjust.f16", 2.9452431f);
NEBULA_TUNABLE_NAMED(f32, cNum_stick2Dadjust_f17, "stick2Dadjust.f17", 1.5707964f);
NEBULA_TUNABLE_NAMED(f32, cNum_stick2Dadjust_f18, "stick2Dadjust.f18", 0.0001f);

void Mario::check2DMode() {
    mMovementStates.mode2D = false;
    mActionStates.circular2D = false;

    AreaObj* area = MR::getAreaObj("PlaneModeCube", mPosition);
    if (area != nullptr) {
        mMovementStates.mode2D = true;

        Vec3f rotate;
        MR::calcCubeRotate(area, &rotate);
        MR::makeMtxTR(m2DRotateMtx, 0.0f, 0.0f, 0.0f, rotate.x, rotate.y, rotate.z);

        if (!(mPrevMovementStates.mode2D)) {
            m2DPlanePos = mPosition;
            MR::getRotatedAxisZ(&m2DPlaneNormal, rotate);
            MR::calcCubePos(area, &m2DCubePos);
        }
    } else {
        area = MR::getAreaObj("PlaneCircularModeCube", mPosition);
        if (area != nullptr) {
            Vec3f rotate;
            MR::calcCubeRotate(area, &rotate);
            NebulaMtxCopy(MR::tmpMtxRotYDeg(rotate.y), m2DRotateMtx);
            mActionStates.circular2D = true;

            if (!mPrevActionStates.circular2D) {
                m2DPlanePos = mPosition;
                MR::getRotatedAxisZ(&m2DPlaneNormal, rotate);
                MR::calcCubePos(area, &m2DCubePos);
            }
        }
    }

    mMovementStates.mode25D = false;
    area = MR::getAreaObj("PipeModeCube", mPosition);
    if (area != nullptr) {
        set25Dmode(area);
    }
}

void Mario::calcMoveDir2D(f32 stickX, f32 stickY, Vec3f* pOut) {
    if (stickX != 0.0f) {
        stickY = 0.0f;
    }

    Vec3f up;
    Vec3f normal;
    Vec3f alongGround;
    Vec3f down;

    up = mUp2D;
    normal = mNormal2D;

    Vec3f groundNorm(m2DGroundNorm);
    alongGround.cross(normal, groundNorm);
    MR::normalize(&alongGround);

    if (groundNorm.dot(up) < 0.0f) {
        alongGround = -alongGround;
    }

    down = -up;
    Vec3f prevMoveUp(mMoveUpVec);

    if (mMoveUpVec.dot(groundNorm) <= 0.0f) {
        mMoveUpVec = groundNorm;
    } else {
        MR::vecBlendSphere(mMoveUpVec, groundNorm, &mMoveUpVec, cNum_calcMoveDir2D_f1);
    }

    if (MR::isNearZero(mMoveUpVec)) {
        mMoveUpVec = prevMoveUp;
    } else {
        MR::normalize(&mMoveUpVec);
    }

    *pOut = alongGround * stickX - down * stickY;
}

void Mario::calcShadowDir2D(const Vec3f& rVec, Vec3f* pOut) {
    *pOut = rVec;
    MR::normalizeOrZero(pOut);
}

void Mario::stick2Dadjust(f32& rStickX, f32& rStickY) {
    bool isNearHalfPi = false;

    if (mActionStates.gravityTurned && mMovementStates.onGround && !mMovementStates.jumping) {
        mActionStates.gravityTurned = false;
        clear2DStick();
    }

    f32 stickXInput = rStickX;
    f32 stickYInput = rStickY;

    mSide2D.set(1.0f, 0.0f, 0.0f);
    mUp2D.set(0.0f, 1.0f, 0.0f);
    mNormal2D.set(0.0f, 0.0f, -1.0f);
    NebulaMtxMultVecSR(m2DRotateMtx, &mSide2D, &mSide2D);
    NebulaMtxMultVecSR(m2DRotateMtx, &mNormal2D, &mNormal2D);

    Vec3f worldY(0.0f, 1.0f, 0.0f);
    Vec3f* camDirZ = &getCamDirZ();
    f32 angle = MR::diffAngleSignedHorizontal(getCamDirY(), worldY, *camDirZ);

    Vec3f worldX(1.0f, 0.0f, 0.0f);
    camDirZ = &getCamDirZ();
    const f32 absHorizontal = MR::diffAngleAbsHorizontal(getCamDirX(), worldX, *camDirZ);
    if (absHorizontal >= cNum_stick2Dadjust_f1 && MR::abs(angle) >= cNum_stick2Dadjust_f2) {
        angle = 0.0f;
        rStickX = -rStickX;
    }

    Mtx rotMtx;
    NebulaMtxRotAxisRad(rotMtx, &m2DPlaneNormal, angle);
    NebulaMtxMultVecSR(rotMtx, &mSide2D, &mSide2D);
    NebulaMtxMultVecSR(rotMtx, &mUp2D, &mUp2D);

    MR::vecKillElement(mGroundNorm, m2DPlaneNormal, &m2DGroundNorm);
    if (MR::normalizeOrZero(&m2DGroundNorm)) {
        m2DGroundNorm = mGroundNorm;
    }

    set2Dmode(m2DGroundNorm.dot(mUp2D) >= 0.0f);

    bool isMainY = false;
    bool isMainX = false;

    if (m2DStickHold) {
        if (MR::isNearZero(mStickPos.z)) {
            m2DStickHold = false;
        } else {
            Vec3f stickVec(rStickX, rStickY, 0.0f);
            const f32 diff = MR::diffAngleAbs(stickVec, m2DPrevStick);

            if (m2DPrevStick.x * rStickX < 0.0f) {
                m2DStickHold = false;
            } else if (m2DPrevStick.y * rStickY < 0.0f) {
                m2DStickHold = false;
            } else if (diff < cNum_stick2Dadjust_f3) {
                rStickX = m2DHeldStick.x;
                rStickY = m2DHeldStick.y;
            } else {
                m2DStickHold = false;
            }
        }
    }

    if (!m2DStickHold && !MR::isNearZero(mStickPos.z) && !m2DStickLockMode && !m2DStickLocked) {
        Vec3f stickVec(rStickX, rStickY, 0.0f);
        NebulaMtxMultVecSR(m2DRotateMtx, &stickVec, &stickVec);
        NebulaMtxMultVecSR(rotMtx, &stickVec, &stickVec);

        Vec3f invMove(-m2DGroundNorm);
        const f32 diff = MR::diffAngleAbs(invMove, stickVec);
        f32 clamp = MR::clamp(1.0f - (mStickPos.z * mStickPos.z), 0.0f, 1.0f);

        const f32 threshold = MR::getInterpolateValue(clamp, cNum_stick2Dadjust_f4, cNum_stick2Dadjust_f5);
        if (diff < threshold) {
            rStickX = 0.0f;
            rStickY = 0.0f;
            mStickPos.z = 0.0f;
        } else if (diff > (cNum_stick2Dadjust_f6 - threshold)) {
            rStickX = 0.0f;
            rStickY = 0.0f;
            mStickPos.z = 0.0f;
        } else {
            m2DPrevStick = stickVec;
            MR::vecKillElement(stickVec, m2DGroundNorm, &stickVec);
            rStickX = stickVec.x;
            rStickY = stickVec.y;
            m2DHeldStick = stickVec;
            m2DStickHold = true;
        }
    }

    if (MR::abs(rStickY) > MR::abs(rStickX)) {
        isMainY = true;
        rStickX = 0.0f;
        if (rStickY > 0.0f) {
            rStickY = mStickPos.z;
        } else {
            rStickY = -mStickPos.z;
        }
    } else {
        isMainX = true;
        rStickY = 0.0f;
        if (rStickX > 0.0f) {
            rStickX = mStickPos.z;
        } else {
            rStickX = -mStickPos.z;
        }
    }

    const f32 angleMove = MR::diffAngleAbs(m2DGroundNorm, mUp2D);
    u8 lockX = 0;
    u8 lockY = 0;

    if (!((m2DStickLockMode != 0 || m2DStickLocked != 0) && m2DLockX == 0 && m2DLockY == 0)) {

        if ((angleMove < cNum_stick2Dadjust_f7 || angleMove > cNum_stick2Dadjust_f8 || m2DLockX != 0) && m2DLockY == 0) {
            if (m2DLockStickY == 0.0f) {
                rStickY = 0.0f;
                if (isMainY && MR::abs(stickXInput) > cNum_stick2Dadjust_f9) {
                    rStickX = mStickPos.z;
                    if (stickXInput < 0.0f) {
                        rStickX = -rStickX;
                    }
                    lockX = 1;
                }
            }

        } else {
            if (((angleMove > cNum_stick2Dadjust_f10 && angleMove < cNum_stick2Dadjust_f11) || m2DLockY != 0) && m2DLockX == 0 &&
                m2DLockStickX == 0.0f) {
                rStickX = 0.0f;
                if (isMainX && MR::abs(stickYInput) > cNum_stick2Dadjust_f12) {
                    rStickY = mStickPos.z;
                    if (stickYInput < 0.0f) {
                        rStickY = -rStickY;
                    }
                    lockY = 1;
                }
            }
        }
    }

    m2DLockStickX = rStickX;
    m2DLockStickY = rStickY;
    m2DLockX = lockX;
    m2DLockY = lockY;

    if (!m2DStickLockMode && !m2DStickLocked && rStickY != 0.0f) {
        if (MR::isInRange(angleMove, cNum_stick2Dadjust_f13, cNum_stick2Dadjust_f14)) {
            if (m2DGroundNorm.dot(mSide2D) < 0.0f) {
                m2DStickLocked = 1;
            } else {
                m2DStickLocked = 2;
            }
        } else if (MR::isInRange(angleMove, cNum_stick2Dadjust_f15, cNum_stick2Dadjust_f16)) {
            if (m2DGroundNorm.dot(mSide2D) < 0.0f) {
                m2DStickLocked = 2;
            } else {
                m2DStickLocked = 1;
            }
        }
    }

    switch (m2DStickLocked) {
    case 1:
        if (rStickY == 0.0f) {
            m2DStickLocked = 0;
        } else {
            rStickX = rStickY;
        }
        break;
    case 2:
        if (rStickY == 0.0f) {
            m2DStickLocked = 0;
        } else {
            rStickX = -rStickY;
            rStickY = -rStickY;
        }
        break;
    default:
        m2DStickLocked = 0;
        break;
    }

    switch (m2DStickLockMode) {
    case 1:
        if (rStickX > 0.0f) {
            rStickX = -rStickX;
        } else {
            m2DStickLockMode = 0;
        }
        break;
    case 2:
        if (rStickX < 0.0f) {
            rStickX = -rStickX;
        } else {
            m2DStickLockMode = 0;
        }
        break;
    default:
        m2DStickLockMode = 0;
        break;
    }

    m2DStickX = rStickX;

    if (MR::isNearZero(angleMove - cNum_stick2Dadjust_f17, cNum_stick2Dadjust_f18)) {
        isNearHalfPi = true;
    }

    if (!isNearHalfPi) {
        rStickY = 0.0f;
        mStickPos.z = MR::abs(rStickX);
    } else {
        rStickX = 0.0f;
        mStickPos.z = MR::abs(rStickY);
    }
}

void Mario::set2Dmode(bool mode) {
    if (mode == m2DUpside) {
        return;
    }

    if (m2DStickX > 0.0f) {
        m2DStickLockMode = 1;
    }

    if (m2DStickX < 0.0f) {
        m2DStickLockMode = 2;
    }

    m2DUpside = mode;
}

void Mario::beforeJumping2D() {
}

void Mario::afterLanding2D() {
}

void Mario::clear2DStick() {
    m2DStickLockMode = 0;
    m2DStickLocked = 0;
    m2DLockX = 0;
    m2DLockY = 0;
    m2DStickX = 0.0f;
    m2DLockStickY = 0.0f;
    m2DLockStickX = 0.0f;
    m2DUpside = false;
    m2DStickHold = false;
    m2DPrevStick.zero();
    m2DHeldStick.zero();
}
