#pragma once

#include "Game/LiveActor/ModelObj.hpp"
#include "Game/Util/Array.hpp"
#include <nebula_geometry.hpp>

class AnimScaleController;
class AreaObj;
class CameraTargetMtx;
class DodoryuBank;
class DodoryuHill;
class DodoryuLeadHill;
class DodoryuRabbit;
class DodoryuStateBase;

namespace DodoryuSub {
    class MoveStateHolder;
};  // namespace DodoryuSub

class JointController;
struct JointControllerInfo;
class JointRumbler;
class TalkMessageCtrl;

class Dodoryu : public LiveActor {
public:
    Dodoryu(const char* pName);

    virtual void init(const TableIter& rIter) override;
    virtual void initAfterPlacement() override;
    virtual void startClipped() override;
    virtual void endClipped() override;
    virtual void control() override;
    virtual void calcAndSetBaseMtx() override;
    virtual void attackSensor(HitSensor*, HitSensor*) override;
    virtual bool receiveMsgPlayerAttack(u32, HitSensor*, HitSensor*) override;

    void snapToGround();
    bool snapToWall();
    void setMtx(const Pos3f&);
    void notifyOnSwitchA();
    void nextState();
    void pauseOff();
    void startPlayerCenterCamera();
    void endPlayerCenterCamera();
    void getPlayerResetMtx(Pos3f* pMtx, u32 index) const;
    bool checkWallWithVelocity();
    void reactJumpOutCommon() const;
    void startSpinOutCamera();
    void endSpinOutCamera();
    bool keepOffFromClosedArea(Vec3f*);
    void tryRumblePad();
    void invalidateEyeSensor();
    void validateEyeSensor();
    void invalidateStarPieceSensor();
    void validateStarPieceSensor();
    bool isHeadNeedle(HitSensor*, HitSensor*) const;
    void shiftMoveStateRail(f32 speed);
    void shiftMoveStateChase(f32 accel, f32 lateralDamping, f32 maxSpeed, f32 turnAngle);
    void shiftMoveStateEscape(f32 speed, s32 dirStepMin, s32 dirStepMax, f32 dirAngleMax, f32 turnAngle);
    void shiftMoveStateJumpOut(f32 speed, f32 turnAngle);
    void shiftMoveStateNull();
    void setMoveRailSpeed(f32 railSpeed);
    void startHill();
    void stopHill();
    void stopHillForce();
    void killAllHillPieces();
    void setHillAppearNumHalf();
    void setHillAppearNumMax();
    void startLeadHillBck(const char*);
    void killLeadHill();
    void appearBank();
    void leaveRabbit();
    void resetRabbit();
    void calcRabbitDir(Vec3f*) const;
    void displayRabbitMessage();
    void initHitSensor();
    void initSwitch(const TableIter&);
    void createMogucchiHill();
    void createDodoryuBank();
    void turnUpVecTo(const Vec3f& rUp);
    void checkHipDrop();
    void initState();
    void updateRumblePad();
    void updateCameraTarget();

    Pos3f mBaseMtx;
    Vec3f mCameraRegisterPos;
    u32 mStateIndex;
    MR::InlineVector< DodoryuStateBase*, 16 > mState;
    DodoryuSub::MoveStateHolder* mMoveStateHolder;
    DodoryuHill* mHill;
    DodoryuLeadHill* mLeadHill;
    DodoryuBank* mBank;
    DodoryuRabbit* mRabbit;
    Vec3f mGroundNormal;
    Vec3f mWallNormal;
    AreaObj* mClosedAreaObj;
    bool mIsSwitchOn;
    CameraTargetMtx* mDemoCameraTarget;
    CameraTargetMtx* mSpinOutCameraTarget;
    bool mIsRumbling;
    AnimScaleController* mAnimScaleCtrl;
};

class DodoryuBank : public ModelObj {
public:
    DodoryuBank();

    virtual void init(const TableIter& rIter) override;

    void exeAppear();

    Mtx34f mBankMtx;
};

class DodoryuRabbit : public ModelObj {
public:
    DodoryuRabbit(Dodoryu* pDodoryu, const TableIter& rIter);

    virtual void init(const TableIter& rIter) override;
    virtual void control() override;
    virtual void attackSensor(HitSensor*, HitSensor*) override;
    virtual bool receiveMsgPlayerAttack(u32, HitSensor*, HitSensor*) override;

    // isPlace also moves the rabbit onto its new rail point.
    void reset(bool isPlace);
    void updatePos(f32 speed);
    f32 calcCoordDiff() const;
    bool tryTalk();

    void exeEscapeWaiting();
    void exeEscape();
    void exeEscapeSlow();
    void exeRest();
    void exeJump();
    void exeWait();
    void exeReturn();
    void exePleasure();

    Dodoryu* mHost;
    Pos3f mRabbitMtx;
    f32 mRailCoord;
    TalkMessageCtrl* mTalkCtrl;
    s32 mTalkSwitchStep;
    s32 mTalkNode;
    JointRumbler* mTrampleRumbler;
    JointRumbler* mStarPieceRumbler;
    bool mIsDisplayMessage;
    Pos3f mTalkMtx;
};

class DodoryuLeadHill : public LiveActor {
public:
    DodoryuLeadHill(Dodoryu* pHost);

    virtual void init(const TableIter& rIter) override;
    virtual void control() override;
    virtual void calcAndSetBaseMtx() override;

    void initJoint();
    bool calcJoint(Pos3f*, const JointControllerInfo&);

    MtxPtr mHostBaseMtx;
    f32 mSpinAngle;
    JointController* mJointCtrls[17];
};
