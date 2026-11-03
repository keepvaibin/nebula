#pragma once

#include "Game/LiveActor/ModelObj.hpp"
#include "Game/Util/Array.hpp"
#include <JSystem/JGeometry/TMatrix.hpp>

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

    virtual void init(const JMapInfoIter& rIter);
    virtual void initAfterPlacement();
    virtual void startClipped();
    virtual void endClipped();
    virtual void control();
    virtual void calcAndSetBaseMtx();
    virtual void attackSensor(HitSensor*, HitSensor*);
    virtual bool receiveMsgPlayerAttack(u32, HitSensor*, HitSensor*);

    void snapToGround();
    bool snapToWall();
    void setMtx(const TPos3f&);
    void notifyOnSwitchA();
    void nextState();
    void pauseOff();
    void startPlayerCenterCamera();
    void endPlayerCenterCamera();
    void getPlayerResetMtx(TPos3f*, u32) const;
    bool checkWallWithVelocity();
    void reactJumpOutCommon() const;
    void startSpinOutCamera();
    void endSpinOutCamera();
    bool keepOffFromClosedArea(TVec3f*);
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
    void calcRabbitDir(TVec3f*) const;
    void displayRabbitMessage();
    void initHitSensor();
    void initSwitch(const JMapInfoIter&);
    void createMogucchiHill();
    void createDodoryuBank();
    void turnUpVecTo(const TVec3f&);
    void checkHipDrop();
    void initState();
    void updateRumblePad();
    void updateCameraTarget();

    TPos3f mBaseMtx;
    TVec3f _BC;
    u8 _C8;
    u32 _CC;
    MR::Vector< MR::FixedArray< DodoryuStateBase*, 16 > > mState;
    DodoryuSub::MoveStateHolder* mMoveStateHolder;
    DodoryuHill* mHill;
    DodoryuLeadHill* mLeadHill;
    DodoryuBank* mBank;
    DodoryuRabbit* mRabbit;
    TVec3f _128;
    TVec3f _134;
    AreaObj* mClosedAreaObj;
    u8 _144;
    CameraTargetMtx* _148;
    CameraTargetMtx* _14C;
    u8 _150;
    u32 _154;
    AnimScaleController* mAnimScaleCtrl;
};

class DodoryuBank : public ModelObj {
public:
    /// @brief Creates a new `DodoryuBank`.
    DodoryuBank();

    virtual void init(const JMapInfoIter& rIter);

    void exeAppear();

    TMtx34f _90;
};

class DodoryuRabbit : public ModelObj {
public:
    DodoryuRabbit(Dodoryu* pDodoryu, const JMapInfoIter& rIter);

    virtual void init(const JMapInfoIter& rIter);
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
    TPos3f _94;
    f32 _C4;
    TalkMessageCtrl* mTalkCtrl;
    s32 _CC;
    s32 _D0;
    JointRumbler* _D4;
    JointRumbler* _D8;
    bool mIsDisplayMessage;
    TPos3f _E0;
};

class DodoryuLeadHill : public LiveActor {
public:
    DodoryuLeadHill(Dodoryu* pHost);

    virtual void init(const JMapInfoIter& rIter);
    virtual void control();
    virtual void calcAndSetBaseMtx();

    void initJoint();
    bool calcJoint(TPos3f*, const JointControllerInfo&);

    MtxPtr mHostBaseMtx;
    f32 _90;
    JointController* _94[17];
};
