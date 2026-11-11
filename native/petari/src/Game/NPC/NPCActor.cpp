#include "Game/NPC/NPCActor.hpp"
#include "Game/System/NebulaFrameClock.hpp"
#include "Game/Enemy/AnimScaleController.hpp"
#include "Game/LiveActor/LodCtrl.hpp"
#include "Game/LiveActor/Nerve.hpp"
#include "Game/LiveActor/PartsModel.hpp"
#include "Game/LiveActor/Spine.hpp"
#include "Game/NPC/NPCActorItem.hpp"
#include "Game/Util/ActorMovementUtil.hpp"
#include "Game/Util/ActorSensorUtil.hpp"
#include "Game/Util/ActorShadowUtil.hpp"
#include "Game/Util/ActorSwitchUtil.hpp"
#include "Game/Util/DemoUtil.hpp"
#include "Game/Util/PlacementUtil.hpp"
#include "Game/Util/JointController.hpp"
#include "Game/Util/JointUtil.hpp"
#include "Game/Util/LiveActorUtil.hpp"
#include "Game/Util/MathUtil.hpp"
#include "Game/Util/MtxUtil.hpp"
#include "Game/Util/NPCUtil.hpp"
#include "Game/Util/ObjUtil.hpp"
#include "Game/Util/PlayerUtil.hpp"
#include "Game/Util/RailUtil.hpp"
#include "Game/Util/StarPointerUtil.hpp"
#include "Game/Util/StringUtil.hpp"
#include <nebula_geometry.hpp>
#include "Game/Util/Tunable.hpp"
#include "Game/System/LumaCore.hpp"

NEBULA_TUNABLE_NAMED(f32, cNum_NPCActorCaps_f1, "NPCActorCaps.f1", 50.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_NPCActorCaps_f2, "NPCActorCaps.f2", 50.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_NPCActorCaps_f3, "NPCActorCaps.f3", 50.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_NPCActorCaps_f4, "NPCActorCaps.f4", 80.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_NPCActorCaps_f5, "NPCActorCaps.f5", 150.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_NPCActorCaps_f6, "NPCActorCaps.f6", 50.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_NPCActor_f1, "NPCActor.f1", 2000.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_NPCActor_f2, "NPCActor.f2", 4.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_NPCActor_f3, "NPCActor.f3", 2.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_NPCActor_f4, "NPCActor.f4", 0.1f);
NEBULA_TUNABLE_NAMED(f32, cNum_NPCActor_f5, "NPCActor.f5", 0.08f);
NEBULA_TUNABLE_NAMED(f32, cNum_setBaseMtx_f1, "setBaseMtx.f1", 57.29578f);
NEBULA_TUNABLE_NAMED(f32, cNum_turnToPlayer_f1, "turnToPlayer.f1", 5.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_turnToPlayer_f2, "turnToPlayer.f2", 0.017453292f);
NEBULA_TUNABLE_NAMED(f32, cNum_turnToDefault_f1, "turnToDefault.f1", 0.017453292f);
NEBULA_TUNABLE_NAMED(f32, cNum_setToDefault_f1, "setToDefault.f1", 57.29578f);
NEBULA_TUNABLE_NAMED(f32, cNum_isScaleAnim_f1, "isScaleAnim.f1", 0.2f);
NEBULA_TUNABLE_NAMED(f32, cNum_isScaleAnim_f2, "isScaleAnim.f2", 0.2f);
NEBULA_TUNABLE_NAMED(f32, cNum_isScaleAnim_f3, "isScaleAnim.f3", 0.2f);


namespace NrvNPCActor {
    NEW_NERVE(NPCActorNrvReaction, NPCActor, Reaction);
    NEW_NERVE(NPCActorNrvWait, NPCActor, Wait);
    NEW_NERVE(NPCActorNrvTalk, NPCActor, Talk);
    NEW_NERVE(NPCActorNrvNull, NPCActor, Null);
};  // namespace NrvNPCActor

namespace {
    // Steps since the pointer's sound last played, held here (past the 60 it waits).
    NEBULA_TUNABLE(s32, cPointingSeStepMax, 0x400);

    class NPCParam : public AnimScaleParam {
    public:
        NPCParam() : AnimScaleParam() {
            mPointerTime = 15.0f;
            mPointerBase = 1.4;
            mHitReactionVelocity = 0.3f;
        };
    };

