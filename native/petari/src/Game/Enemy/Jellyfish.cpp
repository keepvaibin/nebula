#include "Game/Enemy/Jellyfish.hpp"
#include "Game/Enemy/AnimScaleController.hpp"
#include "Game/Enemy/WalkerStateBindStarPointer.hpp"
#include "Game/LiveActor/HitSensor.hpp"
#include "Game/LiveActor/Nerve.hpp"
#include "Game/Util/ActorMovementUtil.hpp"
#include "Game/Util/ActorSensorUtil.hpp"
#include "Game/Util/ActorShadowUtil.hpp"
#include "Game/Util/ActorStateUtil.hpp"
#include "Game/Util/Color.hpp"
#include "Game/Util/EffectUtil.hpp"
#include "Game/Util/PlacementUtil.hpp"
#include "Game/Util/LightUtil.hpp"
#include "Game/Util/LiveActorUtil.hpp"
#include "Game/Util/MathUtil.hpp"
#include "Game/Util/ModelUtil.hpp"
#include "Game/Util/MtxUtil.hpp"
#include "Game/Util/ObjUtil.hpp"
#include "Game/Util/PlayerUtil.hpp"
#include "Game/Util/RailUtil.hpp"
#include "Game/Util/SoundUtil.hpp"
#include "Game/Util/StarPointerUtil.hpp"

namespace NrvJellyfish {
    NEW_NERVE(JellyfishNrvWait, Jellyfish, Wait);
    NEW_NERVE(JellyfishNrvWaitWithRightTurn, Jellyfish, WaitWithRightTurn);
    NEW_NERVE(JellyfishNrvWaitWithLeftTurn, Jellyfish, WaitWithLeftTurn);
    NEW_NERVE(JellyfishNrvFind, Jellyfish, Find);
    NEW_NERVE(JellyfishNrvThreat, Jellyfish, Threat);
    NEW_NERVE(JellyfishNrvThreatWithRightTurn, Jellyfish, ThreatWithRightTurn);
    NEW_NERVE(JellyfishNrvThreatWithLeftTurn, Jellyfish, ThreatWithLeftTurn);
    NEW_NERVE(JellyfishNrvDeath, Jellyfish, Death);
    NEW_NERVE(JellyfishNrvAttack, Jellyfish, Attack);
    NEW_NERVE(JellyfishNrvRailGoal, Jellyfish, RailGoal);
    NEW_NERVE(JellyfishNrvPointerSwoon, Jellyfish, PointerSwoon);
};  // namespace NrvJellyfish

namespace {
    static Color8 sPointLightColor(255, 50, 202, 255);
};  // namespace

Jellyfish::Jellyfish(const char* pName)
    : LiveActor(pName), mController(), mWaveStep(), mFront(0.0f, 0.0f, 1.0f), mIsConnectedRail(), mRailSpeed(5.0f), mRailGoalWaitStep(60), mIsRailTurnBack(), mClipCenter(0.0f, 0.0f, 0.0f) {
}

void Jellyfish::init(const TableIter& rIter) {
    MR::initDefaultPos(this, rIter);

    initModelManagerWithAnm("Jellyfish", nullptr, false);

    MR::connectToSceneEnemy(this);

    MR::addToAttributeGroupSearchTurtle(this);

    initHitSensor(1);
    Vec3f offset;
    offset.set(0.0f, 30.0f, 0.0f);
    MR::addHitSensor(this, "body", ATYPE_KILLER_TARGET_ENEMY, 8, 100.0f, offset);

    initBinder(130.0f, 0.0f, 0);
    MR::offBind(this);

    mController = new AnimScaleController(nullptr);

    initEffectKeeper(2, nullptr);

    MR::addEffectHitNormal(this, nullptr);

    initSound(4, false);

    MR::initShadowVolumeSphere(this, 100.0f);
    MR::setShadowDropLength(this, nullptr, 1900.0f);

    MR::declareCoin(this, 1);

    MR::initStarPointerTarget(this, 100.0f, Vec3f(0.0f, 0.0f, 0.0f));

    mController = new AnimScaleController(nullptr);
    mBindStarPtr = new WalkerStateBindStarPointer(this, mController);

    if (MR::isConnectedWithRail(rIter)) {
        mIsConnectedRail = true;

        initRailRider(rIter);
        MR::initAndSetRailClipping(&mClipCenter, this, 100.0f, 500.0f);
        MR::moveCoordToNearestPos(this, mPosition);

        MR::getPlacementArg0NoInit(rIter, &mRailSpeed);
        MR::getPlacementArg1NoInit(rIter, &mRailGoalWaitStep);

        s32 arg2 = 0;
        MR::getPlacementArg2NoInit(rIter, &arg2);
        mIsRailTurnBack = arg2 == 1;
    }

    MR::calcFrontVec(&mFront, this);

    initNerve(GET_NERVE(Jellyfish, JellyfishNrvWait));

    makeActorAppeared();
}

