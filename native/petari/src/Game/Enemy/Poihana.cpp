#include "Game/Enemy/Poihana.hpp"
#include "Game/System/NebulaFrameClock.hpp"
#include "Game/Enemy/AnimScaleController.hpp"
#include "Game/Enemy/WalkerStateBindStarPointer.hpp"
#include "Game/LiveActor/HitSensor.hpp"
#include "Game/LiveActor/ModelObj.hpp"
#include "Game/LiveActor/Nerve.hpp"
#include "Game/Util/ActorCameraUtil.hpp"
#include "Game/Util/ActorMovementUtil.hpp"
#include "Game/Util/ActorSensorUtil.hpp"
#include "Game/Util/ActorShadowUtil.hpp"
#include "Game/Util/ActorStateUtil.hpp"
#include "Game/Util/EffectUtil.hpp"
#include "Game/Util/PlacementUtil.hpp"
#include "Game/Util/LiveActorUtil.hpp"
#include "Game/Util/MapUtil.hpp"
#include "Game/Util/MathUtil.hpp"
#include "Game/Util/MtxUtil.hpp"
#include "Game/Util/ObjUtil.hpp"
#include "Game/Util/PlayerUtil.hpp"
#include "Game/Util/SoundUtil.hpp"
#include "Game/Util/StarPointerUtil.hpp"
#include <nebula_geometry.hpp>
#include "Game/Util/Tunable.hpp"
#include "Game/System/LumaCore.hpp"

NEBULA_TUNABLE_NAMED(f32, cNum_init_f1, "init.f1", 70.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_init_f2, "init.f2", 100.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_init_f3, "init.f3", 100.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_init_f4, "init.f4", 150.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_init_f5, "init.f5", 150.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_init_f6, "init.f6", 100.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_init_f7, "init.f7", 100.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_init_f8, "init.f8", 80.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_init_f9, "init.f9", 2.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_calcAndSetBaseMtx_f1, "calcAndSetBaseMtx.f1", 0.3f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeShake_f1, "exeShake.f1", 0.01f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeShake_f2, "exeShake.f2", 36.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_calcBound_f1, "calcBound.f1", 0.01f);
NEBULA_TUNABLE_NAMED(f32, cNum_calcBound_f2, "calcBound.f2", 0.9f);
NEBULA_TUNABLE_NAMED(f32, cNum_calcBound_f3, "calcBound.f3", 9.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_calcBound_f4, "calcBound.f4", 27.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_calcBound_f5, "calcBound.f5", 0.9f);
NEBULA_TUNABLE_NAMED(f32, cNum_calcBound_f6, "calcBound.f6", 18.0f);

namespace {
    NEBULA_TUNABLE(f32, sGravity, 2.0f);
    NEBULA_TUNABLE(f32, sReceivePushPower, 1.5f);
    NEBULA_TUNABLE(f32, sVelocityDownRate, 0.95f);
    NEBULA_TUNABLE(f32, sVelocityDownRateSide, 0.8f);
    NEBULA_TUNABLE(f32, sActiveRadius, 3500.0f);
    NEBULA_TUNABLE(f32, sNormalBinderRadius, 125.0f);
    NEBULA_TUNABLE(f32, sTrampleBinderRadius, 200.0f);
    NEBULA_TUNABLE(f32, sWalkSpeedMax, 5.0f);
    NEBULA_TUNABLE(f32, sWalkAccel, 0.5f);
    NEBULA_TUNABLE(f32, sWalkTurnRate, 2.0f);
    NEBULA_TUNABLE(f32, sWalkAroundRadius, 350.0f);
    static const s32 sWalkAroundBreakTime = NebulaFrameClock::stepsOfSeconds(3);
    static const s32 sWalkAroundKeepTime = NebulaFrameClock::stepsOfSeconds(2);
    NEBULA_TUNABLE(s32, sWalkAroundTurn, 2);
    NEBULA_TUNABLE(f32, sJudgeForGetUpSpeed, 10.0f);
    NEBULA_TUNABLE(f32, sGetUpRadius, 500.0f);
    NEBULA_TUNABLE(f32, sSearchRadius, 800.0f);
    NEBULA_TUNABLE(f32, sChaseSpeedMax, 10.0f);
    NEBULA_TUNABLE(f32, sChaseAccel, 0.5f);
    NEBULA_TUNABLE(f32, sChaseTurnRate, 4.0f);
    NEBULA_TUNABLE(f32, sChaseRadius, 1100.0f);
    NEBULA_TUNABLE(f32, sChargeStartDistance, 100.0f);
    NEBULA_TUNABLE(s32, sDefaultShootHeight, 1000);
    NEBULA_TUNABLE(s32, sBindOffTiming, 2);
    NEBULA_TUNABLE(s32, sBindOnTiming, 30);
    NEBULA_TUNABLE(f32, sDistanceForNeedBack, 2000.0f);
    NEBULA_TUNABLE(f32, sJudgeForGoBackRadius, 100.0f);
    static const s32 sStepForNonSearch = NebulaFrameClock::stepsOfSeconds(2);
    NEBULA_TUNABLE(f32, sShockReactionY, -30.0f);
    NEBULA_TUNABLE(f32, sDistanceForHipDropShock, 500.0f);
    NEBULA_TUNABLE(s32, sStepForSwoon, 110);
    NEBULA_TUNABLE(s32, sStepToShockLand, 12);
    NEBULA_TUNABLE(s32, sBoundTime, 40);
    NEBULA_TUNABLE(f32, sBoundAmpMax, 0.4f);
    NEBULA_TUNABLE(s32, sStepForShake, 20);
    NEBULA_TUNABLE(f32, sShakeAmpMax, 0.2f);
    static const s32 sStepForHide = NebulaFrameClock::stepsOfMilliseconds(2500);
    static const s32 sStepToAppearEffectEnd = NebulaFrameClock::stepsOfSeconds(1);
    NEBULA_TUNABLE(f32, sGravityOffsetY, 20.0f);

