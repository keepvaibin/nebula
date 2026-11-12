#include "Game/LiveActor/HitSensor.hpp"
#include "Game/Map/HitInfo.hpp"
#include "Game/Player/MarioActor.hpp"
#include "Game/Player/MarioConst.hpp"
#include "Game/Player/MarioShadow.hpp"
#include "Game/Util/AreaObjUtil.hpp"
#include "Game/Util/GravityUtil.hpp"
#include "Game/Util/MapUtil.hpp"
#include "Game/Util/MathUtil.hpp"
#include <nebula_geometry.hpp>

const Vec3f& MarioActor::getGravityVec() const {
    return *mMario->getGravityVec();
}

const Vec3f& MarioActor::getGravityVector() const {
    return *mMario->getGravityVec();
}

const Vec3f& MarioActor::getAirGravityVec() const {
    return mMario->getAirGravityVec();
}

void MarioActor::getGravityVector(Vec3f* pVec) const {
    pVec->set(mMario->getAirGravityVec());
}

f32 MarioActor::getGravityRatio() const {
    return mGravityRatio;
}

GravityInfo* MarioActor::getGravityInfo() const {
    return mGravityInfo;
}

u8 MarioActor::getGravityLevel() const {
    return MR::isLightGravity(*mGravityInfo);
}

bool MarioActor::checkBeeWallStick(Vec3f& rVec) {
    if (getMovementStates().frontWall && mMario->checkWallCode(CollisionWallCode_Fur, false) && mBeeWallWalk == 0 && !mBeeStickInhibit) {
        Vec3f head = mMario->mHeadVec;

        mBeeWallWalk = 5;

        rVec = -mMario->getWallNorm();

        mPosition = mMario->getWallPos();

        mMario->stopJump();
        mMario->stopAnimation(nullptr);
        mMario->stopWalk();

        mGravityVec = rVec;
        mMario->setGravityVec(rVec);
        mMario->setHeadVec(-rVec);
        mMario->setFrontVecKeepUp(head, 1u);
        setBlendMtxTimer(2);

        mStunTimer = 5;
        mMario->mMovementStates.jumpBuffered = false;
        mCollisionShadow->mSnapDirection = true;

        return true;
    }

    return false;
}

bool MarioActor::checkBeeFloorStick(Vec3f& rVec) {
    if (mMario->getMovementStates().onGround && MR::getWallCodeIndex(mMario->getGroundPolygon()) == CollisionWallCode_Fur && mBeeWallWalk == 0) {
        mBeeWallWalk = 5;
        rVec = -mMario->mGroundNorm;

        return true;
    }

    return false;
}

void MarioActor::syncJumpBeeStickMode() {
    if (mBeeWallWalk != 0 && selectQuickResetBeeWallGravity(mMario->mShadowPolygon->mSensor->mHost->mName)) {
        mBeeWallWalk = 0;
        mBeeStickInhibit = 30;
    }

    if (!(MR::diffAngleAbs(mBeePoseDir, getGravityVec()) < MR::pi() / 36.0f)) {
        return;
    }

    mBeePoseHeadPos = mPosition - mMario->mFrontVec * 2.0f;
    mBeePoseVelocity = mPosition - mBeePoseHeadPos;
    mBeePoseDir = mCenterPos - mBeePoseHeadPos;
    MR::normalize(&mBeePoseDir);
}

void MarioActor::updateBeeModeGravity(Vec3f& rVec) {
    u8 alpha = mBeeWallWalk;

    if (mBeeStickInhibit != 0) {
        mBeeStickInhibit--;
    }

    if (mBeeStickInhibit != 0 || (!checkBeeCeilStick(rVec) && !checkBeeWallStick(rVec) && !checkBeeFloorStick(rVec))) {
        updateBeeStickMode(rVec);
    }

    if (mBeeWallWalk == 5) {
        MR::vecBlendSphere(mGravityVec, rVec, &rVec, 0.1f);
        return;
    }

    if (mBeeWallWalk != 0) {
        MR::vecBlendSphere(mGravityVec, rVec, &rVec, 0.3f);
        return;
    }

    if (alpha != 0) {
        mBeeStickInhibit = 60;
    }
}

bool MarioActor::isInZeroGravitySpot() const {
    return MR::isNearZero(mRawGravityVec);
}

