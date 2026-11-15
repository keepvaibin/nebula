#include "Game/NPC/Tico.hpp"
#include "Game/Demo/AstroDemoFunction.hpp"
#include "Game/Demo/DemoFunction.hpp"
#include "Game/LiveActor/HitSensor.hpp"
#include "Game/LiveActor/Nerve.hpp"
#include "Game/NPC/TalkMessageFunc.hpp"
#include "Game/NPC/TicoDemoGetPower.hpp"
#include "Game/Util/ActorMovementUtil.hpp"
#include "Game/Util/ActorSensorUtil.hpp"
#include "Game/Util/ActorSwitchUtil.hpp"
#include "Game/Util/DemoUtil.hpp"
#include "Game/Util/EventUtil.hpp"
#include "Game/Util/PlacementUtil.hpp"
#include "Game/Util/JointUtil.hpp"
#include "Game/Util/LightUtil.hpp"
#include "Game/Util/LiveActorUtil.hpp"
#include "Game/Util/MathUtil.hpp"
#include "Game/Util/MtxUtil.hpp"
#include "Game/Util/NPCUtil.hpp"
#include "Game/Util/RailUtil.hpp"
#include "Game/Util/SoundUtil.hpp"
#include "Game/Util/TalkUtil.hpp"
#include "Game/System/LumaCore.hpp"

namespace NrvTico {
    NEW_NERVE(TicoNrvNoReaction, Tico, NoReaction);
    NEW_NERVE(TicoNrvReaction, Tico, Reaction);
    NEW_NERVE(TicoNrvDelight, Tico, Delight);
    NEW_NERVE(TicoNrvAppear, Tico, Appear);
    NEW_NERVE(TicoNrvWait, Tico, Wait);
    NEW_NERVE(TicoNrvTalk, Tico, Talk);
    NEW_NERVE(TicoNrvMeta, Tico, Meta);
    NEW_NERVE(TicoNrvBlue0, Tico, Blue0);
    NEW_NERVE(TicoNrvBlue1, Tico, Blue1);
    NEW_NERVE(TicoNrvRed0, Tico, Red0);
    NEW_NERVE(TicoNrvRed1, Tico, Red1);
    NEW_NERVE(TicoNrvRed2, Tico, Red2);
    NEW_NERVE(TicoNrvSpin0, Tico, Spin0);
    NEW_NERVE(TicoNrvGuide0, Tico, Guide0);
    NEW_NERVE(TicoNrvGuide1, Tico, Guide1);
    NEW_NERVE(TicoNrvGuide2, Tico, Guide2);
    NEW_NERVE(TicoNrvGuide3, Tico, Guide3);
    NEW_NERVE(TicoNrvLead0, Tico, Lead0);
};  // namespace NrvTico

namespace {
    const Color8 hPointLight[] = {Color8(0xFF, 0xFF, 0, 0xFF), Color8(0, 0x64, 0xFF, 0xFF), Color8(0, 0xFF, 0x32, 0xFF),
                                   Color8(0xFF, 0, 0, 0xFF),    Color8(0x78, 0, 0xFF, 0xFF), Color8(0xFF, 0x64, 0x64, 0xFF)};
};  // namespace

const f32 Tico::sFloatSeMinVolume = 0.2f;

Tico::Tico(const char* pName)
    : NPCActor(pName), mBehavior(), mPrevPos(0.0f, 0.0f, 0.0f), mFloatOffset(), mDemoGetPower(), mTicoTalkCtrl(), mLightJointMtx(), mLightColor(255, 255, 255, 255), mMetaNerve(), mDemoStarter(this) {
    lumacore::setClass(this, lumacore::kClassId< Tico >);
}

void Tico::initBase(s32 color) {
    initBase(TableIter(), color);
}

