#include "Game/Enemy/Karon.hpp"
#include "Game/System/NebulaFrameClock.hpp"
#include "Game/Enemy/AnimScaleController.hpp"
#include "Game/Enemy/TerritoryMover.hpp"
#include "Game/Enemy/WalkerStateBindStarPointer.hpp"
#include "Game/LiveActor/Binder.hpp"
#include "Game/LiveActor/HitSensor.hpp"
#include "Game/LiveActor/Nerve.hpp"
#include "Game/Util/ActorMovementUtil.hpp"
#include "Game/Util/ActorSensorUtil.hpp"
#include "Game/Util/ActorShadowUtil.hpp"
#include "Game/Util/ActorStateUtil.hpp"
#include "Game/Util/ActorSwitchUtil.hpp"
#include "Game/Util/EffectUtil.hpp"
#include "Game/Util/PlacementUtil.hpp"
#include "Game/Util/LiveActorUtil.hpp"
#include "Game/Util/MapUtil.hpp"
#include "Game/Util/MathUtil.hpp"
#include "Game/Util/ObjUtil.hpp"
#include "Game/Util/PlayerUtil.hpp"
#include "Game/Util/SoundUtil.hpp"
#include "Game/Util/StarPointerUtil.hpp"
#include <nebula_geometry.hpp>
#include "Game/Util/Tunable.hpp"

NEBULA_TUNABLE_NAMED(f32, cNum_exeTurn_f1, "exeTurn.f1", 2.0f);

namespace {
    NEBULA_TUNABLE(f32, cBinderRadius, 70.0f);
    // Body sensor radius and height, both times the placement's Y scale.
    NEBULA_TUNABLE(f32, cBodySize, 90.0f);
    NEBULA_TUNABLE(f32, cShadowRadius, 60.0f);
    NEBULA_TUNABLE(f32, cStarPointerRadius, 100.0f);
    NEBULA_TUNABLE(f32, cStarPointerOffsetY, 50.0f);
    NEBULA_TUNABLE(s32, cGroupClippingNum, 32);
    NEBULA_TUNABLE(f32, cTerritoryRadius, 500.0f);
    NEBULA_TUNABLE(f32, cPursueDistance, 700.0f);
    NEBULA_TUNABLE(f32, cPursueGiveUpDistance, 1200.0f);
    NEBULA_TUNABLE(f32, cFaceToDegree, 80.0f);
    NEBULA_TUNABLE(f32, cFollowRailDistance, 600.0f);
    const s32 cWaitTime = NebulaFrameClock::stepsOfSeconds(2);
    const s32 cStarPieceHitWaitTime = NebulaFrameClock::stepsOfSeconds(1);
    const s32 cWalkLimitTime = NebulaFrameClock::stepsOfSeconds(3);
    NEBULA_TUNABLE(f32, cGoalDist, 40.0f);
    // A turn may end only after this many steps.
    NEBULA_TUNABLE(s32, cTurnMinTime, 10);
    const s32 cBrokenTime = NebulaFrameClock::stepsOfSeconds(5);
    NEBULA_TUNABLE(f32, cPrePursueJumpVel, 10.0f);
    NEBULA_TUNABLE(f32, cHitReactionVelH, 10.0f);
    NEBULA_TUNABLE(f32, cHitReactionVelV, 20.0f);
    // Player counted as above Karon within the body radius and this share of it.
    NEBULA_TUNABLE(f32, cPlayerUpRate, 0.25f);
    NEBULA_TUNABLE(f32, cPushRate, 2.0f);
    // Ledge probe: distance ahead, height above, depth below.
    NEBULA_TUNABLE(f32, cFallCheckAhead, 150.0f);
    NEBULA_TUNABLE(f32, cFallCheckUpper, 140.0f);
    NEBULA_TUNABLE(f32, cFallCheckLower, 150.0f);
    // forceBindOnGround(offset, scale) when broken: first step, then while airborne.
    NEBULA_TUNABLE(f32, cBrokenBindOffsetStart, 0.0f);
    NEBULA_TUNABLE(f32, cBrokenBindMulStart, 1.0f);
    NEBULA_TUNABLE(f32, cBrokenBindOffset, 1.0f);
    NEBULA_TUNABLE(f32, cBrokenBindMul, 0.8f);
    NEBULA_TUNABLE(s32, cStarPieceNum, 3);
    NEBULA_TUNABLE(f32, cStarPieceSpeedH, 10.0f);
    NEBULA_TUNABLE(f32, cStarPieceSpeedV, 40.0f);

