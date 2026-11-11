#include "Game/Enemy/FireBubble.hpp"
#include "Game/System/NebulaFrameClock.hpp"
#include "Game/LiveActor/Nerve.hpp"
#include "Game/Util.hpp"
#include "Game/Util/Tunable.hpp"
#include "Game/System/LumaCore.hpp"

NEBULA_TUNABLE_NAMED(f32, cNum_init_f1, "init.f1", 40.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_init_f2, "init.f2", 40.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_updateChaseFrontVec_f1, "updateChaseFrontVec.f1", 40.0f);

// The numbers this file's functions give the game's motion and animation calls, as tunables: "<file>.<function>.<call>.<k>".
NEBULA_TUNABLE_NAMED(f32, cNum_exeChase_checkPassBckFrame_1, "exeChase.checkPassBckFrame.1", 0.0f);

namespace {
    NEBULA_TUNABLE(f32, cSensorRadius, 30.0f);
    NEBULA_TUNABLE(f32, cBinderRadius, 40.0f);
    NEBULA_TUNABLE(s32, cCoinMaxNum, 2);
    NEBULA_TUNABLE(s32, cRandomActFrame, 120);
    NEBULA_TUNABLE(f32, cStarWandRadius, 150.0f);
    NEBULA_TUNABLE(f32, cAppearGravity, 0.75f);
    NEBULA_TUNABLE(s32, cChaseInvalidFrame, 300);
    NEBULA_TUNABLE(s32, cForceKillFrame, 600);
    NEBULA_TUNABLE(f32, cChaseStartDistance, 2000.0f);
    NEBULA_TUNABLE(f32, cChaseSpeed, 4.0f);
    NEBULA_TUNABLE(f32, cChaseGravity, 2.0f);
    NEBULA_TUNABLE(f32, cChaseRotateSpeed, 1.0f);
    NEBULA_TUNABLE(s32, cChaseFrame, 30);
    NEBULA_TUNABLE(f32, cReflectGravity, 1.5f);
    const s32 cReflectWaitFrame = NebulaFrameClock::stepsOfSeconds(1);
    NEBULA_TUNABLE(f32, cReflectCursorSpeed, 10.0f);
    const s32 cAppearInterval = NebulaFrameClock::stepsOfSeconds(2);
    NEBULA_TUNABLE_RECORD(Vec, cAppearVelocity, {0.0f, 35.0f, 6.0f});
    NEBULA_TUNABLE_RECORD(Vec, cReflectVelocity, {0.0f, 40.0f, -15.0f});
};  // namespace

namespace NrvFireBubble {
    NEW_NERVE(FireBubbleNrvAppear, FireBubble, Appear);
    NEW_NERVE(FireBubbleNrvWait, FireBubble, Wait);
    NEW_NERVE(FireBubbleNrvChase, FireBubble, Chase);
    NEW_NERVE(FireBubbleNrvReflect, FireBubble, Reflect);
    NEW_NERVE(FireBubbleNrvReflectLand, FireBubble, ReflectLand);
    NEW_NERVE(FireBubbleNrvReflectWait, FireBubble, ReflectWait);
    NEW_NERVE(FireBubbleNrvDown, FireBubble, Down);
    NEW_NERVE(FireBubbleNrvWaitToAppear, FireBubble, WaitToAppear);
};  // namespace NrvFireBubble

FireBubble::FireBubble(const char* pName)
    : LiveActor(pName), mIsValidInfo(), mAppearPos(gZeroVec), mAppearFront(0.0f, 0.0f, 1.0f), mAppearVelocity(::cAppearVelocity), mFront(0.0f, 0.0f, 1.0f), mActFrame(),
      mChaseCounter() {
    lumacore::setClass(this, lumacore::kClassId< FireBubble >);
}

inline void FireBubble::initHitSensors(const char* const hitSensor1, const char* const hitSensor2) {
    initHitSensor(1);
    MR::addHitSensorAtJointEnemy(this, hitSensor1, hitSensor2, 8, ::cSensorRadius, Vec3f(0.0f, 0.0f, 0.0f));
}

