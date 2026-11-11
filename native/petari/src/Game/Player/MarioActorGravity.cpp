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
#include "Game/Util/Tunable.hpp"

NEBULA_TUNABLE_NAMED(f32, cNum_syncJumpBeeStickMode_f1, "syncJumpBeeStickMode.f1", 36.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_syncJumpBeeStickMode_f2, "syncJumpBeeStickMode.f2", 2.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_updateBeeModeGravity_f1, "updateBeeModeGravity.f1", 0.1f);
NEBULA_TUNABLE_NAMED(f32, cNum_updateBeeModeGravity_f2, "updateBeeModeGravity.f2", 0.3f);
NEBULA_TUNABLE_NAMED(f32, cNum_updateGravityVec_f1, "updateGravityVec.f1", 100.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_updateGravityVec_f2, "updateGravityVec.f2", 0.9f);
NEBULA_TUNABLE_NAMED(f32, cNum_updateGravityVec_f3, "updateGravityVec.f3", 6.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_updateGravityVec_f4, "updateGravityVec.f4", 0.4f);
NEBULA_TUNABLE_NAMED(f32, cNum_updateGravityVec_f5, "updateGravityVec.f5", 2.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_updateGravityVec_f6, "updateGravityVec.f6", 3.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_updateGravityVec_f7, "updateGravityVec.f7", 0.5f);
NEBULA_TUNABLE_NAMED(f32, cNum_updateGravityVec_f8, "updateGravityVec.f8", 0.75f);
NEBULA_TUNABLE_NAMED(f32, cNum_updateGravityVec_f9, "updateGravityVec.f9", 0.75f);
NEBULA_TUNABLE_NAMED(f32, cNum_updateGravityVec_f10, "updateGravityVec.f10", 70.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_updateGravityVec_f11, "updateGravityVec.f11", 0.5f);
NEBULA_TUNABLE_NAMED(f32, cNum_updateGravityVec_f12, "updateGravityVec.f12", 50.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_updateGravityVec_f13, "updateGravityVec.f13", 70.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_updateGravityVec_f14, "updateGravityVec.f14", 59.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_updateGravityVec_f15, "updateGravityVec.f15", 0.1f);
NEBULA_TUNABLE_NAMED(f32, cNum_updateGravityVec_f16, "updateGravityVec.f16", 0.5f);
NEBULA_TUNABLE_NAMED(f32, cNum_updateGravityVec_f17, "updateGravityVec.f17", 70.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_updateGravityVec_f18, "updateGravityVec.f18", 0.1f);
NEBULA_TUNABLE_NAMED(f32, cNum_updateGravityVec_f19, "updateGravityVec.f19", 15.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_updateGravityVec_f20, "updateGravityVec.f20", 160.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_updateGravityVec_f21, "updateGravityVec.f21", 80.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_updateGravityVec_f22, "updateGravityVec.f22", 80.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_checkBeeCeilStick_f1, "checkBeeCeilStick.f1", 200.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_updateBeeStickMode_f1, "updateBeeStickMode.f1", 100.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_updateBeeStickMode_f2, "updateBeeStickMode.f2", 100.0f);

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

    if (!(MR::diffAngleAbs(mBeePoseDir, getGravityVec()) < MR::pi() / cNum_syncJumpBeeStickMode_f1)) {
        return;
    }

    mBeePoseHeadPos = mPosition - mMario->mFrontVec * cNum_syncJumpBeeStickMode_f2;
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
        MR::vecBlendSphere(mGravityVec, rVec, &rVec, cNum_updateBeeModeGravity_f1);
        return;
    }

    if (mBeeWallWalk != 0) {
        MR::vecBlendSphere(mGravityVec, rVec, &rVec, cNum_updateBeeModeGravity_f2);
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

    MR::calcGravityVectorOrZero(this, mPosition + mMario->mFrontVec.multInLine(cNum_updateGravityVec_f1), &frontGravity, nullptr, 0);

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
    if (gravity.dot(positionGravity) < cNum_updateGravityVec_f2) {
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
            if (angle >= MR::pi() / cNum_updateGravityVec_f3) {
                setBlendMtxTimer(8);
            }
        }

        if (mGravityJumpTimer) {
            gravity = mRawGravityVec;
        } else if (angle >= MR::pi() * cNum_updateGravityVec_f4) {
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
                if (angle >= MR::pi() * (cNum_updateGravityVec_f5 / cNum_updateGravityVec_f6) || !mMario->mMovementStates.mode2D) {
                    mMario->cutGravityElementFromJumpVec(false);
                    mMario->mJumpVec.mult(cNum_updateGravityVec_f7);
                } else {
                    const Vec3f& previousGravity = *mMario->getGravityVec();
                    f32 speed = MR::vecKillElement(mMario->mJumpVec, previousGravity, &mMario->mJumpVec);
                    mMario->mJumpVec.mult(cNum_updateGravityVec_f8);
                    mMario->mJumpVec += *mMario->getGravityVec() * speed * cNum_updateGravityVec_f9;
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
    f32 height = cNum_updateGravityVec_f10;
    Vec3f up;
    if (getDrawStates().wallGravity) {
        MR::vecBlendSphere(mMario->mHeadVec, mMario->mWallNorm, &up, cNum_updateGravityVec_f11 * mBindRatio);
    } else {
        MR::vecBlendSphere(mMario->mHeadVec, mMario->mGroundNorm, &up, mBindRatio);
    }

    Vec3f oldOffset(mBinderOffset);
    if (mMario->mMovementStates.squat) {
        height = cNum_updateGravityVec_f12;
    }

    if (mMario->mMovementStates.onGround) {
        mBinderOffset = up * height;
    } else if (mMario->mActionStates.jumpTakeoff || mInRush) {
        Vec3f offset = -mGravityVec * cNum_updateGravityVec_f13;
        if (mGravityJumpTimer || mMario->calcPolygonAngleD(mMario->mGroundPolygon) >= cNum_updateGravityVec_f14) {
            MR::vecBlend(mBinderOffset, offset, &mBinderOffset, cNum_updateGravityVec_f15);
        } else {
            MR::vecBlend(mBinderOffset, offset, &mBinderOffset, cNum_updateGravityVec_f16);
        }
    } else {
        Vec3f offset = -mGravityVec * cNum_updateGravityVec_f17;
        MR::vecBlend(mBinderOffset, offset, &mBinderOffset, cNum_updateGravityVec_f18);
    }

    if (mGravityBlendTimer) {
        mGravityBlendTimer--;
        f32 rate = static_cast< f32 >(mGravityBlendTimer) / cNum_updateGravityVec_f19;
        mBinderOffset = mBinderOffset * (1.0f - rate) + oldOffset * rate;
        mMario->mActionStates.gravityTurned = true;
    }

    if (mGravityJumpTimer) {
        if (mMario->mVerticalSpeed > cNum_updateGravityVec_f20) {
            Vec3f head = mMario->mHeadVec * cNum_updateGravityVec_f21;
            Vec3f oldHead = mPrevHeadVec * cNum_updateGravityVec_f22;
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
            if (MR::getFirstPolyOnLineToMap(&vec, &triangle, mPosition, (-mGravityVec * cNum_checkBeeCeilStick_f1))) {
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
            if (mMario->mVerticalSpeed < cNum_updateBeeStickMode_f1) {
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
            mMario->push(*mMario->mShadowPolygon->getNormal(0) * cNum_updateBeeStickMode_f2);
            mMario->cutVecElementFromJumpVec(mRawGravityVec);
            return;
        }

        rGravity = -*mMario->mShadowPolygon->getNormal(0);
    }
}
