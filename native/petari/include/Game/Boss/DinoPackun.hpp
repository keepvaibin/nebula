#pragma once

#include "Game/LiveActor/LiveActor.hpp"
#include <nebula_geometry.hpp>

class ActorCameraInfo;
class AnimScaleController;
class CameraTargetMtx;
class DinoPackunBall;
class DinoPackunDemoPosition;
class DinoPackunEggShell;
class DinoPackunSequencer;
class DinoPackunTail;
struct JointControllerInfo;
class FootPrint;
class PartsModel;

template < typename T >
class JointControlDelegator;

class DinoPackun : public LiveActor {
public:
    DinoPackun(const char*);

    virtual void init(const TableIter&) override;
    virtual void makeActorDead() override;
    virtual void control() override;
    virtual void calcAndSetBaseMtx() override;
    virtual void attackSensor(HitSensor*, HitSensor*) override;
    virtual bool receiveMsgPush(HitSensor*, HitSensor*) override;
    virtual bool receiveMsgPlayerAttack(u32, HitSensor*, HitSensor*) override;
    virtual bool receiveOtherMsg(u32, HitSensor*, HitSensor*) override;

    const char* getVsAnimName() const;
    void initTail();
    void initFootPrint();
    void initDemoPosition(const TableIter&);
    void initEggShell();
    void initBall();
    void initCamera(const TableIter&);
    void initScaleJointController();
    void startHitReaction();
    bool isHitReaction(s32) const;
    bool hitScaleJoint(Pos3f*, const JointControllerInfo&);
    DinoPackunEggShell* getEggShell();
    PartsModel* getEggBrokenModel();
    PartsModel* getBallModel();
    void attackSensorTail(HitSensor*, HitSensor*);
    bool receiveMsgPlayerAttackTail(u32, HitSensor*, HitSensor*);
    void startSequence();
    void updatePose();
    void updateFootPrintNerve(s32, s32);
    void updateCameraInfo();
    void updateNormalVelocity();
    void updateRunVelocity();
    void appearStarPiece(s32);
    bool isSensorEgg(const HitSensor*) const;
    void resetPosition();
    void adjustTailRootPosition(const Vec3f&, f32);
    void activateParts();
    void onMovementParts();
    void onAimTailBall(s32);
    void offAimTailBall(s32);
    void startDemo();
    void startDemoAndReset();
    void endDemo(const char*);
    void startDamageCamera();
    void endDamageCamera();

    DinoPackunTail* mTail;
    DinoPackunBall* mBall;
    FootPrint* mFootPrint;
    DinoPackunEggShell* mShell;
    DinoPackunDemoPosition* mDemoPos;
    PartsModel* mShellBreakModel;
    PartsModel* mTailBall;
    CameraTargetMtx* mCamTargetMtx;
    ActorCameraInfo* mCameraInfo;
    DinoPackunSequencer* mSequence;
    JointControlDelegator< DinoPackun >* mScaleJointCtrl;
    AnimScaleController* mAnimScaleCtrl;
    Quat4f mPoseQuat;
    Vec4f mStartQuat;
    Vec3f mStartPos;
    Vec3f mFront;
    s32 mCameraRegisterId;
    Vec3f mCameraVec;
    f32 mAimTailRateSpeed;
    f32 mAimTailRate;
    s32 mDamageCameraState;
    bool mIsPreCalcAnim;
};

namespace MR {
    NameObj* createDinoPackunVs1(const char*);
    NameObj* createDinoPackunVs2(const char*);
};  // namespace MR
