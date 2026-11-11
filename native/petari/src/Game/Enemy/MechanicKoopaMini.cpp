#include "Game/Enemy/MechanicKoopaMini.hpp"
#include "Game/System/NebulaFrameClock.hpp"
#include "Game/Enemy/AnimScaleController.hpp"
#include "Game/Enemy/WalkerStateBindStarPointer.hpp"
#include "Game/LiveActor/HitSensor.hpp"
#include "Game/LiveActor/LiveActor.hpp"
#include "Game/LiveActor/Nerve.hpp"
#include "Game/Util/ActorMovementUtil.hpp"
#include "Game/Util/ActorSensorUtil.hpp"
#include "Game/Util/ActorShadowUtil.hpp"
#include "Game/Util/ActorStateUtil.hpp"
#include "Game/Util/ActorSwitchUtil.hpp"
#include "Game/Util/DemoUtil.hpp"
#include "Game/Util/EffectUtil.hpp"
#include "Game/Util/Table.hpp"
#include "Game/Util/PlacementUtil.hpp"
#include "Game/Util/LiveActorUtil.hpp"
#include "Game/Util/MapUtil.hpp"
#include "Game/Util/MathUtil.hpp"
#include "Game/Util/ModelUtil.hpp"
#include "Game/Util/ObjUtil.hpp"
#include "Game/Util/PlayerUtil.hpp"
#include "Game/Util/RailUtil.hpp"
#include "Game/Util/SoundUtil.hpp"
#include "Game/Util/StarPointerUtil.hpp"
#include <nebula_geometry.hpp>
#include "Game/Util/Tunable.hpp"
#include "Game/System/LumaCore.hpp"

NEBULA_TUNABLE_NAMED(f32, cNum_init_f1, "init.f1", 120.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_init_f2, "init.f2", 130.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_init_f3, "init.f3", 48.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_init_f4, "init.f4", 13.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_init_f5, "init.f5", 29.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_init_f6, "init.f6", 70.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_init_f7, "init.f7", 35.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeWait_f1, "exeWait.f1", 900.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeWalkOnRail_f1, "exeWalkOnRail.f1", 0.99f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeWalkOnRail_f2, "exeWalkOnRail.f2", 900.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeTurn_f1, "exeTurn.f1", 5.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_exePursue_f1, "exePursue.f1", 10.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_exePursue_f2, "exePursue.f2", 0.99f);
NEBULA_TUNABLE_NAMED(f32, cNum_exePursue_f3, "exePursue.f3", 1100.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_exePursue_f4, "exePursue.f4", 300.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_exePursue_f5, "exePursue.f5", 5.729578f);
NEBULA_TUNABLE_NAMED(f32, cNum_exePreFireAttack_f1, "exePreFireAttack.f1", 10.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_updateHitSensor_f1, "updateHitSensor.f1", 400.0f);

// The numbers this file's functions give the game's motion and animation calls, as tunables: "<file>.<function>.<call>.<k>".
NEBULA_TUNABLE_NAMED(f32, cNum_exeWait_isFaceToPlayerDegree_1, "exeWait.isFaceToPlayerDegree.1", 120.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeWalkOnRail_moveAndTurnAlongRail_1, "exeWalkOnRail.moveAndTurnAlongRail.1", 600.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeWalkOnRail_isFaceToPlayerDegree_1, "exeWalkOnRail.isFaceToPlayerDegree.1", 120.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeWalkOnRail_moveAndTurnAlongRail_2, "exeWalkOnRail.moveAndTurnAlongRail.2", 600.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeJumpStart_addVelocityJump_1, "exeJumpStart.addVelocityJump.1", 45.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeFind_addVelocityJump_1, "exeFind.addVelocityJump.1", 26.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_receiveMsgPush_addVelocityFromPush_1, "receiveMsgPush.addVelocityFromPush.1", 3.0f);