void FireBubble::init(const TableIter& rIter) {
    if (MR::isValidInfo(rIter)) {
        initMapToolInfo(rIter);
    }

    initModelManagerWithAnm("FireBubble", nullptr, false);
    MR::connectToSceneEnemy(this);
    initHitSensors("body", "Body");
    initBinder(::cBinderRadius, cNum_init_f1, 0);
    initEffectKeeper(1, nullptr);
    MR::initStarPointerTargetAtJoint(this, "Body", ::cStarWandRadius, Vec3f(0.0f, 0.0f, 0.0f));
    initSound(4, false);
    MR::initShadowVolumeCylinder(this, cNum_init_f2);
    MR::invalidateClipping(this);
    MR::declareCoin(this, ::cCoinMaxNum);
    initNerve(GET_NERVE(FireBubble, FireBubbleNrvAppear));

    if (mIsValidInfo) {
        MR::syncStageSwitchAppear(this);
        makeActorDead();
    } else {
        makeActorDead();
    }
}

void FireBubble::appear() {
    mPosition.set(mAppearPos);
    mFront.set(mAppearFront);
    MR::calcGravity(this);
    LiveActor::appear();
    setNerve(GET_NERVE(FireBubble, FireBubbleNrvAppear));
}

void FireBubble::appear(const Vec3f& rVec1, const Vec3f& rVec2, const Vec3f& rVec3) {
    mAppearPos.set(rVec1);
    mAppearFront.set(rVec2);
    mAppearVelocity.set(rVec3);
    appear();
}

void FireBubble::kill() {
    LiveActor::kill();
    if (!mIsValidInfo) {
        MR::emitEffect(this, "Down");
    }
}

void FireBubble::control() {
}

void FireBubble::calcAndSetBaseMtx() {
    Vec3f minusGravity;
    Pos3f baseMtx;
    minusGravity.negate(mGravity);

    if (MR::isSameDirection(mFront, minusGravity)) {
        baseMtx.set(getBaseMtx());
        baseMtx.setTrans(mPosition);
    } else {
        MR::makeMtxUpFrontPos(&baseMtx, minusGravity, mFront, mPosition);
    }

    MR::setBaseTRMtx(this, baseMtx);
}

void FireBubble::attackSensor(HitSensor* pSender, HitSensor* pReceiver) {
    if (!isNerve(GET_NERVE(FireBubble, FireBubbleNrvAppear)) && !isNerve(GET_NERVE(FireBubble, FireBubbleNrvDown)) &&
        !isNerve(GET_NERVE(FireBubble, FireBubbleNrvWaitToAppear))) {
        if (MR::isSensorPlayer(pReceiver) && MR::sendMsgEnemyAttackFire(pReceiver, pSender)) {
            killOrWaitToApppear();
        }
    }
}

bool FireBubble::receiveMsgPlayerAttack(u32 msg, HitSensor* /*pSender*/, HitSensor* /*pReceiver*/) {
    if (MR::isMsgLockOnStarPieceShoot(msg)) {
        return true;
    }

    if (MR::isMsgStarPieceAttack(msg)) {
        MR::appearCoinPop(this, mPosition, 1);
        killOrWaitToApppear();
        return true;
    }

    return false;
}

void FireBubble::initMapToolInfo(const TableIter& rIter) {
    mIsValidInfo = true;

    MR::initDefaultPos(this, rIter);
    MR::needStageSwitchReadAppear(this, rIter);
    mAppearPos.set(mPosition);
    MR::calcGravity(this);

    Pos3f Mtx;
    Vec3f minusGravity;
    Vec3f rotateZDir;
    minusGravity.negate(mGravity);

    MR::makeMtxRotate(Mtx, mRotation);
    Mtx.getZDir(rotateZDir);

    MR::makeMtxUpFront(&Mtx, minusGravity, rotateZDir);
    Mtx.getZDir(mAppearFront);
}