    NEBULA_TUNABLE_RECORD(Vec, sNormalBinderPos, {0.0f, 130.0f, 120.0f});
    NEBULA_TUNABLE_RECORD(Vec, sTrampleBinderPos, {0.0f, 150.0f, 0.0f});
};  // namespace

namespace NrvPoihana {
    NEW_NERVE_ONEND(PoihanaNrvNonActive, Poihana, NonActive, NonActive);
    NEW_NERVE(PoihanaNrvWait, Poihana, Wait);
    NEW_NERVE(PoihanaNrvWalkAround, Poihana, WalkAround);
    NEW_NERVE(PoihanaNrvSleepStart, Poihana, SleepStart);
    NEW_NERVE(PoihanaNrvSleep, Poihana, Sleep);
    NEW_NERVE(PoihanaNrvGetUp, Poihana, GetUp);
    NEW_NERVE(PoihanaNrvSearch, Poihana, Search);
    NEW_NERVE(PoihanaNrvChasePlayer, Poihana, ChasePlayer);
    NEW_NERVE(PoihanaNrvShootUpCharge, Poihana, ShootUpCharge);
    NEW_NERVE_ONEND(PoihanaNrvShootUp, Poihana, ShootUp, ShootUp);
    NEW_NERVE(PoihanaNrvGoBack, Poihana, GoBack);
    NEW_NERVE(PoihanaNrvShock, Poihana, Shock);
    NEW_NERVE(PoihanaNrvSwoon, Poihana, Swoon);
    NEW_NERVE(PoihanaNrvSwoonLand, Poihana, SwoonLand);
    NEW_NERVE(PoihanaNrvRecover, Poihana, Recover);
    NEW_NERVE(PoihanaNrvShake, Poihana, Shake);
    NEW_NERVE(PoihanaNrvDrown, Poihana, Drown);
    NEW_NERVE(PoihanaNrvHide, Poihana, Hide);
    NEW_NERVE(PoihanaNrvAppear, Poihana, Appear);
    NEW_NERVE_ONEND(PoihanaNrvPointerSwoon, Poihana, PointerSwoon, PointerSwoon);
};  // namespace NrvPoihana

Poihana::Poihana(const char* pName)
    : LiveActor(pName), mAnimScaleCtrl(), mBindStarPointer(), mCamInfo(), mBindedActor(), mLaunchIntensity(), mRespawnPos(0.0f), mHomePos(0.0f),
      mFrontVec(0.0f, 0.0f, 1.0f), mBoundTimer(-1), mBehavior(Behavior_Normal), mCanDrown(), mWaterColumn(), mIsActive() {
    lumacore::setClass(this, lumacore::kClassId< Poihana >);
}

