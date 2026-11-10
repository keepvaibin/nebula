#include "Game/MapObj/Rock.hpp"
#include "Game/System/NebulaFrameClock.hpp"
#include "Game/LiveActor/HitSensor.hpp"
#include "Game/LiveActor/ModelObj.hpp"
#include "Game/LiveActor/Nerve.hpp"
#include "Game/MapObj/RockCreator.hpp"
#include "Game/Util.hpp"
#include "Game/Util/ActorMovementUtil.hpp"
#include "Game/Util/ActorSensorUtil.hpp"
#include "Game/Util/CameraUtil.hpp"
#include "Game/Util/EffectUtil.hpp"
#include "Game/Util/GravityUtil.hpp"
#include "Game/Util/Table.hpp"
#include "Game/Util/LiveActorUtil.hpp"
#include "Game/Util/MapUtil.hpp"
#include "Game/Util/MathUtil.hpp"
#include "Game/Util/MtxUtil.hpp"
#include "Game/Util/ObjUtil.hpp"
#include "Game/Util/RailUtil.hpp"
#include "Game/Util/SoundUtil.hpp"
#include "Game/Util/StarPointerUtil.hpp"
#include "math_types.hpp"
#include <nebula_geometry.hpp>
#include "Game/Util/Tunable.hpp"

NEBULA_TUNABLE_NAMED(f32, cNum_control_f1, "control.f1", 30.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_control_f2, "control.f2", 30.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_attackSensor_f1, "attackSensor.f1", 0.25f);
NEBULA_TUNABLE_NAMED(f32, cNum_initMapToolInfo_f1, "initMapToolInfo.f1", 180.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_initSensor_f1, "initSensor.f1", 0.3f);
NEBULA_TUNABLE_NAMED(f32, cNum_initSensor_f2, "initSensor.f2", 0.3f);
NEBULA_TUNABLE_NAMED(f32, cNum_initSensor_f3, "initSensor.f3", 0.3f);
NEBULA_TUNABLE_NAMED(f32, cNum_isInClippingRange_f1, "isInClippingRange.f1", 300.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_isInClippingRange_f2, "isInClippingRange.f2", 250.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_setBtkForEnvironmentMap_f1, "setBtkForEnvironmentMap.f1", 0.5f);
NEBULA_TUNABLE_NAMED(f32, cNum_setBtkForEnvironmentMap_f2, "setBtkForEnvironmentMap.f2", 2.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_setBtkForEnvironmentMap_f3, "setBtkForEnvironmentMap.f3", 2.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_setBtkForEnvironmentMap_f4, "setBtkForEnvironmentMap.f4", 0.9f);
NEBULA_TUNABLE_NAMED(f32, cNum_setBtkForEnvironmentMap_f5, "setBtkForEnvironmentMap.f5", 0.5f);
NEBULA_TUNABLE_NAMED(f32, cNum_setBtkForEnvironmentMap_f6, "setBtkForEnvironmentMap.f6", 2.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_appearStarPiece_f1, "appearStarPiece.f1", 10.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_appearStarPiece_f2, "appearStarPiece.f2", 40.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_moveOnRail_f1, "moveOnRail.f1", 2.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeAppear_f1, "exeAppear.f1", 180.0f);

namespace {
    inline f32 toDegree(f32 angle) {
        return _180_PI * angle;
    }

