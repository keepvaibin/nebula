#include "Game/NPC/PenguinMaster.hpp"
#include "Game/LiveActor/Nerve.hpp"
#include "Game/Util/ActorSensorUtil.hpp"
#include "Game/Util/NPCUtil.hpp"
#include "Game/Util/SoundUtil.hpp"
#include "Game/Util/TalkUtil.hpp"
#include "Game/Util/Tunable.hpp"

NEBULA_TUNABLE_NAMED(f32, cNum_init_f1, "init.f1", 130.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_init_f2, "init.f2", 430.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_init_f3, "init.f3", 180.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_init_f4, "init.f4", 200.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_init_f5, "init.f5", 80.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_init_f6, "init.f6", 150.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_init_f7, "init.f7", 100.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_init_f8, "init.f8", 150.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_init_f9, "init.f9", 260.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_init_f10, "init.f10", 100.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_init_f11, "init.f11", 350.0f);

namespace NrvPenguinMaster {
    NEW_NERVE(PenguinMasterNrvWait, PenguinMaster, Wait);
    NEW_NERVE(PenguinMasterNrvReaction, PenguinMaster, Reaction);
};  // namespace NrvPenguinMaster

PenguinMaster::PenguinMaster(const char* pName) : NPCActor(pName) {
}

void PenguinMaster::init(const TableIter& rIter) {
    NPCActorCaps caps = "PenguinMaster";
    caps.setDefault();
    caps.mSensor = false;
    caps.mShadowSize = cNum_init_f1;
    caps.mMessageOffset = Vec3f(0.0f, cNum_init_f2, 0.0f);
    caps.mPointerSize = cNum_init_f3;
    caps.mStarPointerOffs.x = 0.0f;
    caps.mStarPointerOffs.y = cNum_init_f4;
    caps.mStarPointerOffs.z = cNum_init_f5;
    caps.mWaitNerve = GET_NERVE(PenguinMaster, PenguinMasterNrvWait);
    LiveActor::initHitSensor(2);
    MR::addHitSensorPush(this, "body", 8, cNum_init_f6, Vec3f(0.0f, cNum_init_f7, 0.0f));
    MR::addHitSensorPush(this, "head", 8, cNum_init_f8, Vec3f(0.0f, cNum_init_f9, cNum_init_f10));
    initialize(rIter, caps);
    const char* talk = "Talk";
    mParam.mWaitAction = "Wait";
    mParam.mTalkAction = talk;
    mParam.mWaitTurnAction = nullptr;
    mParam.mTalkTurnAction = nullptr;
    mParam.mTurnWhenNear = false;
    mParam.mTurnOnTalk = false;
    mTrampleAction = "Trampled";
    mReactionAction = "Reaction";
    mSpinAction = "Reaction";
    mPointingAction = "Pointing";

    if (mMsgCtrl != nullptr) {
        MR::setDistanceToTalk(mMsgCtrl, cNum_init_f11);
    }
}

bool PenguinMaster::receiveMsgPlayerAttack(u32 msg, HitSensor* pSender, HitSensor* pReceiver) {
    if (MR::isMsgPlayerSpinAttack(msg)) {
        mIsSpinAttacked = true;

        return true;
    }

    return NPCActor::receiveMsgPlayerAttack(msg, pSender, pReceiver);
}

void PenguinMaster::exeWait() {
    MR::tryTalkNearPlayerAndStartTalkAction(this);
    MR::tryStartReactionAndPushNerve(this, GET_NERVE(PenguinMaster, PenguinMasterNrvReaction));
}

void PenguinMaster::exeReaction() {
    if (mIsTrampledStart) {
        MR::startSound(this, "SE_SV_PENGUIN_OLD_TRAMPLED");
        MR::startSound(this, "SE_SM_PENGUIN_TRAMPLED");
    }

    if (isPointingSe()) {
        MR::startSound(this, "SE_SV_PENGUIN_OLD_POINTED_2P");
        MR::startPointerHitSound();
    }

    if (mIsSpinAttackedStart) {
        MR::startSound(this, "SE_SV_PENGUIN_OLD_SPIN_HIT");
    }

    if (mIsReactionStart) {
        MR::startSound(this, "SE_SV_PENGUIN_OLD_STAR_PIECE");
    }

    if (MR::tryStartReactionAndPopNerve(this)) {
        return;
    }

    if (MR::tryStartReactionAndPopNerve(this)) {
        return;
    }
}
