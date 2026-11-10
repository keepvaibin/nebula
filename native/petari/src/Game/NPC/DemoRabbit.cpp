#include "Game/NPC/DemoRabbit.hpp"
#include "Game/System/NebulaFrameClock.hpp"
#include "Game/LiveActor/Nerve.hpp"
#include "Game/Util.hpp"
#include "Game/Util/Tunable.hpp"

NEBULA_TUNABLE_NAMED(f32, cNum_fadeIn_f1, "fadeIn.f1", 100.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_fadeIn_f2, "fadeIn.f2", 1000.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_updateStopVelocity_f1, "updateStopVelocity.f1", 10.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_updateRun_f1, "updateRun.f1", 200.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_updateRun_f2, "updateRun.f2", 2.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_updateRun_f3, "updateRun.f3", 2.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeGuide_f1, "exeGuide.f1", 200.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeGuide_f2, "exeGuide.f2", 2.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeGuide_f3, "exeGuide.f3", 2.0f);

// The numbers this file's functions give the game's motion and animation calls, as tunables: "<file>.<function>.<call>.<k>".
NEBULA_TUNABLE_NAMED(f32, cNum_updateNormalVelocity_reboundVelocityFromCollision_1, "updateNormalVelocity.reboundVelocityFromCollision.1", -0.05f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeDemo_turnDirectionToPlayerDegree_1, "exeDemo.turnDirectionToPlayerDegree.1", 10.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeTalk_turnDirectionToPlayerDegree_1, "exeTalk.turnDirectionToPlayerDegree.1", 10.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeWait_turnDirectionToPlayerDegree_1, "exeWait.turnDirectionToPlayerDegree.1", 10.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeGoal_turnDirectionToPlayerDegree_1, "exeGoal.turnDirectionToPlayerDegree.1", 10.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeGoal_isNearPlayer_1, "exeGoal.isNearPlayer.1", 500.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeGuide_checkPassBckFrame_1, "exeGuide.checkPassBckFrame.1", 3.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeRunaway_checkPassBckFrame_1, "exeRunaway.checkPassBckFrame.1", 3.0f);

// The step counts of this file's functions, as tunables: "<file>.<function>.<k>".
NEBULA_TUNABLE_NAMED(s32, cStep_exeAppear_1, "exeAppear.1", 5);

namespace {
    NEBULA_TUNABLE(f32, sUpVecBlendRate, 0.1f);
    NEBULA_TUNABLE(f32, sFrontVecBlendRate, 0.2f);
    NEBULA_TUNABLE(f32, sGroundGravityAccel, 0.1f);
    NEBULA_TUNABLE(f32, sAirGravityAccel, 1.0f);
    NEBULA_TUNABLE(f32, sGroundFric, 0.9f);
    NEBULA_TUNABLE(f32, sAirFric, 0.99f);
    NEBULA_TUNABLE(s32, sIsAirTime, 5);
    NEBULA_TUNABLE(f32, sInRunawayRange, 800.0f);
    NEBULA_TUNABLE(f32, sOutRunawayRange, 1100.0f);
    NEBULA_TUNABLE(f32, sTalkRunawayRange, 1300.0f);
    NEBULA_TUNABLE(f32, sRunawayAccel, 1.55f);
    NEBULA_TUNABLE(f32, sRunawayEndAccel, 1.2f);
    static const s32 sRunawayDeccelTime = NebulaFrameClock::stepsOfSeconds(15);
    NEBULA_TUNABLE(f32, sRunawayTurnStartLimit, 10.0f);
    NEBULA_TUNABLE(f32, sRunawayTurnLimit, 1.5f);
    static const s32 sRunawayTurnDeccelTime = NebulaFrameClock::stepsOfMilliseconds(500);
    NEBULA_TUNABLE(f32, sWallJumpPowerV, 30.0f);
    NEBULA_TUNABLE(f32, sAppearJumpPowerV, 20.0f);
    NEBULA_TUNABLE(f32, sNearGoalDistance, 130.0f);
    NEBULA_TUNABLE(f32, sReachGoalDistance, 100.0f);
};  // namespace

