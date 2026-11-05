#pragma once

#include "Game/LiveActor/LiveActor.hpp"
#include <nebula_geometry.hpp>

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
    void setIKParam(f32, f32, const Vec3f&, const Vec3f&, const Vec3f&);
    void setStepTarget(TripodBossStepPoint*);
    void setWait();
    void setForceEndPoint(const Vec3f&);
    void setDemoEffectTiming(bool);
    const Pos3f& getRootJointMatrix() const;
    const Pos3f& getMiddleJointMatrix() const;
    const Pos3f& getEndJointMatrix() const;
    const Pos3f& getRootLocalYMatrix() const;
    const Pos3f& getRootLocalYZMatrix() const;
    const Pos3f& getAnkleLocalXMatrix() const;
    const Pos3f& getAnkleLocalXZMatrix() const;
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
    void addToTargetPower(const Vec3f&, f32);
    void addIKLimitPower();
    void addAccelUpLeg(TripodBossStepPoint*, f32);
    f32 calcLegHeight(TripodBossStepPoint*) const;
    void updatePose();
    void updateIKPose();
    bool bindEndPosition();
    void separateLocalAxisRootJoint(const Pos3f&);
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
    Pos3f _A0;
    Pos3f mRootLocalYMtx;
    Pos3f mRootLocalYZMtx;
    Pos3f mAnkleLocalXMtx;
    Pos3f mAnkleLocalXZMtx;
    Pos3f mEndJointMtx;
    Pos3f _1C0;
    Quat4f _1F0;
    Quat4f _200;
    Vec3f _210;
    Vec3f _21C;
    Vec3f mForceEndPoint;
    Vec3f _234;
    Vec3f _240;
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
    void separateMatrixRotateYZX(Pos3f*, Pos3f*, const Pos3f&, const Pos3f&);
};  // namespace MR