    static NPCParam sParam;
};  // namespace

NPCActorCaps::NPCActorCaps(const char* pName) {
    mMakeActor = false;
    mInterpole = false;
    mConnectTo = false;
    mLightCtrl = false;
    mEffect = false;
    mSound = false;
    mAttribute = false;
    mPosition = false;
    mLodCtrl = false;
    mNerve = false;
    mSensor = false;
    mBinder = false;
    mShadow = false;
    mUseShadow = false;
    mRailRider = false;
    mSwitchDead = true;
    mSwitchAppear = false;
    mMakeActor = false;
    mInterpole = false;
    mConnectTo = false;
    mLightCtrl = false;
    mEffect = false;
    mSound = false;
    mAttribute = false;
    mPosition = false;
    mLodCtrl = false;
    mNerve = false;
    mSensor = false;
    mBinder = false;
    mShadow = false;
    mUseShadow = false;
    mRailRider = false;
    mSwitchDead = true;
    mSwitchAppear = false;
    mBinderSize = cNum_NPCActorCaps_f1;
    mSensorSize = cNum_NPCActorCaps_f2;
    mShadowSize = cNum_NPCActorCaps_f3;
    mPointerSize = cNum_NPCActorCaps_f4;
    mSoundSize = 4;
    mSceneConnectionType = 0;
    mPointer = false;
    mStarPointerOffs.x = 0.0f;
    mStarPointerOffs.y = 0.0f;
    mStarPointerOffs.z = 0.0f;
    mModel = false;
    mMessage = false;
    mModel = false;
    mMessage = false;
    mMessageOffset.x = 0.0f;
    mMessageOffset.y = cNum_NPCActorCaps_f5;
    mMessageOffset.z = 0.0f;
    mSensorOffset.set< f32 >(0.0f, cNum_NPCActorCaps_f6, 0.0f);
    mMessageId = pName;
    mTalkMtx = nullptr;
    mTalkJointName = nullptr;
    mTalkMtx = nullptr;
    mTalkJointName = nullptr;
    mObjectName = pName;
    mSensorJoint = nullptr;
    mSensorJoint = nullptr;
    mSensorMax = 1;
    mWaitNerve = GET_NERVE(NPCActor, NPCActorNrvWait);
    mTalkNerve = GET_NERVE(NPCActor, NPCActorNrvTalk);
    mReactionNerve = GET_NERVE(NPCActor, NPCActorNrvReaction);
    mScaleJoint = nullptr;
    mPointerJoint = nullptr;
    mScaleJoint = nullptr;
    mPointerJoint = nullptr;
}

void NPCActorCaps::setDefault() {
    mMakeActor = true;
    mInterpole = true;
    mConnectTo = true;
    mLightCtrl = true;
    mEffect = true;
    mSound = true;
    mAttribute = true;
    mPosition = true;
    mLodCtrl = true;
    mNerve = true;
    mSensor = true;
    mBinder = true;
    mShadow = true;
    mRailRider = true;
    mSwitchDead = true;
    mSwitchAppear = true;
    mPointer = true;
    mModel = true;
    mMessage = true;
    mMakeActor = true;
    mInterpole = true;
    mConnectTo = true;
    mLightCtrl = true;
    mEffect = true;
    mSound = true;
    mAttribute = true;
    mPosition = true;
    mLodCtrl = true;
    mNerve = true;
    mSensor = true;
    mBinder = true;
    mShadow = true;
    mRailRider = true;
    mSwitchDead = true;
    mSwitchAppear = true;
    mPointer = true;
    mModel = true;
    mMessage = true;
}

void NPCActorCaps::setIndirect() {
    mSceneConnectionType = 2;
}

