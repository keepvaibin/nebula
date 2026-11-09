#pragma once

#include "Game/LiveActor/LiveActor.hpp"
#include "Game/Util/Array.hpp"
#include <nebula_geometry.hpp>

class ActorCameraInfo;
class DummyDisplayModel;
class ModelObj;
class TripodBossLeg;
class TripodBossMovableArea;
class TripodBossStepPoint;
class TripodBossStepSequence;

class TripodBossBone {
public:
    TripodBossBone() : mAttachMtx() {
        mLocalMtx.identity();
    }

    void setAttachBaseMatrix(const Pos3f&);

    Mtx34f mLocalMtx;
    const Pos3f* mAttachMtx;
};

/// @brief Megaleg
class TripodBoss : public LiveActor {
public:
    // A leg part number; the range test LEFT_LEG..RIGHT_LEG takes BACK_LEG too.
    enum PART_ID {
        PART_ID_LEFT_LEG,
        PART_ID_BACK_LEG,
        PART_ID_RIGHT_LEG,
        PART_ID_MAX,
    };

    enum SUB_PART_ID {
        SUB_PART_ID_ROOT_LOCAL_Y,
        SUB_PART_ID_ROOT_LOCAL_YZ,
        SUB_PART_ID_ROOT_JOINT,
        SUB_PART_ID_MIDDLE_JOINT,
        SUB_PART_ID_ANKLE_LOCAL_X,
        SUB_PART_ID_ANKLE_LOCAL_XZ,
        SUB_PART_ID_END_JOINT,
    };

    enum RIDE_STATE {
        RIDE_ON,     // riding, and was last step
        RIDE_START,  // got on this step
        RIDE_OFF,
        RIDE_LEAVE,  // got off this step
    };

    static const s32 cBodyBone = 21;  // bones 0-20 are the legs, 7 each
    static const s32 cBoneNum = 22;

    TripodBoss(const char*);

    virtual void init(const TableIter&) override;
    virtual void initAfterPlacement() override;
    virtual void kill() override;
    virtual void control() override;
    virtual void calcAndSetBaseMtx() override;

    void initEventCamera(const TableIter&);
    void initLeg(const TableIter&);
    void initLegIKPlacement();
    void initMovableArea(const Pos3f&);
    void initBodyPosition();
    void initBoneInfo();
    void initPose();
    bool tryStartStep();
    bool tryChangeSequence();
    bool tryEndSequence();
    bool tryNextSequence();
    bool tryDamage();
    bool tryWaitStep();
    bool tryNextStep();
    bool tryLeaveLegOutOfPlayer();
    bool tryEndLeaveLegOutOfPlayer();
    bool tryEndDamage();
    bool tryBreak();
    void requestOpeningDemo();
    bool tryDamageDemo();
    void requestEndDamageDemo();
    bool isStopLeg(s32) const;
    bool isStopAllLeg() const;
    bool isStarted() const;
    bool isDemo() const;
    bool isStartDemo() const;
    bool isDamageDemo() const;
    bool isEndDemo() const;
    bool isEndPainDemo() const;
    bool isEndBreakDownDemo() const;
    bool isEndExplosionDemo() const;
    bool isBroken() const;
    bool isRideMario() const;
    bool isLeaveMarioNow() const;
    void setJointAttachBaseMatrix(const Pos3f&, s32);
    void addStepPoint(TripodBossStepPoint*);
    void getBodyMatrix(Pos3f*) const;
    void getJointMatrix(Pos3f* pMtx, s32 boneID) const;
    void getJointAttachMatrix(Pos3f* pMtx, s32 boneID) const;
    void requestStartStepSequence(s32);
    TripodBossStepSequence* getCurrentStepSequence();
    TripodBossStepSequence* getNextStepSequence();
    TripodBossStepSequence* findStepSequence(s32) const;
    void calcLegUpVector(Vec3f* pUp, const Vec3f& rFootPos);
    void calcDemoMovement();
    void calcBodyMovement();
    void calcLegMovement();
    void addAccelToWeightPosition();
    void calcClippingSphere();
    void clippingModel();
    void startDemo();
    void endDemo(const char*);
    void checkRideMario();
    const Pos3f* getLegMatrixPtr(PART_ID, SUB_PART_ID) const;
    void changeBgmState();
    static s32 getPartIDFromBoneID(s32 boneID);

    void exeTryStartDemo();
    void exeNonActive();
    void exeWait();
    void exeStep();
    void exeWaitStep();
    void exeDamage();
    void exeChangeSequence();
    void exeLeaveLegOutOfPlayer();
    void exeStartDemo();
    void exeDamageDemo();
    void exePainDemo();
    void exeBreakDownDemo();
    void exeExplosionDemo();

    inline TripodBossLeg* getLeg(s32 idx) const {
        return mLegs[idx];
    }

    inline TripodBossStepPoint* getStepPoint(int idx) {
        return mStepPoints[idx];
    }

    Pos3f mBodyMtx;
    Pos3f mDummyModelMtx;
    Pos3f mDemoStartMtx;
    TripodBossBone mBossBones[cBoneNum];
    TripodBossLeg* mLegs[3];
    TripodBossStepPoint* mStepPoints[3];
    TripodBossMovableArea* mMovableArea;
    // By the index step points carry (obj_arg3), up to the highest placed; each made when first needed.
    MR::AssignableArray< TripodBossStepSequence* > mStepSequences;
    s32 mStepSequenceNum;
    DummyDisplayModel* mDummyModel;
    Vec3f mDummyModelOffset;
    Vec3f mWeightPos;
    Vec3f mBodyPos;
    Vec3f mBodyVelocity;
    Vec3f mClipCenter;
    f32 mClipRadius;
    f32 mWeightHeightOffset;
    f32 mDemoBlendRate;
    f32 mBodyHeight;
    f32 mLegRootLength;
    f32 mLegMiddleLength;
    f32 mLegReach;
    f32 mLegLift;
    f32 mLegHeightRate;
    f32 mMovableAreaRadius;
    f32 mDummyModelSpinSpeed;
    s32 mCurrentStepSeq;
    s32 mNextStepSeq;
    s32 mRideMarioTimer;
    s32 mRideMarioState;  // RIDE_STATE
    bool mIsPartsHidden;
    s32 mBgmState;
    bool mIsBgmChangePending;
    ActorCameraInfo* mEventCamera;
};

namespace MR {
    NameObj* createTripodBoss(const char*);
    NameObj* createTripod2Boss(const char*);
};  // namespace MR