namespace NrvMechanicKoopaMini {
    NEW_NERVE(HostTypeNrvWait, MechanicKoopaMini, Wait);
    NEW_NERVE(HostTypeNrvWalkOnRail, MechanicKoopaMini, WalkOnRail);
    NEW_NERVE(HostTypeNrvTurn, MechanicKoopaMini, Turn);
    NEW_NERVE(HostTypeNrvJumpStart, MechanicKoopaMini, JumpStart);
    NEW_NERVE(HostTypeNrvJumpEnd, MechanicKoopaMini, JumpEnd);
    NEW_NERVE(HostTypeNrvFind, MechanicKoopaMini, Find);
    NEW_NERVE(HostTypeNrvPursue, MechanicKoopaMini, Pursue);
    NEW_NERVE(HostTypeNrvPreFireAttack, MechanicKoopaMini, PreFireAttack);
    NEW_NERVE_ONEND(HostTypeNrvFireAttack, MechanicKoopaMini, FireAttack, FireAttack);
    NEW_NERVE(HostTypeNrvFireAttackEnd, MechanicKoopaMini, FireAttackEnd);
    NEW_NERVE(HostTypeNrvAttackHit, MechanicKoopaMini, AttackHit);
    NEW_NERVE(HostTypeNrvSpinHit, MechanicKoopaMini, SpinHit);
    NEW_NERVE(HostTypeNrvTrample, MechanicKoopaMini, Trample);
    NEW_NERVE(HostTypeNrvHipDropped, MechanicKoopaMini, HipDropped);
    NEW_NERVE_ONEND(HostTypeNrvBindStarPointer, MechanicKoopaMini, BindStarPointer, BindStarPointer);
};  // namespace NrvMechanicKoopaMini

namespace {
    NEBULA_TUNABLE_RECORD(MR::ActorMoveParam, hNoMoveNoTurnParam, {0.0f, 2.0f, 0.8f, 0.0f});
    NEBULA_TUNABLE_RECORD(MR::ActorMoveParam, hOnAirParam, {0.0f, 2.0f, 0.98f, 0.0f});
    NEBULA_TUNABLE_RECORD(MR::ActorMoveParam, hOnAirTurnParam, {0.0f, 2.0f, 0.98f, 2.0f});
    NEBULA_TUNABLE_RECORD(MR::ActorMoveParam, hOnAirMoveParam, {0.2f, 2.0f, 0.98f, 2.0f});
    NEBULA_TUNABLE_RECORD(MR::ActorMoveParam, hWalkParam, {0.4f, 2.0f, 0.8f, 2.0f});
    NEBULA_TUNABLE_RECORD(MR::ActorMoveParam, hPursueParam, {1.3f, 2.0f, 0.8f, 2.0f});
    NEBULA_TUNABLE_RECORD(MR::ActorMoveParam, hPreFireAttackParam, {0.0f, 2.0f, 0.8f, 0.0f});
    NEBULA_TUNABLE_RECORD(MR::ActorMoveParam, hFireAttackParam, {0.0f, 2.0f, 0.8f, 1.0f});
    NEBULA_TUNABLE_RECORD(MR::ActorMoveParam, hHitReactionOnGroundParam, {0.0f, 1.2f, 0.8f, 5.0f});
    NEBULA_TUNABLE_RECORD(MR::ActorMoveParam, hHitReactionAirParam, {0.0f, 2.0f, 0.95f, 5.0f});
    NEBULA_TUNABLE_RECORD(MR::ActorMoveParam, hJumpOnGroundParam, {0.0f, 1.2f, 0.8f, 0.0f});
    NEBULA_TUNABLE_RECORD(MR::ActorMoveParam, hJumpAirParam, {0.5f, 2.0f, 0.95f, 0.0f});
    NEBULA_TUNABLE_RECORD(MR::ActorMoveParam, hTurnParam, {0.0f, 2.0f, 0.8f, 2.0f});
    static const Vec3f hBinderOffset(0.0f, 110.0f, 20.0f);
    NEBULA_TUNABLE(f32, hTrampleUpRangeSub, 80);
    NEBULA_TUNABLE(f32, hPushUpRangeSub, 130);
};  // namespace

MechanicKoopaMini::MechanicKoopaMini(const char* pName) : LiveActor(pName) {
    lumacore::setClass(this, lumacore::kClassId< MechanicKoopaMini >);
    mIsAppearJumping = false;
    mIsForbidPause = false;
    mScaleController = nullptr;
    mBindStarPointer = nullptr;
}

