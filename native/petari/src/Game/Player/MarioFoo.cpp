#include "Game/Player/MarioFoo.hpp"
#include "Game/Render/TypedDraw.hpp"
#include "Game/Render/FrameList.hpp"
#include "Game/Render/TypedDraw.hpp"
#include "Game/LiveActor/HitSensor.hpp"
#include "Game/MapObj/DashRing.hpp"
#include "Game/Player/Mario.hpp"
#include "Game/Player/MarioActor.hpp"
#include "Game/Player/MarioAnimator.hpp"
#include "Game/Player/MarioConst.hpp"
#include "Game/Util/AreaObjUtil.hpp"
#include "Game/Util/CameraUtil.hpp"
#include "Game/Util/DirectDraw.hpp"
#include "Game/Util/LiveActorUtil.hpp"
#include "Game/Util/MathUtil.hpp"
#include "Game/Util/ModelUtil.hpp"
#include "Game/Util/MtxUtil.hpp"
#include "Game/Render/GameTexture.hpp"
#include "Game/Util/Tunable.hpp"

NEBULA_TUNABLE_NAMED(f32, cNum_start_f1, "start.f1", 0.5235988f);
NEBULA_TUNABLE_NAMED(f32, cNum_update_f1, "update.f1", 0.1f);
NEBULA_TUNABLE_NAMED(f32, cNum_update_f2, "update.f2", 120.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_update_f3, "update.f3", 120.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_update_f4, "update.f4", 2.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_update_f5, "update.f5", 10.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_update_f6, "update.f6", 2.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_update_f7, "update.f7", 8.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_update_f8, "update.f8", 100.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_update_f9, "update.f9", 0.05f);
NEBULA_TUNABLE_NAMED(f32, cNum_update_f10, "update.f10", 5.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_update_f11, "update.f11", 0.05f);
NEBULA_TUNABLE_NAMED(f32, cNum_update_f12, "update.f12", 0.05f);
NEBULA_TUNABLE_NAMED(f32, cNum_update_f13, "update.f13", 5.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_update_f14, "update.f14", 0.5f);
NEBULA_TUNABLE_NAMED(f32, cNum_update_f15, "update.f15", 1.0471976f);
NEBULA_TUNABLE_NAMED(f32, cNum_update_f16, "update.f16", 0.1f);
NEBULA_TUNABLE_NAMED(f32, cNum_update_f17, "update.f17", 1.1f);
NEBULA_TUNABLE_NAMED(f32, cNum_update_f18, "update.f18", 0.1f);
NEBULA_TUNABLE_NAMED(f32, cNum_update_f19, "update.f19", 0.1f);
NEBULA_TUNABLE_NAMED(f32, cNum_update_f20, "update.f20", 1.1f);
NEBULA_TUNABLE_NAMED(f32, cNum_update_f21, "update.f21", 0.1f);
NEBULA_TUNABLE_NAMED(f32, cNum_update_f22, "update.f22", 0.01f);
NEBULA_TUNABLE_NAMED(f32, cNum_update_f23, "update.f23", 3.1415927f);
NEBULA_TUNABLE_NAMED(f32, cNum_update_f24, "update.f24", 0.5235988f);
NEBULA_TUNABLE_NAMED(f32, cNum_update_f25, "update.f25", 0.5235988f);
NEBULA_TUNABLE_NAMED(f32, cNum_update_f26, "update.f26", 0.1f);
NEBULA_TUNABLE_NAMED(f32, cNum_update_f27, "update.f27", 25.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_update_f28, "update.f28", 25.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_update_f29, "update.f29", 0.98f);
NEBULA_TUNABLE_NAMED(f32, cNum_update_f30, "update.f30", 0.98f);
NEBULA_TUNABLE_NAMED(f32, cNum_update_f31, "update.f31", 0.98f);
NEBULA_TUNABLE_NAMED(f32, cNum_jet_f1, "jet.f1", 1.02f);
NEBULA_TUNABLE_NAMED(f32, cNum_jet_f2, "jet.f2", 0.98f);
NEBULA_TUNABLE_NAMED(f32, cNum_jet_f3, "jet.f3", 0.9f);
NEBULA_TUNABLE_NAMED(f32, cNum_jet_f4, "jet.f4", 2.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_jet_f5, "jet.f5", 1.2f);
NEBULA_TUNABLE_NAMED(f32, cNum_jet_f6, "jet.f6", 50.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_jet_f7, "jet.f7", 1.5f);
NEBULA_TUNABLE_NAMED(f32, cNum_updateTilt_f1, "updateTilt.f1", 0.1f);
NEBULA_TUNABLE_NAMED(f32, cNum_updateTilt_f2, "updateTilt.f2", 5.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_updateTilt_f3, "updateTilt.f3", 12.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_updateTilt_f4, "updateTilt.f4", 2.0943952f);
NEBULA_TUNABLE_NAMED(f32, cNum_updateTilt_f5, "updateTilt.f5", 8.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_updateTilt_f6, "updateTilt.f6", 120.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_updateTilt_f7, "updateTilt.f7", 10.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_updateTilt_f8, "updateTilt.f8", 0.1f);
NEBULA_TUNABLE_NAMED(f32, cNum_updateTilt_f9, "updateTilt.f9", 0.9f);
NEBULA_TUNABLE_NAMED(f32, cNum_hitWall_f1, "hitWall.f1", 0.7853982f);
NEBULA_TUNABLE_NAMED(f32, cNum_hitWall_f2, "hitWall.f2", 0.7853982f);
NEBULA_TUNABLE_NAMED(f32, cNum_passRing_f1, "passRing.f1", 0.25f);
NEBULA_TUNABLE_NAMED(f32, cNum_calcRingAcc_f1, "calcRingAcc.f1", 0.5f);
NEBULA_TUNABLE_NAMED(f32, cNum_calcRingAcc_f2, "calcRingAcc.f2", 30.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_calcRingAcc_f3, "calcRingAcc.f3", 30.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_calcRingAcc_f4, "calcRingAcc.f4", 0.5f);
NEBULA_TUNABLE_NAMED(f32, cNum_draw3D_f1, "draw3D.f1", 2.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_draw3D_f2, "draw3D.f2", 50.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_draw3D_f3, "draw3D.f3", 70.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_draw3D_f4, "draw3D.f4", 70.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_draw3D_f5, "draw3D.f5", 2000.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_draw3D_f6, "draw3D.f6", 50.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_draw3D_f7, "draw3D.f7", 2000.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_draw3D_f8, "draw3D.f8", 50.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_draw3D_f9, "draw3D.f9", 0.5f);
NEBULA_TUNABLE_NAMED(f32, cNum_draw3D_f10, "draw3D.f10", 0.5f);
NEBULA_TUNABLE_NAMED(f32, cNum_draw3D_f11, "draw3D.f11", 70.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_draw3D_f12, "draw3D.f12", 70.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_draw3D_f13, "draw3D.f13", 2000.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_draw3D_f14, "draw3D.f14", 50.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_draw3D_f15, "draw3D.f15", 2000.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_draw3D_f16, "draw3D.f16", 50.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_draw3D_f17, "draw3D.f17", 0.5f);
NEBULA_TUNABLE_NAMED(f32, cNum_draw3D_f18, "draw3D.f18", 0.5f);
NEBULA_TUNABLE_NAMED(f32, cNum_draw3D_f19, "draw3D.f19", 2.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_draw3D_f20, "draw3D.f20", 255.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_draw3D_f21, "draw3D.f21", 255.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_draw3D_f22, "draw3D.f22", 10.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_draw3D_f23, "draw3D.f23", 10.0f);

