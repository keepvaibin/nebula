#include "Game/NPC/Penguin.hpp"
#include "Game/System/NebulaFrameClock.hpp"
#include "Game/LiveActor/Nerve.hpp"
#include "Game/Map/HitInfo.hpp"
#include "Game/MapObj/JetTurtle.hpp"
#include "Game/Scene/SceneFunction.hpp"
#include "Game/Util/ActorMovementUtil.hpp"
#include "Game/Util/ActorSensorUtil.hpp"
#include "Game/Util/ActorShadowUtil.hpp"
#include "Game/Util/PlacementUtil.hpp"
#include "Game/Util/JointUtil.hpp"
#include "Game/Util/MapUtil.hpp"
#include "Game/Util/MathUtil.hpp"
#include "Game/Util/NPCUtil.hpp"
#include "Game/Util/ObjUtil.hpp"
#include "Game/Util/PlayerUtil.hpp"
#include "Game/Util/RailUtil.hpp"
#include "Game/Util/SoundUtil.hpp"
#include "Game/System/LumaCore.hpp"

namespace {
    const f32 sDistanceCall = 350.0f;
    const f32 sSpeedWalk = 1.5f;
    const f32 sSpeedDash = 6.0f;
    const f32 sBlendDash = 0.08f;
    const f32 sSpeedSwim = 5.0f;
    const f32 sSpeedSwimTurtle = 10.0f;
    static const s32 sStepToDiveMin = NebulaFrameClock::stepsOfSeconds(2);
    static const s32 sStepToDiveMax = NebulaFrameClock::stepsOfSeconds(5);
};  // namespace

namespace NrvPenguin {
    NEW_NERVE(PenguinNrvWait, Penguin, Wait);
    NEW_NERVE(PenguinNrvReaction, Penguin, Reaction);
    NEW_NERVE(PenguinNrvDive, Penguin, Dive);
    NEW_NERVE(PenguinNrvFlow, Penguin, Flow);
};  // namespace NrvPenguin

RemovableTurtle::RemovableTurtle(LiveActor* pHost, bool isGolden) {
    if (isGolden) {
        mShellModel = new TurtlePartsModel(pHost, "ペンギン用ゴールデンコウラ", "KouraShine", nullptr,
                                           MR::DrawBufferType_NoSilhouettedMapObjStrongLight, false);
        mJetTurtle = new GoldenTurtle("ゴールデン甲羅");
    } else {
        mShellModel = new PartsModel(pHost, "ペンギン用コウラ", "Koura", nullptr, MR::DrawBufferType_NoSilhouettedMapObjStrongLight, false);
        mJetTurtle = new JetTurtle("ジェット亀さん");
    }

    mShellModel->mIsShownWhileHostHidden = true;
    mShellModel->initFixedPosition(Vec3f(-5.85f, -68.0f, 30.0f), Vec3f(0.0f, 0.0f, 0.0f), nullptr);
    mShellModel->initWithoutIter();
    mShellModel->makeActorDead();

    mJetTurtle->initWithoutIter();
    mJetTurtle->mIsRevive = false;
    mJetTurtle->makeActorDead();
}

bool RemovableTurtle::isPullRange() const {
    return MR::isNearPlayer(mShellModel, 450.0f);
}

bool RemovableTurtle::tryRemove() {
    if (MR::isDead(mJetTurtle) && !MR::isPlayerCarryAny()) {
        mShellModel->makeActorDead();
        mJetTurtle->mPosition.set(mShellModel->mPosition);
        mJetTurtle->appearAndTryTaken();
        return true;
    }

    return false;
}

bool RemovableTurtle::tryAttach() {
    if (MR::isDead(mJetTurtle)) {
        mShellModel->appear();
        return true;
    }
    return false;
}

Penguin::Penguin(const char* pName) : NPCActor(pName), mBehavior(Behavior_Default), mStepToDive() {
    lumacore::setClass(this, lumacore::kClassId< Penguin >);
}

