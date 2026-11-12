#include "Game/Boss/DinoPackunStateDamage.hpp"
#include "Game/System/NebulaFrameClock.hpp"
#include "Game/Boss/DinoPackun.hpp"
#include "Game/Boss/DinoPackunEggShell.hpp"
#include "Game/Boss/DinoPackunTail.hpp"
#include "Game/LiveActor/Nerve.hpp"
#include "Game/LiveActor/PartsModel.hpp"
#include "Game/Util.hpp"
#include "Game/Util/StringUtil.hpp"

namespace {
    // Where the tail leaves the body, and the egg, in the boss's frame.
    const Vec3f cOutPosition(0.0f, 100.0f, -300.0f);
    const Vec3f cEggOutPosition(0.0f, 0.0f, -400.0f);
    const f32 cTailRootAdjustRate = 0.1f;
    // Pulled farther than this, the boss turns toward its tail.
    const f32 cPullTurnDistance = 800.0f;
    const f32 cPullTurnDegree = 6.0f;
    const s32 cPullTurnNode = 3;
    // The tail end this far toward the boss's up (cosine) is over its head.
    const f32 cTailTopCos = 0.7f;
    const s32 cEggBreakStep = NebulaFrameClock::stepsOfMilliseconds(500);
    const s32 cEggBrokenEndStep = NebulaFrameClock::stepsOfSeconds(2);
    const s32 cEggHitStopFrames = 5;
    const s32 cDamageHitStopFrames = 15;
    const char* const cDemoName = "ディノパックンダメージ";

    // By DinoPackunStateDamage::PunchedDir.
    const char* const cPunchedBck[] = {nullptr, "PunchedTailLeft", "PunchedTailRight", "PunchedTailTop"};
    const char* const cDamageBck[] = {nullptr, "DamageLeft", "DamageRight", "DamageTop"};
    const char* const cLastDamageBck[] = {nullptr, "LastDamageLeft", "LastDamageRight", "LastDamageTop"};
};  // namespace

namespace NrvDinoPackunStateDamage {
    NEW_NERVE(DinoPackunStateDamageNrvPunched, DinoPackunStateDamage, Punched);
    NEW_NERVE(DinoPackunStateDamageNrvTryPulledDemo, DinoPackunStateDamage, TryPulledDemo);
    NEW_NERVE(DinoPackunStateDamageNrvPulled, DinoPackunStateDamage, Pulled);
    NEW_NERVE(DinoPackunStateDamageNrvEggBroken, DinoPackunStateDamage, EggBroken);
    NEW_NERVE(DinoPackunStateDamageNrvDamage, DinoPackunStateDamage, Damage);
    NEW_NERVE(DinoPackunStateDamageNrvLastDamage, DinoPackunStateDamage, LastDamage);
};  // namespace NrvDinoPackunStateDamage

// Punched by its own tail ball: the player pulls the tail (a demo), lets go, and the ball hits the egg (breaking it) or
// the head.
DinoPackunStateDamage::DinoPackunStateDamage(DinoPackun* pHost)
    : ActorStateBase< DinoPackun >("ディノパックンダメージ状態", pHost), mPunchedDir(PunchedDir_None), mDamageType(DamageType_Normal) {
    initNerve(GET_NERVE(DinoPackunStateDamage, DinoPackunStateDamageNrvPunched));
}

void DinoPackunStateDamage::appear() {
    mIsDead = false;
    setNerve(GET_NERVE(DinoPackunStateDamage, DinoPackunStateDamageNrvPunched));
}

bool DinoPackunStateDamage::isDamageMessage(u32 msg) const {
    return msg == ACTMES_DINO_PACKUN_PUNCHED_BALL;
}

