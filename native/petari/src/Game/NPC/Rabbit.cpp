#include "Game/NPC/Rabbit.hpp"
#include "Game/System/NebulaFrameClock.hpp"
#include "Game/LiveActor/Nerve.hpp"
#include "Game/Map/HitInfo.hpp"
#include "Game/NPC/NPCActor.hpp"
#include "Game/Util/ActorMovementUtil.hpp"
#include "Game/Util/ActorSensorUtil.hpp"
#include "Game/Util/ActorShadowUtil.hpp"
#include "Game/Util/ActorSwitchUtil.hpp"
#include "Game/Util/DemoUtil.hpp"
#include "Game/Util/EffectUtil.hpp"
#include "Game/Util/Table.hpp"
#include "Game/Util/PlacementUtil.hpp"
#include "Game/Util/JointUtil.hpp"
#include "Game/Util/LiveActorUtil.hpp"
#include "Game/Util/MapUtil.hpp"
#include "Game/Util/MathUtil.hpp"
#include "Game/Util/NPCUtil.hpp"
#include "Game/Util/ObjUtil.hpp"
#include "Game/Util/ParabolicPath.hpp"
#include "Game/Util/PlayerUtil.hpp"
#include "Game/Util/RailUtil.hpp"
#include "Game/Util/SoundUtil.hpp"
#include "Game/Util/StarPointerUtil.hpp"
#include "Game/Util/TalkUtil.hpp"
#include <nebula_geometry.hpp>
#include "Game/System/LumaCore.hpp"

namespace {
    const f32 cJumpSpeed = 20.0f;
    const f32 cProgressSpeed = 18.0f;
    const f32 cGravity = 3.0f;
    const f32 cFinalDisappearRadius = 300.0f;
    const f32 cDistEscape = 400.0f;
    const f32 cDistWait = 500.0f;
    const f32 cDistNear = 600.0f;
    const f32 cAppearHeight = 150.0f;
    const f32 cAppearHop = 10.0f;
    const f32 cRegistNormal = 0.98f;
};  // namespace

namespace NrvRabbit {
    NEW_NERVE(RabbitNrvAppear, Rabbit, Appear);
    NEW_NERVE(RabbitNrvAppearLand, Rabbit, AppearLand);
    NEW_NERVE(RabbitNrvWait, Rabbit, Wait);
    NEW_NERVE(RabbitNrvForwardLand, Rabbit, ForwardLand);
    NEW_NERVE(RabbitNrvPreJump, Rabbit, PreJump);
    NEW_NERVE(RabbitNrvMove, Rabbit, Move);
    NEW_NERVE(RabbitNrvGoal, Rabbit, Goal);
    NEW_NERVE(RabbitNrvFinish, Rabbit, Finish);
    NEW_NERVE(RabbitNrvReaction, Rabbit, Reaction);
    NEW_NERVE(RabbitNrvTalk, Rabbit, Talk);
    NEW_NERVE(RabbitNrvJumpV, Rabbit, JumpV);
    NEW_NERVE(RabbitNrvJumpH, Rabbit, JumpH);
    NEW_NERVE(RabbitNrvBackwardLand, Rabbit, BackwardLand);
    NEW_NERVE(RabbitNrvPreJumpBack, Rabbit, PreJumpBack);
    NEW_NERVE(RabbitNrvNear, Rabbit, Near);
};  // namespace NrvRabbit

Rabbit::Rabbit(const char* pName) : NPCActor(pName) {
    lumacore::setClass(this, lumacore::kClassId< Rabbit >);
}

