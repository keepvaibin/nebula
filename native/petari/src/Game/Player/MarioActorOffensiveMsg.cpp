#include "Game/LiveActor/HitSensor.hpp"
#include "Game/Map/HitInfo.hpp"
#include "Game/Player/MarioActor.hpp"
#include "Game/Player/MarioAnimator.hpp"
#include "Game/Player/MarioConst.hpp"
#include "Game/Player/MarioMessenger.hpp"
#include "Game/Player/MarioState.hpp"
#include "Game/Scene/SceneFunction.hpp"
#include "Game/Util/ActorSensorUtil.hpp"
#include "Game/Util/GravityUtil.hpp"
#include "Game/Util/LiveActorUtil.hpp"
#include "Game/Util/MathUtil.hpp"

static f32 mSensorRadiusAttack = 120.0f;
static f32 mSensorRadiusTornadoAttack = 150.0f;
static f32 mSensorRadiusHipDropAttack = 80.0f;
static f32 mSensorRadiusClapCatch = 600.0f;
static f32 mSensorRadiusClapCatchSwim = 2000.0f;
static f32 mSensorRadiusTrampleAttack = 80.0f;
static f32 mSensorRadiusSpinStorm = 600.0f;
static f32 mSensorRadiusTornadoStorm = 1000.0f;
static f32 mSensorRadiusSpinPull = 450.0f;
static f32 mSensorRadiusSpinPullOnGround = 450.0f;

inline bool MarioActor::cylinderPushCheck(HitSensor* pSensor, f32 radius, f32 width, f32 height) {
    Vec3f offset(pSensor->mPosition - mCenterPos);
    return cylinderPushCheck(offset, radius, width, height);
}
void MarioActor::attackOrPushSensor(HitSensor* pSensor, f32 distance) {
    f32 radius = pSensor->getRadius();
    mMario->checkOnimasu(pSensor);
    bool pull = false;
    if (mMario->mMovementStates.tornado) {
        if (distance < radius + mSensorRadiusTornadoAttack && tryTornadoAttack(pSensor)) {
            return;
        }

        if (distance < radius + mSensorRadiusTornadoStorm) {
            pSensor->receiveMessage(ACTMES_TORNADO_STORM_RANGE, getSensor("body"));
            if (mMario->isSwimming()) {
                if (distance < mSensorRadiusSpinPull) {
                    pull = true;
                }
            } else if (distance < mSensorRadiusSpinPullOnGround) {
                pull = true;
            }
        }
    } else {
        if (mPunchSensorOn && mPunchFrame < 15) {
            if (distance < radius + mSensorRadiusSpinStorm) {
                if (selectAction("スピンアタック") < 4) {
                    pSensor->receiveMessage(ACTMES_SPIN_STORM_RANGE, getSensor("body"));
                }

                if (mMario->isSwimming()) {
                    if (distance < mSensorRadiusSpinPull) {
                        pull = true;
                    }
                } else if (distance < mSensorRadiusSpinPullOnGround) {
                    pull = true;
                }
            }
        } else if (isAnimationRun("ファイアスピン") && distance < radius + mSensorRadiusSpinStorm) {
            pSensor->receiveMessage(ACTMES_SPIN_STORM_RANGE, getSensor("body"));
        }
    }

    if (pull && mTakenSensor == nullptr) {
        tryTornadoPull(pSensor);
    }

    if (mMario->mMovementStates.hipDrop && !mMario->mMovementStates.onGround &&
        distance < radius + mSensorRadiusHipDropAttack + mMario->mJumpVec.dot(getGravityVector())) {
        if (tryHipDropAttack(pSensor)) {
            return;
        }

        if (tryGetItem(pSensor)) {
            return;
        }

        mMario->mActionStates.spinHipDrop = false;
    }

    if (distance < radius + mSensorRadiusAttack) {
        if (mPunchHitTimer && tryPunchAirAfter(pSensor)) {
            return;
        }

        if (checkAndTryTrampleAttack(pSensor, distance, false)) {
            return;
        }

        bool touching = false;
        if (mPlayerMode == PlayerMode_Teresa) {
            Vec3f offset = mCenterPos - pSensor->mPosition;
            f32 separation = offset.length();
            if (separation < pSensor->getRadius() + mConst->getTable()->mTeresaBodyRadius) {
                if (selectTeresaThru(pSensor)) {
                    return;
                }

                if (tryGetItem(pSensor)) {
                    return;
                }

                offset.setLength(pSensor->getRadius() + mConst->getTable()->mTeresaBodyRadius - separation);
                if (!MR::isSensorAutoRush(pSensor)) {
                    mMario->doTeresaReflection(offset, false);
                    MR::sendArbitraryMsg(ACTMES_TERESA_PLAYER_TOUCH, pSensor, getSensor("body"));
                    touchSensor(pSensor);
                }

                if (MR::isSensorEnemy(pSensor)) {
                    setPlayerMode(PlayerMode_Normal, true);
                }
            }
        } else {
            f32 sensorRadius;
            f32 width = 20.0f + mBodyRadius;
            f32 height = mBodyHeight;
            if (!mMario->isActiveTaskID(0x200)) {
                if (isJumping()) {
                    height += 10.0f;
                }

                if (mMario->mTargetWalkSpeedIndex >= 5) {
                    width += 15.0f;
                }
            } else {
                width -= 20.0f;
            }

            sensorRadius = pSensor->getRadius();
            if (cylinderPushCheck(pSensor, sensorRadius, width, height)) {
                if (sendBodyAttack(pSensor)) {
                    return;
                }

                if (tryGetItem(pSensor)) {
                    return;
                }

                touchSensor(pSensor);
                bool pushed = MR::sendMsgPush(pSensor, getSensor("dummy"));
                touching = true;
                if (!pushed && !pSensor->isType(ATYPE_POWER_STAR_BIND)) {
                    mMario->mActionStates.spinHipDrop = false;
                }
            }
        }

        addRushSensor(pSensor, touching);
    } else if (mMario->mActionStates.planeCollision) {
        f32 width = 20.0f + mBodyRadius;
        f32 height = mBodyHeight;
        if (isJumping()) {
            height += 10.0f;
        }

        if (mMario->mTargetWalkSpeedIndex >= 5) {
            width += 15.0f;
        }

        f32 sensorRadius = pSensor->getRadius();
        Vec3f offset = pSensor->mPosition - mCenterPos;
        f32 depth = MR::vecKillElement(offset, mCamDirZ, &offset);
        if (depth < 250.0f && depth > -250.0f && cylinderPushCheck(offset, sensorRadius, width, height) && tryGetItem(pSensor)) {
            return;
        }
    }

    f32 catchRadius = mSensorRadiusClapCatch;
    if (mMario->isSwimming()) {
        catchRadius = mSensorRadiusClapCatchSwim;
    }

    if (distance < radius + catchRadius) {
        tryAddClapCoin(pSensor);
    }

    trySetLockOnTarget(pSensor);
    if (mPlayerMode == PlayerMode_Bee && distance < 100.0f + radius) {
        mMario->tryBeeStick(pSensor);
    }

    if (isUnderTarget(pSensor) && !selectNotHomingSensor(pSensor)) {
        f32 angle = MR::diffAngleAbs(pSensor->mPosition - mCenterPos, getGravityVector());
        if (mPushSensorAngle > angle) {
            mPushSensorAngle = angle;
            mHomingSensor = pSensor;
        }
    }
}

