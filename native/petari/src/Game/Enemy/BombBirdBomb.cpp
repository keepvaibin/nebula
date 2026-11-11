#include "Game/Enemy/BombBirdBomb.hpp"
#include "Game/System/NebulaFrameClock.hpp"
#include "Game/LiveActor/HitSensor.hpp"
#include "Game/LiveActor/Nerve.hpp"
#include "Game/Util/ActorMovementUtil.hpp"
#include "Game/Util/ActorSensorUtil.hpp"
#include "Game/Util/ActorShadowUtil.hpp"
#include "Game/Util/EffectUtil.hpp"
#include "Game/Util/LiveActorUtil.hpp"
#include "Game/Util/MathUtil.hpp"
#include "Game/Util/ObjUtil.hpp"
#include "Game/Util/SoundUtil.hpp"
#include "Game/Util/Tunable.hpp"
#include "Game/System/LumaCore.hpp"

NEBULA_TUNABLE_NAMED(f32, cNum_init_f1, "init.f1", 60.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeWait_f1, "exeWait.f1", 5.5f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeWait_f2, "exeWait.f2", 5.5f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeExplosion_f1, "exeExplosion.f1", 800.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeExplosion_f2, "exeExplosion.f2", 1200.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeExplosion_f3, "exeExplosion.f3", 2000.0f);

// The numbers this file's functions give the game's motion and animation calls, as tunables: "<file>.<function>.<call>.<k>".
NEBULA_TUNABLE_NAMED(f32, cNum_exeWait_reboundVelocityFromCollision_1, "exeWait.reboundVelocityFromCollision.1", 1.0f);

namespace {
    NEBULA_TUNABLE(f32, sGroundFric, 1.0f);
    NEBULA_TUNABLE(f32, sMomentFric, 0.96f);
    NEBULA_TUNABLE(f32, sGravityAccel, 1.2f);
    NEBULA_TUNABLE(f32, sReboundRate, 0.4f);
    NEBULA_TUNABLE(f32, sReboundSpeedMin, 5.0f);
    static const s32 sWaitTime = NebulaFrameClock::stepsOfMilliseconds(2500);
    NEBULA_TUNABLE(s32, sExplosionMaxTime, 10);
    NEBULA_TUNABLE(s32, sExplosionTime, 20);
    NEBULA_TUNABLE(f32, sExplosionRadius, 280.0f);
};  // namespace

namespace NrvBombBirdBomb {
    NEW_NERVE(HostTypeNrvHold, BombBirdBomb, Hold);
    NEW_NERVE(HostTypeNrvWait, BombBirdBomb, Wait);
    NEW_NERVE(HostTypeNrvExplosion, BombBirdBomb, Explosion);
};  // namespace NrvBombBirdBomb

BombBirdBomb::BombBirdBomb(const char* pName) : LiveActor(pName), mRotateQuat(0.0f, 0.0f, 0.0f, 1.0f), mRotateMoment(0.0f, 0.0f, 0.0f) {
    lumacore::setClass(this, lumacore::kClassId< BombBirdBomb >);
}

void BombBirdBomb::init(const TableIter& /*rIter*/) {
    initModelManagerWithAnm("BombBirdBomb", nullptr, false);
    MR::connectToSceneEnemy(this);
    MR::onCalcGravity(this);
    initHitSensor(2);
    MR::addHitSensorEnemy(this, "body", 8, cNum_init_f1, Vec3f(0.0f, 0.0f, 0.0f));
    MR::addHitSensorEnemyAttack(this, "bomb", 8, ::sExplosionRadius, Vec3f(0.0f, 0.0f, 0.0f));
    MR::invalidateHitSensor(this, "bomb");
    initBinder(getSensor("body")->mRadius, 0.0f, 0);
    initEffectKeeper(0, nullptr);
    initSound(3, false);
    f32 radius = getSensor("body")->mRadius;
    MR::initShadowVolumeSphere(this, radius);
    initNerve(GET_NERVE(BombBirdBomb, HostTypeNrvHold));
    MR::invalidateClipping(this);
    makeActorDead();
}

void BombBirdBomb::kill() {
    LiveActor::kill();
    MR::deleteEffect(this, "LocusSmoke");
}

void BombBirdBomb::control() {
    mRotateMoment.mult(::sMomentFric);
    MR::rotateQuatMoment(&mRotateQuat, mRotateMoment);
}