namespace NrvDemoRabbit {
    NEW_NERVE(DemoRabbitNrvAppear, DemoRabbit, Appear);
    NEW_NERVE(DemoRabbitNrvDemo, DemoRabbit, Demo);
    NEW_NERVE(DemoRabbitNrvTalk0, DemoRabbit, Talk);
    NEW_NERVE(DemoRabbitNrvTalk1, DemoRabbit, Talk);
    NEW_NERVE(DemoRabbitNrvWait, DemoRabbit, Wait);
    NEW_NERVE(DemoRabbitNrvGuide, DemoRabbit, Guide);
    NEW_NERVE(DemoRabbitNrvGoal, DemoRabbit, Goal);
    NEW_NERVE(DemoRabbitNrvRunaway, DemoRabbit, Runaway);
    NEW_NERVE(DemoRabbitNrvChange, DemoRabbit, Change);
    NEW_NERVE(DemoRabbitNrvStartBGM, DemoRabbit, StartBGM);
};  // namespace NrvDemoRabbit

DemoRabbit::DemoRabbit(const char* pName) : NPCActor(pName) {
}

void DemoRabbit::init(const TableIter& rIter) {
    NPCActorCaps caps("DemoRabbit");
    caps.setDefault();
    caps.mObjectName = "TrickRabbit";
    caps.mRailRider = true;
    caps.mMessage = true;
    caps.mMakeActor = false;
    caps.mWaitNerve = GET_NERVE(DemoRabbit, DemoRabbitNrvAppear);

    if (MR::tryRegisterDemoCast(this, rIter)) {
        if (MR::getDemoCastID(rIter) == 0) {
            caps.mWaitNerve = GET_NERVE(DemoRabbit, DemoRabbitNrvAppear);
            caps.mObjectName = "TrickRabbitBaby";
            MR::invalidateClipping(this);
            MR::registerDemoActionNerve(this, GET_NERVE(DemoRabbit, DemoRabbitNrvTalk0), "チコとの出会い[ウサギ会話]");
            MR::registerDemoActionNerve(this, GET_NERVE(DemoRabbit, DemoRabbitNrvGuide), "チコとの出会い[ウサギ逃走]");
            MR::registerDemoActionFunctor(this, MR::Functor(this, &DemoRabbit::fadeOut), "ウサギ追いかけ[フェードアウト]");
            MR::registerDemoActionFunctor(this, MR::Functor(this, &DemoRabbit::fadeIn), "ウサギ追いかけ[フェードイン]");
            MR::registerDemoActionNerve(this, GET_NERVE(DemoRabbit, DemoRabbitNrvTalk1), "ウサギ追いかけ[会話]");
            MR::registerDemoActionNerve(this, GET_NERVE(DemoRabbit, DemoRabbitNrvRunaway), "ウサギ追いかけ[逃走]");
        } else {
            MR::registerDemoActionNerve(this, GET_NERVE(DemoRabbit, DemoRabbitNrvRunaway), "ウサギ追いかけ[逃走]");
            caps.mWaitNerve = GET_NERVE(DemoRabbit, DemoRabbitNrvDemo);
        }
    }

    initialize(rIter, caps);

    if (mMsgCtrl != nullptr) {
        MR::setDistanceToTalk(mMsgCtrl, ::sTalkRunawayRange);
        MR::offRootNodeAutomatic(mMsgCtrl);
    }

    MR::onCalcShadow(this, nullptr);
    MR::onCalcGravity(this);

    mPoseQuat.getZDir(mFrontVec);

    if (isNerve(GET_NERVE(DemoRabbit, DemoRabbitNrvAppear))) {
        makeActorDead();
    } else {
        makeActorAppeared();
    }
}

void DemoRabbit::initAfterPlacement() {
    if (isNerve(GET_NERVE(DemoRabbit, DemoRabbitNrvAppear)) && MR::isOnGameEventFlagEndTicoGuideDemo()) {
        MR::forwardNode(mMsgCtrl);
        makeActorAppeared();
        setNerve(GET_NERVE(DemoRabbit, DemoRabbitNrvStartBGM));
    }
}

void DemoRabbit::control() {
    MR::blendQuatUpFront(&mPoseQuat, -mGravity, mFrontVec, ::sUpVecBlendRate, ::sFrontVecBlendRate);

    if (MR::isBindedGround(this)) {
        mAirTimer = 0;
    } else {
        mAirTimer++;
    }
}

void DemoRabbit::fadeOut() {
    MR::timeKeepDemoFadeOut();
}

void DemoRabbit::fadeIn() {
    Vec3f railEndPos;
    MR::calcRailEndPos(&railEndPos, this);

    Vec3f gravityVector;
    MR::calcGravityVector(this, railEndPos, &gravityVector, nullptr, 0);


    MR::getFirstPolyOnLineToMap(&railEndPos, nullptr, railEndPos - gravityVector * cNum_fadeIn_f1, gravityVector * cNum_fadeIn_f2);
    mPosition.set(railEndPos);

    MR::timeKeepDemoFadeIn();
}