NPCActor::NPCActor(const char* pName) : LiveActor(pName) {
    lumacore::setClass(this, lumacore::kClassId< NPCActor >);
    mLodCtrl = nullptr;
    mMsgCtrl = nullptr;
    mGoods0 = nullptr;
    mGoods1 = nullptr;
    mLodCtrl = nullptr;
    mMsgCtrl = nullptr;
    mGoods0 = nullptr;
    mGoods1 = nullptr;
    mTrampleCoolDown = 0;
    mIsTrampledStart = 0;
    mIsSpinAttackedStart = 0;
    mIsPointingStart = 0;
    mIsReactionStart = 0;
    mWasTrampled = 0;
    mWasSpinAttacked = 0;
    mWasPointed = 0;
    mWasReaction = 0;
    mIsTrampled = 0;
    mIsSpinAttacked = 0;
    mIsPointed = 0;
    mIsReaction = 0;
    mIsJetTurtleHit = 0;
    mParam.mTurnWhenNear = 1;
    mParam.mTurnOnTalk = 1;
    mParam.mNearDistance = cNum_NPCActor_f1;
    mParam.mTurnSpeed = cNum_NPCActor_f2;
    mParam.mTiltSpeed = 0.0f;
    mParam.mTiltMaxAngle = 0.0f;
    mParam.mWaitAction = nullptr;
    mParam.mWaitTurnAction = nullptr;
    mParam.mTalkAction = nullptr;
    mParam.mTalkTurnAction = nullptr;
    mMoveAction = nullptr;
    mMoveTalkAction = nullptr;
    mRailSpeed = cNum_NPCActor_f3;
    mRailAccel = cNum_NPCActor_f4;
    mRailPoseRate = cNum_NPCActor_f5;
    mMoveTalkBckRate = 1.0f;
    mIsRailOnGround = 0;
    mUseReactionActions = 1;
    mSwingReactDist = 0.0f;
    mSpinAction = 0;
    mTrampleAction = 0;
    mPointingAction = 0;
    mReactionAction = 0;
    mScaleController = nullptr;
    mDelegator = nullptr;
    mCurNerve = nullptr;
    mPointingSeStep = cPointingSeStepMax;
    mPoseQuat.set< f32 >(0.0f, 0.0f, 0.0f, 1.0f);
    mInitPoseQuat.set< f32 >(0.0f, 0.0f, 0.0f, 1.0f);
    mInitPos.zero();
    mPoseRotation.zero();
    mWaitNerve = GET_NERVE(NPCActor, NPCActorNrvWait);
    mTalkNerve = GET_NERVE(NPCActor, NPCActorNrvTalk);
    mReactionNerve = GET_NERVE(NPCActor, NPCActorNrvReaction);
}

void NPCActor::setBaseMtx(const Pos3f& rPos) {
    Vec3f trans;
    rPos.getTrans(trans);

    Vec3f eulerXYZ;
    rPos.getEulerXYZ(eulerXYZ);
    MR::makeQuatRotateRadian(&mPoseQuat, eulerXYZ);
    mPoseQuat.normalize();

    mPosition.set(trans);
    eulerXYZ = eulerXYZ * cNum_setBaseMtx_f1;
    mRotation.set(eulerXYZ);
    mPoseRotation.set(eulerXYZ);

    MR::setBaseTRMtx(this, mPoseQuat);
    MR::resetPosition(this);
}

void NPCActor::setBaseMtx(MtxPtr pMtx) {
    Pos3f pos;
    pos.setInline(pMtx);
    setBaseMtx(pos);
}

void NPCActor::setInitPose() {
    mInitPoseQuat.x = mPoseQuat.x;
    mInitPoseQuat.y = mPoseQuat.y;
    mInitPoseQuat.z = mPoseQuat.z;
    mInitPoseQuat.w = mPoseQuat.w;

    mInitPos.set(mPosition);
}

void NPCActor::init(const TableIter& rIter) {
    LiveActor::init(rIter);
}

void NPCActor::initAfterPlacement() {
    MR::calcGravity(this);

    if (!MR::isNullOrEmptyString(mParam.mWaitAction)) {
        MR::startAction(this, mParam.mWaitAction);

        if (MR::isExistBck(this, mParam.mWaitAction)) {
            MR::startBckNoInterpole(this, mParam.mWaitAction);
        }

        MR::setBckFrameAtRandom(this);
        MR::calcAnimDirect(this);
    }
}