void Poihana::init(const TableIter& rIter) {
    MR::initDefaultPos(this, rIter);
    MR::initActorCamera(this, rIter, &mCamInfo);
    initModelManagerWithAnm("Poihana", nullptr, false);

    MR::connectToSceneEnemy(this);

    initHitSensor(2);
    MR::addHitSensorPriorBinder(this, "binder", 8, ::sNormalBinderRadius, ::sNormalBinderPos);
    MR::addHitSensorAtJoint(this, "body", "Body", ATYPE_KILLER_TARGET_ENEMY, 8, cNum_init_f1, Vec3f(0.0f, 0.0f, 0.0f));

    bool useSmallBinder = false;
    MR::getPlacementArg7NoInit(rIter, &useSmallBinder);

    if (useSmallBinder) {
        initBinder(cNum_init_f2, cNum_init_f3, 0);
    } else {
        initBinder(cNum_init_f4, cNum_init_f5, 0);
    }

    mIsActive = true;
    MR::setGroupClipping(this, rIter, 0x20);
    initEffectKeeper(1, nullptr);
    initSound(4, false);

    MR::initStarPointerTarget(this, cNum_init_f6, Vec3f(0.0f, cNum_init_f7, 0.0f));
    mAnimScaleCtrl = new AnimScaleController(nullptr);
    mBindStarPointer = new WalkerStateBindStarPointer(this, mAnimScaleCtrl);

    MR::initShadowVolumeSphere(this, cNum_init_f8);
    MR::addToAttributeGroupSearchTurtle(this);

    mRespawnPos.set(mPosition);
    mHomePos.set(mRespawnPos);

    s32 launchIntensity = ::sDefaultShootHeight;
    MR::getPlacementArg0NoInit(rIter, &launchIntensity);
    // A negative height would launch it into the ground.
    if (launchIntensity < 0) {
        launchIntensity = 0;
    }
    mLaunchIntensity = NebulaSqrt(::sGravity * launchIntensity);

    MR::getPlacementArg3NoInit(rIter, &mBehavior);
    MR::getPlacementArg4NoInit(rIter, &mCanDrown);

    if (mCanDrown) {
        mWaterColumn = MR::createModelObjMapObj("エフェクト水柱", "WaterColumn", getBaseMtx());
        mWaterColumn->mScale.set(cNum_init_f9);
        makeActorDead();
    }

    MR::tryCreateMirrorActor(this, "Poihana");

    if (mBehavior == Behavior_Sleep) {
        initNerve(GET_NERVE(Poihana, PoihanaNrvSleep));
    } else {
        initNerve(GET_NERVE(Poihana, PoihanaNrvWait));
    }

    makeActorAppeared();
}

void Poihana::initAfterPlacement() {
    Pos3f baseMtx;
    MR::makeMtxUpNoSupportPos(&baseMtx, -mGravity, mPosition);
    MR::setBaseTRMtx(this, baseMtx);
    MR::calcFrontVec(&mFrontVec, this);
    MR::trySetMoveLimitCollision(this);
}

void Poihana::control() {
    if (!isNerve(GET_NERVE(Poihana, PoihanaNrvHide))) {
        mAnimScaleCtrl->updateNerve();
        calcBound();
        controlVelocity();
        calcMyGravity();

        if (!tryDrown() && !tryPointerSwoon() && tryHipDropShock()) {
            return;
        }
    }
}

void Poihana::calcAndSetBaseMtx() {
    Pos3f baseMtx;
    MR::calcMtxFromGravityAndZAxis(&baseMtx, this, mGravity, mFrontVec);

    if (isNerveTypeWalkOrWait()) {
        MR::blendMtx(getBaseMtx(), baseMtx.toMtxPtr(), cNum_calcAndSetBaseMtx_f1, baseMtx.toMtxPtr());
    }

    MR::setBaseTRMtx(this, baseMtx);

    Vec3f scale = mAnimScaleCtrl->mScale * mScale;
    MR::setBaseScale(this, scale);
}

void Poihana::exeNonActive() {
    if (MR::isFirstStep(this)) {
        mVelocity.zero();
        MR::offBind(this);
        MR::offCalcShadow(this, nullptr);
        MR::offCalcAnim(this);

        mIsActive = false;

        MR::invalidateHitSensors(this);
    }

    if (MR::isNearPlayerAnyTime(this, ::sActiveRadius)) {
        if (mBehavior == Behavior_Sleep) {
            setNerve(GET_NERVE(Poihana, PoihanaNrvSleep));
        } else {
            setNerve(GET_NERVE(Poihana, PoihanaNrvWait));
        }
    }
}

void Poihana::endNonActive() {
    MR::onBind(this);
    MR::onCalcShadow(this, nullptr);
    MR::onCalcAnim(this);

    mIsActive = true;

    MR::setSensorOffset(this, "binder", ::sNormalBinderPos);
    MR::setSensorRadius(this, "binder", ::sNormalBinderRadius);
    MR::validateHitSensor(this, "binder");
    MR::validateHitSensors(this);
}

void Poihana::exeWait() {
    if (MR::isFirstStep(this)) {
        MR::startBckNoInterpole(this, "Wait");
        MR::setSensorOffset(this, "binder", ::sNormalBinderPos);
        MR::setSensorRadius(this, "binder", ::sNormalBinderRadius);
        MR::validateHitSensor(this, "binder");
    }

    if (MR::isNearPlayer(this, ::sSearchRadius)) {
        setNerve(GET_NERVE(Poihana, PoihanaNrvSearch));
    } else if (MR::isGreaterStep(this, ::sWalkAroundBreakTime)) {
        setNerve(GET_NERVE(Poihana, PoihanaNrvWalkAround));
    } else {
        tryNonActive();
    }
}

