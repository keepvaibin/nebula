#include "Game/Enemy/BombBird.hpp"
#include "Game/System/NebulaFrameClock.hpp"
#include "Game/Enemy/AnimScaleController.hpp"
#include "Game/Enemy/BombBirdBomb.hpp"
#include "Game/Enemy/SpinHitController.hpp"
#include "Game/Enemy/WalkerStateBindStarPointer.hpp"
#include "Game/LiveActor/HitSensor.hpp"
#include "Game/LiveActor/Nerve.hpp"
#include "Game/Util.hpp"
#include "Game/Util/FixedPosition.hpp"
#include "Game/Util/Tunable.hpp"

NEBULA_TUNABLE_NAMED(f32, cNum_init_f1, "init.f1", 100.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_init_f2, "init.f2", 100.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_init_f3, "init.f3", 100.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_init_f4, "init.f4", 100.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_init_f5, "init.f5", 1.5f);
NEBULA_TUNABLE_NAMED(f32, cNum_init_f6, "init.f6", 30.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_init_f7, "init.f7", 40.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_init_f8, "init.f8", 0.5f);
NEBULA_TUNABLE_NAMED(f32, cNum_attackSensor_f1, "attackSensor.f1", 0.25f);
NEBULA_TUNABLE_NAMED(f32, cNum_addVelocityToUpFromGround_f1, "addVelocityToUpFromGround.f1", 3.4028235e38f);

// The step counts of this file's functions, as tunables: "<file>.<function>.<k>".
NEBULA_TUNABLE_NAMED(s32, cStep_exeFlyOnRailSearch_1, "exeFlyOnRailSearch.1", 54);
NEBULA_TUNABLE_NAMED(s32, cStep_exeFlyOnRailSearch_2, "exeFlyOnRailSearch.2", 54);
NEBULA_TUNABLE_NAMED(s32, cStep_exeFlyWithAttack_1, "exeFlyWithAttack.1", 130);
NEBULA_TUNABLE_NAMED(s32, cStep_exeTrample_1, "exeTrample.1", 36);

namespace {
    NEBULA_TUNABLE_RECORD(BombBirdParam, hFallParam, {0.0f, 1.2f, 0.98f, 0.0f});
    NEBULA_TUNABLE_RECORD(BombBirdParam, hOnGroundParam, {0.0f, 1.2f, 0.8f, 0.0f});

    NEBULA_TUNABLE(f32, hDamp, 0.96f);
    NEBULA_TUNABLE(f32, hGravity, 1.0f);
    NEBULA_TUNABLE(f32, hFlyVel, 0.3f);
    NEBULA_TUNABLE(f32, hToRailDiv, 200.0f);
    NEBULA_TUNABLE(f32, hBombFallUpVel, -10.0f);
    NEBULA_TUNABLE(f32, hTurnLimitRadian, 4.5836625f);
    NEBULA_TUNABLE(f32, hDotEpsilon, 0.001f);
    NEBULA_TUNABLE(f32, hSearchDist, 2000.0f);
    NEBULA_TUNABLE(f32, hSearchKeepHeight, 800.0f);
    NEBULA_TUNABLE(f32, hSearchKeepHeightAccUp, 10.0f);
    NEBULA_TUNABLE(f32, hEscapeVel, 5.0f);
    NEBULA_TUNABLE(f32, hEscapeUpVel, 5.0f);
    NEBULA_TUNABLE(f32, hSpinAttackedVelH, 10.0f);
    NEBULA_TUNABLE(f32, hSpinAttackedVelV, 20.0f);
    const s32 hSwoonTime = NebulaFrameClock::stepsOfSeconds(4);
    NEBULA_TUNABLE(f32, hShiftingSandVel, 1.0f);
    NEBULA_TUNABLE(f32, hSwoonEndJump, 10.0f);
    NEBULA_TUNABLE(f32, hSwoonEndUpVel, 5.0f);
    const s32 hSwoonEndFlyStartTime = NebulaFrameClock::stepsOfMilliseconds(500);
    NEBULA_TUNABLE(f32, hBombFallFrontVel, 0.0f);
};  // namespace