void MarioActor::attackOrPushSensorInDamage(HitSensor* pReceiver, f32 radius) {
    f32 initialRadius = pReceiver->mRadius;

    if (!isEnableMoveMario()) {
        return;
    }

    if (radius < initialRadius + mSensorRadiusAttack) {
        bool touching = false;
        f32 sensorRadius;

        f32 width = 20.0f + mBodyRadius;
        f32 height = mBodyHeight;

        if (isJumping()) {
            height += 10.0f;
        }

        if (mMario->mTargetWalkSpeedIndex >= 5) {
            width += 15.0f;
        }

        sensorRadius = pReceiver->mRadius;

        if (cylinderPushCheck(pReceiver->mPosition - mCenterPos, sensorRadius, width, height)) {
            if (tryGetItem(pReceiver)) {
                return;
            }

            touchSensor(pReceiver);
            MR::sendMsgPush(pReceiver, getSensor("dummy"));
            touching = true;
        }

        addRushSensor(pReceiver, touching);
        return;
    }

    if (!mMario->mActionStates.planeCollision) {
        return;
    }

    f32 width = 20.0f + mBodyRadius;
    f32 height = mBodyHeight;

    if (isJumping()) {
        height += 10.0f;
    }

    if (mMario->mTargetWalkSpeedIndex >= 5) {
        width += 15.0f;
    }

    f32 sensorRadius = pReceiver->mRadius;
    Vec3f offset = pReceiver->mPosition - mCenterPos;
    f32 depth = MR::vecKillElement(offset, mCamDirZ, &offset);
    if (depth < 250.0f && depth > -250.0f && cylinderPushCheck(offset, sensorRadius, width, height) && tryGetItem(pReceiver)) {
        return;
    }
}

