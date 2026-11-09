#pragma once

#include "Game/LiveActor/LiveActor.hpp"
#include "Game/Util/TalkUtil.hpp"
#include <nebula_geometry.hpp>

class AnimScaleController;
class JointController;
struct JointControllerInfo;
class LodCtrl;
class NPCActorItem;
class PartsModel;
class TalkMessageCtrl;

class NPCActorCaps {
public:
    NPCActorCaps(const char*);

    void setDefault();
    void setIndirect();

    bool mModel;
    const char* mObjectName;
    bool mMakeActor;
    bool mMessage;
    const char* mMessageId;
    Vec3f mMessageOffset;
    MtxPtr mTalkMtx;
    const char* mTalkJointName;
    bool mInterpole;
    bool mConnectTo;
    bool mLightCtrl;
    bool mEffect;
    bool mSound;
    s32 mSoundSize;
    bool mAttribute;
    bool mPosition;
    bool mLodCtrl;
    bool mNerve;
    bool mBinder;
    f32 mBinderSize;
    bool mSensor;
    const char* mSensorJoint;
    f32 mSensorSize;
    Vec3f mSensorOffset;
    s32 mSensorMax;
    bool mShadow;
    bool mUseShadow;
    f32 mShadowSize;
    bool mRailRider;
    bool mSwitchDead;
    bool mSwitchAppear;
    bool mPointer;
    const char* mPointerJoint;
    const char* mScaleJoint;
    Vec3f mStarPointerOffs;
    f32 mPointerSize;
    s32 mSceneConnectionType;
    Nerve* mWaitNerve;
    Nerve* mTalkNerve;
    Nerve* mReactionNerve;
};

class NPCActor : public LiveActor {
public:
    NPCActor(const char*);

    virtual void init(const TableIter&) override;
    virtual void initAfterPlacement() override;
    virtual void makeActorAppeared() override;
    virtual void kill() override;
    virtual void makeActorDead() override;
    virtual void control() override;
    virtual void calcAndSetBaseMtx() override;
    virtual void attackSensor(HitSensor* pSender, HitSensor* pReceiver) override;
    virtual bool receiveMsgPlayerAttack(u32 msg, HitSensor* pSender, HitSensor* pReceiver) override;


    bool initTalkCtrl(const TableIter&, const char*, const Vec3f&, MtxPtr);
    bool initTalkCtrlDirect(const TableIter&, const char*, const Vec3f&, MtxPtr);
    bool calcJointScale(Pos3f*, const JointControllerInfo&);
    bool turnToPlayer();
    bool turnToPlayer(f32 turnSpeedDeg);
    bool turnToPlayer(f32 turnSpeedDeg, f32 tiltSpeedDeg, f32 maxTiltDeg);
    bool turnToDefault(f32);
    void setToDefault();
    void pushNerve(const Nerve*);
    const Nerve* popAndPushNerve(const Nerve*);
    const Nerve* popNerve();
    bool isEmptyNerve() const;
    bool isScaleAnim() const;
    bool isPointingSe() const;
    void updateReaction();
    void updateScaleCtrl();

    bool isTrampledStart() const {
        return !mWasTrampled && mIsTrampled;
    }

    bool isReactionStart() const {
        return !mWasReaction && mIsReaction;
    }

    bool isSpinAttackedStart() const {
        return !mWasSpinAttacked && mIsSpinAttacked;
    }

    bool isPointingStart() const {
        return !mWasPointed && mIsPointed;
    }

    bool tryPushNullNerve();

    void exeReaction();
    void exeWait();
    void exeTalk();
    void exeNull();

    void setInitPose();

    void initialize(const TableIter&, const NPCActorCaps&);

    void equipment(const NPCActorItem&, bool);

    void setBaseMtx(const Pos3f&);
    void setBaseMtx(MtxPtr);

    inline void setDefaults() {
        mSpinAction = "Spin";
        mTrampleAction = "Trampled";
        mPointingAction = "Pointing";
        mReactionAction = "Reaction";
    }

    inline void setDefaults(const char* pReaction, const char* pPointing, const char* pTrampled, const char* pSpin) {
        mSpinAction = pSpin;
        mTrampleAction = pTrampled;
        mPointingAction = pPointing;
        mReactionAction = pReaction;
    }

    inline void setDefaults2(const char* pReaction, const char* pPointing, const char* pSpin, const char* pTrampled) {
        mTrampleAction = pTrampled;
        mSpinAction = pSpin;
        mPointingAction = pPointing;
        mReactionAction = pReaction;
    }

    inline void setDefaults3(const char* pReaction, const char* pPointing, const char* pSpin, const char* pTrampled) {
        mTrampleAction = pTrampled;
        mSpinAction = pSpin;
        mReactionAction = pPointing;
        mPointingAction = pReaction;
    }

    inline void setDefaults4(const char* pPointingAction, const char* pReactionAction, const char* pTrampleAction, const char* pSpinAction) {
        mSpinAction = pSpinAction;
        mReactionAction = pReactionAction;
        mTrampleAction = pTrampleAction;
        mPointingAction = pPointingAction;
    }

    inline void setDefaultsParam() {
        mParam.mWaitAction = "Wait";
        mParam.mWaitTurnAction = "Turn";
        mParam.mTalkAction = "Talk";
        mParam.mTalkTurnAction = "TalkTurn";
        mSpinAction = "Spin";
        mTrampleAction = "Trampled";
        mPointingAction = "Pointing";
        mReactionAction = "Reaction";
    }

    inline void setDefaults2() {
        mSpinAction = "Spin";
        mTrampleAction = "Trampled";
        mPointingAction = "Pointing";
    }

    inline void setTalkAction(const char* pActionName) {
        mMoveAction = pActionName;
        mMoveTalkAction = pActionName;
    }

    inline void setTalkAction(const char* pActionName, const char* pActionName2) {
        mMoveAction = pActionName;
        mMoveTalkAction = pActionName2;
    }

    TalkMessageCtrl* getMsgCtrl() const {
        return mMsgCtrl;
    }

    LodCtrl* mLodCtrl;
    TalkMessageCtrl* mMsgCtrl;
    PartsModel* mGoods0;
    PartsModel* mGoods1;
    s32 mTrampleCoolDown;
    Quat4f mPoseQuat;
    Quat4f mInitPoseQuat;
    Vec3f mInitPos;
    Vec3f mPoseRotation;
    u8 mIsTrampledStart;
    u8 mIsSpinAttackedStart;
    u8 mIsPointingStart;
    u8 mIsReactionStart;
    u8 mWasTrampled;
    u8 mWasSpinAttacked;
    u8 mWasPointed;
    u8 mWasReaction;
    u8 mIsTrampled;
    u8 mIsSpinAttacked;
    u8 mIsPointed;
    u8 mIsReaction;
    u8 mIsJetTurtleHit;
    MR::ActorTalkParam mParam;
    f32 mRailSpeed;
    f32 mRailAccel;
    f32 mRailPoseRate;
    f32 mMoveTalkBckRate;
    const char* mMoveAction;
    const char* mMoveTalkAction;
    u8 mIsRailOnGround;  // follow rail on ground
    u8 mUseReactionActions;
    f32 mSwingReactDist;
    const char* mSpinAction;
    const char* mTrampleAction;
    const char* mPointingAction;
    const char* mReactionAction;
    AnimScaleController* mScaleController;
    JointController* mDelegator;
    const Nerve* mCurNerve;
    Nerve* mWaitNerve;
    Nerve* mTalkNerve;
    Nerve* mReactionNerve;
    s32 mPointingSeStep;
};
