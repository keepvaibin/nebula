#include "Game/NPC/PenguinStudent.hpp"
#include "Game/LiveActor/LiveActorGroupArray.hpp"
#include "Game/LiveActor/Nerve.hpp"
#include "Game/NPC/Penguin.hpp"
#include "Game/NPC/TalkMessageFunc.hpp"
#include "Game/Util/ActorMovementUtil.hpp"
#include "Game/Util/ActorSensorUtil.hpp"
#include "Game/Util/ActorShadowUtil.hpp"
#include "Game/Util/ActorSwitchUtil.hpp"
#include "Game/Util/PlacementUtil.hpp"
#include "Game/Util/LiveActorUtil.hpp"
#include "Game/Util/MtxUtil.hpp"
#include "Game/Util/NPCUtil.hpp"
#include "Game/Util/ObjUtil.hpp"
#include "Game/Util/RailUtil.hpp"
#include "Game/Util/SoundUtil.hpp"
#include "Game/Util/TalkUtil.hpp"
#include "Game/Util/Tunable.hpp"

NEBULA_TUNABLE_NAMED(f32, cNum_init_f1, "init.f1", 50.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_init_f2, "init.f2", 50.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_init_f3, "init.f3", 50.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_init_f4, "init.f4", 150.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_init_f5, "init.f5", 100.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_init_f6, "init.f6", 0.3f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeSwim_f1, "exeSwim.f1", 200.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeSwim_f2, "exeSwim.f2", 7.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeSwim_f3, "exeSwim.f3", 0.017453292f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeLead_f1, "exeLead.f1", 7.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeLead_f2, "exeLead.f2", 7.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeSlow_f1, "exeSlow.f1", 5.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeSlow_f2, "exeSlow.f2", 5.0f);

namespace NrvPenguinStudent {
    NEW_NERVE(PenguinStudentNrvSwim, PenguinStudent, Swim);
    NEW_NERVE(PenguinStudentNrvLead, PenguinStudent, Lead);
    NEW_NERVE(PenguinStudentNrvSlow, PenguinStudent, Slow);
};  // namespace NrvPenguinStudent

PenguinStudent::PenguinStudent(const char* pName) : NPCActor(pName) {
    mRemovableTurtle = nullptr;
    mActor = nullptr;
    mObjArg0 = -1;
}

void PenguinStudent::init(const TableIter& rIter) {
    MR::initDefaultPosAndQuat(this, rIter);
    MR::getPlacementArg0NoInit(rIter, &mObjArg0);
    initModelManagerWithAnm("Penguin", nullptr, false);
    MR::connectToSceneNpc(this);
    MR::initLightCtrl(this);
    initHitSensor(1);
    MR::addHitSensorPush(this, "push", 8, cNum_init_f1, Vec3f(0.0f, cNum_init_f2, 0.0f));
    initEffectKeeper(0, nullptr);
    initSound(4, false);
    MR::initShadowVolumeSphere(this, cNum_init_f3);
    NPCActor::initTalkCtrl(rIter, "PenguinStudent", Vec3f(0.0f, cNum_init_f4, 0.0f), nullptr);
    NPCActorCaps caps = "";
    caps.mLodCtrl = 1;
    caps.mNerve = false;
    NPCActor::initialize(rIter, caps);
    initNerve(GET_NERVE(PenguinStudent, PenguinStudentNrvSwim));
    if (MR::isConnectedWithRail(rIter)) {
        initRailRider(rIter);
        MR::moveCoordToNearestPos(this, mPosition);
        mPosition.set(MR::getRailPos(this));
    }

    MR::joinToGroupArray(this, rIter, nullptr, 32);
    MR::setGroupClipping(this, rIter, 8);
    MR::invalidateClipping(this);
    if (mObjArg0 == -1) {
        mRemovableTurtle = new RemovableTurtle(this, true);
        setNerve(GET_NERVE(PenguinStudent, PenguinStudentNrvSlow));
    } else {
        s32 railSpacing = 10;
        MR::getPlacementArg1NoInit(rIter, &railSpacing);
        mRailOffset = cNum_init_f5 * railSpacing;
    }

    mUseReactionActions = false;
    mRailAccel = cNum_init_f6;
    MR::useStageSwitchReadA(this, rIter);
    s32 colorFrame = 0;
    MR::getPlacementArg7NoInit(rIter, &colorFrame);
    MR::startBrk(this, "ColorChange");
    MR::setBrkFrameAndStop(this, colorFrame);
    MR::onCalcShadow(this, nullptr);
    if (mMsgCtrl != nullptr) {
        MR::registerBranchFunc(mMsgCtrl, TalkMessageFunc_Inline(this, &PenguinStudent::branchFunc));
    }
    makeActorAppeared();
}