void MarioActor::attackOrPushSensorInRush(HitSensor* pSensor, f32 radius) {
    f32 sensorRadius = pSensor->mRadius;
    if (radius < sensorRadius + mSensorRadiusAttack) {
        f32 width = 20.0f + mBodyRadius;
        f32 height = mBodyHeight;
        if (cylinderPushCheck(pSensor->mPosition - mCenterPos, sensorRadius, width, height)) {
            if (tryGetItem(pSensor)) {
                return;
            }

            sendMsgToSensor(pSensor, ACTMES_RUSH_PLAYER_TOUCH);
        }

        addRushSensor(pSensor, false);
    }

    if (radius < sensorRadius + mSensorRadiusClapCatch) {
        tryAddClapCoin(pSensor);
    }

    trySetLockOnTarget(pSensor);
}

void MarioActor::tryAddClapCoin(HitSensor* pSensor) {
    if (pSensor->isType(ATYPE_STAR_PIECE)) {
        mClapCoins.at(mClapCoinCount) = pSensor;
        mClapCoinCount++;
    }
}

bool MarioActor::tryTornadoAttack(HitSensor* pSensor) {
    bool isMessageReceived = pSensor->receiveMessage(ACTMES_TORNADO_ATTACK, getSensor("body"));
    if (isMessageReceived) {
        mMario->startPadVib(1);
    }

    return isMessageReceived;
}

bool MarioActor::isUnderTarget(HitSensor* pSensor) {
    Vec3f down;
    if (mBeeWallWalk != 0) {
        down = mGravityVec;
    } else {
        MR::calcGravityVectorOrZero(this, pSensor->mPosition, &down, mGravityInfo, 0);
    }

    if (down.dot(mPosition - pSensor->mPosition) <= 0.0f && mMario->mZeroGravityTimer == 0) {
        return true;
    }

    return false;
}

bool MarioActor::tryHipDropAttack(HitSensor* pSensor) {
    if (isUnderTarget(pSensor) && cylinderHorizontalCheck(pSensor)) {
        if (pSensor->isType(ATYPE_PLAYER_AUTO_JUMP)) {
            return tryTrampleAttack(pSensor);
        }

        return pSensor->receiveMessage(ACTMES_PLAYER_HIP_DROP, getSensor("body"));
    }

    return false;
}

bool MarioActor::checkAndTryTrampleAttack(HitSensor* pSensor, f32 distance, bool force) {
    f32 radius = pSensor->mRadius;
    bool trample = getMovementStates().hipDrop != true;
    bool falling = false;
    if (isJumping() && !mMario->isRising()) {
        falling = true;
    }

    bool near = distance < radius + mSensorRadiusTrampleAttack + 1.5f * mMario->mJumpVec.dot(getGravityVector());
    if (force) {
        falling = isJumping();
    }

    if (trample && falling && near) {
        bool attacked = false;
        if (tryTrampleAttack(pSensor)) {
            attacked = true;
        }

        if (mPlayerMode == PlayerMode_Invincible && MR::sendArbitraryMsg(ACTMES_INVINCIBLE_ATTACK, pSensor, getSensor("body"))) {
            mMario->startPadVib(2);
            printHitMark(pSensor);
        }

        return attacked;
    }

    return false;
}

bool MarioActor::tryTrampleAttack(HitSensor* pSensor) {
    if (mTrampling) {
        return false;
    }

    if (mMario->isStatusActive(MarioStatus_Blown)) {
        return false;
    }

    bool isUnder = isUnderTarget(pSensor);
    bool isHorizontalCheck = cylinderHorizontalCheck(pSensor);
    if (isUnder && isHorizontalCheck) {
        mTrampling = true;

        bool isReceiveMessage = pSensor->receiveMessage(ACTMES_PLAYER_TRAMPLE, getSensor("body"));

        mTrampling = false;

        if (isReceiveMessage) {
            doTrampleJump(pSensor);
            tryGetItem(pSensor);
        }

        return isReceiveMessage;
    }

    return false;
}

bool MarioActor::cylinderHorizontalCheck(HitSensor* pSensor) {
    f32 radius = pSensor->mRadius;
    Vec3f diff = pSensor->mPosition - (mCenterPos + mMario->mPushVec + mMario->mPushVelocity + mVelocity);
    Vec3f vec;
    MR::vecKillElement(diff, getGravityVector(), &vec);

    if (radius + mBodyRadius - vec.length() > 0.0f) {
        return true;
    }

    MR::vecKillElement(diff, mBodyDown, &vec);

    return radius + mBodyRadius - vec.length() > 0.0f;
}