namespace {
    f32 cTurnMotionSpeed = 5.0f;
    f32 cWidth = 70.0f;
    f32 cLimitAngleSink = FloatUtil::PI() / 1.0001f;
    f32 cNeutralAngleWait = FloatUtil::PI() / 2.0f - FloatUtil::PI() / 6.0f;
    f32 cLimitAngleWait = FloatUtil::PI() / 1.0001f;
    f32 cUpperAngleWait = FloatUtil::PI() / 100.0f;
};  // namespace

void Mario::tryStartFoo() {
    if (MR::getAreaObj("CelestrialSphere", getTrans()) || !mFoo->mCelestialSphere) {
        changeStatus(mFoo);
        mFoo->setStartSpeed(mActor->getConst().getTable()->mSwimFrontMaxSpeed);
    }
}

MarioFoo::MarioFoo(MarioActor* pActor) : MarioState(pActor, MarioStatus_Foo) {
    mBraked = 0;
    mJetHeldFromStart = 0;
    mFrameCount = 0;
    mPitchTimer = 0;
    mVerticalTimer = 0;
    mIdleTimer = 0;
    mSphereHitTimer = 0;
    mStickPitch = 0.0f;
    mStickRoll = 0.0f;
    mSpeed = 0.0f;
    mPitch = 0.0f;
    mFrontVec.zero();
    mUpVec.zero();
    mCelestialSphere = nullptr;
    mSpinTimer = 0;
    mRingBoostTimer = 0;
    mRingChargeTimer = 0;
    mBlurOffset = 0.0f;
    mEndKind = 0;
    mRingBoostLength = 0;
    mRingBoostScale = 0.0f;
    mPushTimer = 0;
    mPushVelocity.zero();
    mSideTilt = 0.0f;
    mFrontTilt = 0.0f;
    NebulaMtxIdentity(mTiltMtx);
    mTrailLength = 0;
    mTrailIndex = 0;
    mTrailTexture = nullptr;
    mBrakeTimer = 0;
    mBraking = 0;

    for (u32 i = 0; i < cTrailNum; i++) {
        mTrailPos[i].zero();
        mTrailSide[i].zero();
    }
}