    NEBULA_TUNABLE_RECORD(MR::ActorMoveParam, cNoMoveNoTurnParam, {0.0f, 1.2f, 0.8f, 0.0f});
    NEBULA_TUNABLE_RECORD(MR::ActorMoveParam, cNoMoveNoTurnOnAirParam, {0.0f, 2.0f, 0.95f, 0.0f});
    NEBULA_TUNABLE_RECORD(MR::ActorMoveParam, cWalkParam, {0.12f, 1.2f, 0.95f, 2.0f});
    NEBULA_TUNABLE_RECORD(MR::ActorMoveParam, cSearchParam, {0.0f, 1.2f, 0.95f, 2.0f});
    NEBULA_TUNABLE_RECORD(MR::ActorMoveParam, cPursueParam, {0.25f, 1.2f, 0.95f, 2.0f});
    NEBULA_TUNABLE_RECORD(MR::ActorMoveParam, cHitReactionOnGroundParam, {0.0f, 1.2f, 0.8f, 5.0f});
    NEBULA_TUNABLE_RECORD(MR::ActorMoveParam, cHitReactionAirParam, {0.0f, 2.0f, 0.95f, 5.0f});
}  // namespace

namespace NrvKaron {
    NEW_NERVE(HostTypeNrvFixWait, Karon, FixWait);
    NEW_NERVE(HostTypeNrvWait, Karon, Wait);
    NEW_NERVE(HostTypeNrvStarPieceHitWait, Karon, Wait);
    NEW_NERVE(HostTypeNrvWalk, Karon, Walk);
    NEW_NERVE(HostTypeNrvTurn, Karon, Turn);
    NEW_NERVE(HostTypeNrvWalkOnRail, Karon, WalkOnRail);
    NEW_NERVE(HostTypeNrvSearch, Karon, Search);
    NEW_NERVE_ONEND(HostTypeNrvPursue, Karon, Pursue, Pursue);
    NEW_NERVE(HostTypeNrvBroken, Karon, Broken);
    NEW_NERVE(HostTypeNrvStepBroken, Karon, StepBroken);
    NEW_NERVE(HostTypeNrvRecover, Karon, Recover);
    NEW_NERVE(HostTypeNrvHitReaction, Karon, HitReaction);
    NEW_NERVE(HostTypeNrvStarPieceHit, Karon, HitReaction);
    NEW_NERVE(HostTypeNrvDeath, Karon, Death);
    NEW_NERVE(HostTypeNrvSinkDown, Karon, SinkDown);
    NEW_NERVE_ONEND(HostTypeNrvBindStarPointer, Karon, BindStarPointer, BindStarPointer);
};  // namespace NrvKaron

Karon::Karon(const char* pName)
    : LiveActor(pName), mTerritoryMover(nullptr), mStopSwAnim(false), mScaleController(nullptr), mStateStarPointer(nullptr) {
}