void Tico::initBase(const TableIter& rIter, s32 color) {
    NPCActorCaps caps("Tico");
    caps.setDefault();
    caps.mMessage = 0;
    caps.mUseShadow = true;
    caps.mWaitNerve = GET_NERVE(Tico, TicoNrvWait);
    caps.mSensorJoint = "Body";
    caps.mSensorSize = 60.0f;
    caps.mSensorOffset.zero();
    caps.mPointerJoint = "Body";
    caps.mBinder = 0;

    if (color == -1) {
        caps.mObjectName = "TicoBaby";
    }

    setDefaults2();
    mReactionAction = "Spin";
    mSwingReactDist = 1000.0f;
    const char* wait = "Wait";
    const char* talk = "Talk";
    const char* fly = "Fly";
    mParam.mWaitAction = wait;
    mParam.mWaitTurnAction = wait;
    mParam.mTalkAction = talk;
    mParam.mTalkTurnAction = talk;
    mMoveAction = fly;
    mMoveTalkAction = fly;
    mRailSpeed = 8.0f;
    mRailAccel = 0.2f;
    mParam.mTiltSpeed = 0.5f;
    mParam.mTiltMaxAngle = 12.0f;
    initialize(rIter, caps);
    if (color != -1) {
        MR::startBrk(this, "ColorChange");
        MR::setBrkFrameAndStop(this, color);
        mLightColor = ::hPointLight[color];
    } else {
        mLightColor = ::hPointLight[ARRAY_SIZE(::hPointLight) - 1];
    }

    mMetaNerve = GET_NERVE(Tico, TicoNrvMeta);
    mPrevPos.set(mPosition);
    MR::startAction(this, "Wait");
    MR::setBckFrameAtRandom(this);
    mLightJointMtx = MR::getJointMtx(this, "Body");
}

void Tico::initMessage(const TableIter& rIter, const char* pMessageId) {
    if (initTalkCtrl(rIter, pMessageId, Vec3f(0.0f, 120.0f, 0.0f), nullptr)) {
        MR::registerKillFunc(getMsgCtrl(), TalkMessageFunc< Tico >(this, &Tico::killFunc));
        MR::setDistanceToTalk(mMsgCtrl, 350.0f);
    }

    mTicoTalkCtrl = MR::createTalkCtrlDirect(this, rIter, "Common_Tico000", Vec3f(0.0f, 120.0f, 0.0f), nullptr);
    MR::offRootNodeAutomatic(mTicoTalkCtrl);
}

void Tico::initMessage(const char* pMessageId) {
    TableIter iter;

    if (initTalkCtrlDirect(iter, pMessageId, Vec3f(0.0f, 120.0f, 0.0f), nullptr)) {
        MR::registerKillFunc(getMsgCtrl(), TalkMessageFunc< Tico >(this, &Tico::killFunc));
        MR::setDistanceToTalk(mMsgCtrl, 350.0f);
    }

    mTicoTalkCtrl = MR::createTalkCtrlDirect(this, iter, "Common_Tico000", Vec3f(0.0f, 120.0f, 0.0f), nullptr);
    MR::offRootNodeAutomatic(mTicoTalkCtrl);
}

void Tico::init(const TableIter& rIter) {
    s32 color = 0;
    MR::getPlacementArg0NoInit(rIter, &color);
    if (color < 0 || color >= ARRAY_SIZE(::hPointLight)) {
        color = 0;
    }
    s32 behavior = 0;
    s32 clr = color;
    MR::getPlacementArg1NoInit(rIter, &behavior);
    mBehavior = behavior;

    if (MR::isObjectName(rIter, "TicoBaby")) {
        clr = -1;
    }

    initBase(rIter, clr);
    initMessage(rIter, "Tico");
    MR::joinToGroupArray(this, rIter, nullptr, 32);
    AstroDemoFunction::tryRegisterDemoForTico(this, rIter);

    if (MR::tryRegisterDemoCast(this, rIter)) {
        mBehavior = 0;
        if (MR::isRegisteredDemoActionAppear(this)) {
            makeActorDead();
        }

        if (MR::isDemoCast(this, "チコガイドデモ")) {
            if (clr == -1) {
                mBehavior = 5;
            } else {
                mBehavior = 6;
            }
        } else {
            if (MR::isDemoCast(this, "青チコ変身")) {
                mBehavior = 1;
            } else {
                if (MR::isDemoCast(this, "赤いスター")) {
                    mBehavior = 9;
                    MR::useStageSwitchWriteA(this, rIter);
                    MR::useStageSwitchWriteB(this, rIter);
                    DemoFunction::registerDemoTalkMessageCtrl(this, mMsgCtrl);
                }
            }
        }
    }

    switch (mBehavior) {
    case 5:
        mDemoGetPower = new TicoDemoGetPower(this, rIter);
        setNerve(GET_NERVE(Tico, TicoNrvSpin0));
        break;
    case 1:
        setNerve(GET_NERVE(Tico, TicoNrvBlue0));
        break;
    case 9:
        if (MR::isOnGameEventFlagRosettaTalkAboutTicoInTower()) {
            makeActorAppeared();
            setNerve(GET_NERVE(Tico, TicoNrvRed1));
        } else {
            setNerve(GET_NERVE(Tico, TicoNrvRed0));
        }
        break;
    case 6:
        MR::offRootNodeAutomatic(mMsgCtrl);
        setNerve(GET_NERVE(Tico, TicoNrvGuide0));
        break;
    case 7:
        setNerve(GET_NERVE(Tico, TicoNrvLead0));
        pushNerve(GET_NERVE(Tico, TicoNrvAppear));
        break;
    case 3:
        setNerve(GET_NERVE(Tico, TicoNrvTalk));
        break;
    case 8:
        setNerve(GET_NERVE(Tico, TicoNrvNoReaction));
        break;
    default:
        break;
    }
}

