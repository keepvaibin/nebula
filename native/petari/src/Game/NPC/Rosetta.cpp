#include "Game/NPC/Rosetta.hpp"
#include "Game/System/NebulaFrameClock.hpp"
#include "Game/Demo/AstroDemoFunction.hpp"
#include "Game/LiveActor/Nerve.hpp"
#include "Game/NPC/RosettaDemoAstroDome.hpp"
#include "Game/NPC/RosettaDemoEpilogue.hpp"
#include "Game/NPC/RosettaDemoHeavensDoor.hpp"
#include "Game/NPC/TalkMessageCtrl.hpp"
#include "Game/NPC/TalkMessageFunc.hpp"
#include "Game/NPC/TurnJointCtrl.hpp"
#include "Game/Util.hpp"
#include "Game/System/LumaCore.hpp"

namespace {
    static const s32 sWaitActionInterval = NebulaFrameClock::stepsOfSeconds(5);
};  // namespace

namespace NrvRosetta {
    NEW_NERVE(RosettaNrvDemo, Rosetta, Demo);
    NEW_NERVE(RosettaNrvReaction, Rosetta, Reaction);
};  // namespace NrvRosetta

Rosetta::Rosetta(const char* pName) : NPCActor(pName), mFadeStarter(this, -1), mDemoExecutor(), mObjArg0(-1) {
    lumacore::setClass(this, lumacore::kClassId< Rosetta >);
}

void Rosetta::init(const TableIter& rIter) {
    NPCActorCaps caps("Rosetta");
    caps.setDefault();
    caps.setIndirect();
    caps.mSensor = false;
    caps.mUseShadow = true;
    caps.mMakeActor = false;
    caps.mBinder = false;
    caps.mMessageOffset.x = 0.0f;
    caps.mMessageOffset.y = 0.0f;
    caps.mMessageOffset.z = 0.0f;
    caps.mTalkJointName = "Chin";
    caps.mReactionNerve = &NrvRosetta::RosettaNrvReaction::sInstance;
    NPCActor::initialize(rIter, caps);

    initHitSensor(2);
    MR::addHitSensorNpc(this, "Head", 8, 70.0f, Vec3f(0.0f, 160.0f, 0.0f));
    MR::addHitSensorNpc(this, "Body", 8, 80.0f, Vec3f(0.0f, 50.0f, 0.0f));

    if (mMsgCtrl != nullptr) {
        MR::registerBranchFunc(getMsgCtrl(), TalkMessageFunc(this, &Rosetta::branchFunc));
        MR::registerEventFunc(getMsgCtrl(), TalkMessageFunc(this, &Rosetta::eventFunc));
        MR::onStartOnlyFront(mMsgCtrl);
    }

    MR::startBrk(this, "Normal");
    MR::getPlacementArg0NoInit(rIter, &mObjArg0);
    AstroDemoFunction::tryRegisterAstroDemoAll(this, rIter);
    MR::tryRegisterDemoCast(this, "赤いスター", rIter);
    MR::tryRegisterDemoCast(this, "チコガイドデモ", rIter);
    MR::tryRegisterDemoCast(this, "エピローグデモ", rIter);
    AstroDemoFunction::tryRegisterSimpleCastIfAstroGalaxy(this);

    mTurnJointCtrl = new TurnJointCtrl(this);
    mTurnJointCtrl->init(40.0f, 0.0f, 5.0f);
    mTurnJointCtrl->addFace("Head", 0.6f, TurnJointCtrl::Z, TurnJointCtrl::Y, TurnJointCtrl::X);
    mTurnJointCtrl->addWaist("Spine2", 0.4f, TurnJointCtrl::Z, TurnJointCtrl::X, TurnJointCtrl::Y);
    makeActorAppeared();

    if (MR::isDemoCast(this, "チコガイドデモ")) {
        mDemoExecutor = new RosettaDemoHeavensDoor1(this, rIter);
    } else if (MR::isDemoCast(this, "赤いスター")) {
        mDemoExecutor = new RosettaDemoHeavensDoor2(this, rIter);
    } else if (MR::isDemoCast(this, "ロゼッタ状況説明デモ")) {
        mDemoExecutor = new RosettaDemoAstroDomeExplain(this, rIter);
    } else if (MR::isDemoCast(this, "エピローグデモ")) {
        mDemoExecutor = new RosettaDemoEpilogue(this, rIter);
    } else if (MR::isDemoCast(this, "ロゼッタ最終決戦デモ")) {
        mDemoExecutor = new RosettaDemoAstroDomeFinalBattle(this, rIter);
    }

    new RosettaDemoAstroDomeTalk(this, rIter);  // its demos hold it
    MR::startBckNoInterpole(this, "WaitA");
    MR::calcAnimDirect(this);

    if (mMsgCtrl != nullptr) {
        MR::setDistanceToTalk(mMsgCtrl, 200.0f);
    }

    MR::useStageSwitchWriteB(this, rIter);
    mParam.setMoveTalkNoTurnAction("WaitA", "TalkA");
    setDefaults();
    mSwingReactDist = 1500.0f;
}