void Karon::init(const TableIter& rIter) {
    MR::initDefaultPos(this, rIter);
    MR::useStageSwitchWriteDead(this, rIter);
    if (MR::useStageSwitchReadAppear(this, rIter)) {
        MR::syncStageSwitchAppear(this);
    }

    const bool isFixWait = MR::useStageSwitchReadA(this, rIter);

    if (MR::isConnectedWithRail(rIter)) {
        initRailRider(rIter);
    }

    // Arg0 set: the wait animation keeps playing while waiting for switch A.
    s32 stopSwAnim = -1;
    MR::getPlacementArg0NoInit(rIter, &stopSwAnim);
    mStopSwAnim = stopSwAnim != -1;

    initModelManagerWithAnm("Karon", nullptr, false);
    MR::connectToSceneEnemy(this);
    initBinder(::cBinderRadius, ::cBinderRadius, 0);
    MR::onCalcGravity(this);
    initSound(4, false);
    initEffectKeeper(1, nullptr);

    if (isFixWait) {
        initNerve(GET_NERVE(Karon, HostTypeNrvFixWait));
    } else {
        initNerve(GET_NERVE(Karon, HostTypeNrvWait));
    }

    initHitSensor(1);
    const f32 bodySize = ::cBodySize * mScale.y;
    MR::addHitSensorEnemy(this, "body", 16, bodySize, Vec3f(0.0f, bodySize, 0.0f));
    MR::initShadowVolumeSphere(this, ::cShadowRadius);
    MR::initLightCtrl(this);
    MR::declareStarPiece(this, ::cStarPieceNum);
    MR::validateClipping(this);
    MR::setGroupClipping(this, rIter, ::cGroupClippingNum);
    mTerritoryMover = new TerritoryMover(::cTerritoryRadius);
    mTerritoryMover->mCenter.set(mPosition);
    MR::initStarPointerTarget(this, ::cStarPointerRadius, Vec3f(0.0f, ::cStarPointerOffsetY, 0.0f));
    mScaleController = new AnimScaleController(nullptr);
    mStateStarPointer = new WalkerStateBindStarPointer(this, mScaleController);
    makeActorAppeared();
}

void Karon::initAfterPlacement() {
    MR::trySetMoveLimitCollision(this);
}

void Karon::kill() {
    LiveActor::kill();

    if (MR::isValidSwitchDead(this)) {
        MR::onSwitchDead(this);
    }

    MR::emitEffect(this, "Death");
    MR::startSoundSeVer(this, "SE_EM_EXPLODE_S");
}

void Karon::exeFixWait() {
    if (MR::isFirstStep(this)) {
        MR::startAction(this, "Wait");
        if (!mStopSwAnim) {
            MR::stopBck(this);
        }
    }

    mBinder->mIsStopAtFirstHit = true;
    MR::moveAndTurnToPlayer(this, ::cNoMoveNoTurnParam);

    if (MR::isOnSwitchA(this)) {
        setNerve(GET_NERVE(Karon, HostTypeNrvWait));
    }
}

void Karon::exeWait() {
    if (MR::isFirstStep(this)) {
        MR::startAction(this, "Wait");
    }

    mBinder->mIsStopAtFirstHit = true;
    MR::moveAndTurnToPlayer(this, MR::isOnGround(this) ? ::cNoMoveNoTurnParam : ::cNoMoveNoTurnOnAirParam);

    const s32 waitTime = isNerve(GET_NERVE(Karon, HostTypeNrvStarPieceHitWait)) ? ::cStarPieceHitWaitTime : ::cWaitTime;

    if (isFindPlayer()) {
        setNerve(GET_NERVE(Karon, HostTypeNrvSearch));
    } else if (MR::isGreaterStep(this, waitTime)) {
        if (mRailRider != nullptr) {
            setNerve(GET_NERVE(Karon, HostTypeNrvWalkOnRail));
        } else {
            mTerritoryMover->decideNextTargetPos(this);
            setNerve(GET_NERVE(Karon, HostTypeNrvWalk));
        }
    }
}