void Jellyfish::kill() {
    MR::emitEffect(this, "Death");

    MR::appearCoinPop(this, mPosition, 1);

    LiveActor::kill();
}

void Jellyfish::control() {
    const Vec3f& rPosition = mPosition;
    MR::requestPointLight(this, Vec3f(rPosition), ::sPointLightColor, 0.9999f, -1);

    mController->updateNerve();

    if (!isNerve(GET_NERVE(Jellyfish, JellyfishNrvDeath))) {
        if (mIsConnectedRail) {
            MR::moveCoordAndFollowTrans(this, mRailSpeed);
            if (MR::isRailReachedGoal(this) && !isNerve(GET_NERVE(Jellyfish, JellyfishNrvRailGoal))) {
                setNerve(GET_NERVE(Jellyfish, JellyfishNrvRailGoal));
                return;
            }
        }

        f32 wave = MR::sinDegree(mWaveStep + 45);
        const Vec3f& rGravity = mGravity;
        mVelocity.scale(wave, rGravity);

        mWaveStep++;
    }

    if (tryPointerSwoon()) {
    }
}

void Jellyfish::calcAndSetBaseMtx() {
    Pos3f baseMtx;
    MR::makeMtxFrontUpPos(&baseMtx, mFront, -mGravity, mPosition);
    MR::setBaseTRMtx(this, baseMtx);
    Vec3f baseScale(mController->mScale * mScale);
    MR::setBaseScale(this, baseScale);
}

void Jellyfish::exeWait() {
    if (MR::isFirstStep(this) && !MR::isBckPlaying(this, "Wait")) {
        MR::startBck(this, "Wait");
        MR::startBrk(this, "Wait");
    }

    MR::startLevelSound(this, "SE_EM_LV_JELLYFISH_WAIT");
    selectNerveAfterWait();
}

void Jellyfish::exeFind() {
    if (MR::isFirstStep(this)) {
        MR::startBck(this, "SearchOn");
        MR::startBrk(this, "SearchOn");

        MR::startSound(this, "SE_EM_JELLYFISH_FIND");
    }

    faceToMario();

    if (MR::isBckStopped(this)) {
        selectNerveThreat();
    }
}

void Jellyfish::exeThreat() {
    if (MR::isBckOneTimeAndStopped(this)) {
        MR::startBck(this, "SearchWait");
        MR::startBrk(this, "SearchWait");
    }

    MR::startLevelSound(this, "SE_EM_LV_JELLYFISH_WAIT");

    faceToMario();

    if (selectNerveThreat()) {
        return;
    }
}

void Jellyfish::exeDeath() {
    if (MR::isFirstStep(this)) {
        MR::onBind(this);

        MR::invalidateHitSensors(this);
        MR::invalidateClipping(this);

        MR::stopScene(5);

        MR::startBck(this, "Death");
        MR::startBrk(this, "Death");

        MR::startBlowHitSound(this);

        MR::startSound(this, "SE_EM_JELLYFISH_HIT_PUNCH");
    }

    if (MR::isStep(this, 30) || MR::isBinded(this)) {
        MR::startSound(this, "SE_EM_JELLYFISH_DEAD");
        kill();
    }
}

void Jellyfish::exeAttack() {
    if (MR::isFirstStep(this)) {
        MR::startBck(this, "Attack");
        MR::startBrk(this, "Attack");

        MR::startSound(this, "SE_EM_JELLYFISH_ATTACK");
    }

    if (MR::isBckStopped(this)) {
        if (!MR::isNearPlayer(this, 1500.0f)) {
            setNerve(GET_NERVE(Jellyfish, JellyfishNrvWait));
        } else {
            selectNerveThreat();
        }
    }
}

