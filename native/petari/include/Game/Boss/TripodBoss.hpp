#pragma once

#include "Game/LiveActor/LiveActor.hpp"
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
    TripodBossBone() : _30() {
        _0.identity();
    }

    void setAttachBaseMatrix(const Pos3f&);

    Mtx34f _0;
    const Pos3f* _30;
};

/// @brief Megaleg
class TripodBoss : public LiveActor {
public:
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

    /// @brief Creates a new `TripodBoss`.
    /// @param pName A pointer to the null-terminated name of the object.
    TripodBoss(const char*);

    virtual void init(const TableIter&);
    virtual void initAfterPlacement();
    virtual void kill();
    virtual void control();
    virtual void calcAndSetBaseMtx();

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
    void getJointMatrix(Pos3f*, s32) const;
    void getJointAttachMatrix(Pos3f*, s32) const;
    void requestStartStepSequence(s32);
    TripodBossStepSequence* getCurrentStepSequence();
    TripodBossStepSequence* getNextStepSequence();
    void calcLegUpVector(Vec3f*, const Vec3f&);
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
    static s32 getPartIDFromBoneID(s32);
    void setAttachBaseMatrix(const Pos3f&);

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

    inline bool isStateSomething() {
        return (_634 == 0) || (_634 == 1);
    }

    Pos3f mBodyMtx;
    Pos3f _BC;
    Pos3f _EC;
    ModelObj* mLowModel;
    TripodBossBone mBossBones[22];
    TripodBossLeg* mLegs[3];
    TripodBossStepPoint* mStepPoints[3];
    TripodBossMovableArea* mMovableArea;
    TripodBossStepSequence* mStepSequence;
    DummyDisplayModel* mDummyModel;
    Vec3f _5BC;
    Vec3f _5C8;
    Vec3f _5D4;
    Vec3f _5E0;
    Vec3f _5EC;
    f32 _5F8;
    f32 _5FC;
    f32 _600;
    f32 _604;
    f32 _608;
    f32 _60C;
    f32 _610;
    f32 _614;
    f32 _618;
    f32 _61C;
    f32 _620;
    s32 mCurrentStepSeq;
    s32 mNextStepSeq;
    s32 _62C;
    s32 _630;
    s32 _634;
    u8 _638;
    s32 _63C;
    u8 _640;
    ActorCameraInfo* mEventCamera;
};

namespace MR {
    NameObj* createTripodBoss(const char*);
    NameObj* createTripod2Boss(const char*);
};  // namespace MR