void MarioActor::updateGravityVec(bool reset, bool usePosition) {
    Mtx rotation;
    Vec3f gravity;
    Vec3f positionGravity;
    Vec3f frontGravity;
    Vec3f specialGravity;
    bool resetGround = false;
    MR::calcGravityVectorOrZero(this, mPosition, &positionGravity, mGravityInfo, 0);
    if (!usePosition) {
        MR::calcGravityVectorOrZero(this, mCenterPos, &gravity, mGravityInfo, 0);
    } else {
        gravity = positionGravity;
    }

    MR::calcGravityVectorOrZero(this, mPosition + mMario->mFrontVec.multInLine(100.0f), &frontGravity, nullptr, 0);

    if (MR::isNearZero(positionGravity - frontGravity)) {
        mGravitySameAhead = true;
    } else {
        mGravitySameAhead = false;
    }

    if (mMario->isUseFooSpecialGravity(mPosition, &specialGravity)) {
        gravity = specialGravity;
        positionGravity = specialGravity;
        mGravitySameAhead = false;
    }

    MR::normalizeOrZero(&gravity);
    MR::normalizeOrZero(&positionGravity);
    if (gravity.dot(positionGravity) < 0.9f) {
        f32 dot = mMario->getAirGravityVec().dot(gravity);
        if (mMario->getAirGravityVec().dot(positionGravity) > dot) {
            gravity = positionGravity;
        }
    }

    f32 angle = MR::diffAngleAbs(mRawGravityVec, gravity);
    if (MR::isNearZero(mRawGravityVec)) {
        angle = 0.0f;
    } else if (MR::isNearZero(gravity)) {
        angle = 0.0f;
    }

    if (!reset) {
        if (mMario->isPlayerModeBee()) {
            if (angle >= MR::pi() / 6.0f) {
                setBlendMtxTimer(8);
            }
        }

        if (mGravityJumpTimer) {
            gravity = mRawGravityVec;
        } else if (angle >= MR::pi() * 0.4f) {
            if (!isJumping()) {
                mMario->mActionStates.steepGravityStop = true;
            } else {
                if (mMario->mJumpKind == 11) {
                    mMario->mJumpKind = 0;
                }

                changeAnimation("ショートジャンプ");
                resetGround = true;
            }

            if (mCameraCut) {
                resetGround = true;
            }

            mGravityJumpTimer = 15;
            mPrevHeadVec = mMario->mHeadVec;
            if (!MR::isNearZero(gravity)) {
                if (angle >= MR::pi() * (2.0f / 3.0f) || !mMario->mMovementStates.mode2D) {
                    mMario->cutGravityElementFromJumpVec(false);
                    mMario->mJumpVec.mult(0.5f);
                } else {
                    const Vec3f& previousGravity = *mMario->getGravityVec();
                    f32 speed = MR::vecKillElement(mMario->mJumpVec, previousGravity, &mMario->mJumpVec);
                    mMario->mJumpVec.mult(0.75f);
                    mMario->mJumpVec += *mMario->getGravityVec() * speed * 0.75f;
                }
            }

            mMario->clear2DStick();
            mMario->mActionStates.gravityTurned = true;
            mGravityBlendTimer = 15;
            if (!getMovementStates().mode2D) {
                mMario->mJumpFrontVec = -mMario->mJumpFrontVec;
            }
        }

        if (MR::isNearZero(gravity)) {
            mMario->mActionStates.gravityTurned = true;
        }
    } else {
        mGravityJumpTimer = 0;
    }

    if (!MR::isNearZero(gravity) && !MR::isNearZero(mRawGravityVec)) {
        Vec3f axis;
        Vec3f side;
        f32 cosine;
        if (MR::makeAxisAndCosignVecToVec(&axis, &cosine, mRawGravityVec, gravity)) {
            NebulaMtxRotAxisRad(rotation, &axis, Angle::acosRadian(cosine));
            NebulaMtxMultVecSR(rotation, &mBaseFrontVec, &mBaseFrontVec);
            side.cross(mBaseFrontVec, gravity);
            if (!MR::normalizeOrZero(&side)) {
                mBaseFrontVec.cross(gravity, side);
            }

            MR::normalize(&mBaseFrontVec);
        }
    } else if (!MR::isNearZero(gravity) && MR::isNearZero(mRawGravityVec)) {
        calcBaseFrontVec(-gravity);
    } else if (MR::isNearZero(gravity) && MR::isNearZero(mRawGravityVec)) {
        calcBaseFrontVec(mMario->mPostureUpVec);
    }

    mRawGravityVec = gravity;
    if (!MR::isNearZero(gravity)) {
        MR::normalize(&gravity);
    } else {
        if (!mMario->mMovementStates.onGround) {
            gravity = mGravityVec;
        } else {
            gravity = -mMario->mGroundNorm;
        }

    }

    if (mPlayerMode == PlayerMode_Bee) {
        updateBeeModeGravity(gravity);
    } else {
        mBeeWallWalk = 0;
        mBeeStickInhibit = 0;
    }

    mMario->setGravityVec(gravity);
    mGravityVec = gravity;
    if (resetGround) {
        mMario->setGroundNorm(-gravity);
    }

    calcCenterPos();
    f32 height = 70.0f;
    Vec3f up;
    if (getDrawStates().wallGravity) {
        MR::vecBlendSphere(mMario->mHeadVec, mMario->mWallNorm, &up, 0.5f * mBindRatio);
    } else {
        MR::vecBlendSphere(mMario->mHeadVec, mMario->mGroundNorm, &up, mBindRatio);
    }

    Vec3f oldOffset(mBinderOffset);
    if (mMario->mMovementStates.squat) {
        height = 50.0f;
    }

    if (mMario->mMovementStates.onGround) {
        mBinderOffset = up * height;
    } else if (mMario->mActionStates.jumpTakeoff || mInRush) {
        Vec3f offset = -mGravityVec * 70.0f;
        if (mGravityJumpTimer || mMario->calcPolygonAngleD(mMario->mGroundPolygon) >= 59.0f) {
            MR::vecBlend(mBinderOffset, offset, &mBinderOffset, 0.1f);
        } else {
            MR::vecBlend(mBinderOffset, offset, &mBinderOffset, 0.5f);
        }
    } else {
        Vec3f offset = -mGravityVec * 70.0f;
        MR::vecBlend(mBinderOffset, offset, &mBinderOffset, 0.1f);
    }

    if (mGravityBlendTimer) {
        mGravityBlendTimer--;
        f32 rate = static_cast< f32 >(mGravityBlendTimer) / 15.0f;
        mBinderOffset = mBinderOffset * (1.0f - rate) + oldOffset * rate;
        mMario->mActionStates.gravityTurned = true;
    }

    if (mGravityJumpTimer) {
        if (mMario->mVerticalSpeed > 160.0f) {
            Vec3f head = mMario->mHeadVec * 80.0f;
            Vec3f oldHead = mPrevHeadVec * 80.0f;
            Vec3f displacement = oldHead - head;
            f32 distance = MR::vecKillElement(displacement, mGravityVec, &displacement);
            mMario->push2(mGravityVec * distance);
            mPrevHeadVec = mMario->mHeadVec;
        }

        mGravityJumpTimer--;
    }
}