void Jellyfish::exeRailGoal() {
    if (MR::isStep(this, mRailGoalWaitStep)) {
        if (mIsRailTurnBack) {
            MR::moveCoordAndTransToRailPoint(this, 0);
            mFront.set(MR::getRailDirection(this));
        } else {
            MR::reverseRailDirection(this);
        }

        setNerve(GET_NERVE(Jellyfish, JellyfishNrvWait));
    }
}

void Jellyfish::exeThreatWithLeftTurn() {
    threatTurn();
}

void Jellyfish::exeThreatWithRightTurn() {
    threatTurn();
}

void Jellyfish::exePointerSwoon() {
    MR::updateActorStateAndNextNerve(this, mBindStarPtr, GET_NERVE(Jellyfish, JellyfishNrvWait));
}

void Jellyfish::exeWaitWithLeftTurn() {
    MR::rotateVecDegree(&mFront, mGravity, -0.2f);
    selectNerveAfterWait();
}

void Jellyfish::exeWaitWithRightTurn() {
    MR::rotateVecDegree(&mFront, mGravity, 0.2f);
    selectNerveAfterWait();
}

void Jellyfish::attackSensor(HitSensor* pSender, HitSensor* pReceiver) {
    if (isNerve(GET_NERVE(Jellyfish, JellyfishNrvDeath))) {
        return;
    }

    if (MR::isSensorEnemy(pSender) && MR::isSensorPlayer(pReceiver)) {
        if (!isNerve(GET_NERVE(Jellyfish, JellyfishNrvPointerSwoon)) && MR::sendMsgEnemyAttackStrong(pReceiver, pSender)) {
            MR::emitEffectHitBetweenSensors(this, pSender, pReceiver, 0.0f, nullptr);
            setNerve(GET_NERVE(Jellyfish, JellyfishNrvAttack));
        } else {
            MR::sendMsgPush(pReceiver, pSender);
        }
    } else if ((MR::isSensorEnemy(pReceiver) || MR::isSensorMapObj(pReceiver)) && MR::isSensorEnemy(pSender)) {
        MR::sendMsgPush(pReceiver, pSender);
    }
}

bool Jellyfish::receiveMsgPlayerAttack(u32 msg, HitSensor* pSender, HitSensor* pReceiver) {
    if (MR::isMsgLockOnStarPieceShoot(msg)) {
        return true;
    }

    if (MR::isMsgStarPieceAttack(msg)) {
        mController->startHitReaction();
        return true;
    } else if (MR::isMsgPlayerHitAll(msg)) {
        knockOut(pSender, pReceiver);
        return true;
    }

    return false;
}

bool Jellyfish::receiveMsgEnemyAttack(u32 msg, HitSensor* pSender, HitSensor* pReceiver) {
    if (MR::isMsgExplosionAttack(msg) && !isNerve(GET_NERVE(Jellyfish, JellyfishNrvDeath))) {
        knockOut(pSender, pReceiver);
        return true;
    }

    return false;
}

void Jellyfish::threatTurn() {
    if (MR::isFirstStep(this)) {
        MR::startBck(this, "SearchRotate");
        MR::startBrk(this, "SearchRotate");
    }

    MR::startLevelSound(this, "SE_EM_LV_JELLYFISH_WAIT");

    faceToMario();

    if (MR::isBckStopped(this)) {
        selectNerveThreat();
    }
}