void MechanicKoopaMini::onAppearSwitchOn() {
    if (mIsAppearJumping) {
        setNerve(GET_NERVE(MechanicKoopaMini, HostTypeNrvJumpStart));
    } else {
        setNerve(GET_NERVE(MechanicKoopaMini, HostTypeNrvWait));
    }
}

void MechanicKoopaMini::init(const TableIter& rIter) {
    if (MR::isValidInfo(rIter)) {
        MR::isConnectedWithRail(rIter);
    }

    MR::initDefaultPos(this, rIter);
    MR::useStageSwitchWriteDead(this, rIter);
    MR::useStageSwitchSleep(this, rIter);

    if (MR::useStageSwitchReadAppear(this, rIter)) {
        MR::listenStageSwitchOnAppear(this, MR::Functor(this, &::MechanicKoopaMini::onAppearSwitchOn));
    }

    s32 arg = -1;
    MR::getPlacementArg0NoInit(rIter, &arg);
    if (arg == -1) {
        mIsAppearJumping = false;
    } else {
        mIsAppearJumping = true;
    }

    arg = -1;
    MR::getPlacementArg1NoInit(rIter, &arg);
    if (arg == -1) {
        mIsForbidPause = false;
    } else {
        mIsForbidPause = true;
    }

    initRailRider(rIter);
    initModelManagerWithAnm("MechanicKoopaMini", nullptr, false);
    MR::connectToSceneEnemy(this);
    initBinder(cNum_init_f1, 0.0f, 0);
    MR::setBinderOffsetVec(this, &::hBinderOffset, true);
    MR::onCalcGravity(this);
    initSound(3, false);
    MR::initLightCtrl(this);
    initEffectKeeper(1, nullptr);
    initNerve(GET_NERVE(MechanicKoopaMini, HostTypeNrvWalkOnRail));
    initHitSensor(3);
    f32 yScale = mScale.y;
    MR::addHitSensorAtJointEnemy(this, "body", "JointRoot", 16, cNum_init_f2 * yScale, Vec3f(cNum_init_f3, cNum_init_f4, 0.0f));
    MR::addHitSensorCallbackEnemyAttack(this, "fire", 16, cNum_init_f5 * yScale);
    MR::initShadowFromCSV(this, "Shadow");
    MR::declareCoin(this, 1);
    MR::initStarPointerTarget(this, cNum_init_f6, Vec3f(0.0f, cNum_init_f7, 0.0f));
    MR::validateClipping(this);
    MR::tryRegisterDemoCast(this, rIter);
    mScaleController = new AnimScaleController(nullptr);
    mScaleController->setParamTight();
    mBindStarPointer = new WalkerStateBindStarPointer(this, mScaleController);

    if (MR::isValidSwitchAppear(this)) {
        makeActorDead();
    } else {
        makeActorAppeared();
    }
}

void MechanicKoopaMini::initAfterPlacement() {
    MR::trySetMoveLimitCollision(this);
}

void MechanicKoopaMini::makeActorAppeared() {
    LiveActor::makeActorAppeared();
    getSensor("fire")->invalidate();
}

void MechanicKoopaMini::kill() {
    LiveActor::kill();
    if (MR::isValidSwitchDead(this)) {
        MR::onSwitchDead(this);
    }

    MR::emitEffect(this, "Death");
    MR::startSound(this, "SE_EM_EXPLODE_S");
}

void MechanicKoopaMini::exeWait() {
    if (MR::isFirstStep(this)) {
        MR::startAction(this, "Wait");
    }

    MR::moveAndTurnToPlayer(this, ::hNoMoveNoTurnParam);

    if (MR::calcDistanceToPlayer(this) < cNum_exeWait_f1 && MR::isFaceToPlayerDegree(this, cNum_exeWait_isFaceToPlayerDegree_1)) {
        setNerve(GET_NERVE(MechanicKoopaMini, HostTypeNrvFind));
    } else if (MR::isGreaterStep(this, NebulaFrameClock::stepsOfSeconds(2))) {
        setNerve(GET_NERVE(MechanicKoopaMini, HostTypeNrvWalkOnRail));
    }
}

