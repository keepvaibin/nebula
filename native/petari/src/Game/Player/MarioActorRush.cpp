#include "Game/LiveActor/HitSensor.hpp"
#include <nebula_game_api.h>
#include "Game/Map/HitInfo.hpp"
#include "Game/MapObj/CollectCounter.hpp"
#include "Game/Player/MarioActor.hpp"
#include "Game/Player/MarioAnimator.hpp"
#include "Game/Player/MarioConst.hpp"
#include "Game/Player/MarioDamage.hpp"
#include "Game/Player/MarioModule.hpp"
#include "Game/Player/MarioParts.hpp"
#include "Game/Player/MarioSwim.hpp"
#include "Game/Player/MarioWait.hpp"
#include "Game/Player/RushEndInfo.hpp"
#include "Game/Screen/GameSceneLayoutHolder.hpp"
#include "Game/Util.hpp"
#include "Game/System/NebulaHost.hpp"

void MarioActor::settingRush() {
    rushDropThrowMemoSensor();

    switch ((s32)mRushSensor->mType) {
    case ATYPE_WATER_PRESSURE_BULLET_BIND:
        mMario->touchWater();
        break;
    }

    mInRush = 1;
    resetCondition();
    XanimePlayer* p = mMario->getAnimator()->mXanimePlayer;
    p->mReturnToDefault = false;
}

bool MarioActor::isFixJumpRushSensor(const HitSensor* pSensor) const {
    switch (pSensor->mType) {
    case ATYPE_TRANSFERABLE_BINDER:
    case ATYPE_SPIN_DRIVER_BIND:
    case ATYPE_SPIN_CANNON_BIND:
    case ATYPE_G_CAPTURE_BIND:
    case ATYPE_SPRING_ATTACKER_BIND:
    case ATYPE_WATER_PRESSURE_BULLET_BIND:
    case ATYPE_MARIO_LAUNCHER_BIND:
        return true;

    default:
        return selectJumpRushSensor(pSensor->mHost->mName);
    }
}

bool MarioActor::isLandEffectRushSensor(const HitSensor* pSensor) const {
    switch (pSensor->mType) {
    case ATYPE_MARIO_LAUNCHER_BIND:
    case ATYPE_SPIN_CANNON_BIND:
        return true;
    default:
        return false;
    }
}

void MarioActor::resetCondition() {
    bool inWater = false;

    if (mBaseMtxForced) {
        MR::extractMtxTrans(mForcedBaseMtx.mMtx, &mPosition);
        mMario->mPosition = mPosition;
    }

    if (mMario->mSwim->checkWaterCube(true)) {
        inWater = true;
    }

    mMario->stopAnimationUpper(nullptr);

    if (mMario->mMovementStates.squat) {
        mMario->cancelSquatMode();
    }

    XanimePlayer* p = mMario->getAnimator()->mXanimePlayer;
    p->mReturnToDefault = true;
    mMario->forceStopTornado();
    updateTornado();

    mMario->mSwim->mColdWaterDamageInterval = 0;
    mMario->mHitStates.swimLifeDrain = 0;

    if (mMario->isStatusActive(MarioStatus_Swim) && inWater) {
        if (!mTakeCount) {
            mMario->mSwim->resetJet();
        }
    } else if (mMario->isStatusActive(MarioStatus_Teresa) && mPlayerMode == 6) {
        if (mMario->isStatusActive(MarioStatus_Wait)) {
            mMario->closeStatus(mMario->mWait);
        }

        mMario->resetTeresaMode();
    } else {
        mMario->closeStatus(nullptr);
    }

    bool was1F = mMario->mMovementStates.invincible;
    mMario->mMovementStates = Mario::MovementStates();
    mMario->mActionStates = Mario::ActionStates();
    mMario->mMovementStates.invincible = was1F;

    resetPadSwing();

    if (mMario->isStatusActive(MarioStatus_Swim) && inWater) {
        if (mTakeCount) {
            mMario->changeAnimation("水泳ジェット", "水泳ジェット");
        } else {
            mMario->changeAnimation(nullptr, "水泳基本");
        }
    } else {
        mMario->changeAnimation(nullptr, "基本");

        if (mTakeCount) {
            mMarioAnim->waterToGround();
        }
    }

    mMario->setFrontVecKeepUp(mMario->mFrontVec);
    updateGravityVec(true, true);

    if (!MR::isNearZero(mRawGravityVec)) {
        mMario->setHeadVec(-mRawGravityVec);
    }

    mMario->mPostureUpVec = mMario->mHeadVec;
    mMario->mLookOfs.zero();
    mMario->mLookOfsBody.zero();
    mMario->mLookOfsSink.zero();
    mMario->mVerticalSpeed = 0.0f;
    mMario->mLastGroundPolygon->mIdx = -1;
    stopEffect("いい汗");
    stopEffect("ホッパー尻落ルイージ");
    stopEffect("ホッパー尻落");
    stopEffect("尻落ルイージ");
    stopEffect("尻落");

    mFreezeModel->kill();
    MR::offCalcAnim(mFreezeModel);

    for (u32 i = 0; i < 11; i++) {
        mMario->mExtraTasks[i] = nullptr;
    }

    if (mBaseMtxForced) {
        mMario->changeAnimationInterpoleFrame(0);
        mBlendMtxTimer = 0;
    }
}

