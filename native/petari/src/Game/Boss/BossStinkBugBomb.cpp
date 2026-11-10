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
#include "Game/Util/Tunable.hpp"
#include "Game/System/LumaCore.hpp"

NEBULA_TUNABLE_NAMED(f32, cNum_init_f1, "init.f1", 60.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_init_f2, "init.f2", 280.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_init_f3, "init.f3", 60.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_init_f4, "init.f4", 60.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeWait_f1, "exeWait.f1", 9.5f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeWait_f2, "exeWait.f2", 10.5f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeWait_f3, "exeWait.f3", 30.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeWait_f4, "exeWait.f4", 0.3f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeWait_f5, "exeWait.f5", 100.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeWait_f6, "exeWait.f6", 100.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeWait_f7, "exeWait.f7", 0.9f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeWait_f8, "exeWait.f8", 0.98f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeExplosion_f1, "exeExplosion.f1", 800.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeExplosion_f2, "exeExplosion.f2", 1200.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeExplosion_f3, "exeExplosion.f3", 2000.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeExplosion_f4, "exeExplosion.f4", 280.0f);

// The numbers this file's functions give the game's motion and animation calls, as tunables: "<file>.<function>.<call>.<k>".
NEBULA_TUNABLE_NAMED(f32, cNum_exeWait_addVelocityToGravity_1, "exeWait.addVelocityToGravity.1", 1.2f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeWait_reboundVelocityFromCollision_1, "exeWait.reboundVelocityFromCollision.1", 0.8f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeWait_reboundVelocityFromCollision_2, "exeWait.reboundVelocityFromCollision.2", 5.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeWait_reboundVelocityFromCollision_3, "exeWait.reboundVelocityFromCollision.3", 1.0f);

// The step counts of this file's functions, as tunables: "<file>.<function>.<k>".
NEBULA_TUNABLE_NAMED(s32, cStep_exeWait_1, "exeWait.1", 112);
NEBULA_TUNABLE_NAMED(s32, cStep_exeExplosion_1, "exeExplosion.1", 20);

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

    MR::addHitSensorEnemy(this, "body", 8, cNum_init_f1, Vec3f(0.0f, 0.0f, 0.0f));
    MR::addHitSensorEnemyAttack(this, "bomb", 8, cNum_init_f2, Vec3f(0.0f, 0.0f, 0.0f));
    MR::invalidateHitSensor(this, "bomb");

    initBinder(cNum_init_f3, 0.0f, 0);
    initEffectKeeper(0, nullptr);
    initSound(5, false);
    MR::initShadowVolumeSphere(this, cNum_init_f4);

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

    MR::addVelocityToGravity(this, cNum_exeWait_addVelocityToGravity_1);
    f32 dot = mGravity.dot(mVelocity);

    if (MR::reboundVelocityFromCollision(this, cNum_exeWait_reboundVelocityFromCollision_1, cNum_exeWait_reboundVelocityFromCollision_2, cNum_exeWait_reboundVelocityFromCollision_3) && dot >= cNum_exeWait_f1) {
        f32 liner = MR::getLinerValueFromMinMax(dot, cNum_exeWait_f2, cNum_exeWait_f3, cNum_exeWait_f4, 1.0f);
        MR::startSound(this, "SE_BM_BOSS_BUG_BOMB_GROUND", cNum_exeWait_f5 * liner);
        if (MR::isBindedGroundWater(this)) {
            MR::startSound(this, "SE_BM_BOSS_BUG_BOMB_GROUND_WATER", cNum_exeWait_f6 * liner);
        }
    }

    if (MR::isLessStep(this, cStep_exeWait_1)) {
        MR::startLevelSound(this, "SE_BM_BOSS_BUG_BOMB_COUNT1");
    } else {
        MR::startLevelSound(this, "SE_BM_BOSS_BUG_BOMB_COUNT2");
    }

    MR::attenuateVelocity(this, MR::isBinded(this) ? cNum_exeWait_f7 : cNum_exeWait_f8);

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

        MR::tryRumblePadAndCameraDistanceStrong(this, cNum_exeExplosion_f1, cNum_exeExplosion_f2, cNum_exeExplosion_f3);
        MR::zeroVelocity(this);
    }

    MR::setSensorRadius(this, "bomb", MR::calcNerveEaseOutValue(this, 10, 0.0f, cNum_exeExplosion_f4));

    if (MR::isGreaterStep(this, cStep_exeExplosion_1)) {
        kill();
    }
}