void Poihana::exeWalkAround() {
    if (MR::isFirstStep(this)) {
        if (!MR::isBckPlaying(this, "Walk")) {
            MR::startBck(this, "Walk");
        }

        mRandDir = MR::getRandom(-::sWalkAroundTurn, ::sWalkAroundTurn);
    }

    MR::rotateVecDegree(&mFrontVec, mGravity, mRandDir);
    mVelocity.scaleAdd(::sWalkAccel, mFrontVec, mVelocity);

    if (isNeedForBackHome()) {
        setNerve(GET_NERVE(Poihana, PoihanaNrvGoBack));
    } else if (MR::isNearPlayer(this, ::sSearchRadius)) {
        setNerve(GET_NERVE(Poihana, PoihanaNrvSearch));
    } else if (MR::isGreaterStep(this, ::sWalkAroundKeepTime)) {
        if (mBehavior == Behavior_Sleep) {
            setNerve(GET_NERVE(Poihana, PoihanaNrvSleepStart));
        } else {
            setNerve(GET_NERVE(Poihana, PoihanaNrvWait));
        }
    }
}

void Poihana::exeSleepStart() {
    if (MR::isFirstStep(this)) {
        MR::startBck(this, "SleepStart");
        MR::startSound(this, "SE_EV_POIHANA_SLEEP_START");
        MR::invalidateHitSensor(this, "binder");
    }

    if (MR::isBckStopped(this)) {
        setNerve(GET_NERVE(Poihana, PoihanaNrvSleep));
    }
}

void Poihana::exeSleep() {
    if (MR::isFirstStep(this)) {
        MR::startBck(this, "Sleep");
        MR::invalidateHitSensor(this, "binder");
    }

    if (isNeedForGetUp()) {
        setNerve(GET_NERVE(Poihana, PoihanaNrvGetUp));
    } else {
        tryNonActive();
    }
}

void Poihana::exeGetUp() {
    if (MR::isFirstStep(this)) {
        MR::startBck(this, "GetUp");
        MR::startSound(this, "SE_EV_POIHANA_WAKEUP");
        MR::startSound(this, "SE_EM_POIHANA_WAKEUP");
    }

    if (MR::isBckStopped(this)) {
        MR::setSensorOffset(this, "binder", ::sNormalBinderPos);
        MR::setSensorRadius(this, "binder", ::sNormalBinderRadius);
        MR::validateHitSensor(this, "binder");
        setNerve(GET_NERVE(Poihana, PoihanaNrvWait));
    }
}

void Poihana::exeSearch() {
    if (MR::isFirstStep(this)) {
        MR::startBck(this, "Search");
        MR::startSound(this, "SE_EV_POIHANA_FIND");
    }

    MR::turnDirectionToTargetUseGroundNormalDegree(this, &mFrontVec, *MR::getPlayerPos(), ::sChaseTurnRate);

    if (MR::isBckStopped(this)) {
        setNerve(GET_NERVE(Poihana, PoihanaNrvChasePlayer));
    }
}

void Poihana::exeChasePlayer() {
    if (MR::isFirstStep(this)) {
        MR::startBck(this, "Run");
        MR::setSensorOffset(this, "binder", ::sNormalBinderPos);
        MR::setSensorRadius(this, "binder", ::sNormalBinderRadius);
        MR::validateHitSensor(this, "binder");
    }

    MR::turnDirectionToTargetUseGroundNormalDegree(this, &mFrontVec, *MR::getPlayerPos(), ::sChaseTurnRate);
    mVelocity.scaleAdd(::sChaseAccel, mFrontVec, mVelocity);

    if (isNeedForBackHome()) {
        setNerve(GET_NERVE(Poihana, PoihanaNrvGoBack));
    }
}

void Poihana::exeShootUpCharge() {
    if (MR::isFirstStep(this)) {
        MR::startBck(this, "ThrowStart");
    }

    Vec3f& rGravity = mGravity;
    f32 dot = rGravity.dot(mVelocity);
    mVelocity.scale(dot, rGravity);

    if (MR::isBckStopped(this)) {
        setNerve(GET_NERVE(Poihana, PoihanaNrvShootUp));
    }
}

void Poihana::exeShootUp() {
    if (MR::isFirstStep(this)) {
        MR::startBck(this, "Throw");
        MR::startSound(this, "SE_EV_POIHANA_SHOOT_UP");
        MR::startActorCameraNoTarget(this, mCamInfo);
    }

    Vec3f& rGravity = mGravity;
    f32 dot = rGravity.dot(mVelocity);
    mVelocity.scale(dot, rGravity);

    if (MR::isStep(this, ::sBindOffTiming)) {
        endBind();
        MR::invalidateHitSensor(this, "binder");
    }

    if (MR::isStep(this, ::sBindOnTiming)) {
        MR::setSensorOffset(this, "binder", ::sNormalBinderPos);
        MR::setSensorRadius(this, "binder", ::sNormalBinderRadius);
        MR::validateHitSensor(this, "binder");
    }

    if (MR::isBckStopped(this)) {
        if (mBehavior == Behavior_NewHome) {
            mHomePos = mPosition;
            setNerve(GET_NERVE(Poihana, PoihanaNrvWalkAround));
        } else {
            setNerve(GET_NERVE(Poihana, PoihanaNrvGoBack));
        }
    }
}