void MechanicKoopaMini::exeWalkOnRail() {
    if (MR::isFirstStep(this)) {
        MR::startAction(this, "Walk");
    }

    bool canTurn = false;
    if (MR::isOnGround(this)) {
        MR::moveAndTurnAlongRail(this, cNum_exeWalkOnRail_moveAndTurnAlongRail_1, ::hWalkParam, &canTurn);
        Vec3f frontVec;
        MR::calcFrontVec(&frontVec, this);
        if (MR::isBindedWallOfMap(this)) {
            Vec3f vecc(*MR::getWallNormal(this));
            if (frontVec.dot(vecc) < -cNum_exeWalkOnRail_f1) {
                setNerve(GET_NERVE(MechanicKoopaMini, HostTypeNrvJumpStart));
                return;
            }
        }

        if (MR::calcDistanceToPlayer(this) < cNum_exeWalkOnRail_f2 && MR::isFaceToPlayerDegree(this, cNum_exeWalkOnRail_isFaceToPlayerDegree_1)) {
            setNerve(GET_NERVE(MechanicKoopaMini, HostTypeNrvFind));
            return;
        }
    } else {
        MR::moveAndTurnAlongRail(this, cNum_exeWalkOnRail_moveAndTurnAlongRail_2, ::hOnAirMoveParam, &canTurn);
    }

    if (canTurn) {
        setNerve(GET_NERVE(MechanicKoopaMini, HostTypeNrvTurn));
    } else if (!mIsForbidPause && MR::isGreaterStep(this, NebulaFrameClock::stepsOfSeconds(2))) {
        setNerve(GET_NERVE(MechanicKoopaMini, HostTypeNrvWait));
    }
}

void MechanicKoopaMini::exeTurn() {
    if (MR::isFirstStep(this)) {
        MR::startAction(this, "Turn");
    }

    Vec3f railDir(MR::getRailDirection(this));
    if (MR::isOnGround(this)) {
        MR::moveAndTurnToDirection(this, railDir, ::hTurnParam);
    } else {
        MR::moveAndTurnToDirection(this, railDir, ::hOnAirParam);
    }

    Vec3f vec(mPosition);
    vec.add(railDir);
    if (MR::isFaceToTargetDegree(this, vec, cNum_exeTurn_f1)) {
        setNerve(GET_NERVE(MechanicKoopaMini, HostTypeNrvWalkOnRail));
    }
}

void MechanicKoopaMini::exeJumpStart() {
    if (MR::isFirstStep(this)) {
        MR::startAction(this, "JumpStart");
    }

    if (MR::isOnGround(this)) {
        MR::moveAndTurnToPlayer(this, ::hJumpOnGroundParam);
    } else {
        MR::moveAndTurnToPlayer(this, ::hJumpAirParam);
    }

    if (MR::isStep(this, (s32)MR::getBckFrameMax(this) - 3)) {
        MR::addVelocityJump(this, cNum_exeJumpStart_addVelocityJump_1);
        MR::startSound(this, "SE_EM_MKOOPAMINI_JUMP");
    }

    if (MR::isActionEnd(this)) {
        setNerve(GET_NERVE(MechanicKoopaMini, HostTypeNrvJumpEnd));
    }
}

void MechanicKoopaMini::exeJumpEnd() {
    if (MR::isFirstStep(this)) {
        MR::startAction(this, "JumpEnd");
    }

    if (MR::isOnGround(this)) {
        MR::moveAndTurnToPlayer(this, ::hJumpOnGroundParam);
    } else {
        MR::moveAndTurnToPlayer(this, ::hJumpAirParam);
    }

    if (MR::isActionEnd(this)) {
        setNerve(GET_NERVE(MechanicKoopaMini, HostTypeNrvWalkOnRail));
    }
}

void MechanicKoopaMini::exeFind() {
    if (MR::isFirstStep(this)) {
        MR::startAction(this, "Find");
        MR::addVelocityJump(this, cNum_exeFind_addVelocityJump_1);
        MR::startSound(this, "SE_EM_MKOOPAMINI_JUMP");
    }

    if (MR::isOnGround(this)) {
        MR::moveAndTurnToPlayer(this, ::hTurnParam);
    } else {
        MR::moveAndTurnToPlayer(this, ::hOnAirTurnParam);
    }

    if (MR::isActionEnd(this)) {
        setNerve(GET_NERVE(MechanicKoopaMini, HostTypeNrvPursue));
    }
}