bool DinoPackunStateDamage::receiveOtherMsg(u32 msg, HitSensor* pSender, HitSensor* pReceiver) {
    if (msg == ACTMES_DINO_PACKUN_PULLED_TAIL && isNerve(GET_NERVE(DinoPackunStateDamage, DinoPackunStateDamageNrvPunched))) {
        setNerve(GET_NERVE(DinoPackunStateDamage, DinoPackunStateDamageNrvTryPulledDemo));
        return true;
    }

    if (msg == ACTMES_DINO_PACKUN_BALL_ATTACK && isNerve(GET_NERVE(DinoPackunStateDamage, DinoPackunStateDamageNrvPulled))) {
        MR::emitEffectHitBetweenSensors(getHost(), pSender, pReceiver, 0.0f, "Hit");

        switch (mDamageType) {
        case DamageType_Egg:
            setNerve(GET_NERVE(DinoPackunStateDamage, DinoPackunStateDamageNrvEggBroken));
            break;
        case DamageType_Normal:
            setNerve(GET_NERVE(DinoPackunStateDamage, DinoPackunStateDamageNrvDamage));
            break;
        case DamageType_Last:
            setNerve(GET_NERVE(DinoPackunStateDamage, DinoPackunStateDamageNrvLastDamage));
            break;
        }

        getHost()->offAimTailBall(-1);
        return true;
    }

    return false;
}

void DinoPackunStateDamage::setDamageEgg() {
    mDamageType = DamageType_Egg;
}

void DinoPackunStateDamage::setDamageNormal() {
    mDamageType = DamageType_Normal;
}

void DinoPackunStateDamage::setDamageLast() {
    mDamageType = DamageType_Last;
}

const Vec3f& DinoPackunStateDamage::getTailOutPosition() const {
    return mDamageType == DamageType_Egg ? cEggOutPosition : cOutPosition;
}

void DinoPackunStateDamage::exePunched() {
    if (MR::isFirstStep(this)) {
        getHost()->onAimTailBall(-1);
        getHost()->startDamageCamera();
        MR::zeroVelocity(getHost());
        getHost()->mTail->lockNodePosition(1);
        MR::startSound(getHost(), "SE_BV_D_PAKKUN_SPIN_HIT");
        MR::startSound(getHost(), "SE_BM_D_PAKKUN_SLAVER");
    }

    getHost()->adjustTailRootPosition(getTailOutPosition(), cTailRootAdjustRate);
    getHost()->updateNormalVelocity();
}

// Out of the egg, the boss looks toward where its tail is pulled: over its head, to its right or to its left.
void DinoPackunStateDamage::exePulled() {
    Vec3f tailEnd(0.0f, 0.0f, 0.0f);
    getHost()->mTail->getEndTailPosition(&tailEnd);

    if (MR::isFirstStep(this)) {
        getHost()->startDemo();

        if (mDamageType != DamageType_Egg) {
            Vec3f side;
            MR::calcSideVec(&side, getHost());
            Vec3f up;
            MR::calcUpVec(&up, getHost());
            Vec3f headPos(MR::getSensorPos(getHost()->getSensor("head")));
            Vec3f toTail(tailEnd - headPos);
            MR::normalizeOrZero(&toTail);

            if (up.dot(toTail) > cTailTopCos) {
                mPunchedDir = PunchedDir_Top;
            } else if (side.dot(toTail) < 0.0f) {
                mPunchedDir = PunchedDir_Right;
            } else {
                mPunchedDir = PunchedDir_Left;
            }

            MR::startBck(getHost(), cPunchedBck[mPunchedDir]);
        }
    }

    getHost()->adjustTailRootPosition(getTailOutPosition(), cTailRootAdjustRate);

    if (tailEnd.distance(getHost()->mPosition) > cPullTurnDistance) {
        Vec3f node;
        getHost()->mTail->getTailNodePosition(&node, cPullTurnNode);
        MR::turnDirectionFromTargetDegree(getHost(), &getHost()->mFront, node, cPullTurnDegree);
    }

    getHost()->updateNormalVelocity();
}