void BombBirdBomb::calcAndSetBaseMtx() {
    MR::setBaseTRMtx(this, mRotateQuat);
    MR::setBaseScale(this, mScale);
}

void BombBirdBomb::hold(const Vec3f& rPos) {
    MR::resetPosition(this, rPos);
    appear();
    MR::showModel(this);
    setNerve(GET_NERVE(BombBirdBomb, HostTypeNrvHold));
}

void BombBirdBomb::start(const Vec3f& rTrans, const Vec3f& rVelocity) {
    mPosition.set(rTrans);
    MR::showModel(this);
    MR::validateHitSensor(this, "body");
    MR::invalidateHitSensor(this, "bomb");
    setNerve(GET_NERVE(BombBirdBomb, HostTypeNrvWait));
    mVelocity.set(rVelocity);
    MR::emitEffect(this, "LocusSmoke");
}

void BombBirdBomb::attackSensor(HitSensor* pSender, HitSensor* pReceiver) {
    if (isNerve(GET_NERVE(BombBirdBomb, HostTypeNrvExplosion)) && MR::isSensorEnemyAttack(pSender)) {
        MR::sendMsgEnemyAttackExplosion(pReceiver, pSender);
        return;
    }

    if (!MR::isSensorPlayer(pReceiver)) {
        return;
    }

    if (!isNerve(GET_NERVE(BombBirdBomb, HostTypeNrvWait))) {
        return;
    }

    if (!MR::sendMsgEnemyAttackExplosion(pReceiver, pSender)) {
        return;
    }

    MR::emitEffect(this, "Explosion");
    MR::startSound(this, "SE_EM_BOMB_EXPLOSION");
    kill();
}

bool BombBirdBomb::receiveMsgPush(HitSensor* /*pSender*/, HitSensor* /*pReceiver*/) {
    return false;
}

bool BombBirdBomb::receiveMsgPlayerAttack(u32 msg, HitSensor* /*pSender*/, HitSensor* /*pReceiver*/) {
    return MR::isMsgStarPieceReflect(msg);
}

void BombBirdBomb::exeHold() {
    if (MR::isFirstStep(this)) {
        MR::startBrk(this, "Normal");
    }

    mVelocity.zero();
}

void BombBirdBomb::exeWait() {
    if (MR::isFirstStep(this)) {
        MR::startBrk(this, "Signal");
    }

    f32 dotP = mGravity.dot(mVelocity);
    Vec3f velocity;
    velocity.set(0.0f);

    if (MR::calcVelocityAreaOrRailMoveOnGround(&velocity, this)) {
        mVelocity.set(velocity * ::sGroundFric);
        MR::addVelocityToGravity(this, ::sGravityAccel);

        if (dotP >= cNum_exeWait_f1) {
            MR::startSound(this, "SE_EM_BOMBBIRDBOMB_LAND_SAND");
        }
    } else {
        MR::addVelocityToGravity(this, ::sGravityAccel);

        if (MR::reboundVelocityFromCollision(this, ::sReboundRate, ::sReboundSpeedMin, cNum_exeWait_reboundVelocityFromCollision_1) && dotP >= cNum_exeWait_f2) {
            MR::startSound(this, "SE_EM_BOMBBIRDBOMB_LAND");
        }
    }

    if (MR::isGreaterStep(this, ::sWaitTime)) {
        setNerve(GET_NERVE(BombBirdBomb, HostTypeNrvExplosion));
    }
}

void BombBirdBomb::exeExplosion() {
    if (MR::isFirstStep(this)) {
        MR::hideModel(this);
        MR::validateHitSensor(this, "bomb");
        MR::invalidateHitSensor(this, "body");
        MR::emitEffect(this, "Explosion");
        MR::startSound(this, "SE_EM_BOMB_EXPLOSION");
        MR::tryRumblePadAndCameraDistanceStrong(this, cNum_exeExplosion_f1, cNum_exeExplosion_f2, cNum_exeExplosion_f3);
        mVelocity.zero();
    }

    MR::setSensorRadius(this, "bomb", MR::calcNerveEaseOutValue(this, ::sExplosionMaxTime, 0.0f, ::sExplosionRadius));

    if (MR::isGreaterStep(this, ::sExplosionTime)) {
        MR::invalidateHitSensor(this, "bomb");
        kill();
    }
}