void MarioActor::beginRush() {
    mRushSensor = mRushSensors[0];
    mMarioAnim->clearAllJointTransform();
    MR::invalidateHitSensors(this);
    stopEffect("共通壁手擦り");
    stopEffect("スピンライト");
    bool spinCatch = selectSpinCatchInRush(mRushSensor->mHost->mName);
    if (mPlayerMode == PlayerMode_Bee && selectHideFlyMeter(mRushSensor)) {
        if (!mExtra) {
            MR::getGameSceneLayoutHolder()->changeLifeMeterModeGround();
        }
    }

    if (isFixJumpRushSensor(mRushSensor) || spinCatch) {
        settingRush();
        getSensor("eye")->validate();
        getSensor("body")->validate();
        return;
    }

    getSensor("body")->validate();
    if (mRushSensor->isType(ATYPE_POWER_STAR_BIND)) {
        MR::forceDeleteEffectAll(this);
        mTrampleCounter->kill();
    }

    if (selectLandEffect(mRushSensor)) {
        playEffect("特殊着地");
    }

    switch (mRushSensor->mType) {
    case ATYPE_POWER_STAR_BIND:
        setPlayerMode(PlayerMode_Normal, false);
        resetFog();
        break;
    }

    settingRush();
    if (!mRushSensor->isType(ATYPE_POWER_STAR_BIND)) {
        setBlendMtxTimer(getConst().getTable()->mRushInBlendTimer);
    }
}

