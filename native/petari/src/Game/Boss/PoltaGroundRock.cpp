#include "Game/Boss/PoltaGroundRock.hpp"
#include "Game/System/NebulaFrameClock.hpp"
#include "Game/LiveActor/HitSensor.hpp"
#include "Game/LiveActor/ModelObj.hpp"
#include "Game/LiveActor/Nerve.hpp"
#include "Game/Util/ActorSensorUtil.hpp"
#include "Game/Util/EffectUtil.hpp"
#include "Game/Util/LiveActorUtil.hpp"
#include "Game/Util/MathUtil.hpp"
#include "Game/Util/MtxUtil.hpp"
#include "Game/Util/ObjUtil.hpp"
#include "Game/Util/SoundUtil.hpp"
#include <nebula_geometry.hpp>
#include "Game/Util/Tunable.hpp"
#include "Game/System/LumaCore.hpp"

NEBULA_TUNABLE_NAMED(f32, cNum_init_f1, "init.f1", 170.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_init_f2, "init.f2", 260.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_start_f1, "start.f1", 500.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeJut_f1, "exeJut.f1", 500.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeJut_f2, "exeJut.f2", 800.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeJut_f3, "exeJut.f3", 1200.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeJut_f4, "exeJut.f4", 2000.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeBreak_f1, "exeBreak.f1", 800.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeBreak_f2, "exeBreak.f2", 1200.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeBreak_f3, "exeBreak.f3", 2000.0f);

// The step counts of this file's functions, as tunables: "<file>.<function>.<k>".
NEBULA_TUNABLE_NAMED(s32, cStep_exeJut_1, "exeJut.1", 20);

namespace NrvPoltaGroundRock {
    NEW_NERVE(PoltaGroundRockNrvSign, PoltaGroundRock, Sign);
    NEW_NERVE(PoltaGroundRockNrvJut, PoltaGroundRock, Jut);
    NEW_NERVE(PoltaGroundRockNrvWait, PoltaGroundRock, Wait);
    NEW_NERVE(PoltaGroundRockNrvBreak, PoltaGroundRock, Break);
};  // namespace NrvPoltaGroundRock

PoltaGroundRock::PoltaGroundRock(const char* pName)
    : LiveActor(pName), mOwner(nullptr), mBreakModel(nullptr), mPoseQuat(0.0f, 0.0f, 0.0f, 1.0f), mGroundPos(0.0f, 0.0f, 0.0f) {
    lumacore::setClass(this, lumacore::kClassId< PoltaGroundRock >);
}

void PoltaGroundRock::init(const TableIter& rIter) {
    MR::initDefaultPos(this, rIter);
    initModelManagerWithAnm("PoltaGroundRock", nullptr, false);
    MR::connectToSceneCollisionMapObjWeakLight(this);
    MR::initLightCtrl(this);
    initHitSensor(2);
    MR::addHitSensorMapObj(this, "collision", 0, 0.0f, Vec3f(0.0f, 0.0f, 0.0f));
    MR::addHitSensorEnemy(this, "body", 16, cNum_init_f1, Vec3f(0.0f, cNum_init_f2, 0.0f));
    MR::initCollisionParts(this, "PoltaGroundRock", getSensor("collision"), nullptr);
    initEffectKeeper(0, nullptr);
    MR::setEffectHostSRT(this, "Shadow", &mGroundPos, nullptr, nullptr);
    initSound(4, false);
    initNerve(GET_NERVE(PoltaGroundRock, PoltaGroundRockNrvSign));
    initBreakModel();
    MR::invalidateClipping(this);
    makeActorDead();
}

void PoltaGroundRock::initBreakModel() {
    mBreakModel = MR::createModelObjEnemy("壊れモデル", "PoltaGroundRockBreak", getBaseMtx());
    mBreakModel->initWithoutIter();
    MR::initLightCtrl(mBreakModel);
    MR::invalidateClipping(mBreakModel);
    mBreakModel->makeActorDead();
}

void PoltaGroundRock::control() {
}

void PoltaGroundRock::calcAndSetBaseMtx() {
    MR::setBaseTRMtx(this, mPoseQuat);
    MR::setBaseScale(this, mScale);
}

void PoltaGroundRock::start(LiveActor* pOwner, const Vec3f& rVec1) {
    mOwner = pOwner;
    setNerve(GET_NERVE(PoltaGroundRock, PoltaGroundRockNrvSign));
    mGroundPos.set(rVec1);
    MR::calcGravity(this, rVec1);
    mPosition.scaleAdd(cNum_start_f1, mGravity, mGroundPos);
    Pos3f poseMtx;
    MR::makeMtxUpNoSupportPos(&poseMtx, -mGravity, mPosition);
    MR::rotateMtxLocalY(poseMtx, MR::getRandom(0.0f, TWO_PI));
    poseMtx.getQuat(mPoseQuat);
    MR::validateCollisionParts(this);
    MR::showModel(this);
    appear();
}