void Poihana::endShootUp() {
    endBind();

    MR::setSensorOffset(this, "binder", ::sNormalBinderPos);
    MR::setSensorRadius(this, "binder", ::sNormalBinderRadius);
    MR::validateHitSensor(this, "binder");

    MR::endActorCamera(this, mCamInfo, true);
}

void Poihana::exeGoBack() {
    if (MR::isFirstStep(this) && !MR::isBckPlaying(this, "Walk")) {
        MR::startBck(this, "Walk");
    }

    MR::turnDirectionToTargetUseGroundNormalDegree(this, &mFrontVec, mHomePos, ::sWalkTurnRate);
    mVelocity.scaleAdd(::sWalkAccel, mFrontVec, mVelocity);

    if (MR::isNearPlayer(this, ::sSearchRadius) && MR::isGreaterStep(this, ::sStepForNonSearch)) {
        setNerve(GET_NERVE(Poihana, PoihanaNrvSearch));
    } else if (MR::isNear(this, mHomePos, ::sJudgeForGoBackRadius)) {
        setNerve(GET_NERVE(Poihana, PoihanaNrvWait));
    }
}

void Poihana::exeShock() {
    if (MR::isFirstStep(this)) {
        MR::startBck(this, "PunchDamage");
        MR::startBlowHitSound(this);
        MR::invalidateHitSensor(this, "binder");
    }

    if (MR::isOnGround(this) && MR::isGreaterStep(this, ::sStepToShockLand)) {
        setNerve(GET_NERVE(Poihana, PoihanaNrvSwoonLand));
    }
}

void Poihana::exeSwoonLand() {
    if (MR::isFirstStep(this)) {
        MR::startBckNoInterpole(this, "SwoonLand");
        MR::startSound(this, "SE_EV_POIHANA_SWOON");
    }

    if (MR::isBckStopped(this)) {
        setNerve(GET_NERVE(Poihana, PoihanaNrvSwoon));
    }
}

void Poihana::exeSwoon() {
    if (MR::isFirstStep(this)) {
        MR::startBck(this, "Swoon");
    }

    MR::startLevelSound(this, "SE_EM_LV_SWOON_S");

    if (MR::isStep(this, ::sStepForSwoon)) {
        setNerve(GET_NERVE(Poihana, PoihanaNrvRecover));
    }
}

void Poihana::exeRecover() {
    if (MR::isFirstStep(this)) {
        MR::startBck(this, "Recover");
        MR::startSound(this, "SE_EM_POIHANA_RECOVER");
        MR::startSound(this, "SE_EV_POIHANA_RECOVER");
        mScale.set(1.0f);
    }

    if (MR::isBckStopped(this)) {
        MR::setSensorOffset(this, "binder", ::sNormalBinderPos);
        MR::setSensorRadius(this, "binder", ::sNormalBinderRadius);
        MR::validateHitSensor(this, "binder");
        setNerve(GET_NERVE(Poihana, PoihanaNrvWait));
    }
}

void Poihana::exeShake() {
    f32 amplitude = ::sShakeAmpMax - cNum_exeShake_f1 * getNerveStep();
    f32 scale = MR::sinDegree(getNerveStep() * cNum_exeShake_f2) * amplitude + 1.0f;
    mScale.set(scale);

    if (MR::isStep(this, ::sStepForShake)) {
        mScale.set(1.0f);
        setNerve(GET_NERVE(Poihana, PoihanaNrvSearch));
    }
}

void Poihana::exeDrown() {
    if (MR::isFirstStep(this)) {
        MR::startBck(this, "Drown");
        MR::invalidateClipping(this);
        MR::invalidateHitSensors(this);
        MR::startSound(this, "SE_EM_FALL_INTO_WATER_S");

        mWaterColumn->appear();
        MR::tryStartAllAnim(mWaterColumn, "WaterColumn");
    }

    if (MR::isBckStopped(this)) {
        setNerve(GET_NERVE(Poihana, PoihanaNrvHide));
    }
}

void Poihana::exeHide() {
    if (MR::isFirstStep(this)) {
        MR::startSound(this, "SE_EM_EXPLODE_S_WATER");
        MR::emitEffect(this, "DeathWater");
        MR::hideModel(this);
    }

    if (MR::isStep(this, ::sStepForHide)) {
        setNerve(GET_NERVE(Poihana, PoihanaNrvAppear));
    }
}

void Poihana::exeAppear() {
    if (MR::isFirstStep(this)) {
        MR::resetPosition(this, mRespawnPos);
        MR::emitEffect(this, "Appear");
    }

    if (MR::isLessStep(this, ::sStepToAppearEffectEnd)) {
        MR::startLevelSound(this, "SE_EM_LV_POIHANA_REVIVE_EFFECT");
    }

    if (MR::isStep(this, ::sStepToAppearEffectEnd)) {
        MR::showModel(this);
        MR::startBck(this, "Appear");
        MR::startSound(this, "SE_EM_POIHANA_REVIVE_APPEAR");
    } else if (MR::isBckStopped(this) && MR::isGreaterStep(this, ::sStepToAppearEffectEnd)) {
        MR::validateClipping(this);
        MR::validateHitSensors(this);
        MR::setSensorOffset(this, "binder", ::sNormalBinderPos);
        MR::setSensorRadius(this, "binder", ::sNormalBinderRadius);
        MR::validateHitSensor(this, "binder");
        setNerve(GET_NERVE(Poihana, PoihanaNrvWait));
    }
}