    NEBULA_TUNABLE_RECORD(Vec, cWeakSensorOffset, {0.0f, 0.0f, -150.0f});
    NEBULA_TUNABLE(f32, cBinderRadius, 225.0f);
    NEBULA_TUNABLE(f32, cSensorRadius, 225.0f);
    NEBULA_TUNABLE(f32, cGravity, 1.5f);
    NEBULA_TUNABLE(f32, cWeakGravity, 0.2f);
    NEBULA_TUNABLE(f32, cWallBreakDot, -0.5f);
    NEBULA_TUNABLE(f32, cRotateSpeedRate, 1.1f);
    NEBULA_TUNABLE(f32, cRotateCosineMax, 0.999f);
    NEBULA_TUNABLE(f32, cRumbleDistance, 1000.0f);
    NEBULA_TUNABLE(s32, cNoBindJudgeFrame, 20);
    NEBULA_TUNABLE(f32, cStarWandRadius3d, 275.0f);
    NEBULA_TUNABLE(s32, cAppearMoveFrame, 45);
    NEBULA_TUNABLE(s32, cAppearRumbleFrame, 15);
    NEBULA_TUNABLE(s32, cAppearStopFrame, 25);
    NEBULA_TUNABLE(s32, cAppearMoveFrameRock, 8);
    NEBULA_TUNABLE(s32, cAppearStopFrameRock, 4);
    NEBULA_TUNABLE(s32, cAppearInvalidBindFrame, 45);
    NEBULA_TUNABLE(f32, cAppearMoveSpeed, 7.0f);
    NEBULA_TUNABLE(f32, cAppearRumbleSpeed, 100.0f);
    NEBULA_TUNABLE(f32, cAppearRumbleAngle, 5.0f);
    NEBULA_TUNABLE(s32, cStarPieceNum, 9);
    NEBULA_TUNABLE(s32, cStarPieceNumWanwanMini, 1);
    NEBULA_TUNABLE(f32, cWeakSensorRadius, 125.0f);
    static const s32 cSeWanwanVoiceInterval = NebulaFrameClock::stepsOfSeconds(2);
    NEBULA_TUNABLE(f32, cSeWanwanVoiceDistance, 600.0f);
    NEBULA_TUNABLE(s32, cRollSoundTime, 20);
    NEBULA_TUNABLE(s32, cFreezeFrame, 20);
    NEBULA_TUNABLE(f32, cFreezeRumbleSpeed, 75.0f);
    NEBULA_TUNABLE(f32, cFreezeRumbleWidth, 5.0f);
    NEBULA_TUNABLE(f32, cNearClipDistance, 400.0f);
    NEBULA_TUNABLE(s32, cSlowMoveFrame, 150);
    NEBULA_TUNABLE(s32, cSlowMoveRumbleFrame, 20);
    NEBULA_TUNABLE(s32, cBreakStopStep, 2);
    NEBULA_TUNABLE(s32, cBreakStopFrame, 12);
    NEBULA_TUNABLE(s32, cAppearPowerStarStep, 2);
};  // namespace

namespace NrvRock {
    NEW_NERVE(RockNrvAppear, Rock, Appear);
    NEW_NERVE(RockNrvAppearMoveInvalidBind, Rock, AppearMoveInvalidBind);
    NEW_NERVE(RockNrvMove, Rock, Move);
    NEW_NERVE(RockNrvMoveInvalidBind, Rock, MoveInvalidBind);
    NEW_NERVE(RockNrvBreak, Rock, Break);
    NEW_NERVE(RockNrvFreeze, Rock, Freeze);
};  // namespace NrvRock

Rock::Rock(f32 moveSpeed, const char* pName)
    : LiveActor(pName), mCreator(), mRockType(NormalRock), mBreakModel(), mMoveSpeed(moveSpeed), mBreakModelOnRailGoal(), mSlowDownOnAttack(),
      mAppearPos(gZeroVec), mRadius(::cBinderRadius), mRotateSpeed(), mFallVelocity(gZeroVec), mGravityRate(::cGravity), mCurrentRailPoint(-1),
      mPrevPos(gZeroVec), mFront(0.0f, 0.0f, 1.0f), mAirTime(), mIsNormalGravity(), mWanwanVoiceTimer(), mFreezeTime(), mFreezePos(gZeroVec),
      mUnfreezeNerve(), mInvalidBindTime(), mAppearTime(::cAppearMoveFrameRock), mAppearAngle(), mRollSoundTimer(), mSlowDownTimer() {
    mBaseMtx.identity();
}

void Rock::init(const TableIter& rIter) {
    mRockType = Rock::getType(rIter);

    if (mRockType != NormalRock) {
        switch (mRockType) {
        case WanwanRolling:
            setName("ワンワン");
            break;
        case WanwanRollingMini:
            setName("ミニワンワン");
            break;
        case WanwanRollingGold:
            setName("ゴールドワンワン");
            MR::declarePowerStar(this);
            break;
        default:
            break;
        }

        mAppearTime = ::cAppearMoveFrame;
    }

    initMapToolInfo(rIter);
    initModel();
    MR::connectToSceneNoShadowedMapObjStrongLight(this);
    MR::initLightCtrl(this);
    initSensor();
    initBinder(mRadius, 0.0f, 0);
    initRailRider(rIter);
    initEffect();

    MR::initStarPointerTarget(this, ::cStarWandRadius3d * getRadius(), Vec3f(0, 0, 0));
    initSound(5, false);

    f32 shadowDrop;

    if (MR::getPlacementArg4NoInit(rIter, &shadowDrop)) {
        MR::initShadowVolumeCylinder(this, ::cBinderRadius * getRadius());
        MR::setShadowDropLength(this, nullptr, shadowDrop);
    } else {
        MR::initShadowVolumeSphere(this, ::cBinderRadius * getRadius());
    }

    initNerve(GET_NERVE(Rock, RockNrvAppear));
    makeActorDead();
}