void NPCActor::initialize(const TableIter& rIter, const NPCActorCaps& rCaps) {
    if (rCaps.mPosition) {
        MR::initDefaultPosAndQuat(this, rIter);
    }

    if (rCaps.mModel) {
        initModelManagerWithAnm(rCaps.mObjectName, nullptr, false);
    }

    if (rCaps.mConnectTo) {
        if (rCaps.mSceneConnectionType == 0) {
            MR::connectToSceneNpc(this);
        } else if (rCaps.mSceneConnectionType == 1) {
            MR::connectToSceneNpcMovement(this);
        } else if (rCaps.mSceneConnectionType == 2) {
            MR::connectToSceneIndirectNpc(this);
        }
    }

    if (rCaps.mLightCtrl) {
        MR::initLightCtrl(this);
    }

    if (rCaps.mSound) {
        initSound(rCaps.mSoundSize, false);
    }

    if (rCaps.mAttribute) {
        MR::addToAttributeGroupSearchTurtle(this);
    }

    if (rCaps.mNerve) {
        mWaitNerve = rCaps.mWaitNerve;
        mTalkNerve = rCaps.mTalkNerve;
        mReactionNerve = rCaps.mReactionNerve;
        if (mWaitNerve != nullptr) {
            initNerve(mWaitNerve);
        }
    }

    if (rCaps.mSensor) {
        initHitSensor(rCaps.mSensorMax);
        if (rCaps.mSensorJoint != nullptr) {
            MR::addHitSensorAtJointNpc(this, "Body", rCaps.mSensorJoint, 8, rCaps.mSensorSize, rCaps.mSensorOffset);
        } else {
            MR::addHitSensorNpc(this, "Body", 8, rCaps.mSensorSize, rCaps.mSensorOffset);
        }
    }

    if (rCaps.mBinder) {
        initBinder(rCaps.mBinderSize, rCaps.mBinderSize, 0);
        MR::onCalcGravity(this);
    }

    if (rCaps.mEffect) {
        initEffectKeeper(0, nullptr);
    }

    if (rCaps.mUseShadow) {
        MR::initShadowFromCSV(this, "Shadow");
    } else if (rCaps.mShadow) {
        MR::initShadowVolumeSphere(this, rCaps.mShadowSize);
        MR::onCalcShadowOneTime(this, nullptr);
    }

    if (rCaps.mRailRider && MR::isConnectedWithRail(rIter)) {
        initRailRider(rIter);
        MR::moveCoordToStartPos(this);
        if (MR::isExistShadow(this, nullptr)) {
            MR::onCalcShadow(this, nullptr);
        }
    }

    if (rCaps.mPointer) {
        if (rCaps.mPointerJoint != nullptr) {
            MR::initStarPointerTargetAtJoint(this, rCaps.mPointerJoint, rCaps.mPointerSize, rCaps.mStarPointerOffs);
        } else {
            MR::initStarPointerTarget(this, rCaps.mPointerSize, rCaps.mStarPointerOffs);
        }
    }

    if (rCaps.mMessage) {
        MtxPtr talkMtx;
        if (rCaps.mTalkJointName != nullptr) {
            talkMtx = MR::getJointMtx(this, rCaps.mTalkJointName);
        } else {
            talkMtx = rCaps.mTalkMtx;
        }

        initTalkCtrl(rIter, rCaps.mMessageId, rCaps.mMessageOffset, talkMtx);
    }

    if (rCaps.mLodCtrl) {
        mLodCtrl = MR::createLodCtrlNPC(this, rIter);
    }

    MR::useStageSwitchReadA(this, rIter);
    MR::useStageSwitchReadB(this, rIter);
    MR::useStageSwitchSleep(this, rIter);

    if (rCaps.mSwitchDead) {
        MR::useStageSwitchWriteDead(this, rIter);
    }

    if (rCaps.mScaleJoint != nullptr) {
        mScaleController = new AnimScaleController(&::sParam);
        mDelegator = MR::createJointDelegatorWithNullChildFunc(this, &NPCActor::calcJointScale, rCaps.mScaleJoint);
    }

    if (rCaps.mSwitchAppear && MR::useStageSwitchReadAppear(this, rIter)) {
        MR::syncStageSwitchAppear(this);
        if (rCaps.mMakeActor) {
            makeActorDead();
        }
    } else if (rCaps.mMakeActor) {
        makeActorAppeared();
    }
}