void Poihana::exePointerSwoon() {
    if (MR::isFirstStep(this)) {
        MR::invalidateHitSensor(this, "binder");
    }

    MR::updateActorStateAndNextNerve(this, mBindStarPointer, GET_NERVE(Poihana, PoihanaNrvWait));
}

void Poihana::endPointerSwoon() {
    mBindStarPointer->kill();

    MR::setSensorOffset(this, "binder", ::sNormalBinderPos);
    MR::setSensorRadius(this, "binder", ::sNormalBinderRadius);
    MR::validateHitSensor(this, "binder");
}

void Poihana::attackSensor(HitSensor* pSender, HitSensor* pReceiver) {
    if (!MR::isSensorPlayer(pReceiver) && !MR::isSensorEnemy(pReceiver) && !MR::isSensorMapObj(pReceiver)) {
        return;
    }

    if (!MR::isSensorEnemy(pSender)) {
        return;
    }

    if (MR::isSensorPlayer(pReceiver)) {
        contactMario(pSender, pReceiver);
    }

    if (!MR::sendMsgPush(pReceiver, pSender)) {
        return;
    }

    if (MR::isSensorPlayer(pReceiver)) {
        const bool isStunned = isNerve(GET_NERVE(Poihana, PoihanaNrvShock)) || isNerve(GET_NERVE(Poihana, PoihanaNrvSwoonLand)) ||
                               isNerve(GET_NERVE(Poihana, PoihanaNrvSwoon)) || isNerve(GET_NERVE(Poihana, PoihanaNrvRecover));

        if (isStunned) {
            return;
        }
    }

    Vec3f pushVelocity;
    pushVelocity.sub(mPosition, pReceiver->mHost->mPosition);

    MR::normalizeOrZero(&pushVelocity);

    if (mVelocity.dot(pushVelocity) < 0.0f) {
        const Vec3f& rVelocity = mVelocity;
        mVelocity.scaleAdd(-pushVelocity.dot(rVelocity), pushVelocity, rVelocity);
    }
}

bool Poihana::receiveMsgPlayerAttack(u32 msg, HitSensor* pSender, HitSensor* pReceiver) {
    if (MR::isMsgStarPieceAttack(msg)) {
        return true;
    }

    if (MR::isMsgPlayerTrample(msg) || MR::isMsgPlayerHipDrop(msg)) {
        const bool isStunned = isNerve(GET_NERVE(Poihana, PoihanaNrvShock)) || isNerve(GET_NERVE(Poihana, PoihanaNrvSwoonLand)) ||
                               isNerve(GET_NERVE(Poihana, PoihanaNrvSwoon)) || isNerve(GET_NERVE(Poihana, PoihanaNrvRecover));
        const bool isAsleep = isNerve(GET_NERVE(Poihana, PoihanaNrvSleepStart)) || isNerve(GET_NERVE(Poihana, PoihanaNrvSleep)) ||
                              isNerve(GET_NERVE(Poihana, PoihanaNrvGetUp));

        if (isStunned || isAsleep) {
            startBound();
            MR::startSound(this, "SE_EV_POIHANA_TRAMPLE");

            if (MR::isMsgPlayerHipDrop(msg)) {
                MR::sendMsgAwayJump(pSender, pReceiver);
            }

            return true;
        }

        MR::setSensorOffset(this, "binder", ::sTrampleBinderPos);
        MR::setSensorRadius(this, "binder", ::sTrampleBinderRadius);
        MR::validateHitSensor(this, "binder");
    }

    if (MR::isMsgPlayerHitAll(msg) && tryShock()) {
        MR::stopSceneForDefaultHit(3);
        return true;
    }

    return false;
}

bool Poihana::receiveMsgEnemyAttack(u32 msg, HitSensor* /*pSender*/, HitSensor* /*pReceiver*/) {
    if (MR::isMsgExplosionAttack(msg) && tryShock()) {
        return true;
    }

    return false;
}

bool Poihana::receiveMsgPush(HitSensor* pSender, HitSensor* /*pReceiver*/) {
    if (MR::isSensorEnemy(pSender) || MR::isSensorMapObj(pSender)) {
        Vec3f pushOffset;
        pushOffset.sub(mPosition, pSender->mHost->mPosition);
        MR::normalizeOrZero(&pushOffset);
        mVelocity.scaleAdd(::sReceivePushPower, pushOffset, mVelocity);

        return true;
    }

    return false;
}

