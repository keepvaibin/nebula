#include "Game/NPC/PenguinMaster.hpp"
#include "Game/LiveActor/Nerve.hpp"
#include "Game/Util/ActorSensorUtil.hpp"
#include "Game/Util/NPCUtil.hpp"
#include "Game/Util/SoundUtil.hpp"
#include "Game/Util/TalkUtil.hpp"
#include "Game/System/LumaCore.hpp"

namespace NrvPenguinMaster {
    NEW_NERVE(PenguinMasterNrvWait, PenguinMaster, Wait);
    NEW_NERVE(PenguinMasterNrvReaction, PenguinMaster, Reaction);
};  // namespace NrvPenguinMaster

PenguinMaster::PenguinMaster(const char* pName) : NPCActor(pName) {
    lumacore::setClass(this, lumacore::kClassId< PenguinMaster >);
}

void PenguinMaster::init(const TableIter& rIter) {
    NPCActorCaps caps = "PenguinMaster";
    caps.setDefault();
    caps.mSensor = false;
    caps.mShadowSize = 130.0f;
    caps.mMessageOffset = Vec3f(0.0f, 430.0f, 0.0f);
    caps.mPointerSize = 180.0f;
    caps.mStarPointerOffs.x = 0.0f;
    caps.mStarPointerOffs.y = 200.0f;
    caps.mStarPointerOffs.z = 80.0f;
    caps.mWaitNerve = GET_NERVE(PenguinMaster, PenguinMasterNrvWait);
    LiveActor::initHitSensor(2);
    MR::addHitSensorPush(this, "body", 8, 150.0f, Vec3f(0.0f, 100.0f, 0.0f));
    MR::addHitSensorPush(this, "head", 8, 150.0f, Vec3f(0.0f, 260.0f, 100.0f));
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
        MR::setDistanceToTalk(mMsgCtrl, 350.0f);
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