void Tico::calcAndSetBaseMtx() {
    MR::calcAndSetFloatBaseMtx(this, mFloatOffset);
}

void Tico::control() {
    if (mLightJointMtx) {
        Vec3f trans;
        MR::extractMtxTrans(mLightJointMtx, &trans);
        MR::requestPointLight(this, trans, mLightColor, 0.998646f, -1);
    }

    if (isNerve(mMetaNerve)) {
        NPCActor::control();
    } else {
        if (!MR::isTimeKeepDemoActive()) {
            MR::startLevelSound(this, "SE_SM_LV_TICO_WAIT");
        }
        Vec3f len = mPosition - mPrevPos;
        MR::startLevelSound(this, "SE_SM_LV_TICO_FLOAT", (100.0f * MR::getLinerValueFromMinMax(len.length(), 1.0f, 11.0f, 0.2f, 1.0f)));
        mPrevPos.set(mPosition);
        mFloatOffset = MR::calcFloatOffset(this, mFloatOffset, 150.0f);
        NPCActor::control();
    }
}

bool Tico::receiveMsgPlayerAttack(u32 msg, HitSensor* pSender, HitSensor* pReceiver) {
    if (MR::isMsgStarPieceReflect(msg)) {
        return false;
    }

    if (MR::isMsgStarPieceAttack(msg)) {
        MR::limitedStarPieceHitSound();
        mIsReaction = true;

        return true;
    }

    return NPCActor::receiveMsgPlayerAttack(msg, pSender, pReceiver);
}

bool Tico::tryReaction() {
    if (mIsReaction) {
        pushNerve(GET_NERVE(Tico, TicoNrvDelight));

        return true;
    }

    return MR::tryStartReactionAndPushNerve(this, GET_NERVE(Tico, TicoNrvReaction));
}

void Tico::setNerveMeta() {
    setNerve(mMetaNerve);
}

void Tico::setNerveWait() {
    setNerve(mWaitNerve);
}

bool Tico::killFunc(u32) {
    setNerve(mMetaNerve);

    return true;
}

void Tico::exeReaction() {
    if (mIsTrampledStart) {
        MR::startSound(this, "SE_SM_NPC_TRAMPLED");
        MR::startSound(this, "SE_SV_TICO_TRAMPLED");
    }

    if (isPointingSe()) {
        MR::startPointerHitSound();
        MR::startSound(this, "SE_SV_TICO_POINT");
    }

    if (mIsReactionStart) {
        MR::limitedStarPieceHitSound();
        MR::startSound(this, "SE_BM_BUTLER_ABSORB");
    }

    if (MR::tryStartReactionAndPopNerve(this)) {
        return;
    }
}

void Tico::exeDelight() {
    if (MR::isFirstStep(this)) {
        MR::startAction(this, mReactionAction);
        MR::resetAndForwardNode(mTicoTalkCtrl, MR::getRandom(0, 5));
    }

    MR::tryTalkForce(mTicoTalkCtrl);

    if (MR::isActionEnd(this)) {
        popNerve();
    }
}

void Tico::exeAppear() {
    if (MR::isFirstStep(this)) {
        MR::startAction(this, "Appear");
    }

    if (MR::isBckStopped(this)) {
        popNerve();
    }
}

void Tico::exeNoReaction() {
    if (mMsgCtrl != nullptr) {
        if (MR::tryTalkNearPlayerAndStartTalkAction(this)) {
            setNerve(GET_NERVE(Tico, TicoNrvWait));
        }
    } else {
        MR::tryStartTurnAction(this);
    }
}