void Rock::appear() {
    mFallVelocity.zero();
    mAirTime = 0;
    mGravityRate = ::cGravity;
    mCurrentRailPoint = -1;
    mInvalidBindTime = 0;
    mPosition.set(mAppearPos);
    mPrevPos.set(mAppearPos);
    mRotation.zero();

    if (!MR::isZeroGravity(this)) {
        MR::calcGravity(this);
    }

    MR::moveCoordAndTransToNearestRailPos(this);
    MR::showModel(this);
    MR::invalidateClipping(this);
    MR::validateHitSensors(this);
    MR::vecKillElement(MR::getRailDirection(this), mGravity, &mFront);
    MR::normalize(&mFront);

    if (mRockType == NormalRock) {
        setBtkForEnvironmentMap(this, "Size");

    } else if (mRockType == WanwanRolling) {
        setBtkForEnvironmentMap(this, "WanwanRolling");
    }

    LiveActor::appear();

    if (!MR::isLoopRail(this)) {
        MR::offBind(this);
        setNerve(GET_NERVE(Rock, RockNrvAppear));
    } else {
        MR::onBind(this);
        setNerve(GET_NERVE(Rock, RockNrvMove));
    }
}

void Rock::kill() {
    LiveActor::kill();

    if (mRockType == WanwanRollingMini) {
        appearStarPiece();
        MR::emitEffect(this, "MiniBreak");
        MR::startSound(this, "SE_EM_WANWANROLLMINI_EXPLOSION");
        MR::startSound(this, "SE_OJ_STAR_PIECE_BURST");
    }
}

s32 Rock::getAppearFrame() {
    return ::cAppearMoveFrame + ::cAppearRumbleFrame + ::cAppearStopFrame;
}

Rock::Type Rock::getType(const TableIter& rIter) {
    if (MR::isEqualObjectName(rIter, "WanwanRolling")) {
        return WanwanRolling;
    }

    if (MR::isEqualObjectName(rIter, "WanwanRollingMini")) {
        return WanwanRollingMini;
    }

    if (MR::isEqualObjectName(rIter, "WanwanRollingGold")) {
        return WanwanRollingGold;
    }

    return NormalRock;
}

s32 Rock::getAppearStarPieceNum(Type type) {
    return (type == WanwanRollingMini) ? ::cStarPieceNumWanwanMini : ::cStarPieceNum;
}

bool Rock::isMoveEnabled() const {
    return isNerve(GET_NERVE(Rock, RockNrvAppear)) || isNerve(GET_NERVE(Rock, RockNrvAppearMoveInvalidBind)) ||
           isNerve(GET_NERVE(Rock, RockNrvMove)) || isNerve(GET_NERVE(Rock, RockNrvMoveInvalidBind));
}

void Rock::control() {
    if (isNerve(GET_NERVE(Rock, RockNrvBreak))) {
        return;
    }

    bool updateFront;
    bool hasMoved = false;
    Vec3f front;
    front.sub(mPosition, mPrevPos);

    if (!MR::isNearZero(front)) {
        MR::normalize(&front);
        hasMoved = true;
        mPrevPos.set(mPosition);
    }

    updateFront = false;

    if (hasMoved && isMoveEnabled()) {
        updateFront = true;
    }

    if (isInClippingRange()) {
        MR::hideModel(this);
        MR::deleteEffect(this, "Smoke");

        if (mRockType == WanwanRollingGold) {
            MR::deleteEffect(this, "Light");
        }

        if (updateFront) {
            mFront.set(front);
        }
    } else {
        MR::showModel(this);

        if (updateFront) {
            Vec3f oldFront(mFront);
            MR::turnVecToVecCos(&mFront, oldFront, front, ::cRotateCosineMax, mGravity);
        }
    }

    if (MR::isOnGround(this)) {
        mFallVelocity.zero();
    }

    if (!isNerve(GET_NERVE(Rock, RockNrvMove))) {
        return;
    }

    MR::vecKillElement(MR::getRailPos(this) - mPosition, mGravity, &mVelocity);
    Vec3f grav(mGravity);
    grav *= mGravityRate;
    mFallVelocity.add(grav);
    mVelocity.add(mFallVelocity);

    if (cNum_control_f1 < mVelocity.length()) {
        mVelocity.setLength(cNum_control_f2);
    }
}

void Rock::calcAndSetBaseMtx() {
    Vec3f grav(mGravity);
    Pos3f mtx;
    calcBaseMtx(&mtx);
    MR::setBaseTRMtx(this, mtx);

    if (isNerve(GET_NERVE(Rock, RockNrvAppearMoveInvalidBind)) || (isNerve(GET_NERVE(Rock, RockNrvMove)) && MR::isOnGround(this)) ||
        isNerve(GET_NERVE(Rock, RockNrvMoveInvalidBind))) {
        Vec3f up;

        if (isNerve(GET_NERVE(Rock, RockNrvMove))) {
            up.set(*MR::getGroundNormal(this));
        } else {
            up.negate(grav);
        }

        Vec3f pos;
        pos.sub(mPosition, up * mRadius);

        if (MR::isSameDirection(up, mFront)) {
            MR::makeMtxUpNoSupportPos(&mBaseMtx, up, pos);
        } else {
            MR::makeMtxUpFrontPos(&mBaseMtx, up, mFront, pos);
        }

        mBaseMtx.scaleInline(getRadius());
    }
}