void NPCActor::equipment(const NPCActorItem& rItem, bool indirect) {
    if (indirect) {
        mGoods0 = MR::createIndirectNPCGoods(this, rItem.mGoods0, rItem.mGoodsJoint0);
        mGoods1 = MR::createIndirectNPCGoods(this, rItem.mGoods1, rItem.mGoodsJoint1);
    } else {
        mGoods0 = MR::createNPCGoods(this, rItem.mGoods0, rItem.mGoodsJoint0);
        mGoods1 = MR::createNPCGoods(this, rItem.mGoods1, rItem.mGoodsJoint1);
    }

    if (mGoods0 != nullptr) {
        MR::registerDemoSimpleCastAll(mGoods0);
    }

    if (mGoods1 != nullptr) {
        MR::registerDemoSimpleCastAll(mGoods1);
    }
}

void NPCActor::calcAndSetBaseMtx() {
    mPoseQuat.normalize();
    if (!(mPoseRotation == mRotation)) {
        MR::makeQuatRotateDegree(&mPoseQuat, mRotation);
        mPoseRotation = mRotation;
    }

    MR::setBaseTRMtx(this, mPoseQuat);
    if (mDelegator != nullptr) {
        mDelegator->registerCallBack();
    }
}

void NPCActor::makeActorAppeared() {
    LiveActor::makeActorAppeared();
    if (mLodCtrl != nullptr) {
        mLodCtrl->appear();
    }

    if (mGoods0 != nullptr) {
        mGoods0->appear();
    }

    if (mGoods1 != nullptr) {
        mGoods1->appear();
    }
}

void NPCActor::makeActorDead() {
    if (mLodCtrl != nullptr) {
        mLodCtrl->kill();
    }

    if (mGoods0 != nullptr) {
        mGoods0->kill();
    }

    if (mGoods1 != nullptr) {
        mGoods1->kill();
    }

    LiveActor::makeActorDead();
}

void NPCActor::kill() {
    if (MR::isValidSwitchDead(this)) {
        MR::onSwitchDead(this);
    }

    LiveActor::kill();
}

void NPCActor::control() {
    if (isPointingSe()) {
        mPointingSeStep = 0;
    } else if (mPointingSeStep < cPointingSeStepMax) {
        mPointingSeStep++;
    }

    updateReaction();

    if (mTrampleCoolDown > 0) {
        mTrampleCoolDown--;
    }

    if (mLodCtrl != nullptr) {
        mLodCtrl->update();
    }

    if (mScaleController != nullptr) {
        mScaleController->update();
    }

    if (mModelManager != nullptr && MR::checkPlayerSwingTrigger() && MR::isNearPlayer(this, mSwingReactDist)) {
        mIsSpinAttacked = true;
    }

    if (mStarPointerTarget != nullptr && MR::isStarPointerPointing2POnPressButton(this, "弱", true, false)) {
        mIsPointed = true;
    }

    if (mScaleController != nullptr && mDelegator != nullptr) {
        updateScaleCtrl();
    }
}

bool NPCActor::initTalkCtrl(const TableIter& rIter, const char* pChar, const Vec3f& rVec, MtxPtr pMtx) {
    s32 messageID;
    MR::getPlacementMessageID(rIter, &messageID);
    if (messageID < 0) {
        return false;
    }

    mMsgCtrl = MR::createTalkCtrl(this, rIter, pChar, rVec, pMtx);
    MR::onRootNodeAutomatic(mMsgCtrl);
    return true;
}

bool NPCActor::initTalkCtrlDirect(const TableIter& rIter, const char* pChar, const Vec3f& rVec, MtxPtr pMtx) {
    mMsgCtrl = MR::createTalkCtrlDirect(this, rIter, pChar, rVec, pMtx);
    MR::onRootNodeAutomatic(mMsgCtrl);
    return true;
}

bool NPCActor::calcJointScale(Pos3f* pPos, const JointControllerInfo&) {
    MR::preScaleMtx(pPos->toMtxPtr(), mScaleController->mScale);
    return true;
}

bool NPCActor::turnToPlayer() {
    return turnToPlayer(cNum_turnToPlayer_f1);
}

namespace {
    void calcVecToPlayer(Vec3f* pOut, const LiveActor* pActor) {
        Vec3f playerTrans;
        MR::extractMtxTrans(MR::getPlayerBaseMtx(), &playerTrans);
        MR::normalizeOrZero(playerTrans - pActor->mPosition, pOut);
    }
};  // namespace