void MarioActor::endRush(const RushEndInfo* pInfo) {
    bool launched = false;
    if (mRushSensor && (!pInfo->mActor || mRushSensor->mHost == pInfo->mActor)) {
        mInRush = false;
        NebulaHost::gameContext(NEBULA_CONTEXT_TILT_SECTION, 0);
        mRushJustEnded = true;
        mMario->stopJump();
        RushEndInfo::Mode mode = pInfo->mMode;
        mMario->mMovementStates.inputLocked = false;
        if (mMario->isStatusActive(MarioStatus_Swim) && (mode == RushEndInfo::Mode_Jump || mode == RushEndInfo::Mode_PowerJump)) {
            mode = RushEndInfo::Mode_Release;
        }

        switch (mode) {
        case RushEndInfo::Mode_Jump:
            launched = true;
            mMario->initJumpParam();
            mMario->mMovementStates.squatLocked = true;
            if (mPlayerMode == PlayerMode_Bee) {
                mMario->mBeeFlyInhibitTimer = 60;
            }

            mMario->tryForceFreeJump(pInfo->mVec);
            mMario->mMovementStates.wallJump = true;
            mMario->mActionStates.holdButtonAfterRush = true;
            if (pInfo->mForced) {
                mMario->mMovementStates.forcedLaunch = true;
            } else {
                mMario->mMovementStates.forcedLaunch = false;
            }

            if (pInfo->mInputOff) {
                mMario->mMovementStates.inputLocked = true;
            }

            break;
        case RushEndInfo::Mode_PowerJump:
            launched = true;
            if (mPlayerMode == PlayerMode_Bee) {
                mMario->mBeeFlyInhibitTimer = 60;
            }

            mMario->tryForcePowerJump(pInfo->mVec, false);
            if (pInfo->mForced) {
                mMario->mMovementStates.forcedLaunch = true;
            } else {
                mMario->mMovementStates.forcedLaunch = false;
            }

            if (pInfo->mLimitJump) {
                mMario->mMovementStates.noDropSpeedCap = false;
            }

            if (pInfo->mInputOff) {
                mMario->mMovementStates.inputLocked = true;
            }

            if (pInfo->mDamage != RushEndInfo::Damage_None) {
                switch (pInfo->mDamage) {
                case RushEndInfo::Damage_Hit:
                    mMario->mMovementStates.damaged = true;
                    mMario->mDamage->setVec(pInfo->mVec);
                    break;
                case RushEndInfo::Damage_Acid:
                    mMario->mActionStates.rushFireDamage = true;
                    break;
                case RushEndInfo::Damage_Fire:
                    mMario->mActionStates.rushFireDamage = true;
                    break;
                case RushEndInfo::Damage_Electric:
                    mMario->mActionStates.rushElectricDamage = true;
                    break;
                case RushEndInfo::Damage_Flip:
                    mMario->doFlipLarge(pInfo->mVec);
                    break;
                case RushEndInfo::Damage_Freeze:
                    mMario->doFreeze();
                    break;
                case RushEndInfo::Damage_None:
                    break;
                }
            }

            mMario->mActionStates.holdButtonAfterRush = true;
            if (selectRebindTimer(mRushSensor)) {
                mLastRushEndFrame = mFrameCount;
            }

            break;
        case RushEndInfo::Mode_Wait:
            mMario->stopWalk();
            [[fallthrough]];
        case RushEndInfo::Mode_Release:
            mMario->mMovementStates.hipDrop = false;
            mMario->mMovementStates.jumping = false;
            mMario->mDropWaitTimer = 0;
            break;
        }

        mMario->mActionStates.rollLanding = pInfo->mRollLanding;
        if (!mRushEndFrontMode) {
            Pos3f base;
            Vec3f front;
            NebulaMtxConcat(getBaseMtx(), mBaseCorrectionMtx, base);
            front.set< f32 >(base[0][2], base[1][2], base[2][2]);
            mMario->setFrontVecKeepUp(front);
        } else {
            Pos3f base;
            Vec3f front;
            Vec3f side;
            Vec3f up;
            NebulaMtxConcat(getBaseMtx(), mBaseCorrectionMtx, base);
            front.set< f32 >(base[0][2], base[1][2], base[2][2]);
            up.set< f32 >(base[0][1], base[1][1], base[2][1]);
            side.set< f32 >(base[0][0], base[1][0], base[2][0]);
            MR::normalize(&front);
            MR::normalize(&side);
            MR::normalize(&up);
            if (!MR::isNearZero(up)) {
                mMario->mHeadVec = up;
            }

            if (!MR::isNearZero(side)) {
                mMario->mSideVec = side;
            }

            if (!MR::isNearZero(front)) {
                mMario->setFrontVec(front);
            }

            if (up.dot(-*mMario->getGravityVec()) < 0.0f) {
                mMario->setFrontVecKeepSide(mRushEndFront);
            } else if (mRushEndFrontMode > 1) {
                mMario->setFrontVecKeepUp(mRushEndFront, static_cast< u32 >(mRushEndFrontMode));
            } else {
                mMario->setFrontVecKeepUp(mRushEndFront);
            }
        }

        mMario->mPostureUpVec = mMario->mHeadVec;
        if (!pInfo->mKeepAnimation) {
            mMario->stopAnimation(nullptr);
        }

        if (pInfo->mBlendFrames) {
            setBlendMtxTimer(pInfo->mBlendFrames);
        }

        MR::validateHitSensors(this);
        getSensor("dummy")->invalidate();
        XanimePlayer* player = mMario->getAnimator()->mXanimePlayer;
        player->mReturnToDefault = true;
        if (mMainSpinBlock < 15) {
            mMainSpinBlock = 15;
        }

        if (mSubSpinBlock < 15) {
            mSubSpinBlock = 15;
        }

        if (pInfo->mDamage == RushEndInfo::Damage_None && isLandEffectRushSensor(mRushSensor) && pInfo->mForced) {
            mMario->mMovementStates.launchKind = 1;
            mMario->mMovementStates.jumping = true;
            mMario->mMovementStates.onGround = false;
        } else if (mRushSensor->isType(ATYPE_MARIO_LAUNCHER_BIND)) {
            if (!MR::isNearZero(mRushMove)) {
                mMario->mMovementStates.launchKind = 1;
                mMario->mJumpVec = mRushMove;
            }
        } else if (mRushSensor->isType(ATYPE_SCENARIO_STARTER_BIND)) {
            if (pInfo->mMode != RushEndInfo::Mode_Wait) {
                mMario->mMovementStates.launchKind = 2;
                mMario->mMovementStates.jumping = true;
                mMario->mMovementStates.onGround = false;
            } else {
                changeAnimation("基本");
            }

            updateGravityVec(true, true);
            mBinderOffset = mRawGravityVec * -70.0f;
            mGravityVec = mRawGravityVec;
            mMario->setGravityVec(mGravityVec);
        }

        mLastRushSensor = mRushSensor;
        mRushSensor = nullptr;
        mMario->mActionStates.gravityTurned = true;
        updateGravityVec(true, false);
        if (mPlayerMode == PlayerMode_Hopper) {
            mMario->startRabbitMode();
        }

        if (mPlayerMode == PlayerMode_Bee) {
            if (!mExtra) {
            MR::getGameSceneLayoutHolder()->changeLifeMeterModeBee();
        }
        }

        if (launched) {
            mLastMove = mMario->mVelocity;
        } else {
            mMario->mVelocity = mLastMove;
        }

        mMarioAnim->mPrevWalkState = 0xFF;
        if (selectWaterInOutRush(mLastRushSensor) && !mMario->forceStartSwimAndShoot(pInfo->mVec)) {
            mMario->forceExitSwim();
        }

        mMario->mPushVec.zero();
        mMario->mPushVelocity.zero();
        if (mRushMove.length() > 200.0f) {
            mMario->mHitStates.launchedFar = true;
        }
    }
}

bool MarioActor::takeSensor(HitSensor* pSensor) {
    if (mTakenSensor) {
        return false;
    }

    if (mTakeCount) {
        return false;
    }

    changeAnimation("カブ抜き");
    changeNullAnimation("PullOut", -1);
    mTakenSensor = pSensor;
    memorizeSensorThrow(pSensor);
    mTaking = 1;
    mStunTimer = 60;
    return true;
}