void Rock::attackSensor(HitSensor* pSender, HitSensor* pReceiver) {
    if (!isBodySensor(pSender) || isNerve(GET_NERVE(Rock, RockNrvBreak))) {
        return;
    }

    if (MR::isSensorPlayer(pReceiver) || MR::isSensorRide(pReceiver)) {
        if (!isNerve(GET_NERVE(Rock, RockNrvFreeze)) && !isNerve(GET_NERVE(Rock, RockNrvAppear))) {
            if (MR::sendMsgEnemyAttackStrong(pReceiver, pSender)) {
                if (mSlowDownOnAttack && isNerve(GET_NERVE(Rock, RockNrvMove))) {
                    mSlowDownTimer = ::cSlowMoveFrame;
                }

                rumblePadAndCamera();
                return;
            }

            MR::sendMsgPush(pReceiver, pSender);
            return;
        }

        MR::sendMsgPush(pReceiver, pSender);
        return;
    }

    if (isRockSensor(pReceiver)) {
        if (MR::sendMsgEnemyAttack(pReceiver, pSender) && mRockType != WanwanRollingGold) {
            setNerveBreak(true);
        }

        return;
    }

    if (!isNerve(GET_NERVE(Rock, RockNrvFreeze))) {
        MR::sendMsgToEnemyAttackBlowOrTrample(pReceiver, pSender, cNum_attackSensor_f1);
    } else {
        MR::sendMsgPush(pReceiver, pSender);
    }
}

bool Rock::receiveMsgPlayerAttack(u32 msg, HitSensor* /*pSender*/, HitSensor* pReceiver) {
    if (!isNerve(GET_NERVE(Rock, RockNrvBreak))) {
        if (isBodySensor(pReceiver)) {
            if (MR::isMsgInvincibleAttack(msg)) {
                setNerveBreak(true);
                return true;
            }

            if (MR::isMsgStarPieceReflect(msg)) {
                return true;
            }

            if (MR::isMsgFireBallAttack(msg)) {
                return true;
            }
        } else {
            if (mRockType == NormalRock && !isNerve(GET_NERVE(Rock, RockNrvAppear))) {
                if (MR::isMsgPlayerHitAll(msg)) {
                    setNerveBreak(true);
                    return true;
                }
            }
        }
    }

    return false;
}

bool Rock::receiveMsgEnemyAttack(u32 msg, HitSensor* pSender, HitSensor* pReceiver) {
    const bool isBodyIntact = isBodySensor(pReceiver) && !isNerve(GET_NERVE(Rock, RockNrvBreak));

    if (isBodyIntact && mRockType != WanwanRollingGold) {
        if (MR::isMsgExplosionAttack(msg) || MR::isMsgToEnemyAttackBlow(msg) || isRockSensor(pSender)) {
            setNerveBreak(true);
            return true;
        }
    }

    return false;
}

void Rock::initMapToolInfo(const TableIter& rIter) {
    MR::initDefaultPos(this, rIter);
    mAppearPos.set(mPosition);

    mRadius = getRadius() * ::cBinderRadius;
    mRotateSpeed = (mMoveSpeed * cNum_initMapToolInfo_f1 * ::cRotateSpeedRate) / (MR::pi() * mRadius);

    MR::getPlacementArg3NoInit(rIter, &mAppearTime);

    if (mRockType == NormalRock) {
        MR::getPlacementArg5NoInit(rIter, &mSlowDownOnAttack);
    }

    if (!MR::isZeroGravity(this)) {
        MR::onCalcGravity(this);
    } else {
        Pos3f mtx;
        MR::makeMtxTR(mtx, this);
        mtx.getYDir(mGravity);
        mGravity.negate();
    }
}

void Rock::initModel() {
    switch (mRockType) {
    case NormalRock:
        initModelManagerWithAnm("Rock", nullptr, false);
        mBreakModel = MR::createModelObjMapObjStrongLight("ゴロゴロ岩壊れモデル", "RockBreak", nullptr);
        mBreakModel->initWithoutIter();
        mBreakModel->mScale.set(mScale);
        mBreakModel->makeActorDead();
        break;
    case WanwanRolling:
        initModelManagerWithAnm("WanwanRolling", nullptr, false);
        mBreakModel = MR::createModelObjMapObjStrongLight("ワンワン壊れモデル", "WanwanRollingBreak", nullptr);
        mBreakModel->initWithoutIter();
        mBreakModel->mScale.set(mScale);
        mBreakModel->makeActorDead();
        break;
    case WanwanRollingMini:
        initModelManagerWithAnm("WanwanRollingMini", nullptr, false);
        break;
    case WanwanRollingGold:
        initModelManagerWithAnm("WanwanRollingGold", nullptr, false);
        mBreakModel = MR::createModelObjMapObjStrongLight("ゴールドワンワン壊れモデル", "WanwanRollingGoldBreak", nullptr);
        mBreakModel->initWithoutIter();
        mBreakModel->mScale.set(mScale);
        mBreakModel->makeActorDead();
        break;
    }

    if (mBreakModel != nullptr) {
        MR::registerDemoSimpleCastAll(mBreakModel);
    }
}

