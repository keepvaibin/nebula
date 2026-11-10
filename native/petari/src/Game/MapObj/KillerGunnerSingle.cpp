#include "Game/MapObj/KillerGunnerSingle.hpp"
#include "Game/System/NebulaFrameClock.hpp"
#include "Game/LiveActor/HitSensor.hpp"
#include "Game/LiveActor/Nerve.hpp"
#include "Game/Util.hpp"
#include "Game/Util/ActorCameraUtil.hpp"
#include "Game/Util/ActorMovementUtil.hpp"
#include "Game/Util/ActorSensorUtil.hpp"
#include "Game/Util/EffectUtil.hpp"
#include "Game/Util/LiveActorUtil.hpp"
#include "Game/Util/ObjUtil.hpp"
#include "Game/Util/SoundUtil.hpp"
#include "Game/Util/Tunable.hpp"

NEBULA_TUNABLE_NAMED(f32, cNum_init_f1, "init.f1", 250.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_init_f2, "init.f2", 100.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeShoot_f1, "exeShoot.f1", 500.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeShoot_f2, "exeShoot.f2", 1000.0f);

// The step counts of this file's functions, as tunables: "<file>.<function>.<k>".
NEBULA_TUNABLE_NAMED(s32, cStep_exeShoot_1, "exeShoot.1", 40);

namespace {
    NEBULA_TUNABLE_RECORD(Vec, sShellPos, {0.0f, 0.0f, 700.0f});
};  // namespace

namespace NrvKillerGunnerSingle {
    NEW_NERVE(HostTypeWait, KillerGunnerSingle, Wait);
    NEW_NERVE(HostTypeCharge, KillerGunnerSingle, Charge);
    NEW_NERVE(HostTypeShoot, KillerGunnerSingle, Shoot);
    NEW_NERVE(HostTypeAttack, KillerGunnerSingle, Attack);
};  // namespace NrvKillerGunnerSingle

KillerGunnerSingle::KillerGunnerSingle(const char* pName) : LiveActor(pName) {
}

void KillerGunnerSingle::init(const TableIter& rIter) {
    MR::initDefaultPos(this, rIter);
    initModelManagerWithAnm("KillerGunnerSingle", nullptr, false);
    MR::connectToSceneEnvironment(this);
    initHitSensor(2);
    MR::addHitSensorMapObj(this, "body", 16, 0.0f, Vec3f(0.0f, 0.0f, 0.0f));
    MR::addHitSensorEnemy(this, "shell", 16, cNum_init_f1, Vec3f(::sShellPos));
    MR::initCollisionParts(this, "KillerGunnerSingle", getSensor("body"), nullptr);
    initEffectKeeper(0, nullptr);
    initSound(4, false);
    MR::setClippingTypeSphereContainsModelBoundingBox(this, cNum_init_f2);
    initNerve(GET_NERVE(KillerGunnerSingle, HostTypeWait));
    makeActorAppeared();
}

void KillerGunnerSingle::attackSensor(HitSensor* pSender, HitSensor* pReceiver) {
    if (isNerve(GET_NERVE(KillerGunnerSingle, HostTypeShoot))) {
        if (MR::isSensorPlayer(pReceiver)) {
            if (MR::isNear(pSender, pReceiver, pSender->mRadius)) {
                if (MR::sendMsgEnemyAttackMaximum(pReceiver, pSender)) {
                    setNerve(GET_NERVE(KillerGunnerSingle, HostTypeAttack));
                }
            }
        }
    }
}

void KillerGunnerSingle::exeWait() {
    if (MR::isFirstStep(this)) {
        getSensor("shell")->invalidate();
    }

    if (MR::isStep(this, NebulaFrameClock::stepsOfSeconds(2))) {
        setNerve(GET_NERVE(KillerGunnerSingle, HostTypeCharge));
    }
}

void KillerGunnerSingle::exeCharge() {
    if (MR::isFirstStep(this)) {
        MR::startBtk(this, "KillerGunnerSingleCharge");
    }

    MR::startLevelSound(this, "SE_OJ_LV_K_GUNNER_CHARGE");

    if (MR::isBtkStopped(this)) {
        MR::startBck(this, "KillerGunnerSingleShoot");
        MR::emitEffect(this, "KillerGunnerSingleSmoke");
        MR::startSound(this, "SE_OJ_K_GUNNER_FIRE");
        setNerve(GET_NERVE(KillerGunnerSingle, HostTypeShoot));
    }
}

void KillerGunnerSingle::exeShoot() {
    if (MR::isFirstStep(this)) {
        MR::startRumbleWithShakeCameraWeak(this, "中", "弱", cNum_exeShoot_f1, cNum_exeShoot_f2);
    }

    if (MR::isStep(this, 0)) {
        getSensor("shell")->validate();
    }

    if (MR::isStep(this, cStep_exeShoot_1)) {
        getSensor("shell")->invalidate();
    }

    if (MR::isBckStopped(this)) {
        setNerve(GET_NERVE(KillerGunnerSingle, HostTypeWait));
    }
}

inline void KillerGunnerSingle::exeAttack() {
    if (MR::isBckStopped(this)) {
        setNerve(GET_NERVE(KillerGunnerSingle, HostTypeWait));
    }
}