void Rabbit::init(const TableIter& rIter) {
    const char* name;
    MR::getObjectName(&name, rIter);
    MR::initDefaultPose(this, rIter);
    initModelManagerWithAnm("MoonRabbit", nullptr, false);
    MR::connectToSceneNpc(this);
    MR::initLightCtrl(this);
    initSound(4, false);
    initBinder(10.0f, 10.0f, 0);
    MR::onCalcGravity(this);
    initHitSensor(1);
    MR::addHitSensorMtxNpc(this, "body", 8, 60.0f, MR::getJointMtx(this, "Spine"), Vec3f(0.0f, 0.0f, 0.0f));
    MR::initStarPointerTargetAtJoint(this, "Spine", 60.0f, Vec3f(0.0f, 60.0f, 0.0f));
    initEffectKeeper(0, nullptr);
    MR::initShadowFromCSV(this, "Shadow");
    mLodCtrl = MR::createLodCtrlNPC(this, rIter);
    MR::useStageSwitchWriteDead(this, rIter);
    s32 colorFrameArg = 0;
    MR::getPlacementArg0NoInit(rIter, &colorFrameArg);
    MR::startBtk(this, "MoonRabbit");
    MR::setBtkFrameAndStop(this, colorFrameArg);

    s32 messageId;
    if (MR::getPlacementMessageID(rIter, &messageId)) {
        mMsgCtrl = MR::createTalkCtrl(this, rIter, name, Vec3f(0.0f, 160.0f, 0.0f), nullptr);
        MR::onRootNodeAutomatic(mMsgCtrl);
        MR::useStageSwitchReadA(this, rIter);
        MR::useStageSwitchReadB(this, rIter);
    }

    s32 behaviorArg = 0;
    if (MR::tryRegisterDemoCast(this, rIter)) {
        if (MR::getDemoCastID(rIter) == 0) {
            mBehavior = Behavior_Demo1;
        } else {
            mBehavior = Behavior_Demo2;
        }
    } else {
        MR::getPlacementArg1NoInit(rIter, &behaviorArg);
        switch (behaviorArg) {
        case 0:
            if (MR::checkTableEntries(rIter)) {
                initRailRider(rIter);
                MR::moveCoordAndTransToNearestRailPos(this);
            }

            mBehavior = Behavior_Wait;
            initNerve(GET_NERVE(Rabbit, RabbitNrvWait));
            break;

        case 1:
            mBehavior = Behavior_Unknown;
            initNerve(GET_NERVE(Rabbit, RabbitNrvTalk));
            break;

        case 2:
            mBehavior = Behavior_UpJump;
            initNerve(GET_NERVE(Rabbit, RabbitNrvTalk));
            break;
        case 3:
            mBehavior = Behavior_LongJump;
            initNerve(GET_NERVE(Rabbit, RabbitNrvTalk));
            break;
        }
    }

    mJumpHeight = 0.0f;
    mJumpSpeed = 0.0f;
    mJumpScale = 1.0f;
    mIsLanded = 1;
    mIsFalling = false;
    mWaitStep = 0;

    switch (mBehavior) {
    case Behavior_Demo1:
        makeActorDead();
        break;

    case Behavior_Demo2:
        makeActorDead();
        break;

    default:
        if (MR::useStageSwitchReadAppear(this, rIter)) {
            MR::syncStageSwitchAppear(this);
            makeActorDead();
            pushNerve(GET_NERVE(Rabbit, RabbitNrvAppear));
        } else {
            makeActorAppeared();
            MR::startBck(this, "Wait2");
            MR::emitEffect(this, "Light");
        }

        break;
    }

    MR::setClippingFar100m(this);
    const char* const turn = "TurnSmall";
    const char* const wait = "Wait";
    const char* const reaction = "Reaction";
    const char* const pointing = "Pointing";
    const char* const press = "Press";
    const char* const spin = "Spin";
    mParam.mWaitAction = wait;
    mParam.mWaitTurnAction = turn;
    const char* talk = "Talk";
    mParam.mTalkAction = talk;
    mParam.mTalkTurnAction = turn;
    setDefaults(reaction, pointing, press, spin);
    mSwingReactDist = 450.0f;

    if (mBehavior == Behavior_LongJump) {
        mParam.mTurnWhenNear = false;
        mParam.mTurnOnTalk = false;
    }
}

void Rabbit::control() {
    if (mIsTrampledStart) {
        MR::startSound(this, "SE_SM_NPC_TRAMPLED");
        MR::startSound(this, "SE_SV_RABBIT_TRAMPLED");
    }

    NPCActor::control();
    updateJump();

    if (MR::calcDistanceToPlayer(this) > 5000.0f) {
        MR::validateClipping(this);
    } else if (MR::calcDistanceToPlayer(this) < 3000.0f) {
        MR::invalidateClipping(this);
    }
}