namespace NrvBombBird {
    NEW_NERVE(HostTypeNrvFlyOnRail, BombBird, FlyOnRail);
    NEW_NERVE(HostTypeNrvFlyOnRailSearch, BombBird, FlyOnRailSearch);
    NEW_NERVE(HostTypeNrvFlyWithAttack, BombBird, FlyWithAttack);
    NEW_NERVE(HostTypeNrvBlow, BombBird, Blow);
    NEW_NERVE(HostTypeNrvEscape, BombBird, Escape);
    NEW_NERVE(HostTypeNrvDamage, BombBird, Damage);
    NEW_NERVE(HostTypeNrvFall, BombBird, Fall);
    NEW_NERVE(HostTypeNrvSwoonStart, BombBird, SwoonStart);
    NEW_NERVE(HostTypeNrvSwoon, BombBird, Swoon);
    NEW_NERVE(HostTypeNrvSwoonEnd, BombBird, SwoonEnd);
    NEW_NERVE(HostTypeNrvTrample, BombBird, Trample);
    NEW_NERVE(HostTypeNrvSinkDown, BombBird, SinkDown);
    NEW_NERVE_ONEND(HostTypeNrvBindStarPointer, BombBird, BindStarPointer, BindStarPointer);
};  // namespace NrvBombBird

BombBird::BombBird(const char* pName)
    : LiveActor(pName), mHeldBomb(), mFront(0, 0, 1), mSearchPlayer(), mHoldBomb(), mFixedPos(), mSpinHitController(), mScaleController(),
      mStarPointer() {
}

void BombBird::init(const TableIter& rIter) {
    if (MR::isValidInfo(rIter)) {
        MR::isConnectedWithRail(rIter);
    }

    MR::initDefaultPos(this, rIter);
    initRailRider(rIter);
    MR::useStageSwitchReadB(this, rIter);
    if (MR::useStageSwitchReadAppear(this, rIter)) {
        MR::syncStageSwitchAppear(this);
    }

    s32 arg = -1;
    MR::getPlacementArg0NoInit(rIter, &arg);
    if (arg != -1) {
        mSearchPlayer = true;
    } else {
        mSearchPlayer = false;
    }

    initModelManagerWithAnm("BombBird", nullptr, false);
    MR::connectToSceneEnemy(this);
    initNerve(GET_NERVE(BombBird, HostTypeNrvFlyOnRail));
    initHitSensor(1);
    MR::addHitSensorEnemy(this, "body", 16, cNum_init_f1, Vec3f(0.0f, cNum_init_f2, 0.0f));
    initSound(4, false);
    MR::onCalcGravity(this);
    MR::initShadowVolumeSphere(this, getSensor("body")->getRadius());
    initBinder(cNum_init_f3, cNum_init_f4, 0);
    MR::initLightCtrl(this);
    MR::declareCoin(this, 1);
    MR::declareStarPiece(this, 3);
    mFixedPos = new FixedPosition(this, "BombPos", Vec3f(0, 0, 0), Vec3f(0, 0, 0));
    mFixedPos->mNormalizeScale = false;
    mSpinHitController = new SpinHitController(this, 25, 20, 4, cNum_init_f5, cNum_init_f6, cNum_init_f7, 3, false);

    for (BombBirdBomb** it = mBombs.begin(); it < mBombs.end(); it++) {
        *it = new BombBirdBomb("バクダン鳥のボム");
        (*it)->initWithoutIter();
        (*it)->makeActorDead();
    }

    MR::tryRegisterDemoCast(this, rIter);
    if (mSearchPlayer) {
        MR::invalidateClipping(this);
    } else {
        MR::validateClipping(this);
    }

    initEffectKeeper(1, nullptr);
    MR::initStarPointerTarget(this, getSensor("body")->getRadius(), Vec3f(0.0f, cNum_init_f8 * getSensor("body")->getRadius(), 0.0f));
    mScaleController = new AnimScaleController(nullptr);
    mStarPointer = new WalkerStateBindStarPointer(this, mScaleController);
    if (MR::isValidSwitchAppear(this)) {
        makeActorDead();
    } else {
        makeActorAppeared();
    }
}