bool NPCActor::turnToPlayer(f32 turnSpeedDeg) {
    Vec3f zDir;
    Vec3f toPlayer;
    ::calcVecToPlayer(&toPlayer, this);
    mPoseQuat.getZDir(zDir);

    if (MR::isOppositeDirection(-zDir, toPlayer)) {
        return true;
    }

    return MR::faceToVector(&mPoseQuat, toPlayer, turnSpeedDeg);
}

bool NPCActor::turnToPlayer(f32 turnSpeedDeg, f32 tiltSpeedDeg, f32 maxTiltDeg) {
    bool turned = turnToPlayer(turnSpeedDeg);
    if (maxTiltDeg == 0.0f) {
        return turned;
    }

    Vec3f toPlayer;
    Vec3f yDir;
    mInitPoseQuat.getYDir(yDir);
    ::calcVecToPlayer(&toPlayer, this);

    if (MR::isSameDirection(toPlayer, yDir)) {
        return false;
    }

    // Tilt the up axis toward the player, at most maxTiltDeg from the initial up.
    Vec3f front;
    Vec3f tiltedUp;
    MR::makeAxisFrontUp(&front, &tiltedUp, toPlayer, yDir);
    MR::clampVecAngleDeg(&tiltedUp, yDir, maxTiltDeg);
    return MR::turnQuatYDirRad(&mPoseQuat, mPoseQuat, tiltedUp, tiltSpeedDeg * cNum_turnToPlayer_f2) & turned;
}

bool NPCActor::turnToDefault(f32 turnSpeedDeg) {
    Vec3f zDir;
    Vec3f zDir2;
    Vec3f yDir;
    mInitPoseQuat.getZDir(zDir);
    mInitPoseQuat.getYDir(yDir);
    mPoseQuat.getZDir(zDir2);

    if (MR::isOppositeDirection(-zDir, zDir2)) {
        return true;
    }

    f32 dot = zDir.dot(zDir2);
    dot = MR::clamp(dot, -1.0f, 1.0f);

    f32 rate = MR::abs((turnSpeedDeg * cNum_turnToDefault_f1) / MR::acos(dot));
    rate = MR::clamp(rate, 0.0f, 1.0f);

    MR::blendQuatUpFront(&mPoseQuat, yDir, zDir, rate, rate);
    return MR::isSameDirection(zDir2, zDir);
}

void NPCActor::setToDefault() {
    mPosition = mInitPos;
    mPoseQuat = mInitPoseQuat;
    mPoseQuat.normalize();

    Vec3f euler;
    mPoseQuat.getEuler(euler);
    euler = euler * cNum_setToDefault_f1;
    mRotation = euler;
    mPoseRotation = euler;

    MR::setBaseTRMtx(this, mPoseQuat);
    MR::resetPosition(this);
}

void NPCActor::attackSensor(HitSensor* pSender, HitSensor* pReceiver) {
    if (MR::isSensorPlayerOrRide(pReceiver)) {
        MR::sendMsgPush(pReceiver, pSender);
    }

    if (MR::isSensorEnemy(pReceiver)) {
        MR::sendMsgPush(pReceiver, pSender);
    }
}

bool NPCActor::receiveMsgPlayerAttack(u32 msg, HitSensor* pSender, HitSensor* pReceiver) {
    if (MR::isMsgPlayerTrample(msg)) {
        if (mTrampleCoolDown == 0) {
            mTrampleCoolDown = 30;
            mIsTrampled = true;
            return true;
        }

        return false;
    }

    if (MR::isMsgPlayerHipDrop(msg)) {
        mIsTrampled = true;
        MR::sendMsgAwayJump(pSender, pReceiver);
        return true;
    }

    if (MR::isMsgPlayerSpinAttack(msg) && MR::isNearZero(mSwingReactDist)) {
        mIsSpinAttacked = true;
        return false;
    }

    if (MR::isMsgStarPieceReflect(msg)) {
        mIsReaction = true;
        return true;
    }


    if (MR::isMsgJetTurtleAttack(msg)) {
        mIsReaction = true;
        mIsJetTurtleHit = true;
        return true;
    }

    return false;
}

void NPCActor::pushNerve(const Nerve* pNerve) {
    mCurNerve = mSpine->getCurrentNerve();
    setNerve(pNerve);
}

const Nerve* NPCActor::popAndPushNerve(const Nerve* pNerve) {
    const Nerve* poppedNerve = popNerve();
    pushNerve(pNerve);
    return poppedNerve;
}