void Karon::exeWalk() {
    if (MR::isFirstStep(this)) {
        MR::startAction(this, "Walk");
    }

    MR::moveAndTurnToTarget(this, mTerritoryMover->mTarget, ::cWalkParam);

    if (MR::isGreaterStep(this, ::cWalkLimitTime) || mTerritoryMover->isReachedTarget(this, ::cGoalDist)) {
        setNerve(GET_NERVE(Karon, HostTypeNrvWait));
    } else if (isFallNextMove(true)) {
        setNerve(GET_NERVE(Karon, HostTypeNrvTurn));
    } else if (isFindPlayer()) {
        setNerve(GET_NERVE(Karon, HostTypeNrvSearch));
    }
}

void Karon::exeTurn() {
    if (MR::isFirstStep(this)) {
        MR::startAction(this, "Walk");
        mTerritoryMover->decideNextTargetPos(this);
    }

    MR::moveAndTurnToTarget(this, mTerritoryMover->mTarget, ::cSearchParam);

    if (isFindPlayer()) {
        setNerve(GET_NERVE(Karon, HostTypeNrvPursue));
        return;
    }

    Vec3f front;
    MR::calcFrontVec(&front, this);
    if (MR::isGreaterStep(this, ::cTurnMinTime) &&
        MR::isFaceToTargetHorizontalDegree(this, mTerritoryMover->mTarget, front, ::cSearchParam.mTurnDegree * cNum_exeTurn_f1)) {
        if (isFallNextMove(false)) {
            setNerve(GET_NERVE(Karon, HostTypeNrvTurn));
        } else {
            setNerve(GET_NERVE(Karon, HostTypeNrvWalk));
        }
    }
}

void Karon::exeWalkOnRail() {
    if (MR::isFirstStep(this)) {
        MR::startAction(this, "Walk");
    }

    MR::moveAndTurnAlongRail(this, ::cFollowRailDistance, ::cWalkParam, nullptr);

    if (isFindPlayer()) {
        setNerve(GET_NERVE(Karon, HostTypeNrvSearch));
    }
}

void Karon::exeSearch() {
    if (MR::isFirstStep(this)) {
        MR::startAction(this, "Search");
        MR::startSound(this, "SE_EV_KARON_FIND");
    }

    MR::moveAndTurnToPlayer(this, ::cSearchParam);

    if (MR::isActionEnd(this)) {
        setNerve(GET_NERVE(Karon, HostTypeNrvPursue));
    }
}

void Karon::exePursue() {
    if (MR::isFirstStep(this)) {
        MR::startAction(this, "Pursue");
        MR::invalidateClipping(this);
        mVelocity += mGravity * -::cPrePursueJumpVel;
    }

    MR::moveAndTurnToPlayer(this, ::cPursueParam);

    if (::cPursueGiveUpDistance < MR::calcDistanceToPlayer(this) || !MR::isFaceToPlayerDegree(this, ::cFaceToDegree)) {
        setNerve(GET_NERVE(Karon, HostTypeNrvWait));
    }
}

void Karon::endPursue() {
    MR::validateClipping(this);
}

void Karon::exeBroken() {
    if (MR::isFirstStep(this)) {
        startBroken("SpinBreak");
    }

    updateBroken(true);
}

void Karon::exeStepBroken() {
    if (MR::isFirstStep(this)) {
        startBroken("StepBreak");
    }

    updateBroken(false);
}

void Karon::startBroken(const char* pActionName) {
    MR::startAction(this, pActionName);
    MR::startSound(this, "SE_EM_KARON_BROKEN");
    MR::startSound(this, "SE_EV_KARON_DAMAGE");
    MR::offCalcGravity(this);
    MR::forceBindOnGround(this, ::cBrokenBindOffsetStart, ::cBrokenBindMulStart);
}