void BombBird::makeActorDead() {
    LiveActor::makeActorDead();
    killAllBomb();
}

void BombBird::startClipped() {
    if (isNerve(GET_NERVE(BombBird, HostTypeNrvEscape))) {
        kill();
    } else {
        LiveActor::startClipped();
    }
}

void BombBird::control() {
    mScaleController->updateNerve();
    if (isNerve(GET_NERVE(BombBird, HostTypeNrvFlyOnRail)) || isNerve(GET_NERVE(BombBird, HostTypeNrvFlyOnRailSearch)) ||
        isNerve(GET_NERVE(BombBird, HostTypeNrvFlyWithAttack))) {
        if (mStarPointer->tryStartPointBind()) {
            setNerve(GET_NERVE(BombBird, HostTypeNrvBindStarPointer));
            return;
        }
    }

    if (!isNerve(GET_NERVE(BombBird, HostTypeNrvBlow)) && !isNerve(GET_NERVE(BombBird, HostTypeNrvTrample)) &&
        !isNerve(GET_NERVE(BombBird, HostTypeNrvSinkDown)) && MR::isBindedGroundSinkDeath(this)) {
        setNerve(GET_NERVE(BombBird, HostTypeNrvSinkDown));
        return;
    }

    if (!isNerve(GET_NERVE(BombBird, HostTypeNrvTrample)) && MR::isPressedRoofAndGround(this)) {
        setNerve(GET_NERVE(BombBird, HostTypeNrvTrample));
    }
}

void BombBird::attackSensor(HitSensor* pSender, HitSensor* pReceiver) {
    if (isNerve(GET_NERVE(BombBird, HostTypeNrvBlow)) || isNerve(GET_NERVE(BombBird, HostTypeNrvTrample))) {
        return;
    }

    if (isNerve(GET_NERVE(BombBird, HostTypeNrvEscape)) || isNerve(GET_NERVE(BombBird, HostTypeNrvDamage)) ||
        isNerve(GET_NERVE(BombBird, HostTypeNrvFall)) || isNerve(GET_NERVE(BombBird, HostTypeNrvSwoonStart)) ||
        isNerve(GET_NERVE(BombBird, HostTypeNrvSwoon)) || isNerve(GET_NERVE(BombBird, HostTypeNrvSwoonEnd)) ||
        MR::isPlayerExistUp(this, getSensor("body")->getRadius(), cNum_attackSensor_f1) || MR::isPlayerHipDropFalling() || MR::isPlayerHipDropLand()) {
        MR::sendMsgPushAndKillVelocityToTarget(this, pReceiver, pSender);
    } else if (!MR::sendMsgEnemyAttack(pReceiver, pSender)) {
        MR::sendMsgPushAndKillVelocityToTarget(this, pReceiver, pSender);
    }
}

void BombBird::exeFlyOnRail() {
    if (MR::isFirstStep(this)) {
        MR::startAction(this, "Wait");
        if (!mSearchPlayer) {
            MR::validateClipping(this);
        }
    }

    doFly();
    if (MR::isValidSwitchB(this) && MR::isOnSwitchB(this)) {
        setNerve(GET_NERVE(BombBird, HostTypeNrvEscape));
    } else if (MR::calcDistanceToPlayerH(this) < ::hSearchDist && !MR::isDemoActive() && MR::isActionEnd(this)) {
        setNerve(GET_NERVE(BombBird, HostTypeNrvFlyOnRailSearch));
    } else if (MR::isActionEnd(this)) {
        setNerve(GET_NERVE(BombBird, HostTypeNrvFlyOnRail));
    }
}

