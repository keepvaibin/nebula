#pragma once

#include "Game/LiveActor/ActorStateBase.hpp"
#include "Game/LiveActor/LiveActor.hpp"
#include "Game/Util/LiveActorUtil.hpp"
#include "Game/Util/RailUtil.hpp"
#include <nebula_geometry.hpp>

class ParabolicPath;
class ValueControl;
class Pukupuku;
class WalkerStateBindStarPointer;
class AnimScaleController;

class PukupukuStateLanding : public ActorStateBase< Pukupuku > {
public:
    PukupukuStateLanding(Pukupuku* pParent);

    void exeLandingMoveLand();
    void exeLandingJumpFromWater();
    void exeLandingJumpFromLand();
    void tryEndJumpOnLand();
    void updatePoseByJumpPath(f32 rate);
    void setNerveAfterJumpAccordingToNextPoint();
    void setupJumping(f32 height, f32 averageSpeed);
    void updateJumping();
    void emitWaterColumnIfNeed(bool isFromWater, bool isForce);
    void emitGroundHitEffect();

    ParabolicPath* mPath;
    ValueControl* mValueCtrl;
    bool mIsWaterColumnEmitted;
};

class Pukupuku : public LiveActor {
public:
    Pukupuku(const char* pName);

    virtual void init(const TableIter& rIter) override;
    virtual void kill() override;
    virtual void control() override;
    virtual void calcAndSetBaseMtx() override;
    virtual void attackSensor(HitSensor* pSender, HitSensor* pReceiver) override;
    virtual bool receiveMsgPlayerAttack(u32 msg, HitSensor* pSender, HitSensor* pReceiver) override;
    virtual bool receiveMsgEnemyAttack(u32 msg, HitSensor* pSender, HitSensor* pReceiver) override;

    void updatePoseByRailIgnoreUpScale();
    void rotatePoseByLocalZ();
    void calcGroundHitMtx(Pos3f* pMtx) const;
    bool isReadyToJumpFromLand() const;
    bool isDamaged() const;
    bool isInWater() const;
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
    void startLanding(const Nerve* pLandingNerve);
    f32 getBlownOffSpeedRate() const;
    void startBlownOff(HitSensor* pAttacker, HitSensor* pVictim);

    inline void startAnim(const char* pBck, const char* pBtp) {
        MR::startBck(this, pBck);
        MR::startBtp(this, pBtp);
    }

    inline void getCurrentRailPointPos(Vec3f* pOut) {
        MR::calcRailPointPos(pOut, this, MR::getCurrentRailPointNo(this));
    }

    inline void getNextRailPointPos(Vec3f* pOut) {
        MR::calcRailPointPos(pOut, this, MR::getNextRailPointNo(this));
    }

    inline void exeMoveWaterAfterJumpAfterPointing() {
        updateMoveWaterAfterJump();
    }

    PukupukuStateLanding* mStateLanding;
    Vec3f mClipCenter;
    Vec3f mBinderOffset;
    Quat4f mPoseQuat;
    AnimScaleController* mScaleCtrl;
    WalkerStateBindStarPointer* mStarPointer;
    const char* mBckBeforeBind;
    Nerve* mNerveAfterBind;
    f32 mBckFrameBeforeBind;
    f32 mBtpFrameBeforeBind;
    bool mIsTrampled;
};
