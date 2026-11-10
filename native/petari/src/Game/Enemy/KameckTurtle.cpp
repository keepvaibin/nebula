#include "Game/Enemy/KameckTurtle.hpp"
#include "Game/System/NebulaFrameClock.hpp"
#include "Game/Enemy/Kameck.hpp"
#include "Game/Enemy/KameckBeam.hpp"
#include "Game/LiveActor/Nerve.hpp"
#include "Game/MapObj/JetTurtle.hpp"
#include "Game/Util.hpp"
#include "Game/Util/Tunable.hpp"

NEBULA_TUNABLE_NAMED(f32, cNum_init_f1, "init.f1", 40.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_init_f2, "init.f2", 10.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_init_f3, "init.f3", 60.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_init_f4, "init.f4", 50.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_attackSensor_f1, "attackSensor.f1", 0.8f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeRun_f1, "exeRun.f1", 0.5f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeRun_f2, "exeRun.f2", 0.5f);

namespace {
    // const f32 sBeamRadius =
    NEBULA_TUNABLE(f32, sUpVecBlendRate, 0.2f);
    NEBULA_TUNABLE(f32, sFrontVecBlendRate, 0.2f);
    const s32 sRunTime = NebulaFrameClock::stepsOfSeconds(10);
    const s32 sEnableAttackTime = NebulaFrameClock::stepsOfSeconds(5);
    NEBULA_TUNABLE(s32, sDeccelStartTime, 200);
    NEBULA_TUNABLE(s32, sDeccelEndTime, 400);
    NEBULA_TUNABLE(f32, sRunSpeed, 15.0f);
    NEBULA_TUNABLE(f32, sRunGravity, 0.7f);
    NEBULA_TUNABLE(f32, sRunFric, 0.98f);
    NEBULA_TUNABLE(f32, sRunSpinDegree, -25.0f);
    // const s32 sMorphTime =
};  // namespace

namespace NrvKameckTurtle {
    NEW_NERVE(KameckTurtleNrvRun, KameckTurtle, Run);
    NEW_NERVE(KameckTurtleNrvWait, KameckTurtle, Wait);
};  // namespace NrvKameckTurtle

KameckTurtle::KameckTurtle(const char* pName)
    : LiveActor(pName), mTurtle(), mRotateQuat(0.0f, 0.0f, 0.0f, 1.0f), mFront(0.0f, 0.0f, 1.0f), mRunDir(0.0f, 0.0f, 1.0f) {
}

void KameckTurtle::init(const TableIter& rIter) {
    MR::initDefaultPos(this, rIter);
    initModelManagerWithAnm("Koura", nullptr, false);
    MR::connectToSceneNoSilhouettedMapObjStrongLight(this);
    MR::startBrk(this, "Koura");
    MR::setBrkFrameAndStop(this, 0.0f);
    initJetTurtle();
    initSound(4, false);
    MR::onCalcGravity(this);
    initBinder(cNum_init_f1, cNum_init_f2, 0);
    MR::setKameckBeamCollisionFilter(this);
    initHitSensor(1);
    MR::initShadowVolumeSphere(this, cNum_init_f3);
    MR::invalidateClipping(this);
    MR::addHitSensorEnemyAttack(this, "attack", 8, cNum_init_f4, Vec3f(0.0f));
    initEffectKeeper(0, "BossKameckBeam");
    initNerve(GET_NERVE(KameckTurtle, KameckTurtleNrvWait));
    makeActorAppeared();
}

void KameckTurtle::initJetTurtle() {
    mTurtle = new JetTurtle("カメックビーム用ジェット亀");
    mTurtle->initWithoutIter();
    mTurtle->becomeSlowType();

    MR::setKameckBeamCollisionFilter(mTurtle);
}

void KameckTurtle::makeActorAppeared() {
    LiveActor::makeActorAppeared();
    setNerve(GET_NERVE(KameckTurtle, KameckTurtleNrvRun));
    mTurtle->kill();
    MR::onBind(this);
    MR::showModel(this);
    MR::validateShadow(this, nullptr);
    MR::validateHitSensors(this);
}

void KameckTurtle::makeActorDead() {
    LiveActor::makeActorDead();
    mEventListener = nullptr;
    mTurtle->kill();
}

void KameckTurtle::control() {
    MR::blendQuatFromGroundAndFront(&mRotateQuat, this, mFront, ::sUpVecBlendRate, ::sFrontVecBlendRate);
}

void KameckTurtle::calcAndSetBaseMtx() {
    MR::setBaseTRMtx(this, mRotateQuat);
    MR::setBaseScale(this, mScale);
}