void BombBird::exeFlyOnRailSearch() {
    if (MR::isFirstStep(this)) {
        MR::startAction(this, "Born");
        mHoldBomb = false;
        if (!mSearchPlayer) {
            MR::validateClipping(this);
        }
    }

    doFly();
    if (MR::isValidSwitchB(this) && MR::isOnSwitchB(this)) {
        setNerve(GET_NERVE(BombBird, HostTypeNrvEscape));
    } else {
        if (MR::isStep(this, cStep_exeFlyOnRailSearch_1)) {
            mHoldBomb = true;
            mHeldBomb = nullptr;
            for (BombBirdBomb** it = mBombs.begin(); it < mBombs.end(); it++) {
                if (MR::isDead(*it)) {
                    mHeldBomb = *it;
                    mHeldBomb->hold(mPosition);
                    break;
                }
            }
        } else if (MR::isGreaterStep(this, cStep_exeFlyOnRailSearch_2)) {
            mHoldBomb = true;
            if (mHeldBomb) {
                Vec3f scale;
                MR::copyJointScale(this, "BombPos", &scale);
                mHeldBomb->mScale.set(scale);
            }
        }

        if (MR::isActionEnd(this) && !MR::isDemoActive()) {
            if (mHeldBomb) {
                mHeldBomb->mScale.set(1.0f, 1.0f, 1.0f);
                setNerve(GET_NERVE(BombBird, HostTypeNrvFlyWithAttack));
            } else {
                setNerve(GET_NERVE(BombBird, HostTypeNrvFlyOnRail));
            }
        }
    }
}

void BombBird::doFly() {
    if (mSearchPlayer) {
        MR::flyAndTurnAlongRailSearchingPlayer(this, &mFront, ::hToRailDiv, {::hFlyVel, ::hGravity, ::hDamp, ::hTurnLimitRadian}, true);
        addVelocityToUpFromGround(::hSearchKeepHeightAccUp);
    } else {
        MR::flyAndTurnAlongRailSearchingPlayer(this, &mFront, ::hToRailDiv, {::hFlyVel, 0.0f, ::hDamp, ::hTurnLimitRadian}, false);
    }
}

void BombBird::addVelocityToUpFromGround(f32 vel) {
    f32 proj = MR::getShadowNearProjectionLength(this);
    if (cNum_addVelocityToUpFromGround_f1 != proj && proj < ::hSearchKeepHeight) {
        vel = (::hSearchKeepHeight - proj) >= vel ? vel : (::hSearchKeepHeight - proj);

        MR::addVelocityLimit(this, mGravity * -vel);
    }
}

void BombBird::killAllBomb() {
    for (BombBirdBomb** it = mBombs.begin(); it < mBombs.end(); it++) {
        if (!MR::isDead(*it)) {
            (*it)->kill();
        }
    }

    mHeldBomb = nullptr;
}

void BombBird::exeFlyWithAttack() {
    if (MR::isFirstStep(this)) {
        MR::startAction(this, "Attack");

        if (!mSearchPlayer) {
            MR::validateClipping(this);
        }
    }

    doFly();

    if (MR::isValidSwitchB(this) && MR::isOnSwitchB(this)) {
        setNerve(GET_NERVE(BombBird, HostTypeNrvEscape));
    } else {
        BombBirdBomb* bomb = mHeldBomb;
        if (bomb != nullptr) {
            bomb->mVelocity.zero();
        }

        if (MR::isStep(this, cStep_exeFlyWithAttack_1)) {
            MR::startSound(mHeldBomb, "SE_EM_BOMBBIRD_DROP_BOMB");
            mHeldBomb->start(mPosition, mFront * ::hBombFallFrontVel - mGravity * ::hBombFallUpVel);
            mHeldBomb = nullptr;
            mHoldBomb = false;
        }

        if (MR::isActionEnd(this)) {
            if (MR::calcDistanceToPlayerH(this) < ::hSearchDist) {
                setNerve(GET_NERVE(BombBird, HostTypeNrvFlyOnRailSearch));
            } else {
                setNerve(GET_NERVE(BombBird, HostTypeNrvFlyOnRail));
            }
        }
    }
}