void MarioFoo::init() {
    mTrailTexture = new GameTexture(MR::getTexture(MR::getModelResourceHolder(mActor), "FooLine.bti"));
}

bool MarioFoo::start() {
    mStickPitch = 0.0f;
    mStickRoll = 0.0f;
    mSpeed = 0.0f;
    mSpinTimer = 30;
    mBraked = 0;
    mTrailLength = 0;
    mTrailIndex = 0;
    mRingBoostTimer = 0;
    mRingChargeTimer = 0;
    mBlurOffset = 0.0f;
    mIdleTimer = 0;
    mSphereHitTimer = 0;
    mPushTimer = 0;
    mPushVelocity.zero();
    mSideTilt = 0.0f;
    mFrontTilt = 0.0f;
    mFrameCount = 0;
    mPitch = cNum_start_f1;
    mFrontVec = getFrontVec();
    MR::normalize(&mFrontVec);

    stopAnimationUpper(nullptr);
    getPlayer()->mMovementStates.onGround = false;
    getPlayer()->mMovementStates.squat = false;
    mPitchTimer = 0;
    if (checkLvlA()) {
        mJetHeldFromStart = 1;
    }

    changeAnimation("フーファイター飛行開始", "フーファイター飛行");
    startPadVib(2);
    mCelestialSphere = MR::getAreaObj("CelestrialSphere", getTrans());
    mBrakeTimer = 0;
    mBraking = 0;
    return true;
}