void FireBubble::updateChaseFrontVec(f32 turnDegree) {
    Vec3f toPlayer;
    Vec3f down;
    Vec3f pos;
    Vec3f playerUpVec;

    MR::getPlayerUpVec(&playerUpVec);
    MR::normalize(&playerUpVec);
    playerUpVec.scale(cNum_updateChaseFrontVec_f1);

    pos.add(*MR::getPlayerPos(), playerUpVec);
    toPlayer.sub(pos, *getPosition());

    if (!MR::normalizeOrZero(&toPlayer)) {
        if (MR::isBindedGround(this)) {
            down.negate(*MR::getGroundNormal(this));
        } else {
            down.set(mGravity);
        }

        if (!MR::isSameDirection(toPlayer, down)) {
            MR::vecKillElement(toPlayer, down, &toPlayer);
            MR::normalize(&toPlayer);

            Vec3f copy(mFront);

            MR::turnVecToVecCos(&mFront, copy, toPlayer, MR::cosDegree(turnDegree), down);
        }
    }
}

void FireBubble::updateChaseFrontVecAndVelocity(f32 turnDegree) {
    updateChaseFrontVec(turnDegree);
    mVelocity.set(mFront);
    mVelocity.mult(::cChaseSpeed);
}

void FireBubble::updateGravity(f32 strength) {
    mVelocity.add(mGravity * strength);
}

void FireBubble::calcReflectVelocity(Vec3f* pOut) const {
    Vec3f toPlayer;

    toPlayer.sub(*MR::getPlayerPos(), mPosition);

    if (MR::normalizeOrZero(&toPlayer)) {
        MR::calcFrontVec(&toPlayer, this);
    }

    Vec3f upVec;
    Pos3f rotateMatrix;
    MR::calcUpVec(&upVec, this);

    MR::makeMtxUpFront(&rotateMatrix, upVec, toPlayer);

    rotateMatrix.mult33(Vec3f(::cReflectVelocity), *pOut);
}

bool FireBubble::tryChaseEnd() {
    mChaseCounter++;

    if (mActFrame < mChaseCounter) {
        MR::onCalcGravity(this);
        setNerve(GET_NERVE(FireBubble, FireBubbleNrvDown));
        return true;
    }

    return false;
}

bool FireBubble::tryReflect() {
    if (MR::isStarPointerPointing2POnPressButton(this, "弱", true, false)) {
        Vec2f starPointerScreenVelocity(*MR::getStarPointerScreenVelocity(*MR::getStarPointerLastPointedPort(this)));
        if (::cReflectCursorSpeed < starPointerScreenVelocity.length()) {
            MR::onCalcGravity(this);
            setNerve(GET_NERVE(FireBubble, FireBubbleNrvReflect));
            return true;
        }
    }

    return false;
}

void FireBubble::killOrWaitToApppear() {
    if (mIsValidInfo) {
        MR::emitEffect(this, "Down");
        setNerve(GET_NERVE(FireBubble, FireBubbleNrvWaitToAppear));
    } else {
        kill();
    }
}

bool FireBubble::isMovingDown() {
    Vec3f normalisedVel;
    return !MR::normalizeOrZero(mVelocity, &normalisedVel) && normalisedVel.dot(mGravity) < 0.0f;
}

void FireBubble::exeAppear() {
    if (MR::isFirstStep(this)) {
        MR::startBck(this, "Appear");
        MR::emitEffect(this, "Wait");
        MR::startSound(this, "SE_EM_FIRE_BUBBLE_LAUNCH");
        MR::showModel(this);
        MR::onCalcGravity(this);
        MR::calcGravity(this);
        MR::offBind(this);
        MR::validateHitSensors(this);

        calcAndSetBaseMtx();
        Pos3f baseMtx;
        baseMtx.set(getBaseMtx());
        baseMtx.mult33(mAppearVelocity, mVelocity);

        mChaseCounter = 0;
    }

    if (!MR::isNoBind(this) && MR::isOnGround(this)) {
        MR::startSound(this, "SE_EM_FIRE_BUBBLE_JUMP");
        setNerve(GET_NERVE(FireBubble, FireBubbleNrvWait));
    } else {
        updateGravity(::cAppearGravity);

        if (MR::isNoBind(this) && !isMovingDown()) {
            MR::onBind(this);
        }
    }
}