void Rock::initSensor() {
    s32 sensorType;

    if (mRockType == NormalRock) {
        initHitSensor(2);
        sensorType = ATYPE_ROCK;
    } else {
        initHitSensor(1);
        sensorType = ATYPE_WANWAN;
    }

    MR::addHitSensor(this, "body", sensorType, 16, ::cSensorRadius * (mRockType == WanwanRollingMini ? cNum_initSensor_f1 : mScale.x), Vec3f(0.0f, 0.0f, 0.0f));

    if (mRockType == NormalRock) {
        MR::addHitSensor(this, "weak", sensorType, 16, ::cWeakSensorRadius * (mRockType == WanwanRollingMini ? cNum_initSensor_f2 : mScale.x),
                         static_cast< Vec3f >(::cWeakSensorOffset) * (mRockType == WanwanRollingMini ? cNum_initSensor_f3 : mScale.x));
    }
}

void Rock::initEffect() {
    if (mRockType == WanwanRollingMini) {
        initEffectKeeper(0, "WanwanRolling");
    } else {
        initEffectKeeper(0, nullptr);
    }

    MR::setEffectHostMtx(this, "Smoke", mBaseMtx);
    MR::setEffectHostMtx(this, "Land", mBaseMtx);
}

bool Rock::isBreakByWall() const {
    if (MR::isBindedWall(this)) {
        const Vec3f& dir = MR::getRailDirection(this);
        return dir.dot(*MR::getWallNormal(this)) < ::cWallBreakDot;
    }

    return false;
}

bool Rock::isBodySensor(HitSensor* pSensor) const {
    return pSensor == getSensor("body");
}

bool Rock::move(f32 speed) {
    s32 curPnt = mCurrentRailPoint;
    MR::moveCoord(this, speed);
    mCurrentRailPoint = MR::getCurrentRailPointNo(this);

    if (curPnt != mCurrentRailPoint) {
        s32 pntArg;
        MR::getCurrentRailPointArg0WithInit(this, &pntArg);

        if (pntArg >= 0) {
            mIsNormalGravity = false;
            mGravityRate = ::cWeakGravity;
        }

        MR::getCurrentRailPointArg1WithInit(this, &pntArg);

        if (pntArg >= 0) {
            setNerve(GET_NERVE(Rock, RockNrvMoveInvalidBind));
            return false;
        }
    }

    return true;
}

void Rock::calcBaseMtx(Pos3f* pMtx) const {
    Vec3f up = -mGravity;

    if (MR::isSameDirection(up, mFront)) {
        MR::makeMtxUpNoSupportPos(pMtx, up, mPosition);
    } else {
        MR::makeMtxUpFrontPos(pMtx, up, mFront, mPosition);
    }

    Pos3f mtx;
    mtx.makeRotate(Vec3f(1.0f, 0.0f, 0.0f), getAngle());
    pMtx->concat(*pMtx, mtx);
}

bool Rock::isInClippingRange() const {
    if (MR::isJudgedToClipFrustum(mPosition, (cNum_isInClippingRange_f1 + (cNum_isInClippingRange_f2 * getRadius())))) {
        return true;
    }

    if (MR::isJudgedToNearClip(mPosition, (::cNearClipDistance * getRadius()))) {
        return true;
    }

    return false;
}

void Rock::startSoundWanwanVoice() {
    if (mRockType != NormalRock) {
        if (mWanwanVoiceTimer >= ::cSeWanwanVoiceInterval) {
            if (MR::calcDistanceToPlayer(this) <= ::cSeWanwanVoiceDistance) {
                if (mRockType == WanwanRollingMini) {
                    MR::startSound(this, "SE_EV_WANWANROLLMINI_WANWAN");
                } else {
                    MR::startSound(this, "SE_EV_WANWANROLL_WANWAN");
                }

                mWanwanVoiceTimer = 0;
            }
        } else {
            mWanwanVoiceTimer++;
        }
    }
}