void BombBird::exeBlow() {
    if (MR::isFirstStep(this)) {
        MR::startAction(this, "DownSpin");
        MR::startSound(this, "SE_EV_BOMBBIRD_DEAD");
        MR::startSound(this, "SE_EM_BOMBBIRD_DAMAGE");
        MR::startBlowHitSound(this);
        MR::invalidateClipping(this);
    }

    if (mSpinHitController->execute(this)) {
        MR::startSound(this, "SE_EM_EXPLODE_S");
        MR::emitEffect(this, "Death");
    }
}

void BombBird::exeEscape() {
    if (MR::isFirstStep(this)) {
        MR::startAction(this, "Wait");
        MR::validateClipping(this);
    }

    Vec3f escapeDirection(mPosition);
    escapeDirection -= *MR::getPlayerCenterPos();
    MR::vecKillElement(escapeDirection, mGravity, &escapeDirection);
    MR::normalizeOrZero(&escapeDirection);

    if (!MR::isNearZero(escapeDirection, ::hDotEpsilon)) {
        MR::moveAndTurnToDirection(this, &mFront, escapeDirection, {::hFlyVel, ::hGravity, ::hDamp, ::hTurnLimitRadian});
    }

    mVelocity.set(mFront * ::hEscapeVel - mGravity * ::hEscapeUpVel);

    if (MR::isActionEnd(this)) {
        setNerve(GET_NERVE(BombBird, HostTypeNrvEscape));
    }
}

void BombBird::exeDamage() {
    if (MR::isFirstStep(this)) {
        MR::startAction(this, "Damage");
        MR::startSound(this, "SE_EV_BOMBBIRD_DAMAGE");
        MR::startSound(this, "SE_EM_BOMBBIRD_DAMAGE");
        MR::invalidateClipping(this);
    }

    const BombBirdParam* param = &::hFallParam;
    MR::moveAndTurnToPlayer(this, &mFront, {param->mMoveAccel, param->mGravityAccel, param->mFriction, param->mTurnDegree});

    if (MR::isActionEnd(this)) {
        setNerve(GET_NERVE(BombBird, HostTypeNrvFall));
    }
}

void BombBird::exeFall() {
    if (MR::isFirstStep(this)) {
        MR::startAction(this, "Fall");
        MR::invalidateClipping(this);
    }

    const BombBirdParam* param = &::hFallParam;
    MR::moveAndTurnToPlayer(this, &mFront, {param->mMoveAccel, param->mGravityAccel, param->mFriction, param->mTurnDegree});

    if (MR::isOnGround(this)) {
        MR::startAction(this, "SwoonStart");
        setNerve(GET_NERVE(BombBird, HostTypeNrvSwoonStart));
    }
}

void BombBird::exeSwoonStart() {
    if (MR::isFirstStep(this)) {
        MR::invalidateClipping(this);
    }

    MR::startLevelSound(this, "SE_EM_LV_SWOON_S");
    if (MR::isOnGround(this)) {
        MR::vecKillElement(mVelocity, mGravity, &mVelocity);
    }

    const BombBirdParam* param = &::hOnGroundParam;
    MR::moveAndTurnToPlayer(this, &mFront, {param->mMoveAccel, param->mGravityAccel, param->mFriction, param->mTurnDegree});
    Vec3f area(0, 0, 0);
    if (MR::calcVelocityAreaOrRailMoveOnGround(&area, this)) {
        mVelocity.set(area * ::hShiftingSandVel);
    }

    if (MR::isActionEnd(this)) {
        setNerve(GET_NERVE(BombBird, HostTypeNrvSwoon));
    }
}

