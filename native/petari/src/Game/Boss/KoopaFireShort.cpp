#include "Game/Boss/KoopaFireShort.hpp"
#include "Game/System/NebulaFrameClock.hpp"
#include "Game/Boss/Koopa.hpp"
#include "Game/LiveActor/Nerve.hpp"
#include "Game/Util/ActorMovementUtil.hpp"
#include "Game/Util/ActorSensorUtil.hpp"
#include "Game/Util/ActorShadowUtil.hpp"
#include "Game/Util/EffectUtil.hpp"
#include "Game/Util/JointUtil.hpp"
#include "Game/Util/LiveActorUtil.hpp"
#include "Game/Util/MathUtil.hpp"
#include "Game/Util/ObjUtil.hpp"
#include "Game/Util/PlayerUtil.hpp"
#include "Game/Util/SoundUtil.hpp"
#include "Game/Util/StarPointerUtil.hpp"
#include "Game/System/LumaCore.hpp"

namespace {
    const f32 sFlySpeedNormal = 15.0f;
    const f32 sFlySpeedFast = 30.0f;
    static const s32 sFlyStepNormal = NebulaFrameClock::stepsOfSeconds(3);
    static const s32 sFlyStepLong = NebulaFrameClock::stepsOfSeconds(15);
    static const s32 sCurveLifeTime = NebulaFrameClock::stepsOfSeconds(5);
    const f32 sCurveRotateSpeed = 0.1f;
    static const s32 sStepToValidSensor = NebulaFrameClock::stepsOfMilliseconds(500);
};  // namespace

namespace NrvKoopaFireShort {
    NEW_NERVE(KoopaFireShortNrvFly, KoopaFireShort, Fly);
    NEW_NERVE(KoopaFireShortNrvDisappear, KoopaFireShort, Disappear);
    NEW_NERVE(KoopaFireShortNrvErase2P, KoopaFireShort, Erase2P);
};  // namespace NrvKoopaFireShort

KoopaFireShort::KoopaFireShort(const Koopa* pKoopa)
    : LiveActor("クッパの炎（ショート）"), mKoopa(pKoopa), mFront(0.0f, 0.0f, 1.0f), mSpeed(), mDuration(), mOffset() {
    lumacore::setClass(this, lumacore::kClassId< KoopaFireShort >);
}

void KoopaFireShort::init(const TableIter& /*rIter*/) {
    mScale.set(1.0f, 1.0f, 1.0f);

    initModelManagerWithAnm("KoopaFire", nullptr, false);
    MR::tryStartAllAnim(this, "KoopaFire");

    MR::connectToSceneEnemy(this);
    initHitSensor(1);

    Vec3f sensorOffset = Vec3f(0.0f, 0.0f, 0.0f);
    MR::addHitSensor(this, "Attack", ATYPE_KOOPA_FIRE, 8, 130.0f, sensorOffset);
    initBinder(50.0f, 0.0f, 0);

    MR::offBind(this);
    MR::onCalcGravity(this);

    initEffectKeeper(1, nullptr);

    MR::addEffectHitNormal(this, "Hit");

    initSound(4, false);
    initNerve(GET_NERVE(KoopaFireShort, KoopaFireShortNrvFly));

    Vec3f pointerOffset;
    pointerOffset.set(0.0f);
    MR::initStarPointerTarget(this, 100.0f, pointerOffset);
    MR::initShadowVolumeSphere(this, 200.0f);

    MR::invalidateClipping(this);

    MR::createBloomModel(this, getBaseMtx());

    makeActorDead();
}

void KoopaFireShort::appear() {
    mPosition.set(mKoopa->mPosition);

    MR::calcGravity(this);

    Pos3f transform;
    transform.setInline(MR::getJointMtx(mKoopa, "Tongue2"));
    transform.getTrans(mPosition);
    transform.getYDir(mVelocity);

    MR::vecKillElement(mVelocity, mGravity, &mVelocity);

    if (MR::isNearZero(mVelocity)) {
        transform.getYDir(mVelocity);
    } else {
        MR::normalize(&mVelocity);
    }

    f32 radius = 80.0f * mScale.x;
    mVelocity.x *= ::sFlySpeedNormal;
    mVelocity.y *= ::sFlySpeedNormal;
    mVelocity.z *= ::sFlySpeedNormal;

    MR::setBinderRadius(this, radius);
    MR::setSensorRadius(this, "Attack", radius);
    MR::setShadowVolumeSphereRadius(this, nullptr, radius);

    LiveActor::appear();

    MR::invalidateHitSensors(this);

    setNerve(GET_NERVE(KoopaFireShort, KoopaFireShortNrvFly));
}

void KoopaFireShort::emitNormal() {
    appear();

    mSpeed = ::sFlySpeedNormal;
    mDuration = ::sFlyStepNormal;
    mOffset = 0.0f;
}

