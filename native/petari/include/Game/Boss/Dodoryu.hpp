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
    /// @brief Creates a new `Dodoryu`.
    /// @param pName A pointer to the null-terminated name of the object.
    Dodoryu(const char* pName);

    virtual void init(const TableIter& rIter);
    virtual void initAfterPlacement();
    virtual void startClipped();
    virtual void endClipped();
    virtual void control();
    virtual void calcAndSetBaseMtx();
    virtual void attackSensor(HitSensor*, HitSensor*);
    virtual bool receiveMsgPlayerAttack(u32, HitSensor*, HitSensor*);

    void snapToGround();
    bool snapToWall();
    void setMtx(const Pos3f&);
    void notifyOnSwitchA();
    void nextState();
    void pauseOff();
    void startPlayerCenterCamera();
    void endPlayerCenterCamera();
    void getPlayerResetMtx(Pos3f*, u32) const;
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
    void shiftMoveStateRail(f32);
    void shiftMoveStateChase(f32, f32, f32, f32);
    void shiftMoveStateEscape(f32, s32, s32, f32, f32);
    void shiftMoveStateJumpOut(f32, f32);
    void shiftMoveStateNull();
    void setMoveRailSpeed(f32);
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
    void turnUpVecTo(const Vec3f&);
    void checkHipDrop();
    void initState();
    void updateRumblePad();
    void updateCameraTarget();

    Pos3f mBaseMtx;
    Vec3f mCameraRegisterPos;
    u32 mStateIndex;
    MR::Vector< MR::FixedArray< DodoryuStateBase*, 16 > > mState;
    DodoryuSub::MoveStateHolder* mMoveStateHolder;
    DodoryuHill* mHill;
    DodoryuLeadHill* mLeadHill;
    DodoryuBank* mBank;
    DodoryuRabbit* mRabbit;
    Vec3f mGroundNormal;
    Vec3f mWallNormal;
    AreaObj* mClosedAreaObj;
    u8 mIsSwitchOn;
    CameraTargetMtx* mDemoCameraTarget;
    CameraTargetMtx* mSpinOutCameraTarget;
    u8 mIsRumbling;
    u32 mRumbleStep;
    AnimScaleController* mAnimScaleCtrl;
};

class DodoryuBank : public ModelObj {
public:
    /// @brief Creates a new `DodoryuBank`.
    DodoryuBank();

    virtual void init(const TableIter& rIter);

    void exeAppear();

    Mtx34f mBankMtx;
};

class DodoryuRabbit : public ModelObj {
public:
    DodoryuRabbit(Dodoryu* pDodoryu, const TableIter& rIter);

    virtual void init(const TableIter& rIter);
    virtual void control();
    virtual void attackSensor(HitSensor*, HitSensor*);
    virtual bool receiveMsgPlayerAttack(u32, HitSensor*, HitSensor*);

    void reset(bool);
    void updatePos(f32);
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

    virtual void init(const TableIter& rIter);
    virtual void control();
    virtual void calcAndSetBaseMtx();

    void initJoint();
    bool calcJoint(Pos3f*, const JointControllerInfo&);

    MtxPtr mHostBaseMtx;
    f32 mSpinAngle;
    JointController* mJointCtrls[17];
};