void KameckTurtle::attackSensor(HitSensor* pSender, HitSensor* pReceiver) {
    if (isEnableAttack()) {
        if (MR::isSensorPlayer(pReceiver) && MR::sendMsgEnemyAttackFlipJump(pReceiver, pSender)) {
            MR::reflectVelocityH(this, pSender, pReceiver, cNum_attackSensor_f1);

            if (!MR::isNearZero(mVelocity)) {
                MR::normalize(mVelocity, &mRunDir);
            }

            if (mEventListener != nullptr) {
                mEventListener->hitBeam(KameckBeam::BeamType_Turtle);
            }
        }
    } else if (!isNerve(GET_NERVE(KameckTurtle, KameckTurtleNrvWait))) {
        appearJetTurtle(false);
        setNerve(GET_NERVE(KameckTurtle, KameckTurtleNrvWait));
    }
}

bool KameckTurtle::receiveMsgPlayerAttack(u32 msg, HitSensor* /*pSender*/, HitSensor* /*pReceiver*/) {
    if (MR::isMsgPlayerTrample(msg)) {
        setNerve(GET_NERVE(KameckTurtle, KameckTurtleNrvWait));
        appearJetTurtle(false);

        return true;
    }

    return false;
}

bool KameckTurtle::receiveOtherMsg(u32 msg, HitSensor* /*pSender*/, HitSensor* /*pReceiver*/) {
    if (msg == ACTMES_IS_PULL_ENABLE) {
        return true;
    }

    if (MR::isMsgItemPull(msg)) {
        setNerve(GET_NERVE(KameckTurtle, KameckTurtleNrvWait));
        appearJetTurtle(true);

        return true;
    }

    return false;
}

void KameckTurtle::appearDirection(const Vec3f& rDir) {
    MR::calcGravity(this);
    mVelocity.set(rDir);
    mRunDir.killElement(rDir, mGravity);

    if (MR::normalizeOrZero(&mRunDir)) {
        MR::makeAxisVerticalZX(&mRunDir, mGravity);
    }

    setNerve(GET_NERVE(KameckTurtle, KameckTurtleNrvRun));
    appear();
}

void KameckTurtle::exeRun() {
    if (MR::isFirstStep(this)) {
        MR::startBck(this, "Wait");
    }

    mRunDir.orthogonalize(mGravity);

    if (MR::normalizeOrZero(&mRunDir)) {
        MR::makeAxisVerticalZX(&mRunDir, mGravity);
    }

    if (MR::isBindedGround(this)) {
        MR::startLevelSound(this, "SE_EM_LV_TURTLE_SLIDE");
    }

    if (!MR::isBindedGround(this)) {
        MR::addVelocityToGravity(this, ::sRunGravity);
    }

    f32 easeInOut = 1.0f;

    if (MR::isGreaterStep(this, ::sDeccelStartTime)) {
        easeInOut = 1.0f - MR::calcNerveEaseInOutRate(this, ::sDeccelStartTime, ::sDeccelEndTime);
    }

    MR::addVelocityMoveToDirection(this, mRunDir, easeInOut * cNum_exeRun_f1);
    MR::rotateDirectionGravityDegree(this, &mFront, ::sRunSpinDegree * easeInOut);
    MR::turnDirectionToGround(this, &mFront);
    MR::attenuateVelocity(this, ::sRunFric);

    if (MR::isBindedWall(this) && MR::calcReboundVelocity(&mVelocity, *MR::getWallNormal(this), cNum_exeRun_f2)) {
        Vec3f dir = MR::getVelocityHorizon(this);

        if (!MR::normalizeOrZero(&dir)) {
            mRunDir.set(dir);
        }

        MR::addVelocityJump(this, ::sRunSpeed);
        MR::startSound(this, "SE_EM_TURTLE_REBOUND");
    }

    if (MR::isGreaterStep(this, ::sRunTime)) {
        MR::emitEffectHit(this, mPosition, "BeamTurtleVanish");
        MR::startSound(this, "SE_BM_KAMECK_DISAPPEAR_TURT");
        kill();
    }
}

void KameckTurtle::exeWait() {

    if (mTurtle->isRestart() || (mTurtle->isWait() && MR::isGreaterStep(this, NebulaFrameClock::stepsOfSeconds(10)))) {
        MR::emitEffectHit(this, mTurtle->mPosition, "BeamTurtleVanish");
        MR::startSound(this, "SE_BM_KAMECK_DISAPPEAR_TURT");
        kill();
    }
}

bool KameckTurtle::isEnableAttack() const {
    if (!isNerve(GET_NERVE(KameckTurtle, KameckTurtleNrvRun))) {
        return false;
    }

    if (MR::isLessStep(this, ::sEnableAttackTime)) {
        return true;
    }

    return false;
}

void KameckTurtle::appearJetTurtle(bool tryTaken) {
    MR::zeroVelocity(this);
    MR::offBind(this);
    MR::invalidateShadow(this, nullptr);
    MR::invalidateHitSensors(this);
    MR::hideModel(this);
    mTurtle->mPosition.set(mPosition);
    mTurtle->becomeSlowType();

    if (tryTaken) {
        mTurtle->appearAndTryTaken();
    } else {
        mTurtle->appear();
    }
}