void Rabbit::exeAppear() {
    if (MR::isFirstStep(this)) {
        MR::startBck(this, "Appear");
    }

    ParabolicPath path = ParabolicPath();
    Vec3f hopPos, hopStart;
    hopStart.set(0.0f, ::cAppearHeight, 0.0f);
    path.initFromUpVector(hopStart, Vec3f(0.0f, 0.0f, 0.0f), -mGravity, ::cAppearHop);
    path.calcPosition(&hopPos, getNerveStep() / MR::getBckFrameMax(this));
    mJumpHeight = -hopPos.y;

    if (MR::isBckStopped(this)) {
        mJumpHeight = 0.0f;
        setNerve(GET_NERVE(Rabbit, RabbitNrvAppearLand));
    }
}

void Rabbit::exeAppearLand() {
    if (MR::isFirstStep(this)) {
        MR::tryStartBck(this, "AppearLand");
    }

    if (MR::isBckStopped(this)) {
        popNerve();
    }
}

void Rabbit::exeWait() {
    if (MR::isBckOneTimeAndStopped(this) && mIsLanded) {
        MR::startBck(this, "Wait2");
    }

    if (getNerveStep() > NebulaFrameClock::stepsOfMilliseconds(500)) {
        Vec3f playerPos = *MR::getPlayerPos() - mPosition;
        MR::normalizeOrZero(&playerPos);
        if (!MR::isNearZero(playerPos)) {
            if (isNeedTurn(playerPos) && MR::isBckOneTimeAndStopped(this)) {
                MR::startBck(this, "Turn");
            }

            MR::blendQuatUpFront(&mPoseQuat, -mGravity, playerPos, 0.5f, 0.5f);
        }

        if (MR::isBckOneTimeAndStopped(this)) {
            MR::startBck(this, "Wait2");
        }
    }

    if (MR::isNearPlayer(this, ::cDistEscape)) {
        setNerve(GET_NERVE(Rabbit, RabbitNrvPreJump));
    } else if (mWaitStep) {
        mWaitStep--;
    } else if (!MR::isNearPlayer(this, ::cDistNear)) {
        const f32 nearestCoord = MR::calcNearestRailCoord(this, *MR::getPlayerPos());
        if (nearestCoord < MR::getRailCoord(this) && MR::getRailCoord(this) > 3.0f * ::cProgressSpeed) {
            setNerve(GET_NERVE(Rabbit, RabbitNrvNear));
        }
    }
}

void Rabbit::exeGoal() {
    if (MR::isFirstStep(this)) {
        MR::startBck(this, "Wait");
    }

    if (getNerveStep() > NebulaFrameClock::stepsOfMilliseconds(500)) {
        Vec3f playerPos = *MR::getPlayerPos() - mPosition;
        MR::normalizeOrZero(&playerPos);
        if (!MR::isNearZero(playerPos)) {
            MR::blendQuatUpFront(&mPoseQuat, -mGravity, playerPos, 0.5f, 0.5f);
        }
    }

    if (MR::isNearPlayer(this, ::cFinalDisappearRadius)) {
        setNerve(GET_NERVE(Rabbit, RabbitNrvFinish));
    }
}

void Rabbit::exeFinish() {
    if (MR::isFirstStep(this)) {
        MR::startBck(this, "Change");
        MR::startSound(this, "SE_SM_RABBIT_CHANGE_JUMP");
    }

    if (MR::isBckStopped(this)) {
        MR::startSound(this, "SE_SM_RABBIT_CHANGE_EFFECT");
        if (MR::isValidSwitchDead(this)) {
            MR::onSwitchDead(this);
        }

        kill();
    }
}

void Rabbit::calcAndSetBaseMtx() {
    Vec3f pos(mPosition);
    mPosition += mGravity * mJumpHeight;
    NPCActor::calcAndSetBaseMtx();
    mPosition = pos;
}

void Rabbit::calcRailPos(Vec3f* pPos) {
    Triangle triangle = Triangle();
    Vec3f pos;
    if (MR::getFirstPolyOnLineToMap(&pos, &triangle, *pPos + mGravity * -500.0f, mGravity * 1000.0f)) {
        *pPos = pos;
    }

    mJumpScale = MR::sqrt(MR::max(1.0f, MR::abs((*pPos - mPosition).y / 80.0f)));
}