bool MarioActor::checkBeeCeilStick(Vec3f& rVec) {
    if ((mMario->isCeiling() || getDrawStates().headHit) && mBeeWallWalk == 0 && mBeeStickInhibit == 0) {
        bool out = false;
        if (getDrawStates().headHit) {
            out = true;
        } else {
            if (MR::getWallCodeIndex(mMario->mCeilPolygon) == CollisionWallCode_Fur) {
                out = true;
            }
        }

        if (out) {
            Triangle triangle = Triangle();
            Vec3f vec;
            if (MR::getFirstPolyOnLineToMap(&vec, &triangle, mPosition, (-mGravityVec * 200.0f))) {
                entryWallWalkMode(vec, *(triangle.getNormal(0)));
            }
        }
    }

    return false;
}

void MarioActor::updateBeeStickMode(Vec3f& rGravity) {
    if (mBeeWallWalk != 0) {
        bool detach = false;
        bool fur = getDrawStates().onFur;
        if (!fur) {
            if (MR::getWallCodeIndex(mMario->mShadowPolygon) == CollisionWallCode_Fur) {
                fur = true;
            }
        }

        if (fur && mBeeStickInhibit == 0) {
            f32 radius = mConst->getTable()->mBeeWallWalkCancelRadius;
            if (MR::getAreaObj("BeeWallShortDistArea", mPosition) != nullptr) {
                radius = mConst->getTable()->mBeeWallWalkCancelRadiusShort;
            }

            if (mMario->mVerticalSpeed > radius) {
                detach = true;
            } else if (isJumping() && mMario->checkWallCode(CollisionWallCode_Normal, false)) {
                detach = true;
            } else {
                mBeeWallWalk = 5;
            }

            if (isJumping() && isRequestRush()) {
                mBeeWallWalk = 0;
            }
        } else if (isJumping()) {
            if (mMario->mVerticalSpeed < 100.0f) {
                mBeeWallWalk--;
            }

            if (mMario->checkWallCode(CollisionWallCode_Normal, false)) {
                detach = true;
            }

            f32 radius = mConst->getTable()->mBeeWallWalkCancelRadius;
            if (MR::getAreaObj("BeeWallShortDistArea", mPosition) != nullptr) {
                radius = mConst->getTable()->mBeeWallWalkCancelRadiusShort;
            }

            if (mMario->mVerticalSpeed > radius) {
                detach = true;
            }
        } else {
            if (mMario->mMovementStates.hipDrop) {
                mMario->mMovementStates.hipDrop = false;
            }

            detach = true;
        }

        if (detach) {
            if (mBeeWallWalk != 0) {
                mBeeWallWalk--;
            }
        }

        if (mBeeWallWalk == 0) {
            mMario->stopWalk();
            mMario->tryJump();
            mMario->mBeeGravityTimer = mConst->getTable()->mBeeGravityPowerTime;
            mMario->mAirTimer = mConst->getTable()->mBeeAirWalkInhibitTime - 5;
            mMario->push(*mMario->mShadowPolygon->getNormal(0) * 100.0f);
            mMario->cutVecElementFromJumpVec(mRawGravityVec);
            return;
        }

        rGravity = -*mMario->mShadowPolygon->getNormal(0);
    }
}