bool Poihana::receiveOtherMsg(u32 msg, HitSensor* pSender, HitSensor* pReceiver) {
    if (MR::isMsgAutoRushBegin(msg) && MR::isSensorPlayer(pSender)) {
        if (isNerve(GET_NERVE(Poihana, PoihanaNrvShootUpCharge))) {
            return false;
        }

        if (!MR::isOnGroundPlayer()) {
            if (!tryToStartBind(pSender)) {
                return false;
            }

            setNerve(GET_NERVE(Poihana, PoihanaNrvShootUp));
            return true;
        }

        if (isNerve(GET_NERVE(Poihana, PoihanaNrvShootUp))) {
            if (getNerveStep() >= ::sBindOnTiming) {
                return false;
            }

            return tryToStartBind(pSender);
        }

        if (!isNerve(GET_NERVE(Poihana, PoihanaNrvShootUpCharge)) && MR::isNear(pSender, pReceiver, ::sChargeStartDistance)) {
            setNerve(GET_NERVE(Poihana, PoihanaNrvShootUpCharge));
            return false;
        }

        return false;
    } else if (msg == ACTMES_UPDATE_BASEMTX && mBindedActor != nullptr) {
        updateBindActorMtx();
        return true;
    }

    return false;
}

bool Poihana::tryToStartBind(HitSensor* pSender) {
    if (mBindedActor != nullptr) {
        return false;
    }

    LiveActor* pBindedActor = pSender->mHost;
    if (MR::isInWater(pBindedActor, Vec3f(0.0f, 0.0f, 0.0f))) {
        return false;
    }

    MR::tryRumblePadMiddle(this, PLAYER_1);
    mBindedActor = pSender->mHost;
    MR::startBckPlayer("Rise");
    MR::invalidateClipping(this);
    return true;
}

void Poihana::updateBindActorMtx() {
    Pos3f binderMtx;
    MR::makeMtxTR(binderMtx.toMtxPtr(), mBindedActor);
    MR::setBaseTRMtx(mBindedActor, binderMtx);
}

void Poihana::endBind() {
    MR::validateClipping(this);

    if (MR::isPlayerInRush()) {
        Vec3f jumpVec;
        jumpVec.scale(-mLaunchIntensity, mGravity);
        MR::endBindAndPlayerJump(this, jumpVec, 0);
        MR::startSound(this, "SE_PM_HELI_JUMP");
    }

    mBindedActor = nullptr;
}

void Poihana::startBound() {
    mBoundTimer = 0;
    mScale.x = 1.0f;
    mScale.y = 1.0f;
    mScale.z = 1.0f;
}

void Poihana::calcBound() {
    if (mBoundTimer == -1) {
        return;
    }

    if (mBoundTimer >= ::sBoundTime) {
        mScale.set(1.0f, 1.0f, 1.0f);
        mBoundTimer = -1;
        return;
    }

    mBoundTimer++;

    f32 amplitude = ::sBoundAmpMax - mBoundTimer * cNum_calcBound_f1;

    f32 scaleX = amplitude * cNum_calcBound_f2 * MR::sinDegree(mBoundTimer * cNum_calcBound_f3) + 1.0f;
    f32 scaleY = amplitude * MR::sinDegree(mBoundTimer * cNum_calcBound_f4) + 1.0f;
    f32 scaleZ = amplitude * cNum_calcBound_f5 * MR::sinDegree(mBoundTimer * cNum_calcBound_f6) + 1.0f;
    mScale.set(scaleX, scaleY, scaleZ);
}

void Poihana::contactMario(HitSensor* pSender, HitSensor* pReceiver) {
    if (isNerveTypeWalkOrWait() || isNerve(GET_NERVE(Poihana, PoihanaNrvShootUpCharge)) || isNerve(GET_NERVE(Poihana, PoihanaNrvShootUp))) {
        if (!isBackAttack(pReceiver)) {
            return;
        }

        if (MR::sendMsgEnemyAttackFlipWeak(pReceiver, pSender)) {
            setNerve(GET_NERVE(Poihana, PoihanaNrvShake));
        }
    } else if (isNerve(GET_NERVE(Poihana, PoihanaNrvSleep))) {
        setNerve(GET_NERVE(Poihana, PoihanaNrvGetUp));
    }
}

void Poihana::controlVelocity() {
    if (isNerve(GET_NERVE(Poihana, PoihanaNrvNonActive))) {
        return;
    }

    Vec3f gravity = MR::isBindedGround(this) ? -*MR::getGroundNormal(this) : mGravity;

    Vec3f frontVec(mFrontVec);
    MR::turnVecToPlane(&mFrontVec, frontVec, gravity);

    if (MR::isBindedGround(this)) {
        Vec3f addVel;
        const Vec3f& rVelocity = mVelocity;
        addVel.scaleAdd(-mFrontVec.dot(rVelocity), mFrontVec, rVelocity);
        addVel.scale(::sVelocityDownRateSide);

        mVelocity.scale(mFrontVec.dot(mVelocity), mFrontVec);
        mVelocity.add(addVel);

        if (mVelocity.dot(gravity) > 0.0f) {
            const Vec3f& rVelocity = mVelocity;
            mVelocity.scaleAdd(-gravity.dot(rVelocity), gravity, rVelocity);
        }

        mVelocity.mult(::sVelocityDownRate);
    }

    mVelocity.scaleAdd(::sGravity, gravity, mVelocity);

    if (!isNerve(GET_NERVE(Poihana, PoihanaNrvShock))) {
        f32 magVel = isNerve(GET_NERVE(Poihana, PoihanaNrvChasePlayer)) ? ::sChaseSpeedMax : ::sWalkSpeedMax;

        if (mVelocity.length() > magVel) {
            mVelocity.setLength(magVel);
        }

        if (MR::isNearZero(mVelocity)) {
            mVelocity.zero();
        }
    }
}