// Rides moving ground; otherwise keeps only the velocity along the bound normal (or gravity in the air).
// isBindBeforeKill: in the air, bind to the ground before cutting the velocity (spin break) or after (step break).
void Karon::updateBroken(bool isBindBeforeKill) {
    Vec3f groundVelocity(0.0f, 0.0f, 0.0f);
    if (MR::calcVelocityAreaOrRailMoveOnGround(&groundVelocity, this)) {
        MR::addVelocityLimit(this, groundVelocity);
        MR::moveAndTurnToPlayer(this, ::cNoMoveNoTurnParam);
        MR::calcGravity(this);
    } else if (MR::isBinded(this)) {
        const Vec3f& rNormal = *MR::getBindedNormal(this);
        mVelocity.scale(rNormal.dot(mVelocity), rNormal);
    } else if (isBindBeforeKill) {
        MR::forceBindOnGround(this, ::cBrokenBindOffset, ::cBrokenBindMul);
        keepVelocityAlongGravity();
    } else {
        keepVelocityAlongGravity();
        MR::forceBindOnGround(this, ::cBrokenBindOffset, ::cBrokenBindMul);
    }

    mBinder->mIsStopAtFirstHit = true;

    if (MR::isGreaterStep(this, ::cBrokenTime)) {
        setNerve(GET_NERVE(Karon, HostTypeNrvRecover));
    }
}

void Karon::keepVelocityAlongGravity() {
    mVelocity.scale(mGravity.dot(mVelocity), mGravity);
}

void Karon::exeRecover() {
    if (MR::isFirstStep(this)) {
        MR::startAction(this, "Recovery");
        MR::startSound(this, "SE_EM_KARON_RECOVER");
    }

    if (MR::isOnGround(this)) {
        keepVelocityAlongGravity();
    }

    MR::moveAndTurnToPlayer(this, ::cNoMoveNoTurnParam);
    mBinder->mIsStopAtFirstHit = true;

    if (MR::isActionEnd(this)) {
        MR::onCalcGravity(this);
        setNerve(GET_NERVE(Karon, HostTypeNrvWait));
    }
}

void Karon::exeHitReaction() {
    const bool isStarPieceHit = isNerve(GET_NERVE(Karon, HostTypeNrvStarPieceHit));

    if (MR::isFirstStep(this)) {
        MR::startAction(this, isStarPieceHit ? "StarPiece" : "AttackHit");
    }

    MR::startLevelSound(this, "SE_EV_LV_KARON_ATK_SUCCESS");

    if (isStarPieceHit) {
        MR::moveAndTurnToPlayer(this, MR::isOnGround(this) ? ::cNoMoveNoTurnParam : ::cNoMoveNoTurnOnAirParam);
    } else {
        MR::moveAndTurnToPlayer(this, MR::isOnGround(this) ? ::cHitReactionOnGroundParam : ::cHitReactionAirParam);
    }

    if (MR::isOnGround(this) && MR::isActionEnd(this)) {
        if (isStarPieceHit) {
            setNerve(GET_NERVE(Karon, HostTypeNrvStarPieceHitWait));
        } else {
            setNerve(GET_NERVE(Karon, HostTypeNrvWait));
        }
    }
}

void Karon::exeDeath() {
    if (MR::isFirstStep(this)) {
        MR::startAction(this, "Down");
        MR::startSound(this, "SE_EM_KARON_BROKEN");
        MR::startSound(this, "SE_EV_KARON_DAMAGE");
    }

    MR::moveAndTurnToPlayer(this, ::cNoMoveNoTurnParam);

    if (MR::isActionEnd(this)) {
        kill();
    }
}

void Karon::exeSinkDown() {
    if (MR::isFirstStep(this)) {
        MR::startAction(this, "SinkDown");
        MR::invalidateShadow(this, nullptr);
    }

    if (MR::isOnGround(this)) {
        keepVelocityAlongGravity();
    }

    MR::startLevelSound(this, "SE_EV_LV_KARON_SINK");
    MR::startLevelSound(this, "SE_EM_LV_KARON_SINK");
    MR::moveAndTurnToPlayer(this, ::cNoMoveNoTurnParam);

    if (MR::isActionEnd(this)) {
        kill();
    }
}

void Karon::exeBindStarPointer() {
    mBinder->mIsStopAtFirstHit = true;
    MR::updateActorStateAndNextNerve(this, mStateStarPointer, GET_NERVE(Karon, HostTypeNrvWait));
}