bool MarioFoo::update() {
    mFrameCount++;
    if (checkTrgZ()) {
        playSound("声尻ドロップ");
        playSound("フーブレーキ");
        mBraked = 1;
        mEndKind = 3;
    }

    if (!checkLvlA()) {
        mJetHeldFromStart = 0;
    }

    if (mBraked) {
        return false;
    }

    mUpVec = -getGravityVec();
    jet();
    if (getPlayer()->mHitStates.jet) {
        if (mTrailLength) {
            mTrailLength--;
        }
    } else {
        mTrailPos[mTrailIndex] = getTrans();
        Vec3f leftHand;
        Vec3f rightHand;
        Vec3f handDirection;
        mActor->getRealPos("HandL", &leftHand);
        mActor->getRealPos("HandR", &rightHand);
        handDirection = rightHand - leftHand;
        MR::normalizeOrZero(&handDirection);
        mTrailSide[mTrailIndex] = handDirection;
        if (mTrailLength < cTrailNum) {
            mTrailLength++;
        }

        mTrailIndex = (mTrailIndex + 1) % cTrailNum;
    }

    f32 speedRatio = 1.0f - MR::clamp(mSpeed / mActor->getConst().getTable()->mSwimFrontMaxSpeed, 0.0f, 1.0f);
    mStickPitch = mStickPitch * mActor->getConst().getTable()->mSwimRotXIne + getStickY() * (1.0f - mActor->mConst->getTable()->mSwimRotXIne);
    mStickRoll = mStickRoll * mActor->getConst().getTable()->mSwimRotZIne + getStickX() * (1.0f - mActor->mConst->getTable()->mSwimRotZIne);
    if (!mIdleTimer && mPitchTimer) {
        mPitch += speedRatio * (mStickPitch * mActor->getConst().getTable()->mSwimRotSpeedX);
    }

    if (!checkLvlA() && !checkLvlZ() && !mPitchTimer && mSpeed < ::cTurnMotionSpeed) {
        if (!mPushTimer) {
            mIdleTimer++;
        }

        if (mIdleTimer >= 120) {
            mIdleTimer = 120;
        }
    } else {
        mIdleTimer = 0;
    }

    f32 blend;
    f32 targetAngle = 0.0f;
    if (MR::isNearZero(getStickY(), cNum_update_f1)) {
        f32 ratio = mIdleTimer / cNum_update_f2;
        if (ratio > 1.0f) {
            ratio = 1.0f;
        }

        targetAngle = mPitch + ratio * (::cNeutralAngleWait - mPitch);
    } else {
        if (getStickY() > 0.0f) {
            f32 slowRatio = 0.0f;
            f32 ratio = mIdleTimer / cNum_update_f3;
            if (mSpeed < cNum_update_f4) {
                slowRatio = 1.0f;
            } else if (mSpeed > cNum_update_f5) {
                slowRatio = 0.0f;
            } else {
                slowRatio = 1.0f - (mSpeed - cNum_update_f6) / cNum_update_f7;
            }

            if (ratio < slowRatio) {
                ratio = slowRatio;
            }

            ratio = MR::clamp(ratio, 0.0f, 1.0f);
            f32 limit = ratio * ::cLimitAngleWait + (1.0f - ratio) * ::cLimitAngleSink;
            if (getPlayer()->mVerticalSpeed < cNum_update_f8) {
                limit = ::cNeutralAngleWait;
            }

            targetAngle = ::cNeutralAngleWait + (limit - ::cNeutralAngleWait) * getStickY();
        } else if (getStickY() < 0.0f) {
            targetAngle = ::cNeutralAngleWait + (::cUpperAngleWait - ::cNeutralAngleWait) * -getStickY();
        }
    }

    blend = cNum_update_f9;
    if (mSpeed > cNum_update_f10) {
        blend = cNum_update_f11 - cNum_update_f12 * (cNum_update_f13 / mSpeed);
        if (blend < 0.0f) {
            blend = 0.0f;
        }
    }

    blend *= mActor->getConst().getTable()->mSwimXJetRotRatio;
    if (getStickP() == 0.0f) {
        targetAngle = ::cLimitAngleSink;
        blend *= cNum_update_f14;
    }

    if (getPlayer()->mHitStates.jet) {
        targetAngle = cNum_update_f15;
        if (getStickY() > cNum_update_f16) {
            f32 ratio = cNum_update_f17 * (getStickY() - cNum_update_f18);
            targetAngle = ::cLimitAngleSink * ratio + targetAngle * (1.0f - ratio);
        } else if (getStickY() < -cNum_update_f19) {
            f32 ratio = cNum_update_f20 * (-getStickY() - cNum_update_f21);
            targetAngle = ::cUpperAngleWait * ratio + targetAngle * (1.0f - ratio);
        }

        blend = cNum_update_f22;
    }

    mPitch = mPitch * (1.0f - blend) + targetAngle * blend;
    bool stopTurn = true;
    if (mIdleTimer && !mPushTimer && getStickY() > 0.0f) {
        f32 angle = cNum_update_f23 / mActor->getConst().getTable()->mSwimTiltReal;
        f32 stick = getStickY();
        targetAngle = cNum_update_f24 + stick * (angle - cNum_update_f25);
        if (mPitch < targetAngle) {
            f32 ratio = mActor->getConst().getTable()->mSwimTiltSpd;
            mPitch = mPitch * ratio + targetAngle * (1.0f - ratio);
        } else {
            mPitch = mPitch * (1.0f - blend) + targetAngle * blend;
        }

        stopTurn = false;
    }

    if (stopTurn) {
        stopAnimation("水泳ターン下");
    }

    mPitch = MR::clamp(mPitch, ::cUpperAngleWait, ::cLimitAngleSink);

    f32 turnRatio = speedRatio + mActor->getConst().getTable()->mSwimRotSpeedZStop;
    MR::rotAxisVecRad(mFrontVec, -mUpVec, &mFrontVec, turnRatio * (mStickRoll * mActor->mConst->getTable()->mSwimRotSpeedZ));
    MR::vecKillElement(mFrontVec, mUpVec, &mFrontVec);
    MR::normalize(&mFrontVec);
    Vec3f side = mUpVec.cross(mFrontVec);
    MR::normalize(&side);
    getPlayer()->setSideVec(side);
    Vec3f front;
    MR::rotAxisVecRad(mFrontVec, side, &front, mPitch);
    Vec3f head(getPlayer()->mHeadVec);
    Vec3f oldFront(getFrontVec());
    Vec3f blendedFront;
    MR::vecBlendSphere(oldFront, front, &blendedFront, cNum_update_f26);
    getPlayer()->setFrontVecKeepSide(blendedFront);
    spin();

    Vec3f direction;
    direction = getPlayer()->mPostureUpVec;
    Vec3f velocity = direction * mSpeed;
    f32 vertical = MR::vecKillElement(velocity, mUpVec, &velocity);
    f32 ratio;
    if (mVerticalTimer > 25) {
        ratio = (50 - mVerticalTimer) / cNum_update_f27;
    } else {
        ratio = mVerticalTimer / cNum_update_f28;
    }

    f32 verticalRatio = ratio + (1.0f - ratio) * mActor->getConst().getTable()->mSwimSpdYratio;
    velocity += mUpVec * vertical * verticalRatio;
    addVelocity(velocity);
    if (mPushTimer) {
        mPushTimer--;
        addVelocity(mPushVelocity);
        if (mPushTimer < 120) {
            mPushVelocity.x *= cNum_update_f29;
            mPushVelocity.y *= cNum_update_f30;
            mPushVelocity.z *= cNum_update_f31;
        }
    }

    if (mPitchTimer) {
        mPitchTimer--;
    }

    if (mVerticalTimer) {
        mVerticalTimer--;
    }

    updateTilt();

    if (mCelestialSphere != nullptr) {
        Vec3f center;
        MR::calcSpherePos(&center, mCelestialSphere);
        f32 radius = MR::getSphereRadius(mCelestialSphere);
        Vec3f distance = getTrans() - center;
        if (!mSphereHitTimer && distance.length() > radius) {
            mSphereHitTimer = 30;
            hitWall(-distance, nullptr);
        } else if (mSphereHitTimer) {
            mSphereHitTimer--;
        }
    }

    return true;
}