bool MarioActor::tryJetAttack(HitSensor* pReceiver) {
    return MR::sendArbitraryMsg(ACTMES_JET_TURTLE_ATTACK, pReceiver, getSensor("dummy"));
}

void MarioActor::tryCounterJetAttack(HitSensor* pReceiver) {
    mMessenger->addRequest(pReceiver, MR::MovementType_MsgSharedGroup);
}

bool MarioActor::tryGetItem(HitSensor* pSensor) {
    if (mInRush) {
        switch (pSensor->mType) {
        case ATYPE_JET_TURTLE:
        case ATYPE_JET_TURTLE_SLOW:
        case ATYPE_BOMBHEI:
        case ATYPE_NOKONOKO:
            return false;
        }
    }

    if (!isEnableNerveChange()) {
        return false;
    }

    if (!mHealth) {
        switch (pSensor->mType) {
        case ATYPE_JET_TURTLE:
        case ATYPE_JET_TURTLE_SLOW:
        case ATYPE_BOMBHEI:
        case ATYPE_NOKONOKO:
        case ATYPE_MORPH_ITEM:
            return false;
        }
    }

    switch (pSensor->mType) {
    case ATYPE_SWITCH:
        break;
    case ATYPE_NOKONOKO:
        if (isDamaging()) {
            return false;
        }

        if (!isActionOk("カメ持ち")) {
            return false;
        }

        if (mTakeCount != 0) {
            return false;
        }

        if (!pSensor->receiveMessage(ACTMES_ITEM_GET, getSensor("dummy"))) {
            return false;
        }

        mStunTimer = 2;
        return false;
    case ATYPE_JET_TURTLE:
    case ATYPE_JET_TURTLE_SLOW:
    case ATYPE_COINTHROW:
    case ATYPE_BOMBHEI: {
        if (mItemPickupBlock) {
            return false;
        }

        if (isDamaging()) {
            return false;
        }

        if (!isActionOk("カメ持ち")) {
            return false;
        }

        if (mTakeCount != 0) {
            return false;
        }

        bool moving = false;
        Vec3f horizontal;
        if (MR::vecKillElement(*getShadowPos() - pSensor->mPosition, getGravityVector(), &horizontal) > 100.0f) {
            moving = true;
        }

        if (!MR::isNearZero(pSensor->mHost->mVelocity)) {
            moving = true;
        }

        if (getMovementStates().hipDrop) {
            moving = false;
        }

        if (!moving && !mMario->isSwimming() && !mMario->getMovementStates().onGround && mTakenSensor == nullptr) {
            if (getMovementStates().hipDrop) {
                return false;
            }

            doTrampleJump(pSensor);
        }

        break;
    }

    case ATYPE_MORPH_ITEM:
        if (mMorphTimer) {
            return false;
        }

        if (mMario->mMorphResetTimer) {
            return false;
        }

        break;
    }

    if (!pSensor->receiveMessage(ACTMES_ITEM_GET, getSensor("dummy"))) {
        return false;
    }

    switch (pSensor->mType) {
    case ATYPE_COIN:
    case ATYPE_SWITCH:
    case ATYPE_KINOKO_ONEUP:
        break;
    case ATYPE_JET_TURTLE:
    case ATYPE_JET_TURTLE_SLOW:
    case ATYPE_COINTHROW:
    case ATYPE_BOMBHEI:
        mMarioAnim->changePickupAnimation(pSensor);
        if (!mMario->isSwimming()) {
            if (getMovementStates().onGround && mNullAnimationKind != -3) {
                if (pSensor->mType == ATYPE_BOMBHEI) {
                    mStunTimer = 40;
                } else {
                    mStunTimer = 25;
                }
            }

            mVelocity.zero();
            mMario->mWalkSpeed = 0.0f;
            if (mMario->mMovementStates.onGround) {
                mMario->stopJump();
                mMario->mVerticalSpeed = 0.0f;
            }
        }

        mTakenSensor = pSensor;
        mTaking = 1;
        break;
    case ATYPE_MORPH_ITEM:
        if (mMorphItemSensor != nullptr) {
            mMorphItemSensor->mHost->kill();
        }

        mMorphItemSensor = pSensor;
        break;
    }

    return true;
}

namespace {
    inline HitSensor* getDummySensor(MarioActor* pActor) {
        return pActor->getSensor("dummy");
    }
};  // namespace