void Karon::endBindStarPointer() {
    mStateStarPointer->kill();
}

void Karon::control() {
    mScaleController->updateNerve();

    if (MR::isInDeath(this, Vec3f(0.0f, 0.0f, 0.0f)) || MR::isInWater(mPosition) || MR::isInDarkMatter(mPosition)) {
        kill();
        return;
    }

    if (!isNrvBroken()) {
        Vec3f groundVelocity(0.0f, 0.0f, 0.0f);
        if (MR::calcVelocityAreaOrRailMoveOnGround(&groundVelocity, this)) {
            MR::addVelocityLimit(this, groundVelocity);
        }
    }

    if ((isNerve(GET_NERVE(Karon, HostTypeNrvWait)) || isNerve(GET_NERVE(Karon, HostTypeNrvStarPieceHitWait)) ||
         isNerve(GET_NERVE(Karon, HostTypeNrvWalk)) || isNerve(GET_NERVE(Karon, HostTypeNrvWalkOnRail)) ||
         isNerve(GET_NERVE(Karon, HostTypeNrvSearch)) || isNerve(GET_NERVE(Karon, HostTypeNrvPursue))) &&
        mStateStarPointer->tryStartPointBind()) {
        setNerve(GET_NERVE(Karon, HostTypeNrvBindStarPointer));
        return;
    }

    if (!isNrvDisabled() && !isNerve(GET_NERVE(Karon, HostTypeNrvSinkDown)) && MR::isBindedGroundSinkDeath(this)) {
        setNerve(GET_NERVE(Karon, HostTypeNrvSinkDown));
    }
}

// isAlongVelocity: probe along the velocity; otherwise along the front.
bool Karon::isFallNextMove(bool isAlongVelocity) {
    if (isAlongVelocity) {
        return MR::isFallOrDangerNextMove(this, ::cFallCheckAhead, ::cFallCheckUpper, ::cFallCheckLower);
    }

    Vec3f front;
    MR::calcFrontVec(&front, this);
    return MR::isFallOrDangerNextMove(mPosition, front, mGravity, ::cFallCheckAhead, ::cFallCheckUpper, ::cFallCheckLower);
}

bool Karon::isFindPlayer() const {
    return MR::calcDistanceToPlayer(this) < ::cPursueDistance && MR::isFaceToPlayerDegree(this, ::cFaceToDegree);
}

bool Karon::isNrvBroken() const {
    return isNerve(GET_NERVE(Karon, HostTypeNrvBroken)) || isNerve(GET_NERVE(Karon, HostTypeNrvStepBroken));
}

// Broken, recovering or dying: no attacks, pushes or sinking.
bool Karon::isNrvDisabled() const {
    return isNrvBroken() || isNerve(GET_NERVE(Karon, HostTypeNrvRecover)) || isNerve(GET_NERVE(Karon, HostTypeNrvDeath));
}

void Karon::appearStarPieceBurst() {
    MR::appearStarPiece(this, mPosition, ::cStarPieceNum, ::cStarPieceSpeedH, ::cStarPieceSpeedV, false);
    MR::startSound(this, "SE_OJ_STAR_PIECE_BURST");
}

