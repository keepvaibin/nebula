#include "Game/Enemy/Jellyfish.hpp"
#include "Game/System/NebulaFrameClock.hpp"
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
#include "Game/System/LumaCore.hpp"

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
    const Color8 sPointLightColor(255, 50, 202, 255);
    const f32 sFindDistance = 1000.0f;
    const f32 sThreatDistance = 1500.0f;
    const f32 sThreatFaceAngle = 35.0f;
    const f32 sMaxTiltAngle = 30.0f;
    const f32 sWaitTurnDegree = 0.2f;
    const f32 sThreatTurnDegree = 1.5f;
    const f32 sKnockOutSpeed = 40.0f;
    const s32 sWaitSteps = 280;
    const s32 sDeathSteps = NebulaFrameClock::stepsOfMilliseconds(500);
};  // namespace

Jellyfish::Jellyfish(const char* pName)
    : LiveActor(pName), mController(nullptr), mBindStarPtr(nullptr), mWaveStep(0), mFront(0.0f, 0.0f, 1.0f), mIsConnectedRail(false), mRailSpeed(5.0f),
      mRailGoalWaitStep(60), mIsRailTurnBack(false), mClipCenter(0.0f, 0.0f, 0.0f) {
    lumacore::setClass(this, lumacore::kClassId< Jellyfish >);
}