void MechanicKoopaMini::exePursue() {
    Vec3f frontVec;
    if (MR::isFirstStep(this)) {
        MR::startAction(this, "Pursue");
        MR::invalidateClipping(this);
        mVelocity.set(mGravity * -cNum_exePursue_f1);
    }

    if (MR::isOnGround(this)) {
        MR::moveAndTurnToPlayer(this, ::hPursueParam);
        MR::calcFrontVec(&frontVec, this);
        if (MR::isBindedWall(this)) {
            Vec3f vecc(*MR::getWallNormal(this));
            if (frontVec.dot(vecc) < -cNum_exePursue_f2) {
                setNerve(GET_NERVE(MechanicKoopaMini, HostTypeNrvJumpStart));
            }
        }
    } else {
        MR::moveAndTurnToPlayer(this, ::hOnAirMoveParam);
        MR::calcFrontVec(&frontVec, this);
    }

    f32 distance = MR::calcDistanceToPlayer(this);
    if (cNum_exePursue_f3 < distance) {
        setNerve(GET_NERVE(MechanicKoopaMini, HostTypeNrvWalkOnRail));
    } else if (distance < cNum_exePursue_f4 && MR::isFaceToPlayerHorizontalDegree(this, frontVec, cNum_exePursue_f5) && MR::isOnGround(this)) {
        setNerve(GET_NERVE(MechanicKoopaMini, HostTypeNrvPreFireAttack));
    }
}

void MechanicKoopaMini::exePreFireAttack() {
    if (MR::isFirstStep(this)) {
        MR::startAction(this, "PreFire");
        MR::invalidateClipping(this);
        mVelocity.set(mGravity * -cNum_exePreFireAttack_f1);
    }

    if (MR::isLessEqualStep(this, NebulaFrameClock::stepsOfSeconds(1))) {
        MR::startLevelSound(this, "SE_EM_LV_MKOOPAMINI_PRE_FIRE");
    }

    if (MR::isOnGround(this)) {
        MR::moveAndTurnToPlayer(this, ::hPreFireAttackParam);
    } else {
        MR::moveAndTurnToPlayer(this, ::hOnAirParam);
    }

    if (MR::isActionEnd(this)) {
        setNerve(GET_NERVE(MechanicKoopaMini, HostTypeNrvFireAttack));
    }
}

void MechanicKoopaMini::exeFireAttack() {
    if (MR::isFirstStep(this)) {
        getSensor("fire")->validate();
        MR::emitEffect(this, "Fire");
        MR::startAction(this, "Fire");
    }

    if (MR::isOnGround(this)) {
        MR::moveAndTurnToPlayer(this, ::hFireAttackParam);
    } else {
        MR::moveAndTurnToPlayer(this, ::hOnAirParam);
    }

    if (MR::isGreaterStep(this, NebulaFrameClock::stepsOfSeconds(2))) {
        setNerve(GET_NERVE(MechanicKoopaMini, HostTypeNrvFireAttackEnd));
    } else if (MR::isLessEqualStep(this, NebulaFrameClock::stepsOfMilliseconds(1500))) {
        MR::startLevelSound(this, "SE_EM_LV_MKOOPAMINI_FIRE");
    }
}

void MechanicKoopaMini::endFireAttack() {
    MR::deleteEffect(this, "Fire");
    getSensor("fire")->invalidate();
}

void MechanicKoopaMini::exeFireAttackEnd() {
    if (MR::isFirstStep(this)) {
        MR::startAction(this, "FireEnd");
    }

    if (MR::isOnGround(this)) {
        MR::moveAndTurnToPlayer(this, ::hNoMoveNoTurnParam);
    } else {
        MR::moveAndTurnToPlayer(this, ::hOnAirParam);
    }

    if (MR::isActionEnd(this)) {
        setNerve(GET_NERVE(MechanicKoopaMini, HostTypeNrvWalkOnRail));
    }
}

void MechanicKoopaMini::exeAttackHit() {
    if (MR::isFirstStep(this)) {
        MR::startAction(this, "AttackHit");
    }

    if (MR::isOnGround(this)) {
        MR::moveAndTurnToPlayer(this, ::hHitReactionOnGroundParam);
    } else {
        MR::moveAndTurnToPlayer(this, ::hHitReactionAirParam);
    }

    if (MR::isOnGround(this) && MR::isActionEnd(this)) {
        setNerve(GET_NERVE(MechanicKoopaMini, HostTypeNrvWait));
    }
}