void BombBird::exeSwoon() {
    if (MR::isFirstStep(this)) {
        MR::startAction(this, "Swoon");
        MR::invalidateClipping(this);
    }

    MR::startLevelSound(this, "SE_EM_LV_SWOON_S");

    if (MR::isOnGround(this)) {
        MR::vecKillElement(mVelocity, mGravity, &mVelocity);
    }

    MR::moveAndTurnToPlayer(this, &mFront, {::hOnGroundParam.mMoveAccel, ::hOnGroundParam.mGravityAccel, ::hOnGroundParam.mFriction, ::hOnGroundParam.mTurnDegree});
    Vec3f area(0, 0, 0);
    if (MR::calcVelocityAreaOrRailMoveOnGround(&area, this)) {
        mVelocity.set(area * ::hShiftingSandVel);
    }

    if (MR::isGreaterStep(this, ::hSwoonTime)) {
        setNerve(GET_NERVE(BombBird, HostTypeNrvSwoonEnd));
    }
}

void BombBird::exeSwoonEnd() {
    if (MR::isFirstStep(this)) {
        MR::startAction(this, "SwoonEnd");
        MR::startSound(this, "SE_EV_BOMBBIRD_RECOVER");
        MR::invalidateClipping(this);
    }

    if (MR::isOnGround(this)) {
        MR::vecKillElement(mVelocity, mGravity, &mVelocity);
    }

    const BombBirdParam* param = &::hOnGroundParam;
    MR::moveAndTurnToPlayer(this, &mFront, {param->mMoveAccel, param->mGravityAccel, param->mFriction, param->mTurnDegree});

    Vec3f area(0, 0, 0);
    if (MR::calcVelocityAreaOrRailMoveOnGround(&area, this)) {
        mVelocity.set(area * ::hShiftingSandVel);
    }

    if (MR::isGreaterEqualStep(this, ::hSwoonEndFlyStartTime)) {
        if (MR::isStep(this, ::hSwoonEndFlyStartTime)) {
            MR::addVelocityJump(this, ::hSwoonEndJump);
        }

        doFly();
        addVelocityToUpFromGround(::hSwoonEndUpVel);
    }

    if (MR::isActionEnd(this)) {
        setNerve(GET_NERVE(BombBird, HostTypeNrvFlyOnRail));
    }
}

void BombBird::exeTrample() {
    if (MR::isFirstStep(this)) {
        MR::startAction(this, "DownStep");
        MR::startSound(this, "SE_EM_STOMPED_S");
        MR::invalidateClipping(this);
    }

    if (MR::isOnGround(this)) {
        MR::vecKillElement(mVelocity, mGravity, &mVelocity);
    }

    const BombBirdParam* param = &::hOnGroundParam;
    MR::moveAndTurnToPlayer(this, &mFront, {param->mMoveAccel, param->mGravityAccel, param->mFriction, param->mTurnDegree});

    if (MR::isStep(this, cStep_exeTrample_1)) {
        Vec3f pos(mPosition - mGravity * getSensor("body")->getRadius());
        MR::appearCoinPop(this, pos, 1);
        MR::startSound(this, "SE_EM_EXPLODE_S");
        MR::emitEffect(this, "Death");
        kill();
    }
}

void BombBird::exeSinkDown() {
    if (MR::isFirstStep(this)) {
        MR::startAction(this, "SinkDown");
        MR::invalidateShadow(this, nullptr);
        MR::invalidateClipping(this);
    }

    MR::startLevelSound(this, "SE_EM_LV_SINK");
    MR::startLevelSound(this, "SE_EV_LV_BOMBBIRD_SINK");
    const BombBirdParam* param = &::hOnGroundParam;
    MR::moveAndTurnToPlayer(this, &mFront, {param->mMoveAccel, param->mGravityAccel, param->mFriction, param->mTurnDegree});

    if (MR::isActionEnd(this)) {
        MR::emitEffect(this, "SinkDeath");
        kill();
    }
}

void BombBird::endBindStarPointer() {
    mStarPointer->kill();
}