void FireBubble::exeWait() {
    if (MR::isFirstStep(this)) {
        MR::startBck(this, "Wait");
        mVelocity.zero();
        mActFrame = MR::getRandom((s32)0, ::cRandomActFrame) + ::cForceKillFrame;
    }

    if (MR::isNearPlayer(this, ::cChaseStartDistance) && tryReflect()) {
        return;
    }

    if (MR::isGreaterStep(this, ::cChaseFrame) && MR::isNearPlayer(this, ::cChaseStartDistance)) {
        MR::startSound(this, "SE_EM_FIRE_BUBBLE_JUMP");
        mActFrame = MR::getRandom((s32)0, ::cRandomActFrame) + ::cChaseInvalidFrame;
        setNerve(GET_NERVE(FireBubble, FireBubbleNrvChase));
    } else if (MR::isStep(this, mActFrame)) {
        setNerve(GET_NERVE(FireBubble, FireBubbleNrvDown));
    }
}

void FireBubble::exeChase() {
    if (MR::isFirstStep(this)) {
        MR::startBck(this, "Wait");
        MR::emitEffect(this, "Wait");
    }

    updateChaseFrontVecAndVelocity(::cChaseRotateSpeed);
    updateGravity(::cChaseGravity);

    if (MR::checkPassBckFrame(this, cNum_exeChase_checkPassBckFrame_1)) {
        MR::startSound(this, "SE_EM_FIRE_BUBBLE_JUMP");
    }

    if (!tryChaseEnd()) {
        tryReflect();
    }
}

void FireBubble::exeReflect() {
    if (MR::isFirstStep(this)) {
        MR::startBck(this, "DPDHit");
        calcReflectVelocity(&mVelocity);
        MR::deleteEffect(this, "Wait");
        MR::emitEffect(this, "WaitS");
        MR::startSound(this, "SE_EM_FIRE_BUBBLE_REFLECT");
        MR::start2PAttackAssistSound();
    }

    updateChaseFrontVec(::cChaseRotateSpeed);
    mChaseCounter++;

    if (MR::isOnGround(this)) {
        setNerve(GET_NERVE(FireBubble, FireBubbleNrvReflectLand));
    } else {
        updateGravity(::cReflectGravity);
    }
}

void FireBubble::exeReflectLand() {
    if (MR::isFirstStep(this)) {
        MR::startBck(this, "DPDHitDown");
        mVelocity.zero();
        MR::offCalcGravity(this);
    }

    if (!tryChaseEnd() && !tryReflect()) {
        MR::setNerveAtBckStopped(this, GET_NERVE(FireBubble, FireBubbleNrvReflectWait));
    }
}

void FireBubble::exeReflectWait() {
    if (MR::isFirstStep(this)) {
        MR::startBck(this, "DPDHitDownWait");
    }

    if (!tryChaseEnd() && !tryReflect() && MR::isStep(this, ::cReflectWaitFrame)) {
        MR::deleteEffect(this, "WaitS");
        MR::onCalcGravity(this);
        setNerve(GET_NERVE(FireBubble, FireBubbleNrvChase));
    }
}

void FireBubble::exeDown() {
    if (MR::isFirstStep(this)) {
        MR::startBck(this, "Down");
        MR::deleteEffect(this, "Wait");
        MR::deleteEffect(this, "WaitS");
        MR::startSound(this, "SE_EM_FIRE_BUBBLE_DIE");
        mVelocity.zero();
        MR::invalidateHitSensors(this);
    }

    updateChaseFrontVecAndVelocity(::cChaseRotateSpeed);

    updateGravity(::cChaseGravity);
    if (MR::isBckStopped(this)) {
        killOrWaitToApppear();
    }
}

void FireBubble::exeWaitToAppear() {
    if (MR::isFirstStep(this)) {
        MR::hideModel(this);
        MR::offCalcGravity(this);
        MR::offBind(this);
        MR::invalidateHitSensors(this);
    }

    if (MR::isStep(this, ::cAppearInterval)) {
        mPosition.set(mAppearPos);
        mFront.set(mAppearFront);
        mAppearVelocity.set(::cAppearVelocity);
        MR::calcGravity(this);
        setNerve(GET_NERVE(FireBubble, FireBubbleNrvAppear));
    }
}
