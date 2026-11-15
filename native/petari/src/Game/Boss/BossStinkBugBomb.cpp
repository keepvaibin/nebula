#include "Game/Boss/BossStinkBugBomb.hpp"
#include "Game/System/NebulaFrameClock.hpp"
#include "Game/LiveActor/Nerve.hpp"
#include "Game/Util/ActorMovementUtil.hpp"
#include "Game/Util/ActorSensorUtil.hpp"
#include "Game/Util/ActorShadowUtil.hpp"
#include "Game/Util/EffectUtil.hpp"
#include "Game/Util/LiveActorUtil.hpp"
#include "Game/Util/MapUtil.hpp"
#include "Game/Util/MathUtil.hpp"
#include "Game/Util/ObjUtil.hpp"
#include "Game/Util/SoundUtil.hpp"
#include "Game/System/LumaCore.hpp"

namespace NrvBossStinkBugBomb {
    NEW_NERVE(BossStinkBugBombNrvWait, BossStinkBugBomb, Wait);
    NEW_NERVE(BossStinkBugBombNrvExplosion, BossStinkBugBomb, Explosion);
};  // namespace NrvBossStinkBugBomb

BossStinkBugBomb::BossStinkBugBomb(const char* pName) : LiveActor(pName) {
    lumacore::setClass(this, lumacore::kClassId< BossStinkBugBomb >);
}

void BossStinkBugBomb::init(const TableIter& /*rIter*/) {
    initModelManagerWithAnm("BossStinkBugBomb", nullptr, false);
    MR::connectToSceneEnemy(this);
    MR::onCalcGravity(this);
    initHitSensor(2);

    MR::addHitSensorEnemy(this, "body", 8, 60.0f, Vec3f(0.0f, 0.0f, 0.0f));
    MR::addHitSensorEnemyAttack(this, "bomb", 8, 280.0f, Vec3f(0.0f, 0.0f, 0.0f));
    MR::invalidateHitSensor(this, "bomb");

    initBinder(60.0f, 0.0f, 0);
    initEffectKeeper(0, nullptr);
    initSound(5, false);
    MR::initShadowVolumeSphere(this, 60.0f);

    initNerve(GET_NERVE(BossStinkBugBomb, BossStinkBugBombNrvWait));
    MR::invalidateClipping(this);
    makeActorDead();
}

// The bomb never turns: its moment is zero from the start (BossStinkBugBomb.s:27/122/130).
void BossStinkBugBomb::calcAndSetBaseMtx() {
    MR::setBaseTRMtx(this, Quat4f(0.0f, 0.0f, 0.0f, 1.0f));
    MR::setBaseScale(this, mScale);
}

void BossStinkBugBomb::start(const Vec3f& startPos, const Vec3f& startVelocity) {
    mPosition.set(startPos);
    MR::showModel(this);

    MR::validateHitSensor(this, "body");
    MR::invalidateHitSensor(this, "bomb");

    setNerve(GET_NERVE(BossStinkBugBomb, BossStinkBugBombNrvWait));
    appear();
    mVelocity.set(startVelocity);
}

void BossStinkBugBomb::attackSensor(HitSensor* pSender, HitSensor* pReceiver) {
    if (MR::isSensorEnemyAttack(pSender)) {
        MR::sendMsgEnemyAttackExplosion(pReceiver, pSender);
    } else if (MR::isSensorPlayer(pReceiver)) {
        if (MR::sendMsgEnemyAttackExplosion(pReceiver, pSender)) {
            MR::emitEffect(this, "Explosion");
            kill();
        }
    }
}

bool BossStinkBugBomb::receiveMsgPlayerAttack(u32 msg, HitSensor* /*pSender*/, HitSensor* /*pReceiver*/) {
    if (MR::isMsgLockOnStarPieceShoot(msg)) {
        return true;
    }

    if (MR::isMsgStarPieceAttack(msg)) {
        MR::emitEffect(this, "Explosion");
        kill();
        return true;
    }

    return false;
}

bool BossStinkBugBomb::receiveMsgPush(HitSensor* /*pSender*/, HitSensor* /*pReceiver*/) {
    return false;
}

void BossStinkBugBomb::exeWait() {
    if (MR::isFirstStep(this)) {
        MR::startBrk(this, "Signal");
    }

    MR::addVelocityToGravity(this, 1.2f);
    f32 dot = mGravity.dot(mVelocity);

    if (MR::reboundVelocityFromCollision(this, 0.8f, 5.0f, 1.0f) && dot >= 9.5f) {
        f32 liner = MR::getLinerValueFromMinMax(dot, 10.5f, 30.0f, 0.3f, 1.0f);
        MR::startSound(this, "SE_BM_BOSS_BUG_BOMB_GROUND", 100.0f * liner);
        if (MR::isBindedGroundWater(this)) {
            MR::startSound(this, "SE_BM_BOSS_BUG_BOMB_GROUND_WATER", 100.0f * liner);
        }
    }

    if (MR::isLessStep(this, 112)) {
        MR::startLevelSound(this, "SE_BM_BOSS_BUG_BOMB_COUNT1");
    } else {
        MR::startLevelSound(this, "SE_BM_BOSS_BUG_BOMB_COUNT2");
    }

    MR::attenuateVelocity(this, MR::isBinded(this) ? 0.9f : 0.98f);

    if (MR::isGreaterStep(this, NebulaFrameClock::stepsOfMilliseconds(2500))) {
        setNerve(GET_NERVE(BossStinkBugBomb, BossStinkBugBombNrvExplosion));
    }
}

void BossStinkBugBomb::exeExplosion() {
    if (MR::isFirstStep(this)) {
        MR::hideModel(this);

        MR::validateHitSensor(this, "bomb");
        MR::invalidateHitSensor(this, "body");

        MR::emitEffect(this, "Explosion");
        MR::startSound(this, "SE_BM_BOSS_BUG_BOMB_BANG");

        if (MR::isBindedGroundWater(this)) {
            MR::startSound(this, "SE_BM_BOSS_BUG_BOMB_BANG_WATER");
        }

        MR::tryRumblePadAndCameraDistanceStrong(this, 800.0f, 1200.0f, 2000.0f);
        MR::zeroVelocity(this);
    }

    MR::setSensorRadius(this, "bomb", MR::calcNerveEaseOutValue(this, 10, 0.0f, 280.0f));

    if (MR::isGreaterStep(this, 20)) {
        kill();
    }
}
