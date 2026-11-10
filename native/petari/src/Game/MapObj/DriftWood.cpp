#include "Game/MapObj/DriftWood.hpp"
#include "Game/System/NebulaFrameClock.hpp"
#include "Game/LiveActor/HitSensor.hpp"
#include "Game/MapObj/MapObjActorInitInfo.hpp"
#include "Game/Util.hpp"
#include "Game/Util/ActorMovementUtil.hpp"
#include "Game/Util/ActorSensorUtil.hpp"
#include "Game/Util/EffectUtil.hpp"
#include "Game/Util/LiveActorUtil.hpp"
#include "Game/Util/MathUtil.hpp"
#include "Game/Util/ModelUtil.hpp"
#include "Game/Util/ObjUtil.hpp"
#include "Game/Util/RailUtil.hpp"
#include "Game/Util/SoundUtil.hpp"
#include <nebula_geometry.hpp>
#include <nebula_math.h>
#include "Game/Util/Tunable.hpp"

NEBULA_TUNABLE_NAMED(f32, cNum_init_f1, "init.f1", 300.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_init_f2, "init.f2", 60.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_init_f3, "init.f3", 100.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeWait_f1, "exeWait.f1", 0.05f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeVibrate_f1, "exeVibrate.f1", 30.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeVibrate_f2, "exeVibrate.f2", 16.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeVibrate_f3, "exeVibrate.f3", 0.1f);

namespace {
    const s32 sWaveSoundStepMin = NebulaFrameClock::stepsOfMilliseconds(500);
    const s32 sWaveSoundStepMax = NebulaFrameClock::stepsOfMilliseconds(1500);
    NEBULA_TUNABLE(s32, sVibrateStep, 45);
    NEBULA_TUNABLE(f32, sRailSpeed, 3.0f);
    NEBULA_TUNABLE(f32, sAttackRange, 140.0f);
};  // namespace

DriftWood::DriftWood(const char* pName)
    : MapObjActor(pName), mRailDirection(0.0f, 0.0f, 1.0f), mRailClipping(0.0f, 0.0f, 0.0f), mVibrateOffset(0.0f, 0.0f, 0.0f), mWaveSoundTimer() {
}

namespace NrvDriftWood {
    NEW_NERVE(DriftWoodNrvWait, DriftWood, Wait);
    NEW_NERVE(DriftWoodNrvWaitLand, DriftWood, Wait);
    NEW_NERVE(DriftWoodNrvVibrateLand, DriftWood, Vibrate);
    NEW_NERVE(DriftWoodNrvVibrateTakeOff, DriftWood, Vibrate);
};  // namespace NrvDriftWood

void DriftWood::init(const TableIter& rIter) {
    MapObjActor::init(rIter);
    MapObjActorInitInfo info;
    info.setupDefaultPos();
    info.setupConnectToScene();
    info.setupHitSensor();
    info.setupHitSensorParam(8, cNum_init_f1, Vec3f(0.0f, -cNum_init_f2, 0.0f));
    info.setupEffect(nullptr);
    info.setupSound(4);
    info.setupNerve(GET_NERVE(DriftWood, DriftWoodNrvWait));
    initialize(rIter, info);
    MR::moveCoordAndTransToNearestRailPos(this);
    mRailDirection.set(MR::getRailDirection(this));
    getSensor(nullptr)->setType(ATYPE_EYE);
    mWaveSoundTimer = MR::getRandom(::sWaveSoundStepMin, ::sWaveSoundStepMax);
    f32 radius = 0.0f;
    MR::calcModelBoundingRadius(&radius, this);
    MR::initAndSetRailClipping(&mRailClipping, this, cNum_init_f3, radius);
    MR::tryCreateMirrorActor(this, mObjectName);
}

void DriftWood::exeWait() {
    if (MR::isFirstStep(this) && !MR::isEffectValid(this, "Ripple")) {
        MR::emitEffect(this, "Ripple");
    }

    MR::moveCoordAndFollowTrans(this, ::sRailSpeed);
    MR::rotateVecDegree(&mRailDirection, mGravity, cNum_exeWait_f1);

    tryVibrate();

    if (mWaveSoundTimer <= 0) {
        MR::startSound(this, "SE_OJ_DRIFT_WOOD_WAVE");
        mWaveSoundTimer = MR::getRandom(::sWaveSoundStepMin, ::sWaveSoundStepMax);
    } else {
        mWaveSoundTimer--;
    }
}

void DriftWood::exeVibrate() {
    if (MR::isFirstStep(this)) {
        MR::startSound(this, "SE_OJ_DRIFT_WOOD_PLAYER_ON");
        mVibrateOffset.zero();
    }

    MR::moveCoord(this, ::sRailSpeed);

    f32 wave = cNum_exeVibrate_f1 * MR::sinDegree(cNum_exeVibrate_f2 * getNerveStep());
    f32 nerveValue = MR::calcNerveValue(this, ::sVibrateStep, cNum_exeVibrate_f3, 1.0f);

    mVibrateOffset.scale((1.0f - nerveValue) * wave, *getGravity());
    mPosition.add(MR::getRailPos(this), mVibrateOffset);

    if (!tryVibrate() && MR::isGreaterEqualStep(this, ::sVibrateStep)) {
        mVibrateOffset.zero();
        setNerve(MR::isOnPlayer(this) ? static_cast< const Nerve* >(GET_NERVE(DriftWood, DriftWoodNrvWaitLand))
                                      : GET_NERVE(DriftWood, DriftWoodNrvWait));
    }
}

void DriftWood::connectToScene(const MapObjActorInitInfo&) {
    MR::connectToSceneCollisionMapObj(this);
}

void DriftWood::calcAndSetBaseMtx() {
    Pos3f mtx;
    MR::calcMtxFromGravityAndZAxis(&mtx, this, mGravity, mRailDirection);
    MR::setBaseTRMtx(this, mtx);
}

void DriftWood::attackSensor(HitSensor* pSender, HitSensor* pReceiver) {
    if (MR::isSensorPlayer(pReceiver)) {
        Vec3f delta;
        delta.sub(pSender->mPosition, pReceiver->mPosition);

        Vec3f upVec;
        MR::calcUpVec(&upVec, this);

        Vec3f side = delta.killElement(upVec);

        if (!(side.length() > ::sAttackRange + pReceiver->mRadius) &&!MR::isInWater(pReceiver->mHost, Vec3f(0.0f, 0.0f, 0.0f))) {
            MR::sendMsgEnemyAttackFlip(pReceiver, pSender);
            MR::startSound(this, "SE_OJ_DRIFT_WOOD_ATTACK");
        }
    } else if (MR::isSensorMapObj(pReceiver) && MR::sendMsgEnemyAttack(pReceiver, pSender) && MR::isOnPlayer(this)) {
        MR::tryRumblePadMiddle(this, PLAYER_1);
    }
}

bool DriftWood::tryVibrate() {
    if (MR::isOnPlayer(this)) {
        if (isNerve(GET_NERVE(DriftWood, DriftWoodNrvVibrateTakeOff)) || isNerve(GET_NERVE(DriftWood, DriftWoodNrvWait))) {
            setNerve(GET_NERVE(DriftWood, DriftWoodNrvVibrateLand));
            return true;
        }
    } else if (isNerve(GET_NERVE(DriftWood, DriftWoodNrvWaitLand))) {
        setNerve(GET_NERVE(DriftWood, DriftWoodNrvVibrateTakeOff));
        return true;
    }

    return false;
}