void Rock::startRollLevelSound(bool resetTimer) {
    if (resetTimer) {
        mRollSoundTimer = ::cRollSoundTime;
    }

    if (mRollSoundTimer > 0) {
        if (mRockType == NormalRock) {
            MR::startLevelSound(this, "SE_OJ_LV_ROCK_MOVE");
        } else if (mRockType == WanwanRollingMini) {
            MR::startLevelSound(this, "SE_EM_LV_WANWANROLLMINI_MOVE");
        } else {
            MR::startLevelSound(this, "SE_EM_LV_WANWANROLL_MOVE");
        }

        if (MR::isBindedGroundBrake(this)) {
            MR::startLevelSound(this, "SE_OJ_LV_ROCK_MOVE_MUD");
        }

        mRollSoundTimer--;
    }

    if (mRockType == WanwanRollingGold) {
        MR::startLevelSound(this, "SE_EM_LV_WANWANROLLGOLD_GLITTER");
    }
}

bool Rock::tryFreeze(const Nerve* pNerve) {
    if (MR::isStarPointerPointing2POnPressButton(this, "弱", true, false)) {
        mFreezeTime = 0;
        mFreezePos.set(mPosition);
        mUnfreezeNerve = pNerve;
        setNerve(GET_NERVE(Rock, RockNrvFreeze));
        return true;
    }

    return false;
}

void Rock::setBtkForEnvironmentMap(LiveActor* pActor, const char* pBtkName) {
    f32 frame = 0.0f;

    if (mRockType == NormalRock) {
        if (MR::isNearZero(mScale.x - cNum_setBtkForEnvironmentMap_f1)) {
            frame = 0.0f;
        } else if (MR::isNearZero(mScale.x - cNum_setBtkForEnvironmentMap_f2)) {
            frame = cNum_setBtkForEnvironmentMap_f3;
        } else {
            frame = 1.0f;
        }
    } else if (mRockType == WanwanRolling) {
        if (MR::isNearZero(mScale.x - cNum_setBtkForEnvironmentMap_f4)) {
            frame = 1.0f;
        } else if (MR::isNearZero(mScale.x - cNum_setBtkForEnvironmentMap_f5)) {
            frame = cNum_setBtkForEnvironmentMap_f6;
        } else {
            frame = 0.0f;
        }
    }

    MR::startBtkAndSetFrameAndStop(pActor, pBtkName, frame);
}

void Rock::rumblePadAndCamera() {
    MR::startRumbleWithShakeCameraWeak(this, "強", "中", ::cRumbleDistance, ::cRumbleDistance * 2);

    f32 dist = MR::calcDistanceToPlayer(this);

    if (dist < ::cRumbleDistance) {
        MR::shakeCameraNormal();
    } else if (dist < ::cRumbleDistance * 2) {
        MR::shakeCameraWeak();
    }
}

void Rock::setNerveBreak(bool createStarPieces) {
    if (createStarPieces && (u32)mRockType <= WanwanRolling) {
        appearStarPiece();
    }

    setNerve(GET_NERVE(Rock, RockNrvBreak));
}

void Rock::updateRotateX(f32 angle) {
    mRotation.x = MR::repeatDegree(angle);
}

void Rock::appearStarPiece() {
    Vec3f pieceDir = mGravity;
    pieceDir.negate();

    if (MR::appearStarPieceToDirection(mCreator, mPosition, pieceDir, getAppearStarPieceNum(mRockType), cNum_appearStarPiece_f1, cNum_appearStarPiece_f2, false)) {
        MR::startSound(this, "SE_OJ_STAR_PIECE_BURST");
    }
}

void Rock::moveOnRail(f32 speed, f32 rotateSpeed, bool isValidBind) {
    MR::moveCoordAndFollowTrans(this, speed);
    updateRotateX(mRotation.x + rotateSpeed);

    if (isValidBind) {
        Vec3f pos;

        if (MR::getFirstPolyOnLineToMap(&pos, nullptr, mPosition, mGravity * (mRadius * cNum_moveOnRail_f1))) {
            mPosition.add(pos, mGravity * (-mRadius));
        }
    }
}

bool Rock::isForceInvalidBindSection() const {
    for (s32 idx = MR::getCurrentRailPointNo(this); idx < MR::getRailPointNum(this); idx++) {
        bool invalidBind;
        MR::getNextRailPointArg2WithInit(this, &invalidBind);

        if (invalidBind) {
            return true;
        }
    }

    return false;
}

bool Rock::tryBreakReachedGoal() {
    if (MR::isRailReachedGoal(this)) {
        if (mBreakModelOnRailGoal) {
            setNerve(GET_NERVE(Rock, RockNrvBreak));
        } else {
            makeActorDead();
        }

        return true;
    }

    return false;
}

