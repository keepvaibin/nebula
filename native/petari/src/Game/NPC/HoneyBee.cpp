#include "Game/NPC/HoneyBee.hpp"
#include "Game/System/NebulaFrameClock.hpp"
#include "Game/LiveActor/LodCtrl.hpp"
#include "Game/LiveActor/ModelObj.hpp"
#include "Game/LiveActor/Nerve.hpp"
#include "Game/NPC/NPCActorItem.hpp"
#include "Game/Util/ActorShadowUtil.hpp"
#include "Game/Util/PlacementUtil.hpp"
#include "Game/Util/JointUtil.hpp"
#include "Game/Util/LiveActorUtil.hpp"
#include "Game/Util/ModelUtil.hpp"
#include "Game/Util/MtxUtil.hpp"
#include "Game/Util/NPCUtil.hpp"
#include "Game/Util/RailUtil.hpp"
#include "Game/Util/SoundUtil.hpp"
#include "Game/Util/TalkUtil.hpp"
#include "Game/System/LumaCore.hpp"

namespace {
    static const s32 sFlyLectureWaitStep = NebulaFrameClock::stepsOfSeconds(2);
    static const s32 sHipLectureWaitStep = NebulaFrameClock::stepsOfSeconds(2);
    const f32 sFlyMoveSpeed = 5.0f;
};  // namespace

namespace NrvHoneyBee {
    NEW_NERVE(HoneyBeeNrvWait, HoneyBee, Wait);
    NEW_NERVE(HoneyBeeNrvReaction, HoneyBee, Reaction);
    NEW_NERVE(HoneyBeeNrvFly, HoneyBee, Fly);
    NEW_NERVE(HoneyBeeNrvDropLecture, HoneyBee, DropLecture);
    NEW_NERVE(HoneyBeeNrvJumpLecture, HoneyBee, JumpLecture);
    NEW_NERVE(HoneyBeeNrvFlyLectureA, HoneyBee, FlyLectureA);
    NEW_NERVE(HoneyBeeNrvFlyLectureB, HoneyBee, FlyLectureB);
};  // namespace NrvHoneyBee

HoneyBee::HoneyBee(const char* pName) : NPCActor(pName), mRailPointNo(-1), mInitAction(), mFloatOffset() {
    lumacore::setClass(this, lumacore::kClassId< HoneyBee >);
    mTalkMtx.identity();
    mClippingCenter.zero();
}

void HoneyBee::init(const TableIter& rIter) {
    f32 radius;
    NPCActorCaps caps = "HoneyBee";
    caps.setDefault();
    caps.mBinder = false;
    caps.mPointerJoint = "Center";
    caps.mSensorJoint = "Center";
    caps.mSensorSize = 70.0f;
    caps.mSensorOffset.x = 0.0f;
    caps.mSensorOffset.y = -20.0f;
    caps.mSensorOffset.z = 0.0f;
    caps.mWaitNerve = GET_NERVE(HoneyBee, HoneyBeeNrvWait);
    caps.mScaleJoint = "Center";

    s32 initAction = InitAction_Wait;
    MR::getPlacementArg0NoInit(rIter, &initAction);
    mInitAction = initAction;

    setDefaultsParam();
    mUseReactionActions = 0;
    mSwingReactDist = 450.0f;

    switch (mInitAction) {
    case InitAction_Wait:
        mUseReactionActions = 1;
        break;
    case InitAction_JumpLecture:
        caps.mWaitNerve = GET_NERVE(HoneyBee, HoneyBeeNrvJumpLecture);
        break;
    case InitAction_Fly:
        caps.mWaitNerve = GET_NERVE(HoneyBee, HoneyBeeNrvFly);
        break;
    case InitAction_Sleep:
        mParam.setNoTurnAction("SleepWait");
        break;
    case InitAction_Stick:
        mParam.setNoTurnAction("StickWait");
        break;
    case InitAction_Gatekeeper:
        mParam.setSingleAction("GatekeeperWait");
        mUseReactionActions = 1;
        break;
    case InitAction_DropLecture:
        caps.mWaitNerve = GET_NERVE(HoneyBee, HoneyBeeNrvDropLecture);
        break;
    case InitAction_Flustered:
        mParam.setSingleAction("Flustered");
        mUseReactionActions = 1;
        break;
    case InitAction_FlyLectureA:
        caps.mWaitNerve = GET_NERVE(HoneyBee, HoneyBeeNrvFlyLectureA);
        caps.mTalkMtx = mTalkMtx;
        caps.mMessageOffset.zero();
        break;
    case InitAction_FlyLectureB:
        caps.mWaitNerve = GET_NERVE(HoneyBee, HoneyBeeNrvFlyLectureB);
        caps.mTalkMtx = mTalkMtx;
        caps.mMessageOffset.zero();
        break;
    default:
        break;
    }

    NPCActor::initialize(rIter, caps);
    NPCActorItem item = ("HoneyBee");

    switch (mInitAction) {
    case InitAction_FlyLectureA:
    case InitAction_FlyLectureB:
        radius = 0.0f;
        mTalkMtx.set(getBaseMtx());
        MR::extractMtxTrans(mTalkMtx, &mClippingCenter);
        MR::calcModelBoundingRadius(&radius, this);
        MR::setShadowDropPositionAtJoint(this, nullptr, "Center", Vec3f(0.0f, 0.0f, 0.0f));
        MR::onCalcShadow(this, nullptr);
        MR::setClippingTypeSphere(this, radius, &mClippingCenter);
        mLodCtrl->invalidate();
        break;
    }

    MR::getNPCItemData(&item, mInitAction);
    NPCActor::equipment(item, false);
    MR::initFur(this);
}

