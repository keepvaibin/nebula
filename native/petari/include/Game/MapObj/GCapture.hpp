#pragma once

#include "Game/LiveActor/LiveActor.hpp"
#include <nebula_geometry.hpp>

class CameraTargetMtx;
class GCaptureRibbon;

class GCaptureTargetable {
public:
    GCaptureTargetable() {
    }

    virtual void decidedTarget() = 0;
    virtual void releasedTarget() = 0;
    virtual void getTargetPosition(Vec3f*) = 0;
    virtual bool canEndHold() const {
        return true;
    }
    virtual bool isReleaseForce() const {
        return false;
    }
    virtual f32 releaseDistance() const {
        return -1.0f;
    }
    virtual f32 getPointableRange() const {
        return 3000.0f;
    }
};

class GCapture : public LiveActor {
public:
    GCapture(const char*);

    virtual void init(const TableIter&) override;
    virtual void draw() const override;
    virtual void control() override;
    virtual void attackSensor(HitSensor*, HitSensor*) override;
    virtual bool receiveMsgEnemyAttack(u32, HitSensor*, HitSensor*) override;
    virtual bool receiveOtherMsg(u32, HitSensor*, HitSensor*) override;

    bool requestBind(HitSensor*);
    bool requestCancelBind();
    bool requestDamageCancel(u32);
    void reset();
    bool tryCapture();
    bool tryCancelCapture();
    bool tryEndTraction();
    bool tryRecapture();
    bool tryRetraction();
    bool tryBreak();
    bool tryFireDamage();
    bool tryRelease();
    void endBindByDamage(s32);
    void endBindByFireDamage();
    void endBindAfterDamage();
    void calcBindActorPose();
    void updateBindActorMatrix();
    bool canRequestTarget() const;
    bool canCancelBind() const;
    void upTractPower();
    void downTractPower();
    void fillGapBindTrans();
    void updateRibbon(const Vec3f&, s32);
    void updateRibbonPointEffectMatrix(const Vec3f&);
    void addRotateAccelPointing();
    bool tryAddVelocityReflectJumpCollision();
    bool requestTarget(GCaptureTargetable*);
    bool isRequestedTarget(GCaptureTargetable*);
    void decideTractTarget();
    void releaseTractTarget();
    void updateCameraTargetMatrix();

    void exeCoolDown();
    void exeWait();
    void exeCapture();
    void endCapture();
    void exeRecapture();
    void endRecapture();
    void exeTraction();
    void exeHold();
    void exeBreak();

    Pos3f mRibbonPointMtx;
    Quat4f mBindQuat;
    Vec3f mEffectPos;
    Vec3f mEffectOffset;
    Vec3f mRotateVelocity;
    Vec3f mRotateAxis;
    LiveActor* mBindActor;
    GCaptureTargetable* mTarget;
    GCaptureTargetable* mTractTarget;
    GCaptureRibbon* mCaptureRibbon;
    CameraTargetMtx* mTargetMtx;
    f32 mTractPower;
    f32 mEffectBlend;
    f32 mRibbonRate;
    s32 mTargetKeepTime;
    f32 mTargetDist;
    s32 mGuidanceTime;
};

namespace MR {
    void createGCapture();
    void resetGCapture();
    bool requestGCaptureTarget(GCaptureTargetable*);
    void unrequestGCaptureTarget(GCaptureTargetable*);
    bool isRequestedGCaptureTarget(GCaptureTargetable*);
    void noticeInTouchableRange();
    bool isOnTractTrigger();
    bool isPlayerGCaptured();
};  // namespace MR