void PoltaGroundRock::requestBreak() {
    if (PoltaGroundRock::isEnableBreak()) {
        setNerve(GET_NERVE(PoltaGroundRock, PoltaGroundRockNrvBreak));
    }
}

void PoltaGroundRock::attackSensor(HitSensor* pSender, HitSensor* pReceiver) {
    if (isEnableAttack()) {
        if (MR::isSensorPlayer(pReceiver)) {
            MR::sendMsgEnemyAttackStrong(pReceiver, pSender);
        } else {
            MR::sendArbitraryMsg(ACTMES_BREAK_POLTA_GROUND_ROCK, pReceiver, pSender);
        }
    }
}

bool PoltaGroundRock::receiveMsgPlayerAttack(u32 msg, HitSensor* /*pSender*/, HitSensor* pReceiver) {
    if (getSensor("collision") != pReceiver) {
        return false;
    }
    if (MR::isMsgPlayerSpinAttack(msg) && isEnableBreak()) {
        setNerve(GET_NERVE(PoltaGroundRock, PoltaGroundRockNrvBreak));
        return true;
    }

    return false;
}

bool PoltaGroundRock::receiveMsgEnemyAttack(u32 msg, HitSensor* /*pSender*/, HitSensor* pReceiver) {
    if (getSensor("collision") != pReceiver) {
        return false;
    }
    if (MR::isMsgEnemyAttack(msg) || MR::isMsgExplosionAttack(msg)) {
        if (isEnableBreak()) {
            setNerve(GET_NERVE(PoltaGroundRock, PoltaGroundRockNrvBreak));
            return true;
        }
    }
    return false;
}

bool PoltaGroundRock::receiveOtherMsg(u32 msg, HitSensor* /*pSender*/, HitSensor* pReceiver) {
    if (msg == ACTMES_BREAK_POLTA_GROUND_ROCK && getSensor("body") == pReceiver && isEnableBreak()) {
        setNerve(GET_NERVE(PoltaGroundRock, PoltaGroundRockNrvBreak));
        return true;
    }
    return false;
}

void PoltaGroundRock::exeSign() {
    if (MR::isFirstStep(this)) {
        MR::emitEffect(this, "Shadow");
    }
    MR::startLevelSound(this, "SE_OJ_LV_POLTA_G_ROCK_SIGN");
    if (MR::isGreaterStep(this, NebulaFrameClock::stepsOfSeconds(1))) {
        setNerve(GET_NERVE(PoltaGroundRock, PoltaGroundRockNrvJut));
    }
}

void PoltaGroundRock::exeJut() {
    if (MR::isFirstStep(this)) {
        MR::startSound(this, "SE_OJ_POLTA_G_ROCK_JUT");
    }
    mPosition.scaleAdd(MR::calcNerveEaseOutValue(this, 20, cNum_exeJut_f1, 0.0f), mGravity, mGroundPos);
    if (MR::isGreaterStep(this, cStep_exeJut_1)) {
        MR::tryRumblePadAndCameraDistanceMiddle(this, cNum_exeJut_f2, cNum_exeJut_f3, cNum_exeJut_f4);
        setNerve(GET_NERVE(PoltaGroundRock, PoltaGroundRockNrvWait));
        MR::deleteEffect(this, "Shadow");
    }
}

void PoltaGroundRock::exeWait() {
    if (MR::isFirstStep(this)) {
        mPosition.set(mGroundPos);
        MR::emitEffect(this, "Rock");
    }
}

void PoltaGroundRock::exeBreak() {
    if (MR::isFirstStep(this)) {
        mBreakModel->makeActorAppeared();
        MR::startAction(mBreakModel, "Break");
        MR::deleteEffect(this, "Rock");
        MR::startSound(this, "SE_OJ_POLTA_G_ROCK_BREAK");
        MR::hideModel(this);
        MR::invalidateCollisionParts(this);
        MR::tryRumblePadAndCameraDistanceStrong(this, cNum_exeBreak_f1, cNum_exeBreak_f2, cNum_exeBreak_f3);
    }
    if (MR::isActionEnd(mBreakModel)) {
        kill();
        mBreakModel->kill();
    }
}

bool PoltaGroundRock::isEnableAttack() const {
    return isNerve(GET_NERVE(PoltaGroundRock, PoltaGroundRockNrvJut));
}

bool PoltaGroundRock::isEnableBreak() const {
    return !isNerve(GET_NERVE(PoltaGroundRock, PoltaGroundRockNrvBreak));
}