void Jellyfish::init(const TableIter& rIter) {
    MR::initDefaultPos(this, rIter);
    initModelManagerWithAnm("Jellyfish", nullptr, false);
    MR::connectToSceneEnemy(this);
    MR::addToAttributeGroupSearchTurtle(this);

    initHitSensor(1);
    MR::addHitSensor(this, "body", ATYPE_KILLER_TARGET_ENEMY, 8, 100.0f, Vec3f(0.0f, 30.0f, 0.0f));

    initBinder(130.0f, 0.0f, 0);
    MR::offBind(this);
    initEffectKeeper(2, nullptr);
    MR::addEffectHitNormal(this, nullptr);
    initSound(4, false);
    MR::initShadowVolumeSphere(this, 100.0f);
    MR::setShadowDropLength(this, nullptr, 1900.0f);
    MR::declareCoin(this, 1);
    MR::initStarPointerTarget(this, 100.0f, Vec3f(0.0f, 0.0f, 0.0f));

    mController = new AnimScaleController(nullptr);
    mBindStarPtr = new WalkerStateBindStarPointer(this, mController);

    // On a rail: arg 0 the speed, arg 1 the wait at the goal, arg 2 = 1 to jump back to the start instead of turning round.
    if (MR::isConnectedWithRail(rIter)) {
        mIsConnectedRail = true;

        initRailRider(rIter);
        MR::initAndSetRailClipping(&mClipCenter, this, 100.0f, 500.0f);
        MR::moveCoordToNearestPos(this, mPosition);

        MR::getPlacementArg0NoInit(rIter, &mRailSpeed);
        MR::getPlacementArg1NoInit(rIter, &mRailGoalWaitStep);

        s32 turnBack = 0;
        MR::getPlacementArg2NoInit(rIter, &turnBack);
        mIsRailTurnBack = turnBack == 1;
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
    MR::requestPointLight(this, mPosition, sPointLightColor, 0.9999f, -1);
    mController->updateNerve();

    if (!isNerve(GET_NERVE(Jellyfish, JellyfishNrvDeath))) {
        if (mIsConnectedRail) {
            MR::moveCoordAndFollowTrans(this, mRailSpeed);
            if (MR::isRailReachedGoal(this) && !isNerve(GET_NERVE(Jellyfish, JellyfishNrvRailGoal))) {
                setNerve(GET_NERVE(Jellyfish, JellyfishNrvRailGoal));
                return;
            }
        }

        // Bobs along gravity, one degree of the wave a step.
        f32 wave = MR::sinDegree(mWaveStep + 45);
        mVelocity.scale(wave, mGravity);
        mWaveStep++;
    }

    tryPointerSwoon();
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
    selectNerveThreat();
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

    if (MR::isStep(this, sDeathSteps) || MR::isBinded(this)) {
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
        if (!MR::isNearPlayer(this, sThreatDistance)) {
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
    MR::rotateVecDegree(&mFront, mGravity, -sWaitTurnDegree);
    selectNerveAfterWait();
}

void Jellyfish::exeWaitWithRightTurn() {
    MR::rotateVecDegree(&mFront, mGravity, sWaitTurnDegree);
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
    }

    if (MR::isMsgPlayerHitAll(msg)) {
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

// Turns toward the player, tilting at most 30 degrees off level; a turn nerve also spins it, slowing over the animation.
void Jellyfish::faceToMario() {
    Vec3f toPlayer;
    toPlayer.sub(*MR::getPlayerPos(), mPosition);
    MR::normalizeOrZero(&toPlayer);
    Vec3f side;
    MR::calcSideVec(&side, this);

    if (!MR::isNearZero(toPlayer)) {
        MR::turnVecToVecCosOnPlane(&mFront, toPlayer, side, MR::cosDegree(0.5f));
        Vec3f up;
        up.negate(mGravity);
        Vec3f levelFront;
        MR::turnVecToPlane(&levelFront, mFront, up);
        MR::clampVecAngleDeg(&mFront, levelFront, sMaxTiltAngle);
    }

    if (!isNerve(GET_NERVE(Jellyfish, JellyfishNrvThreat))) {
        f32 frameMax = MR::getBckFrameMax(this, "SearchRotate");
        f32 restRate = (1.0f - (getNerveStep() / frameMax));
        f32 sign = isNerve(GET_NERVE(Jellyfish, JellyfishNrvThreatWithLeftTurn)) ? 1.0f : -1.0f;

        Vec3f up;
        up.negate(mGravity);
        MR::rotateVecDegree(&mFront, up, (sign * (sThreatTurnDegree * restRate)));
    }
}

void Jellyfish::knockOut(HitSensor* pSender, HitSensor* pReceiver) {
    Vec3f toReceiverDir;
    MR::normalize(pReceiver->mPosition - pSender->mPosition, &toReceiverDir);
    mVelocity.scale(sKnockOutSpeed, toReceiverDir);
    mFront.negate(toReceiverDir);
    setNerve(GET_NERVE(Jellyfish, JellyfishNrvDeath));
}

// Near the player it finds; otherwise every wait period picks a plain wait or (from a plain wait) a slow turn either way.
// A rail that jumps back never turns.
bool Jellyfish::selectNerveAfterWait() {
    if (MR::isNearPlayer(this, sFindDistance)) {
        setNerve(GET_NERVE(Jellyfish, JellyfishNrvFind));
        return true;
    }

    if (mIsConnectedRail && mIsRailTurnBack) {
        return false;
    }

    if (!MR::isStep(this, sWaitSteps)) {
        return false;
    }

    s32 pick = isNerve(GET_NERVE(Jellyfish, JellyfishNrvWait)) ? MR::getRandom(0, 3) : 0;
    if (pick == 0) {
        setNerve(GET_NERVE(Jellyfish, JellyfishNrvWait));
    } else if (pick == 1) {
        setNerve(GET_NERVE(Jellyfish, JellyfishNrvWaitWithRightTurn));
    } else {
        setNerve(GET_NERVE(Jellyfish, JellyfishNrvWaitWithLeftTurn));
    }

    return true;
}

// Facing the player within the angle threatens; otherwise turns toward the player's side. False when it keeps threatening.
bool Jellyfish::selectNerveThreat() {
    if (!MR::isNearPlayer(this, sThreatDistance)) {
        setNerve(GET_NERVE(Jellyfish, JellyfishNrvWait));
        return true;
    }

    Vec3f toPlayerDir;
    toPlayerDir.sub(*MR::getPlayerPos(), mPosition);
    MR::normalizeOrZero(&toPlayerDir);

    Vec3f levelToPlayer;
    MR::turnVecToPlane(&levelToPlayer, toPlayerDir, mGravity);

    Vec3f levelFront;
    MR::turnVecToPlane(&levelFront, mFront, mGravity);

    if (levelFront.angle(levelToPlayer) * _180_PI < sThreatFaceAngle) {
        if (isNerve(GET_NERVE(Jellyfish, JellyfishNrvThreat))) {
            return false;
        }

        setNerve(GET_NERVE(Jellyfish, JellyfishNrvThreat));
        return true;
    }

    Vec3f toPlayer;
    toPlayer.sub(*MR::getPlayerPos(), mPosition);
    Vec3f side;
    MR::calcSideVec(&side, this);

    if (toPlayer.dot(side) > 0.0f) {
        setNerve(GET_NERVE(Jellyfish, JellyfishNrvThreatWithLeftTurn));
    } else {
        setNerve(GET_NERVE(Jellyfish, JellyfishNrvThreatWithRightTurn));
    }

    return true;
}

bool Jellyfish::tryPointerSwoon() {
    if (isNerve(GET_NERVE(Jellyfish, JellyfishNrvPointerSwoon)) || isNerve(GET_NERVE(Jellyfish, JellyfishNrvDeath))) {
        return false;
    }

    if (!mBindStarPtr->tryStartPointBind()) {
        return false;
    }

    setNerve(GET_NERVE(Jellyfish, JellyfishNrvPointerSwoon));
    return true;
}
