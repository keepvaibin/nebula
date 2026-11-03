#pragma once

#include "Game/LiveActor/ActorStateBase.hpp"
#include "Game/LiveActor/LiveActor.hpp"
#include "Game/Util/LiveActorUtil.hpp"
#include "Game/Util/RailUtil.hpp"
#include <JSystem/JGeometry/TMatrix.hpp>

class ParabolicPath;
class ValueControl;
class Pukupuku;
class WalkerStateBindStarPointer;
class AnimScaleController;

class PukupukuStateLanding : public ActorStateBase< Pukupuku > {
public:
    PukupukuStateLanding(Pukupuku* pParent);

    virtual ~PukupukuStateLanding();

    void exeLandingMoveLand();
    void exeLandingJumpFromWater();
    void exeLandingJumpFromLand();
    void updatePoseByJumpPath(f32);
    void setNerveAfterJumpAccordingToNextPoint();
    void setupJumping(f32, f32);
    void updateJumping();
    void emitWaterColumIfNeed(bool, bool);
    void emitGroundHitEffect();

    ParabolicPath* mPath;
    ValueControl* mValueCtrl;
    u8 _18;
};

class Pukupuku : public LiveActor {
public:
    Pukupuku(const char* pName);

    virtual ~Pukupuku();
    virtual void init(const JMapInfoIter& rIter);
    virtual void kill();
    virtual void control();
    virtual void calcAndSetBaseMtx();
    virtual void attackSensor(HitSensor* pSender, HitSensor* pReceiver);
    virtual bool receiveMsgPlayerAttack(u32 msg, HitSensor* pSender, HitSensor* pReceiver);
    virtual bool receiveMsgEnemyAttack(u32 msg, HitSensor* pSender, HitSensor* pReceiver);

    void updatePoseByRailIgnoreUpScale();
    void rotatePoseByLocalZ();
    void calcGroundHitMtx(TPos3f* pMtx) const;
    bool isReadyToJumpFromLand() const;
    bool isDamaged() const;
    void exeWait();
    void exeMoveWater();
    void exeMoveWaterAfterJump();
    void exeLanding();
    void exeTrampled();
    void exeBlownOff();
    void exeBindStarPointer();
    void updatePoseByRail();
    void updateMoveWaterAfterJump();
    bool tryBindStarPointer();
    void exeMoveWaterCommon();
    f32 getBlownOffSpeedRate() const;

    inline void startAnim(const char* pBck, const char* pBtp) {
        MR::startBck(this, pBck);
        MR::startBtp(this, pBtp);
    }

    inline void getCurrentRailPointPos(TVec3f* pOut) {
        MR::calcRailPointPos(pOut, this, MR::getCurrentRailPointNo(this));
    }

    inline void getNextRailPointPos(TVec3f* pOut) {
        MR::calcRailPointPos(pOut, this, MR::getNextRailPointNo(this));
    }

    inline void exeMoveWaterAfterJumpAfterPointing() {
        updateMoveWaterAfterJump();
    }

    PukupukuStateLanding* mStateLanding;
    TVec3f _90;
    TVec3f _9C;
    TQuat4f _A8;
    AnimScaleController* mScaleCtrl;
    WalkerStateBindStarPointer* mStarPointer;
    const char* _C0;
    Nerve* _C4;
    f32 _C8;
    f32 _CC;
    u8 _D0;
    u8 _D1;
    u8 _D2;
    u8 _D3;
};