bool Jellyfish::faceToMario() {
    Vec3f v13;
    const Vec3f& rPosition = mPosition;
    v13.sub(*MR::getPlayerPos(), rPosition);
    MR::normalizeOrZero(&v13);
    Vec3f v12;
    MR::calcSideVec(&v12, this);

    if (!MR::isNearZero(v13)) {
        MR::turnVecToVecCosOnPlane(&mFront, v13, v12, MR::cosDegree(0.5f));
        Vec3f v11;
        Vec3f v10;
        const Vec3f& rGravity = mGravity;
        v10.negate(rGravity);
        const Vec3f& rUp = v10;
        MR::turnVecToPlane(&v11, mFront, rUp);
        MR::clampVecAngleDeg(&mFront, v11, 30.0f);
    }

    if (!isNerve(GET_NERVE(Jellyfish, JellyfishNrvThreat))) {
        f32 frameMax = MR::getBckFrameMax(this, "SearchRotate");
        f32 v8;
        f32 v7 = (1.0f - (getNerveStep() / frameMax));
        if (isNerve(GET_NERVE(Jellyfish, JellyfishNrvThreatWithLeftTurn))) {
            v8 = 1.0f;
        } else {
            v8 = -1.0f;
        }

        Vec3f v9;
        const Vec3f& rGravity = mGravity;
        v9.negate(rGravity);
        const Vec3f& rUp = v9;
        MR::rotateVecDegree(&mFront, rUp, (v8 * (1.5f * v7)));
    }

    return true;
}

void Jellyfish::knockOut(HitSensor* pSender, HitSensor* pReceiver) {
    Vec3f toReceiverDir;
    MR::normalize(pReceiver->mPosition - pSender->mPosition, &toReceiverDir);

    mVelocity.scale(40.0f, toReceiverDir);

    mFront.negate(toReceiverDir);

    setNerve(GET_NERVE(Jellyfish, JellyfishNrvDeath));
}

bool Jellyfish::selectNerveAfterWait() {
    if (MR::isNearPlayer(this, 1000.0f)) {
        setNerve(GET_NERVE(Jellyfish, JellyfishNrvFind));
        return true;
    }

    if (mIsConnectedRail && mIsRailTurnBack) {
        return false;
    }

    if (MR::isStep(this, 280)) {
        s32 randomVal;
        if (isNerve(GET_NERVE(Jellyfish, JellyfishNrvWait))) {
            randomVal = MR::getRandom(0, 3);
        } else {
            randomVal = 0;
        }

        if (randomVal == 0) {
            setNerve(GET_NERVE(Jellyfish, JellyfishNrvWait));
        } else if (randomVal == 1) {
            setNerve(GET_NERVE(Jellyfish, JellyfishNrvWaitWithRightTurn));
        } else {
            setNerve(GET_NERVE(Jellyfish, JellyfishNrvWaitWithLeftTurn));
        }

        return true;
    }

    return false;
}

bool Jellyfish::selectNerveThreat() {
    if (!MR::isNearPlayer(this, 1500.0f)) {
        setNerve(GET_NERVE(Jellyfish, JellyfishNrvWait));
        return true;
    }

    Vec3f vec2C;
    vec2C.sub(*MR::getPlayerPos(), mPosition);
    MR::normalizeOrZero(&vec2C);

    Vec3f vec38;
    MR::turnVecToPlane(&vec38, vec2C, mGravity);

    Vec3f vec44;
    MR::turnVecToPlane(&vec44, mFront, mGravity);

    if (vec44.angle(vec38) * _180_PI < 35.0f) {
        if (isNerve(GET_NERVE(Jellyfish, JellyfishNrvThreat))) {
            return false;
        }

        setNerve(GET_NERVE(Jellyfish, JellyfishNrvThreat));
    } else {
        Vec3f vec50;
        vec50.sub(*MR::getPlayerPos(), mPosition);

        Vec3f sideVec;
        MR::calcSideVec(&sideVec, this);

        if (vec50.dot(sideVec) > 0.0f) {
            setNerve(GET_NERVE(Jellyfish, JellyfishNrvThreatWithLeftTurn));
        } else {
            setNerve(GET_NERVE(Jellyfish, JellyfishNrvThreatWithRightTurn));
        }
    }

    return true;
}

bool Jellyfish::tryPointerSwoon() {
    if (isNerve(GET_NERVE(Jellyfish, JellyfishNrvPointerSwoon))) {
        return false;
    }

    if (isNerve(GET_NERVE(Jellyfish, JellyfishNrvDeath))) {
        return false;
    }

    if (!mBindStarPtr->tryStartPointBind()) {
        return false;
    }

    setNerve(GET_NERVE(Jellyfish, JellyfishNrvPointerSwoon));
    return true;
}
