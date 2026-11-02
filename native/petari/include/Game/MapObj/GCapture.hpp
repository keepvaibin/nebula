#pragma once

#include "Game/LiveActor/LiveActor.hpp"
#include <JSystem/JGeometry/TMatrix.hpp>

class CameraTargetMtx;
class GCaptureRibbon;
class SpringValue;

class GCaptureTargetable {
public:
    GCaptureTargetable() {
    }

    virtual void decidedTarget() = 0;
    virtual void releasedTarget() = 0;
    virtual void getTargetPosition(TVec3f*) = 0;
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

    virtual void init(const JMapInfoIter&);
    virtual void draw() const;
    virtual void control();
    virtual void attackSensor(HitSensor*, HitSensor*);
    virtual bool receiveMsgEnemyAttack(u32, HitSensor*, HitSensor*);
    virtual bool receiveOtherMsg(u32, HitSensor*, HitSensor*);

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
    void calcBindActorPose();
    void updateBindActorMatrix();
    bool canRequestTarget() const;
    bool canCancelBind() const;
    void upTractPower();
    void downTractPower();
    void fillGapBindTrans();
    void updateRibbon(const TVec3f&, s32);
    void updateRibbonPointEffectMatrix(const TVec3f&);
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

    TPos3f _8C;
    TQuat4f _BC;
    TVec3f mEffectPos;
    TVec3f _D8;
    TVec3f _E4;
    TVec3f _F0;
    TVec3f _FC;
    LiveActor* _108;
    GCaptureTargetable* mTarget;
    GCaptureTargetable* _110;
    GCaptureRibbon* mCaptureRibbon;
    CameraTargetMtx* mTargetMtx;
    SpringValue* mSpringValue;
    f32 mTractPower;
    f32 _124;
    f32 _128;
    s32 _12C;
    f32 _130;
    s32 mGuidanceTime;
    bool mIsStarPointerPointing;  // useless
    bool _139;                    // useless
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