void MechanicKoopaMini::exeSpinHit() {
    if (MR::isFirstStep(this)) {
        MR::startAction(this, "SpinHit");
        MR::startBlowHitSound(this);
        MR::startSound(this, "SE_EM_MKOOPAMINI_SPIN_HIT");
    }

    if (MR::isGreaterEqualStep(this, NebulaFrameClock::stepsOfSeconds(1))) {
        MR::startLevelSound(this, "SE_EM_LV_MKOOPAMINI_PRE_FIRE");
    }

    if (MR::isOnGround(this)) {
        MR::moveAndTurnToPlayer(this, ::hNoMoveNoTurnParam);
    } else {
        MR::moveAndTurnToPlayer(this, ::hOnAirParam);
    }

    if (MR::isActionEnd(this)) {
        setNerve(GET_NERVE(MechanicKoopaMini, HostTypeNrvFireAttack));
    }
}

void MechanicKoopaMini::exeTrample() {
    if (MR::isFirstStep(this)) {
        MR::startAction(this, "Trample");
    }

    if (MR::isGreaterEqualStep(this, NebulaFrameClock::stepsOfSeconds(1))) {
        MR::startLevelSound(this, "SE_EM_LV_MKOOPAMINI_PRE_FIRE");
    }

    if (MR::isOnGround(this)) {
        MR::moveAndTurnToPlayer(this, ::hNoMoveNoTurnParam);
    } else {
        MR::moveAndTurnToPlayer(this, ::hOnAirParam);
    }

    if (MR::isActionEnd(this)) {
        setNerve(GET_NERVE(MechanicKoopaMini, HostTypeNrvFireAttack));
    }
}

void MechanicKoopaMini::exeHipDropped() {
    if (MR::isFirstStep(this)) {
        MR::startAction(this, "Break");
        MR::invalidateHitSensors(this);
        MR::startSound(this, "SE_EM_MKOOPAMINI_BREAK");
    }

    if (MR::isActionEnd(this)) {
        kill();
        MR::appearCoinPop(this, mPosition, 1);
    }
}

void MechanicKoopaMini::exeBindStarPointer() {
    MR::updateActorStateAndNextNerve(this, mBindStarPointer, GET_NERVE(MechanicKoopaMini, HostTypeNrvWalkOnRail));
}

void MechanicKoopaMini::endBindStarPointer() {
    mBindStarPointer->kill();
}

void MechanicKoopaMini::control() {
    mScaleController->updateNerve();
    if (MR::isInDeath(this, Vec3f(0.0f, 0.0f, 0.0f)) || MR::isInWater(mPosition)) {
        kill();
    } else if ((isNerve(GET_NERVE(MechanicKoopaMini, HostTypeNrvFireAttack)) || isNerve(GET_NERVE(MechanicKoopaMini, HostTypeNrvPreFireAttack)) ||
                isNerve(GET_NERVE(MechanicKoopaMini, HostTypeNrvWalkOnRail)) || isNerve(GET_NERVE(MechanicKoopaMini, HostTypeNrvPursue))) &&
               mBindStarPointer->tryStartPointBind()) {
        setNerve(GET_NERVE(MechanicKoopaMini, HostTypeNrvBindStarPointer));
    }
}

void MechanicKoopaMini::attackSensor(HitSensor* pSender, HitSensor* pReceiver) {
    if (!isNerve(GET_NERVE(MechanicKoopaMini, HostTypeNrvHipDropped))) {
        if (getSensor("fire") == pSender) {
            if (MR::isSensorPlayer(pReceiver)) {
                MR::sendMsgEnemyAttackFire(pReceiver, pSender);
            }
        } else if (MR::isSensorPlayer(pReceiver)) {
            if (!MR::isPlayerHipDropFalling()) {
                Vec3f pos(pReceiver->mPosition);
                pos -= pSender->mPosition;
                f32 SenderRad = pSender->mRadius;
                f32 ReceiverRad = pReceiver->mRadius;
                if (!(((ReceiverRad + SenderRad) - ::hPushUpRangeSub) < pos.dot(-mGravity))) {
                    MR::sendMsgPush(pReceiver, pSender);
                }
            }
        } else if (MR::isSensorEnemy(pReceiver)) {
            MR::sendMsgPush(pReceiver, pSender);
        }
    }
}