const Nerve* NPCActor::popNerve() {
    const Nerve* currentNerve = mSpine->getCurrentNerve();
    setNerve(mCurNerve);
    mCurNerve = nullptr;
    return currentNerve;
}

bool NPCActor::tryPushNullNerve() {
    if (isEmptyNerve()) {
        pushNerve(GET_NERVE(NPCActor, NPCActorNrvNull));
        return true;
    }

    return false;
}

bool NPCActor::isEmptyNerve() const {
    return mCurNerve == nullptr;
}

bool NPCActor::isScaleAnim() const {
    AnimScaleController* pScaleController = mScaleController;
    if (mScaleController == nullptr) {
        return false;
    }

    bool isDefaultScale = (MR::isNearZero(1.0f - pScaleController->mScale.x, cNum_isScaleAnim_f1) && MR::isNearZero(1.0f - pScaleController->mScale.y, cNum_isScaleAnim_f2) &&
                           MR::isNearZero(1.0f - pScaleController->mScale.z, cNum_isScaleAnim_f3));
    if (isDefaultScale) {
        return false;
    }

    return true;
}

bool NPCActor::isPointingSe() const {
    if (mIsPointingStart && mPointingSeStep >= NebulaFrameClock::stepsOfSeconds(1)) {
        return true;
    }

    return false;
}

void NPCActor::updateReaction() {
    mIsTrampledStart = 0;
    mIsSpinAttackedStart = 0;
    mIsPointingStart = 0;
    mIsReactionStart = 0;

    if (!mWasTrampled && mIsTrampled) {
        mIsTrampledStart = true;
    } else if (!mWasReaction && mIsReaction) {
        mIsReactionStart = true;
    } else if (!mWasSpinAttacked && mIsSpinAttacked) {
        mIsSpinAttackedStart = true;
    } else if (!mWasPointed && mIsPointed) {
        mIsPointingStart = true;
    }

    mWasTrampled = mIsTrampled;
    mWasSpinAttacked = mIsSpinAttacked;
    mWasPointed = mIsPointed;
    mWasReaction = mIsReaction;
    mIsTrampled = false;
    mIsSpinAttacked = false;
    mIsPointed = false;
    mIsReaction = false;
    mIsJetTurtleHit = false;
}

void NPCActor::updateScaleCtrl() {
    if (mUseReactionActions) {
        if (mIsSpinAttackedStart && MR::isNullOrEmptyString(mSpinAction)) {
            mScaleController->startHitReaction();
        }

        if (mIsTrampledStart && MR::isNullOrEmptyString(mTrampleAction)) {
            mScaleController->startHitReaction();
        }

        if (mIsReactionStart && MR::isNullOrEmptyString(mReactionAction)) {
            mScaleController->startHitReaction();
        }

        if (!mWasPointed && mIsPointed) {
            if (MR::isNullOrEmptyString(mPointingAction)) {
                mScaleController->startPointerHitVibration();
            }
        } else if (mWasPointed && !mIsPointed && MR::isNullOrEmptyString(mPointingAction)) {
            mScaleController->startAnim();
        }
    } else {
        if (mIsSpinAttackedStart || mIsTrampledStart || mIsReactionStart) {
            mScaleController->startHitReaction();
        }

        if (!mWasPointed && mIsPointed) {
            mScaleController->startPointerHitVibration();
        } else if (mWasPointed && !mIsPointed) {
            mScaleController->startAnim();
        }
    }
}

void NPCActor::exeReaction() {
    MR::tryStartReactionAndPopNerve(this);
}

void NPCActor::exeWait() {
    if (!MR::tryStartReactionAndPushNerve(this, mReactionNerve)) {
        if (mMsgCtrl != nullptr) {
            if (MR::tryTalkNearPlayerAndStartTalkAction(this)) {
                setNerve(mTalkNerve);
            }
        } else {
            MR::tryStartTurnAction(this);
        }
    }
}

void NPCActor::exeTalk() {
    if (!MR::tryStartReactionAndPushNerve(this, mReactionNerve) && (MR::tryTalkNearPlayerAtEndAndStartMoveTalkAction(this))) {
        setNerve(mWaitNerve);
    }
}

void NPCActor::exeNull() {
}

