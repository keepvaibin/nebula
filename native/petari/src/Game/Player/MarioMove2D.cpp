#include "Game/LiveActor/Nerve.hpp"
#include "Game/Player/Mario.hpp"
#include "Game/Player/MarioActor.hpp"
#include "Game/Util/AreaObjUtil.hpp"
#include "Game/Util/MathUtil.hpp"
#include "Game/Util/MtxUtil.hpp"
#include <nebula_math.h>

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
        MR::vecBlendSphere(mMoveUpVec, groundNorm, &mMoveUpVec, 0.1f);
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
    if (absHorizontal >= 3.0415928f && MR::abs(angle) >= 3.0415928f) {
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
            } else if (diff < 0.3f) {
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

        const f32 threshold = MR::getInterpolateValue(clamp, 0.5f, 1.5f);
        if (diff < threshold) {
            rStickX = 0.0f;
            rStickY = 0.0f;
            mStickPos.z = 0.0f;
        } else if (diff > (3.1415927f - threshold)) {
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

        if ((angleMove < 0.5235988f || angleMove > 2.617994f || m2DLockX != 0) && m2DLockY == 0) {
            if (m2DLockStickY == 0.0f) {
                rStickY = 0.0f;
                if (isMainY && MR::abs(stickXInput) > 0.3f) {
                    rStickX = mStickPos.z;
                    if (stickXInput < 0.0f) {
                        rStickX = -rStickX;
                    }
                    lockX = 1;
                }
            }

        } else {
            if (((angleMove > 1.3962635f && angleMove < 1.7453294f) || m2DLockY != 0) && m2DLockX == 0 &&
                m2DLockStickX == 0.0f) {
                rStickX = 0.0f;
                if (isMainX && MR::abs(stickYInput) > 0.3f) {
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
        if (MR::isInRange(angleMove, 0.19634955f, 1.5707964f)) {
            if (m2DGroundNorm.dot(mSide2D) < 0.0f) {
                m2DStickLocked = 1;
            } else {
                m2DStickLocked = 2;
            }
        } else if (MR::isInRange(angleMove, 1.5707964f, 2.9452431f)) {
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

    if (MR::isNearZero(angleMove - 1.5707964f, 0.0001f)) {
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