void HoneyBee::calcAndSetBaseMtx() {
    MR::calcAndSetFloatBaseMtx(this, mFloatOffset);
}

void HoneyBee::control() {
    if (mIsTrampledStart) {
        MR::startSound(this, "SE_SM_NPC_TRAMPLED");
        MR::startSound(this, "SE_SV_HONEYBEE_TRAMPLED");
    }

    if (NPCActor::isPointingSe()) {
        MR::startPointerHitSound();
        MR::startSound(this, "SE_SV_HONEYBEE_POINT");
    }

    if (mIsSpinAttackedStart) {
        MR::startSound(this, "SE_SV_HONEYBEE_SPIN");
    }

    if (mIsReactionStart) {
        MR::startSound(this, "SE_SV_HONEYBEE_STAR_PIECE_HIT");
    }

    NPCActor::control();
    f32 a = mTalkMtx[1][3] - 80.0f;
    mTalkMtx.set(MR::getJointMtx(this, "Center"));
    MR::extractMtxTrans(mTalkMtx, &mClippingCenter);
    mTalkMtx[1][3] = 80.0f + (((9.0f * a) + mTalkMtx[1][3]) / 10.0f);

    if (mInitAction != InitAction_Sleep && mInitAction != InitAction_Stick) {
        mFloatOffset = MR::calcFloatOffset(this, mFloatOffset, 120.0f);
    }
}

void HoneyBee::exeWait() {
    if (!MR::tryStartReactionAndPushNerve(this, GET_NERVE(HoneyBee, HoneyBeeNrvReaction))) {
        MR::tryTalkNearPlayerAndStartMoveTalkAction(this);
    }
}

void HoneyBee::exeReaction() {
    MR::tryStartReactionAndPopNerve(this);
}

void HoneyBee::exeFly() {
    if (MR::isFirstStep(this)) {
        MR::onCalcShadow(this, nullptr);
    }

    MR::startLevelSound(this, "SE_SM_LV_HONEYBEE_FLY");

    if (mMsgCtrl != nullptr) {
        MR::tryTalkNearPlayer(mMsgCtrl);
    }

    MR::moveCoordAndFollowTrans(this, ::sFlyMoveSpeed);

    if (mRailPointNo != MR::getCurrentRailPointNo(this)) {
        s32 isFlying = 0;
        mRailPointNo = MR::getCurrentRailPointNo(this);
        MR::getRailPointArg0WithInit(this, mRailPointNo, &isFlying);

        if (isFlying == 0) {
            if (!MR::isBckPlaying(this, "WalkWait")) {
                MR::startBck(this, "Wait");
            }
        } else {
            if (!MR::isBckPlaying(this, "FlyWait")) {
                MR::startBck(this, "FlyWait");
            }
        }
    }
}

void HoneyBee::exeDropLecture() {
    if (MR::isFirstStep(this)) {
        MR::startBck(this, "Wait");
    }

    if (MR::isBckPlaying(this, "Wait")) {
        if (mMsgCtrl && MR::tryTalkNearPlayer(mMsgCtrl)) {
            NPCActor::turnToPlayer();
        }

        if (MR::isGreaterStep(this, ::sHipLectureWaitStep)) {
            MR::startBck(this, "HipDropWait");
        }
    } else if (MR::isBckLooped(this)) {
        setNerve(GET_NERVE(HoneyBee, HoneyBeeNrvDropLecture));
    }
}

void HoneyBee::exeJumpLecture() {
    if (MR::isFirstStep(this)) {
        MR::startBck(this, "Wait");
    }

    if (MR::isBckPlaying(this, "Wait")) {
        if (mMsgCtrl && MR::tryTalkNearPlayer(mMsgCtrl)) {
            NPCActor::turnToPlayer();
        }

        if (MR::isGreaterStep(this, ::sFlyLectureWaitStep)) {
            MR::startBck(this, "FlyLectureWait");
        }
    } else if (MR::isBckLooped(this)) {
        setNerve(GET_NERVE(HoneyBee, HoneyBeeNrvJumpLecture));
    }
}

void HoneyBee::exeFlyLectureA() {
    if (MR::isFirstStep(this)) {
        MR::onCalcShadow(this, nullptr);
        MR::startBck(this, "FlyLectureA");
    }

    MR::startLevelSound(this, "SE_SM_LV_HONEYBEE_FLY_SOFT");
    MR::tryTalkNearPlayer(mMsgCtrl);
}

void HoneyBee::exeFlyLectureB() {
    if (MR::isFirstStep(this)) {
        MR::onCalcShadow(this, nullptr);
        MR::startBck(this, "FlyLectureB");
    }

    MR::startLevelSound(this, "SE_SM_LV_HONEYBEE_FLY_SOFT");
    MR::tryTalkNearPlayer(mMsgCtrl);
}
