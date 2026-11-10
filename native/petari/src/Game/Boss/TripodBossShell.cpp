#include "Game/Boss/TripodBossShell.hpp"
#include "Game/Boss/TripodBossAccesser.hpp"
#include "Game/Boss/TripodBossFixPartsBase.hpp"
#include "Game/LiveActor/HitSensor.hpp"
#include "Game/LiveActor/ModelObj.hpp"
#include "Game/LiveActor/Nerve.hpp"
#include "Game/Scene/SceneFunction.hpp"
#include "Game/Util/ActorMovementUtil.hpp"
#include "Game/Util/ActorSensorUtil.hpp"
#include "Game/Util/ActorSwitchUtil.hpp"
#include "Game/Util/LiveActorUtil.hpp"
#include "Game/Util/MathUtil.hpp"
#include "Game/Util/ObjUtil.hpp"
#include "Game/Util/PlayerUtil.hpp"
#include "Game/Util/SoundUtil.hpp"
#include "Game/Util/Tunable.hpp"

NEBULA_TUNABLE_NAMED(f32, cNum_init_f1, "init.f1", 300.0f);

namespace {
    NEBULA_TUNABLE(f32, sHitSensorRadius, 900.0f);
    NEBULA_TUNABLE(s32, sBreakStopStep, 4);
    NEBULA_TUNABLE(s32, sBreakStopFrame, 30);
    NEBULA_TUNABLE(f32, sBreakStopDistance, 1000.0f);
    NEBULA_TUNABLE(f32, sStartEventForceJumpPlayerPower, 40.0f);
};  // namespace

namespace NrvTripodBossShell {
    NEW_NERVE(TripodBossShellNrvNonActive, TripodBossShell, NonActive);
    NEW_NERVE(TripodBossShellNrvWait, TripodBossShell, Wait);
    NEW_NERVE(TripodBossShellNrvBreak, TripodBossShell, Break);
};  // namespace NrvTripodBossShell

TripodBossShell::TripodBossShell(const char* pName) : TripodBossFixPartsBase(pName), mBreakModel() {
}

void TripodBossShell::init(const TableIter& rIter) {
    TripodBossFixPartsBase::init(rIter);
    initModelManagerWithAnm("TripodBossShell", nullptr, false);
    MR::connectToScene(this, MR::MovementType_MapObjDecoration, MR::CalcAnimType_MapObjDecoration, MR::DrawBufferType_TripodBoss, MR::DrawType_None);
    initHitSensor(2);
    MR::addHitSensorMapObj(this, "body", 16, ::sHitSensorRadius, Vec3f(0.0f, cNum_init_f1, 0.0f));
    MR::addHitSensor(this, "killer_terget", ATYPE_BREAKABLE_CAGE, 8, ::sHitSensorRadius * mScale.x, Vec3f(0.0f, 0.0f, 0.0f));
    MR::initCollisionParts(this, "TripodBossShell", getSensor("killer_terget"), nullptr);
    initSound(4, false);

    mBreakModel = MR::createModelObjMapObjStrongLight("壊れモデル", "TripodBossShellBreak", getBaseMtx());
    mBreakModel->initWithoutIter();
    MR::invalidateClipping(mBreakModel);
    mBreakModel->makeActorDead();
    MR::addTripodBossPartsMovement(mBreakModel);

    initNerve(GET_NERVE(TripodBossShell, TripodBossShellNrvNonActive));
    MR::invalidateCollisionParts(this);
    getSensor("body")->invalidate();
    MR::useStageSwitchWriteDead(this, rIter);
    makeActorDead();
}

void TripodBossShell::kill() {
    LiveActor::kill();
    mBreakModel->kill();
}

bool TripodBossShell::receiveMsgEnemyAttack(u32 /*msg*/, HitSensor* /*pSender*/, HitSensor* pReceiver) {
    if (pReceiver == getSensor("killer_terget") && !isNerve(GET_NERVE(TripodBossShell, TripodBossShellNrvBreak))) {
        setNerve(GET_NERVE(TripodBossShell, TripodBossShellNrvBreak));

        return true;
    }

    return false;
}

void TripodBossShell::activateTripodBoss() {
    if (isNerve(GET_NERVE(TripodBossShell, TripodBossShellNrvNonActive))) {
        MR::onCalcAnim(this);
        MR::validateCollisionParts(this);
        setNerve(GET_NERVE(TripodBossShell, TripodBossShellNrvWait));
    }
}

void TripodBossShell::exeNonActive() {
}

void TripodBossShell::exeWait() {
    updateTripodMatrix();
}

void TripodBossShell::exeBreak() {
    updateTripodMatrix();

    if (MR::isFirstStep(this)) {
        MR::startSound(this, "SE_BM_TRIPOD_CORE_BREAK");
        MR::offEntryDrawBuffer(this);
        getSensor("killer_terget")->invalidate();
        MR::invalidateCollisionParts(this);
        MR::invalidateClipping(this);
        mBreakModel->appear();
        MR::requestMovementOn(mBreakModel);
        MR::startBck(mBreakModel, "Break");

        f32 playerDistance;
        Vec3f toPlayerDir = *MR::getPlayerPos() - mPosition;
        MR::separateScalarAndDirection(&playerDistance, &toPlayerDir, toPlayerDir);

        if (playerDistance <= ::sBreakStopDistance) {
            Vec3f up;
            MR::calcUpVec(&up, this);

            if (toPlayerDir.dot(up) > 0.0f) {
                MR::forceJumpPlayer(toPlayerDir * ::sStartEventForceJumpPlayerPower);
            }
        }
    }

    if (MR::isStep(this, ::sBreakStopStep)) {
        MR::stopScene(::sBreakStopFrame);

        if (MR::isValidSwitchDead(this)) {
            MR::onSwitchDead(this);
        }
    }

    if (MR::isBckStopped(mBreakModel)) {
        kill();
    }
}