void Poihana::calcMyGravity() {
    if (!mIsActive) {
        return;
    }

    Vec3f upVec, gravityPos;
    MR::calcUpVec(&upVec, this);
    gravityPos.scaleAdd(::sGravityOffsetY, upVec, mPosition);
    MR::calcGravity(this, gravityPos);
}

bool Poihana::tryNonActive() {
    if (!MR::isStageStateScenarioOpeningCamera()) {
        bool notNear = !MR::isNearPlayerAnyTime(this, ::sActiveRadius);
        if (notNear && MR::isBindedGround(this)) {
            setNerve(GET_NERVE(Poihana, PoihanaNrvNonActive));
            return true;
        }
    }

    return false;
}

bool Poihana::tryDrown() {
    if (!mCanDrown) {
        return false;
    }

    if (isNerve(GET_NERVE(Poihana, PoihanaNrvAppear))) {
        return false;
    }

    if (isNerve(GET_NERVE(Poihana, PoihanaNrvDrown))) {
        return false;
    }

    if (!MR::isInWater(this, Vec3f(0.0f, 0.0f, 0.0f))) {
        return false;
    }

    setNerve(GET_NERVE(Poihana, PoihanaNrvDrown));
    return true;
}

bool Poihana::tryPointerSwoon() {
    if (!isNerveTypeWalkOrWait()) {
        return false;
    }

    if (!mBindStarPointer->tryStartPointBind()) {
        return false;
    }

    setNerve(GET_NERVE(Poihana, PoihanaNrvPointerSwoon));
    return true;
}

bool Poihana::tryShock() {
    if (isNerve(GET_NERVE(Poihana, PoihanaNrvDrown)) || isNerve(GET_NERVE(Poihana, PoihanaNrvHide)) || isNerve(GET_NERVE(Poihana, PoihanaNrvAppear)) ||
        isNerve(GET_NERVE(Poihana, PoihanaNrvShock)) || isNerve(GET_NERVE(Poihana, PoihanaNrvSwoonLand))) {
        return false;
    }

    mVelocity.scale(::sShockReactionY, mGravity);
    setNerve(GET_NERVE(Poihana, PoihanaNrvShock));
    return true;
}

bool Poihana::tryHipDropShock() {
    return MR::isNearPlayerAnyTime(this, ::sDistanceForHipDropShock) && MR::isPlayerHipDropLand() && tryShock();
}

bool Poihana::isNerveTypeWalkOrWait() const {
    return isNerve(GET_NERVE(Poihana, PoihanaNrvWait)) || isNerve(GET_NERVE(Poihana, PoihanaNrvSearch)) ||
           isNerve(GET_NERVE(Poihana, PoihanaNrvWalkAround)) || isNerve(GET_NERVE(Poihana, PoihanaNrvChasePlayer)) ||
           isNerve(GET_NERVE(Poihana, PoihanaNrvGoBack));
}

bool Poihana::isNeedForBackHome() const {
    if (isNerve(GET_NERVE(Poihana, PoihanaNrvWalkAround))) {
        return !MR::isNear(this, mHomePos, ::sWalkAroundRadius);
    }

    if (isNerve(GET_NERVE(Poihana, PoihanaNrvChasePlayer))) {
        if (mBehavior == Behavior_NewHome) {
            return !MR::isNearPlayer(this, ::sChaseRadius);
        }

        return !MR::isNear(this, mHomePos, ::sDistanceForNeedBack) || !MR::isNearPlayer(this, ::sChaseRadius);
    }

    return false;
}

bool Poihana::isNeedForGetUp() const {
    // A near player who moves fast or spins wakes it.
    if (!MR::isNearPlayer(this, ::sGetUpRadius)) {
        return false;
    }

    return MR::getPlayerVelocity()->length() >= ::sJudgeForGetUpSpeed || MR::isPlayerSwingAction();
}

bool Poihana::isBackAttack(HitSensor* pMySensor) const {
    Vec3f frontVec;
    MR::calcFrontVec(&frontVec, this);
    frontVec.negate();

    Vec3f offset;
    offset.sub(pMySensor->mPosition, mPosition);
    return offset.dot(frontVec) > 0.0f;
}