void KoopaFireShort::emitFast() {
    appear();

    mSpeed = ::sFlySpeedFast;
    mDuration = ::sFlyStepNormal;
    mOffset = 0.0f;
}

void KoopaFireShort::emitCurve() {
    appear();

    mDuration = ::sCurveLifeTime;
    mSpeed = ::sFlySpeedFast;

    if (MR::isPlayerLeftSide(this)) {
        mOffset = ::sCurveRotateSpeed;
    } else {
        mOffset = -::sCurveRotateSpeed;
    }
}

void KoopaFireShort::emitLongTime() {
    appear();

    mSpeed = ::sFlySpeedNormal;
    mDuration = ::sFlyStepLong;
    mOffset = 0.0f;
}

void KoopaFireShort::exeFly() {
    if (MR::isFirstStep(this)) {
        MR::startBck(this, "Wait");
        MR::startSound(this, "SE_OJ_KOOPA_BULLET_SHOOT");
    }

    if (MR::isStep(this, ::sStepToValidSensor)) {
        MR::validateHitSensors(this);
    }

    if (MR::isStarPointerPointing2POnPressButton(this, "弱", true, false)) {
        Vec3f gravity = -mGravity;
        MR::calcStarPointerWorldVelocityDirectionOnPlane(&mVelocity, mPosition, gravity, 1);

        mSpeed = 80.0f;

        MR::tryRumblePadMiddle(this, PLAYER_2);

        MR::scaleVelocity(this, mSpeed);

        MR::invalidateHitSensors(this);

        setNerve(GET_NERVE(KoopaFireShort, KoopaFireShortNrvErase2P));
    } else {
        updateFly();

        if (MR::isLessStep(this, 50)) {
            mPosition += mGravity * 4.0f;
        }

        if (MR::isGreaterStep(this, mDuration)) {
            setNerve(GET_NERVE(KoopaFireShort, KoopaFireShortNrvDisappear));
        }
    }
}

void KoopaFireShort::exeDisappear() {
    if (MR::isFirstStep(this)) {
        MR::startBck(this, "Disappear");
        MR::startSound(this, "SE_OJ_KOOPA_BULLET_EXPLODE");
    }

    updateFly();

    if (MR::isBckStopped(this)) {
        kill();
    }
}

void KoopaFireShort::exeErase2P() {
    if (MR::isFirstStep(this)) {
        MR::invalidateHitSensors(this);

        MR::startBck(this, "Disappear");
        MR::start2PAttackAssistSound();
        MR::startSound(this, "SE_EM_FIRE_BUBBLE_REFLECT");

        MR::setBckRate(this, 3.0f);
    }

    updateFrontAndVelocity();

    if (MR::isBckStopped(this)) {
        kill();
    }
}

void KoopaFireShort::calcAndSetBaseMtx() {
    MR::calcAndSetBaseMtxFromGravityAndZAxis(this, mFront);
}

void KoopaFireShort::attackSensor(HitSensor* pSender, HitSensor* pReceiver) {
    if (!isNerve(GET_NERVE(KoopaFireShort, KoopaFireShortNrvFly))) {
        return;
    }

    if (MR::isSensorPlayer(pReceiver) && MR::sendMsgEnemyAttackFire(pReceiver, pSender)) {
        mSpeed = 0.0f;
        MR::zeroVelocity(this);
        setNerve(GET_NERVE(KoopaFireShort, KoopaFireShortNrvDisappear));
    } else if (!MR::isSensorPlayer(pReceiver) && !MR::sendMsgPush(pReceiver, pSender) && !MR::isSensorType(pReceiver, ATYPE_KOOPA_FIRE) &&
               !MR::isSensorType(pReceiver, ATYPE_KOOPA_DAMAGE_PLATE) && !MR::isSensorType(pReceiver, ATYPE_KOOPA_COIN_PLATE) &&
               !MR::isSensorType(pReceiver, ATYPE_KOOPA_PLATE)) {
        mSpeed = 0.0f;
        MR::zeroVelocity(this);
        setNerve(GET_NERVE(KoopaFireShort, KoopaFireShortNrvDisappear));
    }
}

void KoopaFireShort::updateFly() {
    Vec3f difference = *MR::getPlayerPos() - mPosition;
    MR::normalizeOrZero(&difference);
    difference.scale(0.0f);
    mVelocity.add(difference);
    updateFrontAndVelocity();

    MR::rotateVecDegree(&mVelocity, -mGravity, mOffset);

    MR::startLevelSound(this, "SE_OJ_LV_KOOPA_BULLET_FLY");
}

void KoopaFireShort::updateFrontAndVelocity() {
    MR::vecKillElement(mVelocity, mGravity, &mVelocity);
    MR::normalizeOrZero(&mVelocity);

    if (!MR::isNearZero(mVelocity)) {
        mFront.set(mVelocity);
    }

    mVelocity.scale(mSpeed);
}
