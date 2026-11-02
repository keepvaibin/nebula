#pragma once

#include "Game/LiveActor/LiveActor.hpp"
#include <JSystem/JGeometry/TMatrix.hpp>

class IKJoint;
class TripodBoss;
class TripodBossMovableArea;
class TripodBossStepPoint;

class TripodBossLeg : public LiveActor {
public:
    /// @brief Creates a new `TripodBossLeg`.
    /// @param pName A pointer to the null-terminated name of the object.
    TripodBossLeg(const char* pName);

    virtual ~TripodBossLeg();
    virtual void init(const JMapInfoIter&);
    virtual void makeActorAppeared();
    virtual void makeActorDead();
    virtual void control();
    virtual void attackSensor(HitSensor*, HitSensor*);

    void initShadow(const char*);
    void setBody(TripodBoss*);
    void setMovableArea(TripodBossMovableArea*);
    void setIKParam(f32, f32, const TVec3f&, const TVec3f&, const TVec3f&);
    void setStepTarget(TripodBossStepPoint*);
    void setWait();
    void setForceEndPoint(const TVec3f&);
    void setDemoEffectTiming(bool);
    const TPos3f& getRootJointMatrix() const;
    const TPos3f& getMiddleJointMatrix() const;
    const TPos3f& getEndJointMatrix() const;
    const TPos3f& getRootLocalYMatrix() const;
    const TPos3f& getRootLocalYZMatrix() const;
    const TPos3f& getAnkleLocalXMatrix() const;
    const TPos3f& getAnkleLocalXZMatrix() const;
    void requestStepTarget(TripodBossStepPoint*);
    void requestLeaveOut();
    void requestBreak();
    void requestStartDemo();
    void requestEndDemo();
    bool isLanding() const;
    bool isDamage() const;
    bool isStop() const;
    bool isBroken() const;
    bool isPressPlayer() const;
    bool canStep() const;
    bool canCancelStep() const;
    bool canWeighting() const;
    void addToTargetPower(const TVec3f&, f32);
    void addIKLimitPower();
    void addAccelUpLeg(TripodBossStepPoint*, f32);
    f32 calcLegHeight(TripodBossStepPoint*) const;
    void updatePose();
    void updateIKPose();
    bool bindEndPosition();
    void separateLocalAxisRootJoint(const TPos3f&);
    void separateAnkleJointLocalAxis();
    void updateAnkleUp(f32);
    void updateAnkleSlerpToBasePose();
    void updateAnkleLanding();
    void updateAnkleShadowMatrix();

    void exeHold();
    void exeDemo();
    void exeMove();
    void exeUp();
    void exeLeaveOut();
    void exeMoveToLandingPos();
    void exeStampSign();
    void exeLanding();
    void exeDamageVibration();
    void exeDamage();
    void exeBreak();

    TripodBoss* mBoss;
    TripodBossMovableArea* mMoveArea;
    TripodBossStepPoint* _94;
    TripodBossStepPoint* _98;
    IKJoint* mJoint;
    TPos3f _A0;
    TPos3f mRootLocalYMtx;
    TPos3f mRootLocalYZMtx;
    TPos3f mAnkleLocalXMtx;
    TPos3f mAnkleLocalXZMtx;
    TPos3f mEndJointMtx;
    TPos3f _1C0;
    TQuat4f _1F0;
    TQuat4f _200;
    TVec3f _210;
    TVec3f _21C;
    TVec3f mForceEndPoint;
    TVec3f _234;
    TVec3f _240;
    s32 _24C;
    f32 _250;
    bool _254;
    bool mIsPressPlayer;
    bool _256;
    bool _257;
    s32 mDemoEffectTiming;
    AudAnmSoundObject* _25C;
    AudAnmSoundObject* _260;
};

namespace MR {
    void separateMatrixRotateYZX(TPos3f*, TPos3f*, const TPos3f&, const TPos3f&);
};  // namespace MR