void DemoRabbit::updateStopVelocity() {
    MR::attenuateVelocity(this, mAirTimer < ::sIsAirTime ? ::sGroundFric : ::sAirFric);
    MR::addVelocityToGravityOrGround(this, ::sGroundGravityAccel);
    MR::reboundVelocityFromCollision(this);

    // No ground within 10 units: the up vector (the fall kept, the drift stopped). Left unset, `normal` can be a long
    // vector, which makes the kept part of the velocity grow each frame until the binder steps through the ground.
    Vec3f normal(-mGravity);
    MR::getFirstPolyNormalOnLineToMap(&normal, mPosition, mGravity * cNum_updateStopVelocity_f1, nullptr, nullptr);
    MR::vecKillElement(mVelocity, normal, &normal);
    mVelocity.sub(normal);
}

void DemoRabbit::updateNormalVelocity() {
    MR::attenuateVelocity(this, ::sGroundFric);

    if (MR::isBindedWall(this)) {
        MR::addVelocityToGravityOrGround(this, ::sGroundGravityAccel);
    } else if (MR::isBindedGround(this)) {
        MR::addVelocityToGravityOrGround(this, ::sGroundGravityAccel);
    } else {
        MR::addVelocityToGravityOrGround(this, ::sAirGravityAccel);
    }

    MR::reboundVelocityFromCollision(this, cNum_updateNormalVelocity_reboundVelocityFromCollision_1);
}

void DemoRabbit::updateRun(const Vec3f& targetDir, bool /*unused*/) {
    f32 turnAngle = MR::calcNerveValue(this, ::sRunawayTurnDeccelTime, ::sRunawayTurnStartLimit, ::sRunawayTurnLimit);

    MR::turnDirectionDegree(this, &mFrontVec, targetDir - mPosition, turnAngle);

    f32 accel = MR::calcNerveValue(this, ::sRunawayDeccelTime, ::sRunawayAccel, ::sRunawayEndAccel);

    while (MR::isExistMapCollision(mPosition, mFrontVec * cNum_updateRun_f1)) {
        f32 oldLength = mFrontVec.length();
        mFrontVec = (mFrontVec + -mGravity) / cNum_updateRun_f2;
        mFrontVec.setLength(oldLength);
    }

    MR::addVelocityMoveToDirection(this, mFrontVec, mAirTimer < ::sIsAirTime ? accel : accel / cNum_updateRun_f3);
}

void DemoRabbit::updateJump() {
    if (MR::isOnGround(this)) {
        mVelocity -= *MR::getGroundNormal(this) * mVelocity.dot(*MR::getGroundNormal(this));
    }

    if (MR::isBindedWall(this) && MR::calcHitPowerToWall(this) >= 0.0f) {
        MR::addVelocityJump(this, ::sWallJumpPowerV);
    }
}

bool DemoRabbit::tryGuide() {
    if (MR::isNearPlayer(this, ::sInRunawayRange)) {
        MR::invalidateClipping(this);
        setNerve(GET_NERVE(DemoRabbit, DemoRabbitNrvGuide));
        return true;
    }
    return false;
}

bool DemoRabbit::tryWait() {
    bool isFar = !MR::isNearPlayer(this, ::sOutRunawayRange);
    if (isFar) {
        MR::validateClipping(this);
        setNerve(GET_NERVE(DemoRabbit, DemoRabbitNrvWait));
        return true;
    }

    return false;
}

bool DemoRabbit::tryGoal() {
    if (MR::isOnGround(this) && MR::isRailReachedNearGoal(this, ::sReachGoalDistance)) {
        MR::validateClipping(this);
        setNerve(GET_NERVE(DemoRabbit, DemoRabbitNrvGoal));
        return true;
    }

    return false;
}

void DemoRabbit::exeAppear() {
    if (MR::isFirstStep(this)) {
        MR::startAction(this, "Appear");
        MR::addVelocityJump(this, ::sAppearJumpPowerV);
        MR::calcVecToPlayerH(&mFrontVec, this, &mGravity);
        MR::startSound(this, "SE_SM_DEMORABBIT_APPEAR");
        MR::startSound(this, "SE_SM_DEMORABBIT_SMOKE");
    }

    MR::addVelocityToGravity(this, ::sAirGravityAccel);
    MR::attenuateVelocity(this, ::sAirFric);

    if (MR::isGreaterStep(this, cStep_exeAppear_1) && MR::isBindedGround(this)) {
        mVelocity.zero();
        setNerve(GET_NERVE(DemoRabbit, DemoRabbitNrvDemo));
    }
}