void Karon::attackSensor(HitSensor* pSender, HitSensor* pReceiver) {
    if (isNrvDisabled() || getSensor("body") != pSender) {
        return;
    }

    if (MR::isSensorEnemy(pReceiver) && !MR::isSensorPlayer(pReceiver)) {
        MR::sendMsgPushAndKillVelocityToTarget(this, pReceiver, pSender);
        return;
    }

    if (!MR::isSensorPlayer(pReceiver)) {
        return;
    }

    const bool isPlayerUp = MR::isPlayerExistUp(this, pSender->mRadius, ::cPlayerUpRate);
    if (isPlayerUp && MR::isOnGroundPlayer()) {
        if (MR::sendMsgTouchJump(pReceiver, pSender)) {
            setNerve(GET_NERVE(Karon, HostTypeNrvStepBroken));
        }
    } else if (!isNerve(GET_NERVE(Karon, HostTypeNrvHitReaction)) && !isNerve(GET_NERVE(Karon, HostTypeNrvFixWait)) && !isPlayerUp &&
               MR::isOnGroundPlayer() && !MR::isPlayerHipDropFalling() && !MR::isPlayerHipDropLand() && MR::sendMsgEnemyAttack(pReceiver, pSender)) {
        Vec3f away(pSender->mPosition);
        away -= pReceiver->mPosition;
        MR::vecKillElement(away, mGravity, &away);
        MR::normalizeOrZero(&away);
        mVelocity.add(away * ::cHitReactionVelH);
        mVelocity.add(mGravity * -::cHitReactionVelV);
        setNerve(GET_NERVE(Karon, HostTypeNrvHitReaction));
    } else {
        MR::sendMsgPushAndKillVelocityToTarget(this, pReceiver, pSender);
    }
}

bool Karon::receiveMsgPlayerAttack(u32 msg, HitSensor* /*pSender*/, HitSensor* /*pReceiver*/) {
    if (isNrvBroken() || isNerve(GET_NERVE(Karon, HostTypeNrvDeath)) || isNerve(GET_NERVE(Karon, HostTypeNrvSinkDown))) {
        return false;
    }

    if (MR::isMsgPlayerTrample(msg)) {
        if (MR::isPlayerElementModeInvincible()) {
            appearStarPieceBurst();
            setNerve(GET_NERVE(Karon, HostTypeNrvDeath));
        } else {
            setNerve(GET_NERVE(Karon, HostTypeNrvStepBroken));
        }
        return true;
    }

    if (MR::isMsgPlayerHipDrop(msg)) {
        setNerve(GET_NERVE(Karon, HostTypeNrvStepBroken));
        return true;
    }

    if (MR::isMsgStarPieceAttack(msg)) {
        setNerve(GET_NERVE(Karon, HostTypeNrvStarPieceHit));
        return true;
    }

    if (MR::isMsgPlayerSpinAttack(msg) || MR::isMsgJetTurtleAttack(msg)) {
        MR::startBlowHitSound(this);
        setNerve(GET_NERVE(Karon, HostTypeNrvBroken));
        return true;
    }

    if (MR::isMsgFireBallAttack(msg)) {
        mScaleController->startHitReaction();
        setNerve(GET_NERVE(Karon, HostTypeNrvStarPieceHit));
        return true;
    }

    if (MR::isMsgInvincibleAttack(msg)) {
        setNerve(GET_NERVE(Karon, HostTypeNrvDeath));
        appearStarPieceBurst();
        MR::startBlowHitSound(this);
        return true;
    }

    return false;
}

bool Karon::receiveMsgEnemyAttack(u32 msg, HitSensor* /*pSender*/, HitSensor* /*pReceiver*/) {
    if (!MR::isMsgExplosionAttack(msg)) {
        return false;
    }

    kill();
    appearStarPieceBurst();
    return true;
}

bool Karon::receiveMsgPush(HitSensor* pSender, HitSensor* pReceiver) {
    if (MR::isSensorPlayer(pSender) || isNrvDisabled()) {
        return false;
    }

    MR::addVelocityFromPush(this, ::cPushRate, pSender, pReceiver);
    return true;
}

bool Karon::receiveOtherMsg(u32 msg, HitSensor* /*pSender*/, HitSensor* /*pReceiver*/) {
    if (MR::isMsgInhaleBlackHole(msg) || MR::isMsgStartPowerStarGet(msg)) {
        kill();
        return true;
    }

    return false;
}

void Karon::calcAndSetBaseMtx() {
    LiveActor::calcAndSetBaseMtx();
    MR::setBaseScale(this, mScaleController->mScale * mScale);
}