// The egg cracks, holds, then breaks off.
void DinoPackunStateDamage::exeEggBroken() {
    if (MR::isFirstStep(this)) {
        MR::stopBck(getHost());
        getHost()->mTail->lockEndNodePosition();
        MR::startSystemSE("SE_SY_VS_BOSS_DAMAGE_1");
        MR::startSound(getHost(), "SE_BM_D_PAKKUN_EGG_BRK_HIT");
        MR::startSound(getHost(), "SE_BV_D_PAKKUN_EGG_BREAK");
        MR::startSound(getHost(), "SE_BM_D_PAKKUN_SLAVER");
        MR::shakeCameraStrong();
        MR::tryRumblePadMiddle(getHost(), PLAYER_1);
        MR::zeroVelocity(getHost());
        MR::startBva(getHost()->getEggShell(), "EggBroken");
        MR::invalidateHitSensor(getHost(), "egg");
        MR::stopScene(cEggHitStopFrames);
    }

    if (MR::isStep(this, cEggBreakStep)) {
        getHost()->getEggShell()->kill();
        getHost()->getEggBrokenModel()->appear();
        MR::startBck(getHost()->getEggBrokenModel(), "Break");
        MR::startBrk(getHost()->getEggBrokenModel(), "Break");
        MR::startBck(getHost(), "EggDamageTop");
        MR::startSound(getHost(), "SE_BM_D_PAKKUN_HEAD_EGG_BRK");
        getHost()->mTail->unlockEndNodePosition();
        getHost()->mTail->unlockNodePosition(1);
        MR::shakeCameraStrong();
        MR::tryRumblePadStrong(getHost(), PLAYER_1);
    }

    if (MR::isGreaterStep(this, cEggBrokenEndStep)) {
        getHost()->endDemo(cDemoName);
        getHost()->getEggBrokenModel()->kill();
        kill();
        getHost()->endDamageCamera();
    }
}

void DinoPackunStateDamage::exeDamage() {
    if (MR::isFirstStep(this)) {
        MR::startSystemSE("SE_SY_VS_BOSS_DAMAGE_1");
        MR::startSound(getHost(), "SE_BM_D_PAKKUN_DAMAGE");
        MR::startSound(getHost(), "SE_BV_D_PAKKUN_DAMAGE");
        MR::startSound(getHost(), "SE_BM_D_PAKKUN_SLAVER");
        startBckByPunchedDir(cDamageBck);
    }

    updateDamage();
}

void DinoPackunStateDamage::exeLastDamage() {
    if (MR::isFirstStep(this)) {
        MR::startSystemSE("SE_SY_VS_BOSS_LAST_HIT");
        MR::startSound(getHost(), "SE_BM_D_PAKKUN_LAST_DAMAGE");
        MR::startSound(getHost(), "SE_BV_D_PAKKUN_LAST_DAMAGE");
        MR::startSound(getHost(), "SE_BM_D_PAKKUN_SLAVER");
        startBckByPunchedDir(cLastDamageBck);
    }

    updateDamage();
}

void DinoPackunStateDamage::startBckByPunchedDir(const char* const* pBckNames) {
    if (mPunchedDir != PunchedDir_None) {
        MR::startBck(getHost(), pBckNames[mPunchedDir]);
    }
}

void DinoPackunStateDamage::updateDamage() {
    if (MR::isFirstStep(this)) {
        MR::tryRumblePadVeryStrong(getHost(), PLAYER_1);
        MR::shakeCameraNormalStrong();
        MR::stopScene(cDamageHitStopFrames);
        MR::zeroVelocity(getHost());
        getHost()->mTail->unlockNodePosition(1);
    }

    if (MR::isBckStopped(getHost())) {
        getHost()->endDemo(cDemoName);
        kill();
        getHost()->endDamageCamera();
    }
}

void DinoPackunStateDamage::exeTryPulledDemo() {
    if (MR::tryStartDemoMarioPuppetable(getHost(), cDemoName)) {
        setNerve(GET_NERVE(DinoPackunStateDamage, DinoPackunStateDamageNrvPulled));
    }
}
