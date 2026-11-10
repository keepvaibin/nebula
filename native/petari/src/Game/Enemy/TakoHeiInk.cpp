#include "Game/Enemy/TakoHeiInk.hpp"
#include "Game/System/NebulaFrameClock.hpp"
#include "Game/LiveActor/Nerve.hpp"
#include "Game/Util.hpp"
#include "Game/Util/Tunable.hpp"
#include "Game/System/LumaCore.hpp"

namespace {
    NEBULA_TUNABLE(f32, sModelSize, 50.0f);
    NEBULA_TUNABLE(f32, sNormalFric, 0.99f);
    NEBULA_TUNABLE(f32, sGravityAccel, 0.8f);
    NEBULA_TUNABLE(s32, sBindOnTime, 10);
    static const s32 sForceBreakTime = NebulaFrameClock::stepsOfSeconds(4);
};  // namespace

namespace NrvTakoHeiInk {
    NEW_NERVE(TakoHeiInkNrvWait, TakoHeiInk, Wait);
};  // namespace NrvTakoHeiInk

TakoHeiInk::TakoHeiInk(const char* pName) : LiveActor(pName) {
    lumacore::setClass(this, lumacore::kClassId< TakoHeiInk >);
}

void TakoHeiInk::init(const TableIter& rIter) {
    MR::initDefaultPos(this, rIter);
    initModelManagerWithAnm("TakoHeiInk", nullptr, false);
    MR::connectToSceneEnemy(this);
    MR::onCalcGravity(this);
    initHitSensor(1);

    MR::addHitSensorEnemy(this, "Body", 8, ::sModelSize, Vec3f(0.0f, 0.0f, 0.0f));
    initBinder(::sModelSize, 0.0f, 0);
    initEffectKeeper(0, nullptr);
    initSound(1, false);
    MR::initShadowVolumeSphere(this, ::sModelSize);
    initNerve(GET_NERVE(TakoHeiInk, TakoHeiInkNrvWait));
    MR::invalidateClipping(this);
    makeActorDead();
}

void TakoHeiInk::kill() {
    LiveActor::kill();
    MR::emitEffect(this, "Break");
}

void TakoHeiInk::control() {
}

void TakoHeiInk::start(const Vec3f& startPos, const Vec3f& startVelocity, bool startOffBind) {
    mPosition.set(startPos);
    setNerve(GET_NERVE(TakoHeiInk, TakoHeiInkNrvWait));
    appear();
    mVelocity.set(startVelocity);

    if (startOffBind) {
        MR::offBind(this);
    } else {
        MR::onBind(this);
    }
}

void TakoHeiInk::attackSensor(HitSensor* pSender, HitSensor* pReceiver) {
    if (MR::isSensorPlayer(pReceiver) && MR::sendMsgEnemyAttack(pReceiver, pSender)) {
        MR::startSound(this, "SE_EM_TAKOHEI_INK_BREAK");
        kill();
    }
}

void TakoHeiInk::exeWait() {
    if (MR::isFirstStep(this)) {
        MR::tryStartBck(this, "Wait");
    }

    if (MR::isGreaterStep(this, ::sBindOnTime) && MR::isNoBind(this)) {
        MR::onBind(this);
    }

    MR::addVelocityToGravity(this, ::sGravityAccel);
    MR::attenuateVelocity(this, ::sNormalFric);

    if (MR::isBinded(this) || MR::isGreaterStep(this, ::sForceBreakTime)) {
        MR::startSound(this, "SE_EM_TAKOHEI_INK_BREAK");
        kill();
    }
}