void BombBird::exeBindStarPointer() {
    if (MR::isFirstStep(this)) {
        MR::invalidateClipping(this);
    }

    killAllBomb();
    if (MR::updateActorStateAndNextNerve(this, mStarPointer, GET_NERVE(BombBird, HostTypeNrvFlyOnRail))) {
        return;
    }
}

bool BombBird::receiveMsgPlayerAttack(u32 msg, HitSensor* pSender, HitSensor* pReceiver) {
    if (MR::isMsgLockOnStarPieceShoot(msg)) {
        return true;
    }

    if (isNerve(GET_NERVE(BombBird, HostTypeNrvSwoonStart)) || isNerve(GET_NERVE(BombBird, HostTypeNrvSwoon)) ||
        (isNerve(GET_NERVE(BombBird, HostTypeNrvSwoonEnd)) && MR::isLessStep(this, ::hSwoonEndFlyStartTime))) {
        if (MR::isMsgPlayerTrample(msg) || MR::isMsgPlayerHipDrop(msg)) {
            setNerve(GET_NERVE(BombBird, HostTypeNrvTrample));
            return true;
        }

        if (MR::isMsgStarPieceAttack(msg)) {
            mScaleController->startHitReaction();
            return true;
        }

        if (MR::isMsgPlayerHitAll(msg)) {
            mSpinHitController->start(this, pSender->mPosition, pReceiver->mPosition);
            setNerve(GET_NERVE(BombBird, HostTypeNrvBlow));
            return true;
        }

        return false;
    }

    if (isNerve(GET_NERVE(BombBird, HostTypeNrvFlyOnRail)) || isNerve(GET_NERVE(BombBird, HostTypeNrvFlyOnRailSearch)) ||
        isNerve(GET_NERVE(BombBird, HostTypeNrvFlyWithAttack)) || isNerve(GET_NERVE(BombBird, HostTypeNrvBindStarPointer)) ||
        isNerve(GET_NERVE(BombBird, HostTypeNrvSwoonEnd))) {
        if (MR::isMsgPlayerHitAll(msg) || MR::isMsgPlayerTrample(msg) || MR::isMsgPlayerHipDrop(msg) || MR::isMsgStarPieceAttack(msg)) {
            if (mHeldBomb) {
                mHeldBomb->kill();
                mHeldBomb = nullptr;
            }

            MR::setVelocitySeparateHV(this, pSender, pReceiver, ::hSpinAttackedVelH, ::hSpinAttackedVelV);
            setNerve(GET_NERVE(BombBird, HostTypeNrvDamage));
            if (MR::isMsgPlayerHipDrop(msg)) {
                MR::sendMsgAwayJump(pSender, pReceiver);
            }

            return true;
        }
    } else if (MR::isMsgStarPieceAttack(msg)) {
        return true;
    }

    return false;
}

bool BombBird::receiveOtherMsg(u32 msg, HitSensor* pSender, HitSensor* pReceiver) {
    if (MR::isMsgPlayerKick(msg) && (isNerve(GET_NERVE(BombBird, HostTypeNrvSwoonStart)) || isNerve(GET_NERVE(BombBird, HostTypeNrvSwoon)))) {
        mSpinHitController->start(this, pSender->mPosition, pReceiver->mPosition);
        setNerve(GET_NERVE(BombBird, HostTypeNrvBlow));
        return true;
    }

    return false;
}

void BombBird::calcAndSetBaseMtx() {
    Pos3f mtx;
    MR::calcMtxFromGravityAndZAxis(&mtx, this, mGravity, mFront);
    MR::setBaseTRMtx(this, mtx);
    Vec3f scale(mScaleController->mScale * mScale);
    MR::setBaseScale(this, scale);
    if (mHoldBomb && mHeldBomb) {
        mFixedPos->calc();
        mFixedPos->copyTrans(&mHeldBomb->mPosition);
        Vec3f scale;
        mFixedPos->mMtx.getScale(scale);
        MR::setBaseScale(mHeldBomb, scale);
    }
}