void DemoRabbit::exeDemo() {
    if (MR::isFirstStep(this)) {
        MR::startAction(this, "Wait");
    }

    MR::turnDirectionToPlayerDegree(this, &mFrontVec, cNum_exeDemo_turnDirectionToPlayerDegree_1);
    updateStopVelocity();
}

void DemoRabbit::exeTalk() {
    if (MR::isFirstStep(this)) {
        if (isNerve(GET_NERVE(DemoRabbit, DemoRabbitNrvTalk1))) {
            MR::forwardNode(mMsgCtrl);
        }

        MR::tryTalkTimeKeepDemoMarioPuppetable(mMsgCtrl);

        if (isNerve(GET_NERVE(DemoRabbit, DemoRabbitNrvTalk0))) {
            MR::forwardNode(mMsgCtrl);
        }
    }

    MR::turnDirectionToPlayerDegree(this, &mFrontVec, cNum_exeTalk_turnDirectionToPlayerDegree_1);
    updateStopVelocity();
}

void DemoRabbit::exeWait() {
    if (MR::isFirstStep(this)) {
        MR::startAction(this, "Wait");
    }

    MR::tryTalkNearPlayer(mMsgCtrl);
    MR::turnDirectionToPlayerDegree(this, &mFrontVec, cNum_exeWait_turnDirectionToPlayerDegree_1);
    updateStopVelocity();

    if (tryGuide()) {
        (void)0.0f;
    }
}

void DemoRabbit::exeGoal() {
    if (MR::isFirstStep(this)) {
        MR::startAction(this, "Wait");
    }

    MR::turnDirectionToPlayerDegree(this, &mFrontVec, cNum_exeGoal_turnDirectionToPlayerDegree_1);
    updateStopVelocity();

    if (MR::isDemoActive()) {
        return;
    }

    if (MR::isNearPlayer(mMsgCtrl, cNum_exeGoal_isNearPlayer_1)) {
        MR::startTimeKeepDemoMarioPuppetable(this, "チコガイドデモ", "ウサギ追いかけ[フェードアウト]");
    } else {
        MR::tryTalkNearPlayer(mMsgCtrl);
    }
}

void DemoRabbit::exeGuide() {
    if (MR::isFirstStep(this)) {
        MR::startAction(this, "Run");
        MR::startSound(this, "SE_SM_RABBIT_JUMP");
    }

    if (MR::checkPassBckFrame(this, cNum_exeGuide_checkPassBckFrame_1)) {
        MR::startSound(this, "SE_SM_RABBIT_JUMP");
    }

    Vec3f railPosFrontCoord;
    MR::calcRailPosFrontCoord(&railPosFrontCoord, this, cNum_exeGuide_f1);

    if (MR::isRailReachedNearGoal(this, ::sNearGoalDistance)) {
        updateRun(railPosFrontCoord - mGravity * cNum_exeGuide_f2, true);
    } else {
        updateRun(railPosFrontCoord - mGravity * cNum_exeGuide_f3, false);
    }

    updateNormalVelocity();
    updateJump();
    MR::moveCoordToNearestPos(this, mPosition);

    if (tryGoal() || tryWait()) {
        (void)0.0f;
    }
}

void DemoRabbit::exeRunaway() {
    if (MR::isFirstStep(this)) {
        MR::startAction(this, "Run");
        MR::startSound(this, "SE_SM_RABBIT_JUMP");
    }

    MR::invalidateClipping(this);
    if (MR::checkPassBckFrame(this, cNum_exeRunaway_checkPassBckFrame_1)) {
        MR::startSound(this, "SE_SM_RABBIT_JUMP");
    }

    updateRun(mPosition + mPosition - *MR::getPlayerPos(), false);
    updateNormalVelocity();
    updateJump();

    if (MR::isGreaterEqualStep(this, NebulaFrameClock::stepsOfSeconds(2))) {
        setNerve(GET_NERVE(DemoRabbit, DemoRabbitNrvChange));
    }
}

void DemoRabbit::exeChange() {
    if (MR::isFirstStep(this)) {
        MR::startAction(this, "Change");
        MR::startSound(this, "SE_SM_RABBIT_HIDE");
    }

    updateNormalVelocity();

    if (MR::isBckStopped(this)) {
        MR::startSound(this, "SE_SM_METAMORPHOSE_SMOKE");
        return kill();
    }
}

void DemoRabbit::exeStartBGM() {
    if (MR::isFirstStep(this) && !MR::isPlayingStageBgm()) {
        MR::startStageBGM("MBGM_GALAXY_24", false);
    }

    setNerve(GET_NERVE(DemoRabbit, DemoRabbitNrvGuide));
}