bool MarioActor::cylinderPushCheck(const Vec3f& rOffset, f32 radius, f32 width, f32 height) {
    Vec3f radial;
    f32 axial = MR::vecKillElement(rOffset, mBodyDown, &radial);
    if (axial > -radius) {
        f32 radialPenetration = radius + width - radial.length();
        f32 axialPenetration = radius + height - axial;
        if (radialPenetration > 0.0f && axialPenetration > 0.0f) {
            if (radialPenetration > 0.0f) {
                getDummySensor(this)->mPosition.set(mCenterPos + mBodyDown * axial);
                getSensor("dummy")->mRadius = width;
            } else {
                HitSensor* dummy = getSensor("dummy");
                dummy->mPosition.set(mCenterPos + radial);
                getSensor("dummy")->mRadius = height;
            }

            return true;
        }
    }

    if (mMario->mMovementStates.squat && mMario->getMovementStates().onGround) {
        return false;
    }

    axial = MR::vecKillElement(rOffset, mBodyUp, &radial);
    if (axial < -radius) {
        return false;
    }

    f32 radialPenetration = radius + width - radial.length();
    f32 axialPenetration = radius + height - axial;
    if (radialPenetration > 0.0f && axialPenetration > 0.0f) {
        if (radialPenetration > 0.0f) {
            getDummySensor(this)->mPosition.set(mCenterPos + mBodyUp * axial);
            getSensor("dummy")->mRadius = width;
        } else {
            HitSensor* dummy = getSensor("dummy");
            dummy->mPosition.set(mCenterPos + radial);
            getSensor("dummy")->mRadius = height;
        }

        return true;
    }

    return false;
}

void MarioActor::attackOrPushPolygons() {
    HitSensor* bodySensor = getSensor("body");

    mAttacking = true;

    if (mMario->getMovementStates().onGround && mPressTimer == 0) {
        HitSensor* groundSensor = mMario->mGroundPolygon->mSensor;

        if (groundSensor != nullptr && !MR::isDead(groundSensor->mHost)) {
            groundSensor->receiveMessage(ACTMES_FLOOR_TOUCH, bodySensor);
        }
    }

    if (mMario->getMovementStates().frontWall) {
        sendWallTouch(mMario->mFrontWallTriangle->mSensor, bodySensor);
    }

    if (mMario->getMovementStates().backWall) {
        sendWallTouch(mMario->mBackWallTriangle->mSensor, bodySensor);
    }

    if (mMario->getMovementStates().sideWall) {
        sendWallTouch(mMario->mSideWallTriangle->mSensor, bodySensor);
    }

    if (mMario->getMovementStates().jumping && !mMario->getMovementStates().onGround && mMario->getMovementStates().hipDrop) {
        mPolygonAttackDone = true;
    } else {
        if (mPolygonAttackDone) {
            if (getMovementStates().onGround) {
                if (mMario->mGroundPolygon->isValid()) {
                    HitSensor* groundSensor = mMario->mGroundPolygon->mSensor;

                    if (groundSensor != nullptr && !MR::isDead(groundSensor->mHost)) {
                        HitSensor* hitSensor = mMario->mGroundPolygon->mSensor;
                        if (!hitSensor->receiveMessage(ACTMES_PLAYER_HIP_DROP_FLOOR, getSensor("body"))) {
                            mPolygonAttackDone = false;
                        }
                    }
                }
            } else {
                mPolygonAttackDone = false;
            }
        }

        if (!mMario->getMovementStates().jumping) {
            mPolygonAttackDone = false;
        }
    }

    mAttacking = false;
}

void MarioActor::sendWallTouch(HitSensor* pReceiver, HitSensor* pSender) {
    if (pReceiver == nullptr) {
        return;
    }

    if (MR::isDead(pReceiver->mHost)) {
        return;
    }

    pReceiver->receiveMessage(ACTMES_WALL_TOUCH, pSender);

    if (!mMario->getMovementStates().tornado) {
        return;
    }

    pReceiver->receiveMessage(ACTMES_TORNADO_ATTACK, pSender);
}

bool MarioActor::sendMsgUpperPunch(HitSensor* pSensor) {
    if (!isActionOk("アッパーパンチ")) {
        return false;
    }

    if (pSensor != nullptr && !MR::isDead(pSensor->mHost) && pSensor->receiveMessage(ACTMES_PLAYER_UPPER_PUNCH, getSensor("body"))) {
        playSound("声蹴り", -1);

        if (!mMario->isSwimming()) {
            changeAnimation("アッパーパンチ");
        }

        return true;
    }

    return false;
}
