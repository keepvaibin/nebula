#include "Game/MapObj/NeedlePlant.hpp"
#include "Game/LiveActor/HitSensor.hpp"
#include "Game/LiveActor/Nerve.hpp"
#include "Game/MapObj/MapObjActorInitInfo.hpp"
#include "Game/Util/ActorMovementUtil.hpp"
#include "Game/Util/ActorSensorUtil.hpp"
#include "Game/Util/EffectUtil.hpp"
#include "Game/Util/PlacementUtil.hpp"
#include "Game/Util/LiveActorUtil.hpp"
#include "Game/Util/ObjUtil.hpp"
#include "Game/Util/SoundUtil.hpp"
#include "Game/Util/StarPointerUtil.hpp"
#include "Game/Util/Tunable.hpp"
#include "Game/System/LumaCore.hpp"

NEBULA_TUNABLE_NAMED(f32, cNum_init_f1, "init.f1", 180.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_init_f2, "init.f2", 150.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_init_f3, "init.f3", 150.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_init_f4, "init.f4", 150.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_kill_f1, "kill.f1", 10.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_kill_f2, "kill.f2", 40.0f);

namespace {
    NEBULA_TUNABLE(f32, sAttackSensorSize, 70.0f);
};  // namespace

namespace NrvNeedlePlant {
    NEW_NERVE(NeedlePlantNrvWait, NeedlePlant, Wait);
    NEW_NERVE(NeedlePlantNrvShake, NeedlePlant, Shake);
};  // namespace NrvNeedlePlant

NeedlePlant::NeedlePlant(const char* pName) : MapObjActor(pName), mObjArg0(-1) {
    lumacore::setClass(this, lumacore::kClassId< NeedlePlant >);
}

void NeedlePlant::init(const TableIter& rIter) {
    MapObjActor::init(rIter);
    MapObjActorInitInfo info;
    info.setupDefaultPos();
    info.setupConnectToScene();
    info.setupAffectedScale();
    info.setupHitSensor();
    info.setupHitSensorParam(4, cNum_init_f1, Vec3f(0.0f, mScale.x * cNum_init_f2, 0.0f));
    info.setupShadow(nullptr);
    info.setupNerve(GET_NERVE(NeedlePlant, NeedlePlantNrvWait));
    initialize(rIter, info);
    initEffectKeeper(1, "NeedlePlant");
    MR::addEffectHitNormal(this, nullptr);
    initSound(4, 0);
    MR::initStarPointerTarget(this, mScale.x * cNum_init_f3, Vec3f(0.0f, mScale.x * cNum_init_f4, 0.0f));
    MR::getPlacementArg0NoInit(rIter, &mObjArg0);

    if (mObjArg0 == -1) {
        MR::declareCoin(this, 1);
    } else if (mObjArg0 == 0) {
        MR::declareStarPiece(this, 3);
    }
}

void NeedlePlant::exeWait() {
    if (MR::isFirstStep(this)) {
        MR::tryStartAllAnim(this, mObjectName);
    }
}

void NeedlePlant::exeShake() {
    if (MR::isFirstStep(this)) {
        MR::startBck(this, "Shake");
        MR::startSound(this, "SE_OJ_LEAVES_SWING");
    }

    if (MR::isBckStopped(this)) {
        setNerve(GET_NERVE(NeedlePlant, NeedlePlantNrvWait));
    }
}

void NeedlePlant::kill() {
    MR::emitEffect(this, "Break");
    MR::startSound(this, "SE_OJ_NEEDLE_PLANT_BREAK");

    if (mObjArg0 == 0) {
        MR::startSound(this, "SE_OJ_STAR_PIECE_BURST");
        MR::appearStarPiece(this, mPosition, 3, cNum_kill_f1, cNum_kill_f2, false);
    } else if (mObjArg0 == -1) {
        MR::appearCoinPop(this, mPosition, 1);
    }

    MapObjActor::kill();
}

void NeedlePlant::attackSensor(HitSensor* pSender, HitSensor* pReceiver) {
    f32 distance = MR::calcDistance(pSender, pReceiver, nullptr);
    f32 size = mScale.x * ::sAttackSensorSize;
    f32 radius = pReceiver->mRadius;

    if (distance > size + radius) {
        return;
    }

    if (MR::isSensorPlayerOrRide(pReceiver) && MR::sendMsgEnemyAttack(pReceiver, pSender)) {
        MR::emitEffectHitBetweenSensors(this, pSender, pReceiver, 0.0f, nullptr);
        setNerve(GET_NERVE(NeedlePlant, NeedlePlantNrvShake));
    } else if (MR::isSensorPlayerOrRide(pReceiver) || MR::isSensorEnemy(pReceiver)) {
        f32 radius = getSensor(nullptr)->mRadius;
        getSensor(nullptr)->mRadius = ::sAttackSensorSize * mScale.x;
        MR::sendMsgPush(pReceiver, pSender);
        getSensor(nullptr)->mRadius = radius;
    }
}

bool NeedlePlant::receiveMsgPush(HitSensor* pSender, HitSensor* /*pReceiver*/) {
    if (isNerve(GET_NERVE(NeedlePlant, NeedlePlantNrvShake))) {
        return false;
    }

    if (!MR::isSensorEnemy(pSender)) {
        return false;
    }

    setNerve(GET_NERVE(NeedlePlant, NeedlePlantNrvShake));

    return true;
}

bool NeedlePlant::receiveMsgPlayerAttack(u32 msg, HitSensor* /*pSender*/, HitSensor* /*pReceiver*/) {
    if (MR::isMsgStarPieceReflect(msg)) {
        setNerve(GET_NERVE(NeedlePlant, NeedlePlantNrvShake));

        return true;
    }

    if (MR::isMsgInvincibleAttack(msg)) {
        kill();

        return true;
    }

    return false;
}

bool NeedlePlant::receiveMsgEnemyAttack(u32 /*msg*/, HitSensor* /*pSender*/, HitSensor* /*pReceiver*/) {
    kill();

    return true;
}

void NeedlePlant::connectToScene(const MapObjActorInitInfo&) {
    MR::connectToSceneNoShadowedMapObj(this);
}
