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

    virtual ~DinoPackun();
    virtual void init(const TableIter&);
    virtual void makeActorDead();
    virtual void control();
    virtual void calcAndSetBaseMtx();
    virtual void attackSensor(HitSensor*, HitSensor*);
    virtual bool receiveMsgPush(HitSensor*, HitSensor*);
    virtual bool receiveMsgPlayerAttack(u32, HitSensor*, HitSensor*);
    virtual bool receiveOtherMsg(u32, HitSensor*, HitSensor*);

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
    JointControlDelegator< DinoPackun >* _B4;
    AnimScaleController* _B8;
    Quat4f _BC;
    Vec4f _CC;
    Vec3f _DC;
    Vec3f _E8;
    s32 _F4;
    Vec3f mCameraVec;
    f32 _104;
    f32 _108;
    s32 _10C;
    u8 _110;
};

namespace MR {
    NameObj* createDinoPackunVs1(const char*);
    NameObj* createDinoPackunVs2(const char*);
};  // namespace MR