void Tico::exeWait() {
    if (tryReaction()) {
        return;
    }

    if (mMsgCtrl != nullptr) {
        MR::tryTalkNearPlayerAndStartTalkAction(this);
    } else {
        MR::tryStartTurnAction(this);
    }
}

void Tico::exeTalk() {
    if (tryReaction()) {
        return;
    }

    if (MR::tryTalkNearPlayerAtEndAndStartTalkAction(this)) {
        setNerve(GET_NERVE(Tico, TicoNrvMeta));
    }
}

void Tico::exeMeta() {
    if (MR::isFirstStep(this)) {
        MR::startAction(this, "Metamorphosis");
    }

    if (MR::isBckStopped(this)) {
        MR::startSound(this, "SE_SM_METAMORPHOSE_SMOKE");
        kill();
    }
}

void Tico::exeBlue0() {
    if (tryReaction()) {
        return;
    }

    if (MR::tryTalkNearPlayerAtEndAndStartTalkAction(this)) {
        setNerve(GET_NERVE(Tico, TicoNrvBlue1));
        MR::startTimeKeepDemo(this, "青チコ変身", nullptr);
    }
}

void Tico::exeBlue1() {
    if (MR::isFirstStep(this)) {
        MR::startAction(this, "Fly");
        MR::setRailCoordSpeed(this, (MR::getRailTotalLength(this) / MR::getDemoPartTotalStep("青チコ変身[移動]")));
    }

    MR::moveCoordAndFollowTrans(this);

    if (MR::isRailReachedGoal(this)) {
        setNerve(GET_NERVE(Tico, TicoNrvMeta));
    }
}

void Tico::exeRed0() {
    if (MR::tryStartTimeKeepDemoMarioPuppetable(this, "赤いスター", "赤いスター[開始]")) {
        setNerve(GET_NERVE(Tico, TicoNrvWait));
    }
}

void Tico::exeRed1() {
    if (tryReaction()) {
        return;
    }

    MR::tryStartTurnAction(this);

    if (MR::isNearPlayer(this, 400.0f)) {
        setNerve(GET_NERVE(Tico, TicoNrvRed2));
    }
}

void Tico::exeRed2() {
    if (MR::isFirstStep(this)) {
        mDemoStarter.start();
    }

    if (mDemoStarter.update()) {
        MR::tryStartTimeKeepDemoMarioPuppetable(this, "赤いスター", "赤いスター[開始]");
        setNerve(GET_NERVE(Tico, TicoNrvWait));
    }
}

void Tico::exeSpin0() {
    mDemoGetPower->updateNerve();
}

void Tico::exeGuide0() {
    if (MR::isFirstStep(this)) {
        MR::startAction(this, "Appear");
    }

    if (MR::isBckStopped(this)) {
        setNerve(GET_NERVE(Tico, TicoNrvGuide1));
    }
}

void Tico::exeGuide1() {
    if (MR::countShowGroupMember(this) == 0) {
        MR::forwardNode(mMsgCtrl);
        MR::forwardNode(mMsgCtrl);
        setNerve(GET_NERVE(Tico, TicoNrvGuide2));
    } else if (MR::countHideGroupMember(this) != 0) {
        MR::forwardNode(mMsgCtrl);
        setNerve(GET_NERVE(Tico, TicoNrvGuide2));
    } else {
        MR::tryTalkNearPlayerAndStartTalkAction(this);

        if (tryReaction()) {
            return;
        }
    }
}

void Tico::exeGuide2() {
    if (MR::countShowGroupMember(this) == 0) {
        MR::forwardNode(mMsgCtrl);
        setNerve(GET_NERVE(Tico, TicoNrvGuide3));
    } else {
        MR::tryTalkNearPlayerAndStartTalkAction(this);

        if (tryReaction()) {
            return;
        }
    }
}

void Tico::exeGuide3() {
    if (MR::isFirstStep(this)) {
        return;
    }

    if (MR::tryTalkNearPlayerAtEndAndStartTalkAction(this) && MR::isExistNextNode(mMsgCtrl)) {
        MR::forwardNode(mMsgCtrl);
    } else if (tryReaction()) {
        return;
    }
}

void Tico::exeLead0() {
    if (mMsgCtrl != nullptr) {
        MR::tryTalkForceAndStartMoveTalkAction(this);
    } else {
        MR::tryStartMoveTurnAction(this);
    }

    if (tryReaction()) {
        return;
    }

    if (MR::isRailGoingToEnd(this)) {
        return;
    }

    setNerve(mWaitNerve);
}