bool Rosetta::receiveMsgPlayerAttack(u32 msg, HitSensor* pSender, HitSensor* pReceiver) {
    return NPCActor::receiveMsgPlayerAttack(msg, pSender, pReceiver);
}

void Rosetta::calcAndSetBaseMtx() {
    mTurnJointCtrl->setCallBackFunction();
    NPCActor::calcAndSetBaseMtx();
}

void Rosetta::startDemo(NerveExecutor* pExecutor) {
    mDemoExecutor = pExecutor;
    pushNerve(&NrvRosetta::RosettaNrvDemo::sInstance);
    mTurnJointCtrl->invalidate();
}

void Rosetta::endDemo() {
    MR::startBckNoInterpole(this, "WaitA");
    mDemoExecutor = nullptr;
    setToDefault();
    popNerve();
    mTurnJointCtrl->validate();
}

void Rosetta::endDemoWithInterpole() {
    mDemoExecutor = nullptr;
    popNerve();
    mTurnJointCtrl->startCtrl(60);
}

void Rosetta::control() {
    if (canUpdateStarePos()) {
        Vec3f starePos;
        MR::calcPlayerFaceStarePos(&starePos, MR::getJointMtx(this, "Head"), getBaseMtx());
        mTurnJointCtrl->setStarePos(starePos);
    }

    mTurnJointCtrl->update();

    if (mMsgCtrl != nullptr && MR::isTalkStart(mMsgCtrl)) {
        switch (MR::getRandom(0, 3)) {
        case 0:
            mParam.mTalkAction = "TalkA";
            break;
        case 1:
            mParam.mTalkAction = "TalkB";
            break;
        case 2:
            mParam.mTalkAction = "TalkC";
            break;
        }
    }

    if (MR::isIntervalStep(this, ::sWaitActionInterval)) {
        switch (MR::getRandom(0, 2)) {
        case 0:
            mParam.mWaitAction = "WaitA";
            break;
        case 1:
            mParam.mWaitAction = "WaitB";
            break;
        }
    }

    NPCActor::control();
}

bool Rosetta::branchFunc(u32) {
    return false;
}

bool Rosetta::eventFunc(u32 msg) {
    switch (msg) {
    case 0:
        if (!MR::isPlayingStageBgmName("BGM_FLYING_A")) {
            MR::setNextStageBGM("BGM_SENARIO_SEL_3");
            MR::stopStageBGM(30);
        }
        break;
    case 1:
        MR::onGameEventFlagGalaxyOpen("KoopaBattleVs3Galaxy");
        if (!isEmptyNerve()) {
            popNerve();
        }
        tryPushNullNerve();
        break;
    case 2:
        if (!MR::isPlayingStageBgmName("BGM_FLYING_A")) {
            MR::setNextStageBGM("STM_ASTRO_OUT_3");
            MR::stopStageBGM(60);
        }
        break;
    case 4:
        if (mFadeStarter.update()) {
            MR::onSwitchA(this);
            return true;
        }
        return false;
    }

    return true;
}

bool Rosetta::canUpdateStarePos() const {
    if (isNerve(&NrvRosetta::RosettaNrvReaction::sInstance)) {
        return false;
    }

    Vec3f dir = *MR::getPlayerPos() - mPosition;
    Vec3f yDir;
    MR::extractMtxYDir(getBaseMtx(), &yDir);

    if (MR::normalizeOrZero(&dir)) {
        return false;
    }

    return !(MR::vecKillElement(dir, yDir, &dir) > 0.95f);
}

void Rosetta::exeReaction() {

    if (mIsTrampledStart) {
        MR::startSound(this, "SE_SM_ROSETTA_BARRIER");
    }

    if (isPointingSe()) {
        MR::startPointerHitSound();
        MR::startSound(this, "SE_SV_ROSETTA_POINT");
    }

    if (mIsSpinAttackedStart) {
        MR::startSound(this, "SE_SV_ROSETTA_SPIN");
    }

    if (mIsReactionStart) {
        MR::startSound(this, "SE_SV_ROSETTA_STAR_PIECE_HIT");
    }

    if (MR::tryStartReactionAndPopNerve(this)) {
        return;
    }
}

void Rosetta::exeDemo() {
    mDemoExecutor->updateNerve();
}