void PenguinStudent::initAfterPlacement() {
    mActor = nullptr;

    LiveActorGroup* group = MR::getGroupFromArray(this);

    // A student placed without a group follows no one.
    if (group == nullptr) {
        return;
    }

    for (s32 i = 0; i < group->getObjNum(); i++) {
        LiveActor* actor = group->getActor(i);

        if (actor->isNerve(GET_NERVE(PenguinStudent, PenguinStudentNrvSwim))) {
            continue;
        }

        mActor = actor;
        break;
    }
}

bool PenguinStudent::branchFunc(u32 /*msg*/) {
    return mActor != nullptr && mActor->isNerve(GET_NERVE(PenguinStudent, PenguinStudentNrvLead));
}

bool PenguinStudent::tryReleaseTurtle() {
    if (mObjArg0 == -1 && isNerve(GET_NERVE(PenguinStudent, PenguinStudentNrvLead)) && mRemovableTurtle->isPullRange() && mRemovableTurtle->tryRemove()) {
        setNerve(GET_NERVE(PenguinStudent, PenguinStudentNrvSlow));
        return true;
    }
    return false;
}

void PenguinStudent::exeSwim() {
    if (MR::isFirstStep(this)) {
        MR::tryStartAction(this, "Swim");
        MR::setBckFrameAtRandom(this);
    }
    // A student with no leader (no group) stays where it is.
    if (mActor == nullptr) {
        return;
    }
    f32 speed = ((cNum_exeSwim_f1 * MR::getRailCoordSpeed(mActor)) / cNum_exeSwim_f2);
    MR::setRailCoord(this, MR::getRailCoord(mActor) - mRailOffset);
    MR::moveCoordAndFollowTrans(this);
    MR::followRailPose(this, mRailPoseRate, 1.0f);
    // The swimmer circles the rail: up turns one degree a step about the front.
    Vec3f front, up, circleDir;
    MR::calcUpVec(&up, this);
    MR::calcFrontVec(&front, this);
    MR::rotAxisVecRad(up, front, &circleDir, cNum_exeSwim_f3 * (getNerveStep() % 360));
    mPosition.set(mPosition + circleDir * speed);
    if (mMsgCtrl) {
        MR::tryTalkNearPlayer(mMsgCtrl);
    }
}

void PenguinStudent::exeLead() {
    if (MR::isFirstStep(this)) {
        mMoveAction = "SwimTurtle";
        mMoveTalkAction = "SwimTurtle";
        mRailSpeed = cNum_exeLead_f1;
    }
    MR::startLevelSound(this, "SE_SM_LV_PENGUIN_SWIM_JET");
    MR::tryStartMoveTalkAction(this);
    if (MR::getRailCoordSpeed(this) <= cNum_exeLead_f2) {
        MR::tryTalkNearPlayer(mMsgCtrl);
    }
}

void PenguinStudent::exeSlow() {
    if (MR::isFirstStep(this)) {
        mMoveAction = "Swim";
        mMoveTalkAction = "Swim";
        mRailSpeed = cNum_exeSlow_f1;
    }

    MR::tryStartMoveTalkAction(this);
    if (MR::getRailCoordSpeed(this) <= cNum_exeSlow_f2) {
        MR::tryTalkNearPlayer(mMsgCtrl);
    }

    if (!MR::isStageStatePowerStarAppeared() && MR::isOnSwitchA(this)) {
        if (mRemovableTurtle->tryAttach()) {
            setNerve(GET_NERVE(PenguinStudent, PenguinStudentNrvLead));
        }
    }
}

void PenguinStudent::attackSensor(HitSensor* pSender, HitSensor* pReceiver) {
    if (MR::isSensorPlayer(pReceiver)) {
        tryReleaseTurtle();
    }

    NPCActor::attackSensor(pSender, pReceiver);
}

bool PenguinStudent::receiveOtherMsg(u32 msg, HitSensor* /*pSender*/, HitSensor* /*pReceiver*/) {
    if (MR::isMsgSpinStormRange(msg) && tryReleaseTurtle()) {
        return true;
    }

    return false;
}