bool MarioFoo::notice() {
    if (getNoticedStatus() == MarioStatus_FpView) {
        return true;
    }

    return getNoticedStatus() == MarioStatus_Swim;
}

bool MarioFoo::close() {
    stopEffect("フーマリオブレーキ左");
    stopEffect("フーマリオブレーキ右");
    stopEffect("フーマリオグロー左");
    stopEffect("フーマリオグロー右");
    playEffect("フーマリオ解除左");
    playEffect("フーマリオ解除右");
    stopAnimationUpper(nullptr);
    setYangleOffset(0.0f);
    u32 upperJoint = getAnimator()->getUpperJointID();
    setJointGlobalMtx(upperJoint, nullptr);

    switch (mEndKind) {
    case 0:
        changeAnimation("飛び込み失敗回転着地");
        break;
    case 1:
        changeAnimation("フーファイター着地");
        break;
    case 2:
        stopAnimation(nullptr);
        break;
    case 3:
        changeAnimation("フーファイター解除");
        getPlayer()->mJumpVec = Vec3f(0.0f, 0.0f, 0.0f);
        getPlayer()->mActionStates.fooFlying = true;
        break;
    }

    if (getPlayer()->mMovementStates.jumping) {
        changeAnimation(nullptr, "落下");
    } else {
        changeAnimation(nullptr, "基本");
    }

    Mario* player = getPlayer();
    player->mJumpStartPos = player->mPosition;
    getPlayer()->forceSetHeadVecKeepSide(-getGravityVec());
    changeAnimationInterpoleFrame(6);
    mActor->setBlendMtxTimer(4);
    getPlayer()->unlockGroundCheck(this);
    mActor->mBindOn = true;
    mActor->resetWaterLife();
    mBlurOffset = 0.0f;
    getPlayer()->mActionStates.swimExited = true;
    return true;
}

const Vec3f& MarioFoo::getGravityVec() const {
    return MarioModule::getGravityVec();
}

void MarioFoo::jet() {
    f32 acceleration = calcRingAcc();
    f32 speed;
    if (acceleration != 1.0f) {
        speed = acceleration * mActor->getConst().getTable()->mSwimFrontMaxSpeed;
    } else {
        speed = mActor->getConst().getTable()->mSwimFrontJetSpeed;
    }

    f32 speedUp = cNum_jet_f1;
    f32 slowDown = cNum_jet_f2;
    if (checkLvlA() && !mJetHeldFromStart) {
        speed = 0.0f;
        slowDown = cNum_jet_f3;
        mBrakeTimer++;
        if (!isAnimationRun("フーファイタースピン")) {
            changeAnimation("フーファイター静止");
        }

        playEffect("フーマリオブレーキ左");
        playEffect("フーマリオブレーキ右");
        stopEffect("フーマリオグロー左");
        stopEffect("フーマリオグロー右");
        if (!mBraking) {
            playSound("フーブレーキ");
        }

        mBraking = 1;
        getPlayer()->mHitStates.jet = true;
    } else if (mFrameCount < 8) {
        mBrakeTimer++;
        mBraking = 1;
        mSpeed = 0.0f;
    } else if (mBrakeTimer) {
        if (mBrakeTimer < 8) {
            mBrakeTimer = 0;
            mBraking = 0;
        } else {
            if (mBraking) {
                playEffect("共通ひこうきブースト");
                playSound("フー加速");
                mBraking = 0;
                if (isAnimationRun("フーファイター静止")) {
                    changeAnimation("フーファイター飛行再開");
                }
            }

            if (mBrakeTimer > 20) {
                mBrakeTimer = 20;
            }

            mBrakeTimer--;
            speed *= cNum_jet_f4;
            speedUp = cNum_jet_f5;
        }

        stopAnimation("フーファイター静止");
        playEffect("フーマリオグロー左");
        playEffect("フーマリオグロー右");
        stopEffect("フーマリオブレーキ左");
        stopEffect("フーマリオブレーキ右");
    }

    f32 soundSpeed = cNum_jet_f6 * (1.0f + getStickY());
    if (!mBraking) {
        playSound("フー飛行中", soundSpeed);
    }

    playSound("フー滞空中", soundSpeed);
    if (mSpeed < speed) {
        if (mSpeed < 1.0f) {
            mSpeed = 1.0f;
        }

        mSpeed *= speedUp;
        if (mRingBoostTimer) {
            mSpeed *= cNum_jet_f7;
        }
    } else if (mSpeed > speed) {
        mSpeed *= slowDown;
    } else {
        mSpeed = speed;
    }
}