void Penguin::init(const TableIter& rIter) {
    MR::initDefaultPosAndQuat(this, rIter);
    MR::getPlacementArg0NoInit(rIter, &mBehavior);
    initModelManagerWithAnm("Penguin", nullptr, false);
    MR::connectToSceneNpc(this);
    MR::initLightCtrl(this);

    initHitSensor(1);
    MR::addHitSensorMtxNpc(this, "push", 8, 80.0f, MR::getJointMtx(this, "CenterJoint"), Vec3f(0.0f, 0.0f, 0.0f));

    initEffectKeeper(0, nullptr);
    initSound(4, false);

    MR::initShadowVolumeSphere(this, 50.0f);
    MR::onCalcShadowOneTime(this, nullptr);

    initTalkCtrl(rIter, "Penguin", Vec3f(0.0f, 150.0f, 0.0f), nullptr);

    NPCActorCaps caps("");
    caps.mLodCtrl = true;
    caps.mNerve = false;
    caps.mPointer = true;
    caps.mScaleJoint = "CenterJoint";
    initialize(rIter, caps);

    initNerve(GET_NERVE(Penguin, PenguinNrvWait));

    if (MR::isConnectedWithRail(rIter)) {
        initRailRider(rIter);
        MR::moveCoordToNearestPos(this, mPosition);
        mPosition.set(MR::getRailPos(this));
    }

    if (mBehavior == Behavior_SwimTurtle) {
        mTurtle = new RemovableTurtle(this, false);
        mTurtle->tryAttach();
    }

    s32 color = 0;
    MR::getPlacementArg7NoInit(rIter, &color);
    MR::startBrk(this, "ColorChange");
    MR::setBrkFrameAndStop(this, color);

    setDefaults();
    mUseReactionActions = false;
    mSwingReactDist = ::sDistanceCall;

    switch (mBehavior) {
    case Behavior_Default:
    case Behavior_Stand:
        mParam.setMoveAction("Wait", "Turn");
        mParam.setTalkAction("Talk", "TalkTurn");
        setTalkAction("Walk");
        mRailSpeed = ::sSpeedWalk;
        mMoveTalkBckRate = 2.0f;  // talkBckRate?
        mIsRailOnGround = true;  // follow rail on ground
        mUseReactionActions = true;
        break;
    case Behavior_Sit:
        mParam.setMoveTalkNoTurnAction("SitDown", "SitDownTalk");
        break;
    case Behavior_Swim:
        mParam.setMoveAction("SwimWait", "SwimWaitTalk");
        setTalkAction("Swim");
        mRailSpeed = ::sSpeedSwim;
        break;
    case Behavior_SwimSurface:
        mParam.setMoveAction("SwimWaitSurface", "SwimSurfaceTalk");
        setTalkAction("SwimSurface");
        mRailSpeed = ::sSpeedSwim;
        break;
    case Behavior_Dive:
        mParam.setSingleAction("SwimWaitSurface");
        break;
    case Behavior_SwimTurtle:
        mParam.setSingleAction("SwimTurtleTalk");
        setTalkAction("SwimTurtle");
        mRailSpeed = ::sSpeedSwimTurtle;
        break;
    case Behavior_Dash:
        mParam.setMoveAction("Wait", "Turn");
        mParam.setTalkAction("Talk", "TalkTurn");
        setTalkAction("DashA");
        mRailSpeed = ::sSpeedDash;
        mRailPoseRate = ::sBlendDash;
        mIsRailOnGround = true;  // follow rail on ground
        mUseReactionActions = true;
        break;
    default:
        break;
    }
    setNerve(GET_NERVE(Penguin, PenguinNrvWait));
    makeActorAppeared();
}

void Penguin::initAfterPlacement() {
    NPCActor::initAfterPlacement();

    switch (mBehavior) {
    case Behavior_SwimSurface:
    case Behavior_Dive: {
        MR::calcGravity(this);
        MR::turnQuatYDirRad(&mPoseQuat, mPoseQuat, -mGravity, PI);

        Vec3f pos;
        Vec3f grav = mGravity;
        Triangle triangle;
        if (MR::getFirstPolyOnLineToWaterSurface(&pos, &triangle, mPosition - grav * 100.0f, grav * 500.0f)) {
            mPosition.set(pos);
        }

        setInitPose();
    }
    default:
        break;
    }
}