void Rock::exeAppear() {
    f32 rotateSpeed = (::cAppearMoveSpeed * cNum_exeAppear_f1 * ::cRotateSpeedRate) / (mRadius * MR::pi());
    s32 spawnDelay = mRockType == NormalRock ? ::cAppearStopFrameRock : ::cAppearStopFrame;

    if (MR::isFirstStep(this)) {
        mAppearAngle = mRockType == WanwanRollingMini ? MR::getRandomDegree() : 0.0f;

        updateRotateX(mAppearAngle - rotateSpeed * mAppearTime);
    }

    if (MR::isLessStep(this, mAppearTime)) {
        moveOnRail(::cAppearMoveSpeed, rotateSpeed, false);

        if (mRockType != NormalRock) {
            MR::startLevelSound(this, "SE_EM_LV_WANWANROLL_STANDBY");
        }
    }

    if (MR::isStep(this, mAppearTime) && mRockType != NormalRock) {
        MR::startSound(this, "SE_EM_WANWANROLL_STANDBY_END");
    }

    if (MR::isGreaterStep(this, mAppearTime) && MR::isLessStep(this, mAppearTime + ::cAppearRumbleFrame)) {
        s32 step = getNerveStep() - mAppearTime;
        f32 rot = MR::sinDegree(MR::repeatDegree(step * ::cAppearRumbleSpeed)) * ::cAppearRumbleAngle;
        f32 rate = ::cAppearRumbleFrame - step;
        mRotation.x = rate * rot;
        mRotation.x /= ::cAppearRumbleFrame;
        f32 appearAngle = mAppearAngle;
        updateRotateX(mRotation.x + appearAngle);
    }

    if (MR::isStep(this, spawnDelay + mAppearTime + ::cAppearRumbleFrame)) {
        setNerve(GET_NERVE(Rock, RockNrvAppearMoveInvalidBind));
    }
}

void Rock::exeAppearMoveInvalidBind() {
    bool isInvalid = isForceInvalidBindSection();

    if (!MR::isHiddenModel(this)) {
        MR::emitEffect(this, "Smoke");

        if (mRockType == WanwanRollingGold) {
            MR::emitEffect(this, "Light");
        }
    }

    startRollLevelSound(true);
    startSoundWanwanVoice();

    if (tryFreeze(GET_NERVE(Rock, RockNrvAppearMoveInvalidBind))) {
        return;
    }

    moveOnRail(mMoveSpeed, mRotateSpeed, isInvalid);
    mInvalidBindTime++;

    if (!isInvalid && mInvalidBindTime > ::cAppearInvalidBindFrame) {
        MR::onBind(this);
        setNerve(GET_NERVE(Rock, RockNrvMove));
    }
}

void Rock::exeMove() {
    if (MR::isOnGround(this)) {
        if (mRockType == NormalRock && MR::isBindedGroundDamageFire(this)) {
            setNerve(GET_NERVE(Rock, RockNrvBreak));
            return;
        }

        if (!MR::isHiddenModel(this)) {
            MR::emitEffect(this, "Smoke");

            if (mRockType == WanwanRollingGold) {
                MR::emitEffect(this, "Light");
            }
        }

        if (mAirTime >= ::cNoBindJudgeFrame) {
            if (!MR::isHiddenModel(this)) {
                MR::emitEffect(this, "Land");
                rumblePadAndCamera();
            }

            if (mRockType == NormalRock) {
                MR::startSound(this, "SE_OJ_ROCK_BOUND");
            } else {
                MR::startSound(this, "SE_EM_WANWANROLL_BOUND");
            }
        }

        mAirTime = 0;
    } else {
        if (mAirTime < ::cNoBindJudgeFrame) {
            mAirTime++;
        } else {
            MR::deleteEffect(this, "Smoke");
        }
    }

    startRollLevelSound(true);
    startSoundWanwanVoice();

    if (MR::isOnGround(this)) {
        if (mIsNormalGravity && mGravityRate != ::cGravity) {
            mGravityRate = ::cGravity;
        }
    } else {
        if (!mIsNormalGravity) {
            mIsNormalGravity = true;
        }
    }

    if (isBreakByWall()) {
        if (mRockType == NormalRock) {
            setNerve(GET_NERVE(Rock, RockNrvBreak));
        } else {
            makeActorDead();
        }

        return;
    }

    if (tryBreakReachedGoal()) {
        return;
    }

    f32 moveSpeed = mMoveSpeed;

    if (mSlowDownTimer > 0) {
        moveSpeed = MR::getEaseOutValue(mSlowDownTimer, moveSpeed, 0.0f, ::cSlowMoveFrame);
        mSlowDownTimer--;
    }

    move(moveSpeed);

    // A rock with no speed does not turn.
    f32 rotateSpeed = mMoveSpeed != 0.0f ? mRotateSpeed * moveSpeed / mMoveSpeed : 0.0f;

    if (mSlowDownTimer > ::cSlowMoveFrame - ::cSlowMoveRumbleFrame) {
        s32 step = ::cSlowMoveFrame - mSlowDownTimer;
        f32 rumble = ::cAppearRumbleAngle * MR::sinDegree(MR::repeatDegree(step * ::cAppearRumbleSpeed));
        f32 remaining = ::cSlowMoveRumbleFrame - step;
        rotateSpeed += remaining * rumble / ::cSlowMoveRumbleFrame;
    }

    updateRotateX(mRotation.x + rotateSpeed);

    if (tryFreeze(GET_NERVE(Rock, RockNrvMove))) {
        return;
    }
}