void MarioFoo::updateTilt() {
    f32 sideTilt;
    f32 blend = cNum_updateTilt_f1;
    sideTilt = getStickX() * FloatUtil::PI() / cNum_updateTilt_f2;
    f32 frontTilt = 0.0f;
    if (!mIdleTimer) {
        frontTilt = getStickY() * FloatUtil::PI() / cNum_updateTilt_f3;
        if (getStickY() > 0.0f && mPitch <= cNum_updateTilt_f4) {
            frontTilt = getStickY() * FloatUtil::PI() / cNum_updateTilt_f5;
        }
    } else if (getStickY() < 0.0f) {
        frontTilt = getStickY() * FloatUtil::PI() / mActor->getConst().getTable()->mSwimTiltZup;
    } else if (getStickY() > 0.0f) {
        frontTilt = getStickY() * FloatUtil::PI() / mActor->getConst().getTable()->mSwimTiltZdown;
    } else if (mIdleTimer) {
        f32 ratio = mIdleTimer / cNum_updateTilt_f6;
        if (ratio > 1.0f) {
            ratio = 1.0f;
        }

        frontTilt -= (FloatUtil::PI() / cNum_updateTilt_f7) * ratio;
    } else {
        frontTilt = 0.0f;
    }

    mSideTilt = blend * sideTilt + (1.0f - blend) * mSideTilt;
    mFrontTilt = cNum_updateTilt_f8 * frontTilt + cNum_updateTilt_f9 * mFrontTilt;
    Mtx34f sideMtx;
    Mtx34f frontMtx;
    NebulaMtxRotRad(sideMtx, 'X', mSideTilt);
    NebulaMtxRotRad(frontMtx, 'Z', mFrontTilt);
    NebulaMtxConcat(sideMtx, frontMtx, mTiltMtx);
    u32 upperJoint = getAnimator()->getUpperJointID();
    setJointGlobalMtx(upperJoint, mTiltMtx);
}

void MarioFoo::hitWall(const Vec3f& rNormal, HitSensor* pSensor) {
    if (sendPunch(pSensor, false) != true) {
        mBraked = 1;
        if (MR::diffAngleAbs(rNormal, -getGravityVec()) <= cNum_hitWall_f1) {
            if (MR::diffAngleAbs(rNormal, -getPlayer()->mPostureUpVec) <= cNum_hitWall_f2) {
                mEndKind = 0;
            } else {
                mEndKind = 1;
            }
        } else {
            mEndKind = 2;
        }

        playSound("フーブレーキ");
    }
}

f32 MarioFoo::getStickY() const {
    return MarioModule::getStickY();
}

void MarioFoo::spin() {
    if (!mPushTimer) {
        if (mActor->isRequestSpin() && !mSpinTimer) {
            mSpinTimer = 20;
            if (!isAnimationRun("フーファイタースピン") && !checkLvlA()) {
                playSound("声スピン");
                playSound("スピンジャンプ");
            }

            changeAnimation("フーファイタースピン");
            MarioActor* actor = mActor;
            if (!actor->mPunchHitTimer) {
                actor->mPunchFrame = 0;
                actor->mAirPunchHitCount = 0;
            }

            actor->mPunchHitTimer = 30;
            mActor->mSpinIntervalTimer = 60;
        }

        if (mSpinTimer) {
            mSpinTimer--;
        }
    }
}

bool MarioFoo::passRing(const HitSensor* pSensor) {
    const Vec3f& center = pSensor->mPosition;
    if (!mRingBoostTimer) {
        mRingChargeTimer = mActor->getConst().getTable()->mSwimRingDashChargeTime;
        changeAnimation("リングダッシュ準備");
    }

    if (mRingChargeTimer) {
        getPlayer()->push((center - getTrans()) * cNum_passRing_f1);
    }

    const DashRing* ring = static_cast< const DashRing* >(pSensor->mHost);
    mRingBoostLength = ring->mMaxDuration;
    mRingBoostScale = ring->mSpeedScale;
    mRingBoostTimer = ring->mBoostTime;
    return true;
}