// More than 45 degrees off the facing.
bool Rabbit::isNeedTurn(const Vec3f& rDir) {
    Vec3f front;
    mPoseQuat.getZDir(front);
    return MR::diffAngleAbs(rDir, front) > 0.78539819f;
}

void Rabbit::updateJump() {
    if (!isNerve(GET_NERVE(Rabbit, RabbitNrvAppear))) {
        if (mIsLanded) {
            mJumpHeight = 0.0f;
            return;
        }

        if (mJumpHeight >= 0.0f) {
            if (isNerve(GET_NERVE(Rabbit, RabbitNrvMove)) || isNerve(GET_NERVE(Rabbit, RabbitNrvNear))) {
                mJumpSpeed = -::cJumpSpeed * mJumpScale;
            } else {
                mJumpSpeed = 0.0f;
            }

            mJumpHeight = 0.0f;

            if (mIsFalling) {
                mIsLanded = 1;
                mIsFalling = false;
                return;
            }
        } else {
            mJumpSpeed += ::cGravity;

            if (mJumpSpeed >= 0.0f) {
                mIsFalling = true;
            }
        }

        mJumpHeight += mJumpSpeed;

        if (mJumpHeight >= 0.0f) {
            mJumpHeight = 0.0f;
        }
    }
}

void Rabbit::exeForwardLand() {
    if (MR::isFirstStep(this)) {
        MR::startBck(this, "JumpEnd");
    }

    if (MR::isBckStopped(this)) {
        if (!MR::isNearPlayer(this, ::cDistWait)) {
            mWaitStep = NebulaFrameClock::stepsOfSeconds(2);
            setNerve(GET_NERVE(Rabbit, RabbitNrvWait));
        } else {
            setNerve(GET_NERVE(Rabbit, RabbitNrvPreJump));
        }
    }
}

void Rabbit::exePreJump() {
    if (MR::isFirstStep(this)) {
        MR::setRailDirectionToEnd(this);
        MR::startBck(this, "JumpStart");
        MR::startSound(this, "SE_SM_RABBIT_HOP");
    }

    Vec3f railDir(MR::getRailDirection(this));
    MR::blendQuatUpFront(&mPoseQuat, -mGravity, railDir, 0.5f, 0.5f);

    if (MR::isBckStopped(this)) {
        setNerve(GET_NERVE(Rabbit, RabbitNrvMove));
    }
}

void Rabbit::exeMove() {
    if (MR::isFirstStep(this)) {
        MR::setRailDirectionToEnd(this);
        MR::startBck(this, "Jump");
        MR::startSound(this, "SE_SM_RABBIT_JUMP");
        mIsLanded = 0;
        mMoveSpeed = ::cProgressSpeed;
        f32 distance = MR::getPlayerPos()->distance(mPosition);
        f32 hopStrengh = 1.0f;

        if (distance < 100.0f) {
            hopStrengh = 2.0f;
        } else if (distance < 150.0f) {
            hopStrengh = 1.8f;
        } else if (distance < 200.0f) {
            hopStrengh = 1.6f;
        } else if (distance < 250.0f) {
            hopStrengh = 1.4f;
        } else if (distance < 300.0f) {
            hopStrengh = 1.2f;
        } else if (distance < 350.0f) {
            hopStrengh = 1.1f;
        }

        mMoveSpeed *= hopStrengh;
        MR::moveCoordToNearestPos(this, mPosition);
        MR::moveCoord(this, 20.0f * mMoveSpeed);
        Vec3f railPos(MR::getRailPos(this));
        calcRailPos(&railPos);
        mMoveDir = railPos - mPosition;
        MR::normalizeOrZero(&mMoveDir);
    }

    mPosition += mMoveDir * mMoveSpeed;
    mMoveSpeed *= ::cRegistNormal;

    if (MR::isRailReachedGoal(this) && (mPosition - MR::getRailPos(this)).length() < 10.0f) {
        setNerve(GET_NERVE(Rabbit, RabbitNrvGoal));
    } else if (mIsLanded) {
        setNerve(GET_NERVE(Rabbit, RabbitNrvForwardLand));
    }
}