void Penguin::exeReaction() {
    if (mIsTrampledStart) {
        MR::startSound(this, "SE_SM_PENGUIN_TRAMPLED");
        MR::startSound(this, "SE_SV_PENGUIN_S_TRAMPLED");
    }

    if (isPointingSe()) {
        MR::startSound(this, "SE_SV_PENGUIN_S_POINTED_2P");
        MR::startPointerHitSound();
    }

    if (mIsSpinAttackedStart) {
        MR::startSound(this, "SE_SV_PENGUIN_S_SPIN_HIT");
    }

    if (mIsReactionStart) {
        MR::startSound(this, "SE_SV_PENGUIN_S_STAR_PIECE");
    }

    if (MR::tryStartReactionAndPopNerve(this)) {
        return;
    }
}

void Penguin::exeWait() {
    if (MR::isFirstStep(this)) {
        mStepToDive = MR::getRandom(::sStepToDiveMin, ::sStepToDiveMax);
        if (MR::isExistRail(this)) {
            MR::onCalcShadow(this, nullptr);
        }
    }

    if (MR::tryStartReactionAndPushNerve(this, GET_NERVE(Penguin, PenguinNrvReaction))) {
        return;
    }

    switch (mBehavior) {
    case Behavior_Default:
    case Behavior_Stand:
    case Behavior_Dash:
        MR::tryChangeTalkActionRandom(this, "Talk", "TalkA", "TalkB");
    default:
        break;
    }

    MR::tryTalkNearPlayerAndStartMoveTalkAction(this);

    if (mBehavior == Behavior_SwimTurtle) {
        MR::startLevelSound(this, "SE_SM_LV_PENGUIN_SWIM_JET");
    }

    if (mBehavior == Behavior_Dive && MR::isStep(this, mStepToDive)) {
        setNerve(GET_NERVE(Penguin, PenguinNrvDive));
    }
}

void Penguin::exeDive() {
    if (MR::isFirstStep(this)) {
        MR::startBck(this, "SwimDive");
        MR::startSound(this, "SE_SV_PENGUIN_S_DIVE");
    }

    if (MR::isBckStopped(this)) {
        setNerve(GET_NERVE(Penguin, PenguinNrvWait));
    }
}

void Penguin::exeFlow() {
    if (MR::isFirstStep(this)) {
        mMoveAction = "Swim";
        mMoveTalkAction = "Swim";
        mRailSpeed = ::sSpeedSwim;
    }

    MR::tryTalkNearPlayerAndStartMoveTalkAction(this);

    if (mTurtle->tryAttach()) {
        mParam.setSingleAction("SwimTurtleTalk");
        mMoveAction = "SwimTurtle";
        mMoveTalkAction = "SwimTurtle";
        mRailSpeed = ::sSpeedSwimTurtle;
        setNerve(GET_NERVE(Penguin, PenguinNrvWait));
    }
}

void Penguin::attackSensor(HitSensor* pSender, HitSensor* pReceiver) {
    if (MR::isSensorPlayer(pReceiver)) {
        tryReleaseTurtle();
    }

    NPCActor::attackSensor(pSender, pReceiver);
}

bool Penguin::receiveOtherMsg(u32 msg, HitSensor* /*pSender*/, HitSensor* /*pReceiver*/) {
    if (MR::isMsgSpinStormRange(msg) && tryReleaseTurtle()) {
        return true;
    }

    return false;
}

bool Penguin::tryReleaseTurtle() {
    if (mBehavior == Behavior_SwimTurtle && isNerve(GET_NERVE(Penguin, PenguinNrvWait)) && mTurtle->isPullRange() && mTurtle->tryRemove()) {
        setNerve(GET_NERVE(Penguin, PenguinNrvFlow));
        return true;
    }

    return false;
}