f32 MarioFoo::calcRingAcc() {
    if (mRingBoostTimer) {
        if (mRingChargeTimer) {
            mRingChargeTimer--;
            if (mRingChargeTimer == 1) {
                MarioActor* actor = mActor;
                actor->mFlashFogTimer = actor->getConst().getTable()->mStarPieceFogTime;
                actor->mFlashFogRate = 1.0f;
                actor->mFlashFogColor.set(255, 255, 255, 0);
                actor->mFlashFogOn = false;
            }

            if (!mRingChargeTimer) {
                startPadVib(3);
                changeAnimation("リングダッシュ");
            }

            return 1.0f;
        }

        mBlurOffset += cNum_calcRingAcc_f1;
        if (mBlurOffset > cNum_calcRingAcc_f2) {
            mBlurOffset = cNum_calcRingAcc_f3;
        }

        u16 duration = mRingBoostLength;
        u16 remaining = mRingBoostTimer;
        f32 acceleration = mRingBoostScale;
        if (remaining < duration) {
            f32 ratio = static_cast< f32 >(remaining) / static_cast< f32 >(duration);
            acceleration = (1.0f - ratio) + acceleration * ratio;
        }

        mRingBoostTimer--;
        return acceleration;
    }

    if (mBlurOffset > 0.0f) {
        mBlurOffset -= cNum_calcRingAcc_f4;
    } else {
        mBlurOffset = 0.0f;
    }

    return 1.0f;
}

namespace {
    void sendVertex(VertexPT*& rVtx, const Vec3f& rPosition, const Vec2f& rCoord) {
        *rVtx++ = {rPosition.x, rPosition.y, rPosition.z, rCoord.x, rCoord.y};
    }
}  // namespace