bool MechanicKoopaMini::receiveMsgPlayerAttack(u32 msg, HitSensor* pSender, HitSensor* pReceiver) {
    if (getSensor("fire") == pReceiver) {
        return false;
    }

    if (isNerve(GET_NERVE(MechanicKoopaMini, HostTypeNrvHipDropped))) {
        return false;
    }

    if (MR::isMsgStarPieceAttack(msg)) {
        setNerve(GET_NERVE(MechanicKoopaMini, HostTypeNrvSpinHit));
        return true;
    }

    if (MR::isMsgPlayerTrample(msg)) {
        Vec3f toSender(pSender->mPosition);
        toSender -= pReceiver->mPosition;
        f32 receiverRadius = pReceiver->mRadius;
        f32 senderRadius = pSender->mRadius;
        if (((senderRadius + receiverRadius) - ::hTrampleUpRangeSub) < toSender.dot(-mGravity)) {
            return false;
        } else {
            MR::startSound(this, "SE_EM_MKOOPAMINI_TRAMPLE");
            if (isNerve(GET_NERVE(MechanicKoopaMini, HostTypeNrvSpinHit))) {
                return true;
            }

            if (!isNerve(GET_NERVE(MechanicKoopaMini, HostTypeNrvTrample))) {
                setNerve(GET_NERVE(MechanicKoopaMini, HostTypeNrvTrample));
            }
            return true;
        }
    }

    if (MR::isMsgPlayerHipDrop(msg) && !isNerve(GET_NERVE(MechanicKoopaMini, HostTypeNrvHipDropped))) {
        setNerve(GET_NERVE(MechanicKoopaMini, HostTypeNrvHipDropped));
        return true;
    }

    if (MR::isMsgPlayerSpinAttack(msg)) {
        if (!isNerve(GET_NERVE(MechanicKoopaMini, HostTypeNrvSpinHit)) || !MR::isLessStep(this, NebulaFrameClock::stepsOfSeconds(2))) {
            setNerve(GET_NERVE(MechanicKoopaMini, HostTypeNrvSpinHit));
        }
        return true;
    }

    return false;
}

bool MechanicKoopaMini::receiveMsgEnemyAttack(u32 msg, HitSensor* /*pSender*/, HitSensor* /*pReceiver*/) {
    if (MR::isMsgExplosionAttack(msg)) {
        kill();
        MR::appearCoinPop(this, mPosition, 1);
        return true;
    }

    return false;
}

bool MechanicKoopaMini::receiveOtherMsg(u32 msg, HitSensor* /*pSender*/, HitSensor* /*pReceiver*/) {
    if (MR::isMsgInhaleBlackHole(msg)) {
        kill();
        return true;
    }

    return false;
}

bool MechanicKoopaMini::receiveMsgPush(HitSensor* pSender, HitSensor* pReceiver) {
    if (!MR::isSensorPlayer(pSender)) {
        MR::addVelocityFromPush(this, cNum_receiveMsgPush_addVelocityFromPush_1, pSender, pReceiver);
        return true;
    }

    return false;
}

void MechanicKoopaMini::updateHitSensor(HitSensor* pSensor) {
    Vec3f up, front;
    MR::calcUpVec(&up, this);
    MR::calcFrontVec(&front, this);
    f32 dot = front.dot(*MR::getPlayerCenterPos() - mPosition);
    f32 nerveRate = cNum_updateHitSensor_f1 * MR::calcNerveRate(this, 10, 30);
    f32 frontDist = MR::clamp(dot, 0.0f, nerveRate);

    pSensor->mPosition.set(mPosition);
    pSensor->mPosition.add(up * getSensor("body")->getRadius());
    pSensor->mPosition.add(front * frontDist);
}

void MechanicKoopaMini::calcAndSetBaseMtx() {
    LiveActor::calcAndSetBaseMtx();
    MR::setBaseScale(this, mScaleController->mScale * mScale);
}