void Rock::exeMoveInvalidBind() {
    if (MR::isFirstStep(this)) {
        MR::moveCoordAndTransToNearestRailPos(this);
        mVelocity.zero();
        MR::offBind(this);
        MR::deleteEffect(this, "Smoke");
    }

    startRollLevelSound(true);
    startSoundWanwanVoice();

    if (!tryBreakReachedGoal()) {
        moveOnRail(mMoveSpeed, mRotateSpeed, false);
    }
}

void Rock::exeBreak() {
    if (MR::isFirstStep(this)) {
        bool clipped = isInClippingRange();

        if (clipped || mBreakModel == nullptr) {
            makeActorDead();

            if (!clipped && mRockType == WanwanRollingMini) {
                MR::emitEffect(this, "MiniBreak");
                MR::startSound(this, "SE_EM_WANWANROLLMINI_EXPLOSION");
            }

            return;
        }

        MR::hideModel(this);
        MR::invalidateHitSensors(this);
        mRotation.zero();
        mVelocity.zero();
        mFallVelocity.zero();
        MR::deleteEffectAll(this);

        mBreakModel->mPosition.set(mPosition);

        Pos3f mtx;
        calcBaseMtx(&mtx);

        Pos3f rotMtx;
        rotMtx.makeRotate(mGravity, MR::getRandom(0.0f, TWO_PI));
        mtx.concat(rotMtx, mtx);

        Vec3f rot;
        mtx.getEulerXYZ(rot);
        mBreakModel->mRotation.x = ::toDegree(rot.x);
        mBreakModel->mRotation.y = ::toDegree(rot.y);
        mBreakModel->mRotation.z = ::toDegree(rot.z);

        mBreakModel->appear();

        MR::invalidateClipping(mBreakModel);
        MR::startBck(mBreakModel, "Break");

        if (mRockType == NormalRock) {
            MR::startSound(this, "SE_OJ_ROCK_BREAK");
        } else {
            MR::startSound(this, "SE_EM_WANWANROLL_EXPLOSION");

            if (mRockType == WanwanRolling) {
                setBtkForEnvironmentMap(mBreakModel, "WanwanRollingBreak");
            }
        }

        rumblePadAndCamera();
    }

    if (mRockType == WanwanRollingGold) {
        MR::stopSceneAtStep(this, ::cBreakStopStep, ::cBreakStopFrame);

        if (MR::isStep(this, ::cAppearPowerStarStep)) {
            MR::requestAppearPowerStar(this, mBreakModel->mPosition);
        }
    }

    if (MR::isBckStopped(mBreakModel)) {
        kill();
        mBreakModel->kill();
    }
}

void Rock::exeFreeze() {
    if (MR::isFirstStep(this)) {
        mVelocity.zero();

        const char* name;
        bool isMini = mRockType == WanwanRollingMini;
        if (isMini) {
            name = "MiniTouch";
        } else {
            name = "Touch";
        }

        MR::emitEffect(this, name);
        MR::deleteEffect(this, "Smoke");

        if (mFreezeTime == 0) {
            MR::startPointerHitSound();
        }
    }

    mFreezeTime++;
    MR::startPointerFreezeLevelSound(this);

    f32 rumbleOffset = ::cFreezeRumbleWidth * MR::cosDegree(MR::repeatDegree(mFreezeTime * ::cFreezeRumbleSpeed)) *
                       static_cast< f32 >(::cFreezeFrame - getNerveStep()) / ::cFreezeFrame;

    Vec3f rumble;
    rumble.set(MR::getCamXdir());
    rumble *= rumbleOffset;
    mPosition.add(mFreezePos, rumble);

    if (MR::isStarPointerPointing2POnPressButton(this, "弱", true, false)) {
        setNerve(GET_NERVE(Rock, RockNrvFreeze));
        return;
    }

    if (MR::isStep(this, ::cFreezeFrame)) {
        mPosition.set(mFreezePos);

        const char* name;
        bool isMini = mRockType == WanwanRollingMini;
        if (isMini) {
            name = "MiniTouch";
        } else {
            name = "Touch";
        }

        MR::deleteEffect(this, name);
        setNerve(mUnfreezeNerve);
    }
}