void Rabbit::exeBackwardLand() {
    if (MR::isFirstStep(this)) {
        MR::startBck(this, "JumpEnd");
    }

    if (MR::isBckStopped(this)) {
        if (MR::isNearPlayer(this, ::cDistWait)) {
            setNerve(GET_NERVE(Rabbit, RabbitNrvWait));
        } else {
            setNerve(GET_NERVE(Rabbit, RabbitNrvPreJumpBack));
        }
    }
}

void Rabbit::exePreJumpBack() {
    if (MR::isFirstStep(this)) {
        MR::setRailDirectionToStart(this);
        MR::startBck(this, "JumpStart");
        MR::startSound(this, "SE_SM_RABBIT_HOP");
    }

    if (MR::isBckStopped(this)) {
        setNerve(GET_NERVE(Rabbit, RabbitNrvNear));
    }
}

void Rabbit::exeNear() {
    if (MR::isFirstStep(this)) {
        MR::setRailDirectionToStart(this);
        mIsLanded = 0;
        MR::startBck(this, "Jump");
        MR::startSound(this, "SE_SM_RABBIT_JUMP");
        mMoveSpeed = ::cProgressSpeed;
        MR::moveCoordToNearestPos(this, mPosition);
        MR::moveCoord(this, 20.0f * mMoveSpeed);
        Vec3f railPos(MR::getRailPos(this));
        calcRailPos(&railPos);
        mMoveDir = railPos - mPosition;
        MR::normalizeOrZero(&mMoveDir);
    }

    mPosition += mMoveDir * mMoveSpeed;
    mMoveSpeed *= ::cRegistNormal;

    if (MR::isNearPlayer(this, ::cDistWait)) {
        setNerve(GET_NERVE(Rabbit, RabbitNrvWait));
    } else if (MR::isRailReachedGoal(this) && (mPosition - MR::getRailPos(this)).length() < 10.0f) {
        setNerve(GET_NERVE(Rabbit, RabbitNrvWait));
    } else {
        MR::calcNearestRailCoord(this, *MR::getPlayerPos());
        if (mIsLanded) {
            setNerve(GET_NERVE(Rabbit, RabbitNrvBackwardLand));
        }
    }
}

void Rabbit::exeReaction() {
    if (mIsTrampledStart) {
        MR::startSound(this, "SE_SM_NPC_TRAMPLED");
        MR::startSound(this, "SE_SV_RABBIT_TRAMPLED");
    }

    if (isPointingSe()) {
        MR::startPointerHitSound();
    }

    if (mIsPointed) {
        MR::startLevelSound(this, "SE_SM_LV_RABBIT_POINT");
    }

    if (mIsSpinAttackedStart) {
        MR::startSound(this, "SE_SV_RABBIT_SPIN");
        MR::startSound(this, "SE_SM_RABBIT_SPIN");
    }

    if (mIsReactionStart) {
        MR::limitedStarPieceHitSound();
        MR::startSound(this, "SE_SM_RABBIT_STAR_PIECE_HIT");
        MR::startSound(this, "SE_SV_RABBIT_STAR_PIECE_HIT");
    }

    if (MR::tryStartReactionAndPopNerve(this)) {
        return;
    }
}

void Rabbit::exeTalk() {

    if (!MR::tryStartReactionAndPushNerve(this, GET_NERVE(Rabbit, RabbitNrvReaction)) &&
        (!MR::tryTalkNearPlayerAndStartTalkAction(this) || MR::isShortTalk(mMsgCtrl))) {
        if (mBehavior == Behavior_UpJump && MR::isGreaterStep(this, NebulaFrameClock::stepsOfSeconds(3))) {
            pushNerve(GET_NERVE(Rabbit, RabbitNrvJumpV));
        } else if (mBehavior == Behavior_LongJump) {
            if (MR::isGreaterStep(this, NebulaFrameClock::stepsOfSeconds(3))) {
                pushNerve(GET_NERVE(Rabbit, RabbitNrvJumpH));
            }
        }
    }
}

void Rabbit::exeJumpV() {
    if (MR::isFirstStep(this)) {
        MR::startAction(this, "SpinLecture");
    }

    if (MR::isBckStopped(this)) {
        popNerve();
    }
}

void Rabbit::exeJumpH() {
    if (MR::isFirstStep(this)) {
        MR::startAction(this, "JumpSpinLectureMove");
    }

    if (MR::isBckLooped(this)) {
        popNerve();
    }
}