void MarioFoo::draw3D() const {
    Vec3f beamVertices[4];
    Vec3f leftEdge[2];
    Vec3f rightEdge[2];
    Vec3f nextLeftEdge[2];
    Vec3f nextRightEdge[2];

    if (getPlayer()->mHitStates.jet) {
        SiteMaterial material;
        TDDraw::setupNative(&material, 1, 2, 0);
        material.setShader(NEBULA_SHADER_TEX_ALPHA_LERP);
        material.constants[9] = cNum_draw3D_f1;
        material.setTexture(0, mTrailTexture);
        const Color8 color0(255, 32, 32, 128);
        material.setColor(0, color0.r, color0.g, color0.b, color0.a);
        const Color8 color1(255, 64, 64, 255);
        material.setColor(4, color1.r, color1.g, color1.b, color1.a);

        Vec3f center = mActor->mCenterPos + getPlayer()->getUpVec() * cNum_draw3D_f2;
        Vec3f back = -getPlayer()->getUpVec();
        Vec3f side(getPlayer()->mSideVec);
        Vec3f up;
        up.cross(back, side);
        MR::normalizeOrZero(&up);
        Vec3f front = -back;
        f32 length = (cTrailNum - mTrailLength) / static_cast< f32 >(cTrailNum);

        beamVertices[0] = center - side * cNum_draw3D_f3;
        beamVertices[1] = center - side * cNum_draw3D_f4 + front * cNum_draw3D_f5 * length;
        beamVertices[2] = center - side * cNum_draw3D_f6 + front * cNum_draw3D_f7 * length;
        beamVertices[3] = center - side * cNum_draw3D_f8;
        TypedDraw site(material, nullptr, MR::getCameraViewMtx());
        VertexPT* vtx = site.vertices< VertexPT >(NEBULA_PRIMITIVE_QUADS, 4);
        sendVertex(vtx, beamVertices[0], Vec2f(0.0f, 0.0f));
        sendVertex(vtx, beamVertices[1], Vec2f(cNum_draw3D_f9, 0.0f));
        sendVertex(vtx, beamVertices[2], Vec2f(cNum_draw3D_f10, 1.0f));
        sendVertex(vtx, beamVertices[3], Vec2f(0.0f, 1.0f));

        beamVertices[0] = center + side * cNum_draw3D_f11;
        beamVertices[1] = center + side * cNum_draw3D_f12 + front * cNum_draw3D_f13 * length;
        beamVertices[2] = center + side * cNum_draw3D_f14 + front * cNum_draw3D_f15 * length;
        beamVertices[3] = center + side * cNum_draw3D_f16;
        vtx = site.vertices< VertexPT >(NEBULA_PRIMITIVE_QUADS, 4);
        sendVertex(vtx, beamVertices[0], Vec2f(0.0f, 0.0f));
        sendVertex(vtx, beamVertices[1], Vec2f(cNum_draw3D_f17, 0.0f));
        sendVertex(vtx, beamVertices[2], Vec2f(cNum_draw3D_f18, 1.0f));
        sendVertex(vtx, beamVertices[3], Vec2f(0.0f, 1.0f));
    }

    const Color8 color2(255, 64, 64, 255);
    if (mTrailLength) {
        SiteMaterial material;
        TDDraw::setupNative(&material, 1, 2, 0);
        material.setShader(NEBULA_SHADER_TEX_ALPHA_LERP);
        material.constants[9] = cNum_draw3D_f19;
        material.setTexture(0, mTrailTexture);

        u32 midpoint;
        u32 count = mTrailLength;
        midpoint = count / 2;
        Vec3f previous;
        Vec2f nextTexCoords[2];
        Vec2f texCoords[2];
        for (u32 i = 0; i < count; i++) {
            u8 alpha;
            if (i > midpoint) {
                alpha = (s32)(cNum_draw3D_f20 * (static_cast< f32 >(count - i) / static_cast< f32 >(count)));
            } else {
                alpha = (s32)(cNum_draw3D_f21 * (static_cast< f32 >(i) / static_cast< f32 >(count)));
            }

            const Color8 color3(255, 0, 0, alpha);
            material.setColor(0, color3.r, color3.g, color3.b, color3.a);
            material.setColor(4, color2.r, color2.g, color2.b, color2.a);
            TypedDraw site(material, nullptr, MR::getCameraViewMtx());
            VertexPT* vtx;
            u32 index = (mTrailIndex + cTrailNum * 2 - 1 - i) % cTrailNum;
            if (i == 0) {
                u32 j;
                for (j = 1; j < count; j++) {
                    Vec3f direction = mTrailPos[(mTrailIndex + cTrailNum * 2 - 1 - j) % cTrailNum] - mTrailPos[index];
                    Vec3f horizontal;
                    MR::vecKillElement(direction, MR::getCamZdir(), &horizontal);
                    if (!MR::normalizeOrZero(&horizontal)) {
                        Vec3f width;
                        width.cross(horizontal, MR::getCamZdir());
                        width.scale(cNum_draw3D_f22);
                        leftEdge[0] = mTrailPos[index] + width - mTrailSide[index] * ::cWidth;
                        leftEdge[1] = mTrailPos[index] - width - mTrailSide[index] * ::cWidth;
                        rightEdge[0] = mTrailPos[index] + width + mTrailSide[index] * ::cWidth;
                        rightEdge[1] = mTrailPos[index] - width + mTrailSide[index] * ::cWidth;
                        break;
                    }
                }

                if (j >= count) {
                    break;
                }

                previous = mTrailPos[index];
                texCoords[0].set(0.0f, 0.0f);
                texCoords[1].set(1.0f, 0.0f);
            } else {
                f32 textureY = static_cast< f32 >(i + 1) / static_cast< f32 >(count);
                nextTexCoords[0].set(0.0f, textureY);
                nextTexCoords[1].set(1.0f, textureY);
                const Vec3f& rPosition = mTrailPos[index];
                Vec3f width(mTrailSide[index]);
                width.setLength(cNum_draw3D_f23);
                nextLeftEdge[0] = rPosition + width - mTrailSide[index] * ::cWidth;
                nextLeftEdge[1] = rPosition - width - mTrailSide[index] * ::cWidth;
                vtx = site.vertices< VertexPT >(NEBULA_PRIMITIVE_QUADS, 4);
                sendVertex(vtx, leftEdge[0], texCoords[0]);
                sendVertex(vtx, nextLeftEdge[0], nextTexCoords[0]);
                sendVertex(vtx, nextLeftEdge[1], nextTexCoords[1]);
                sendVertex(vtx, leftEdge[1], texCoords[1]);

                nextRightEdge[0] = rPosition + width + mTrailSide[index] * ::cWidth;
                nextRightEdge[1] = rPosition - width + mTrailSide[index] * ::cWidth;
                vtx = site.vertices< VertexPT >(NEBULA_PRIMITIVE_QUADS, 4);
                sendVertex(vtx, rightEdge[0], texCoords[0]);
                sendVertex(vtx, nextRightEdge[0], nextTexCoords[0]);
                sendVertex(vtx, nextRightEdge[1], nextTexCoords[1]);
                sendVertex(vtx, rightEdge[1], texCoords[1]);

                leftEdge[0] = nextLeftEdge[0];
                texCoords[0] = nextTexCoords[0];
                leftEdge[1] = nextLeftEdge[1];
                texCoords[1] = nextTexCoords[1];
                rightEdge[0] = nextRightEdge[0];
                rightEdge[1] = nextRightEdge[1];
                previous = rPosition;
            }
        }
    }
}

f32 MarioFoo::getBlurOffset() const {
    return mBlurOffset;
}
