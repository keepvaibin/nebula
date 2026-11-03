#pragma once

#include "Game/LiveActor/HitSensor.hpp"
#include "Game/LiveActor/LiveActor.hpp"
#include "Game/Util/ActorSensorUtil.hpp"
#include <JSystem/JGeometry/TMatrix.hpp>

class AudTamakoroBgmCtrl;
class JointController;
struct JointControllerInfo;
class SphereAccelSensorController;
class TamakoroTutorial;

class Tamakoro : public LiveActor {
public:
    /// @brief Creates a new `Tamakoro`.
    /// @param pName A pointer to the null-terminated name of the object.
    Tamakoro(const char* pName);

    virtual void init(const JMapInfoIter& rIter);
    virtual void control();
    virtual void calcAndSetBaseMtx();
    virtual void attackSensor(HitSensor*, HitSensor*);
    virtual bool receiveMsgPlayerAttack(u32, HitSensor*, HitSensor*);
    virtual bool receiveMsgEnemyAttack(u32, HitSensor*, HitSensor*);
    virtual bool receiveOtherMsg(u32, HitSensor*, HitSensor*);

    void initSensor();
    void initJointControl();
    bool ringMtxCallBack(TPos3f*, const JointControllerInfo&);
    void updateBindActorMatrix();
    void updateRingUpVec();
    bool requestBind(HitSensor*);
    bool requestEndBind();
    void startRide();
    bool requestTutorialEnd();
    bool tryFall();
    bool tryJump();
    bool tryBumpWall();
    bool tryLand();
    void reactionCollision();
    void exeStandByTutorial();
    void exeStandByBind();
    void exeBindStart();
    void exeBindStartLand();
    void exeTutorial();
    void exeWait();
    void exeFall();
    void exeJump();
    void exeBumpWall();
    void exeLand();
    void exeJumpHole();
    void exeJumpHoleSetUp();
    void exeJumpHoleLaunch();
    void endJumpHoleLaunch();
    void exeDashRail();
    void exeDashRailEnd();
    void exeRideRail();
    void exeRideRailFastStart();
    void exeRideRailFast();
    void exeRideRailFastEnd();
    void exeBindEnd();
    void addVelocityOperate();
    f32 updateRideRail();
    void updateVelocityNormal(f32);
    void updateMoment();
    void updateAirTime();
    void updateMarioPose(f32);
    void updateMarioOffset();
    void updateMoveBckBlend(f32);
    void updateSquatBckBlend(f32);
    bool isEnablePushPlayer() const;
    bool isEnableEnemyAttack() const;
    bool isEnableBallBind() const;
    bool isEnebleHitCollisionMessage() const;
    bool isUseMarioOffset() const;
    bool isGroundMomentBall() const;
    bool isRideRail() const;
    bool isNeedTutorial() const;
    void startRotateLevelSound();

    inline void setTutorial() {
        mHasTutorial = true;
    }

    inline bool isBindedSphereDash(HitSensor* pSender, HitSensor* pReceiver) const {
        if (pReceiver->isType(ATYPE_SPHERE_DASH)) {
            return pReceiver->receiveMessage(ACTMES_SPHERE_PLAYER_BINDED, pSender);
        }
        return false;
    }

    inline bool isBindedJumpHole(HitSensor* pSender, HitSensor* pReceiver) const {
        if (pReceiver->isType(ATYPE_JUMP_HOLE)) {
            return pReceiver->receiveMessage(ACTMES_SPHERE_PLAYER_BINDED, pSender);
        }
        return false;
    }

    inline bool isBindedBallRail(HitSensor* pSender, HitSensor* pReceiver) const {
        if (pReceiver->isType(ATYPE_BALL_RAIL)) {
            return pReceiver->receiveMessage(ACTMES_SPHERE_PLAYER_BINDED, pSender);
        }
        return false;
    }

private:
    SphereAccelSensorController* mAccelSensorCtrl;
    TamakoroTutorial* mTutorial;
    JointController* mJointCtrl;
    TQuat4f mBallRotateQuat;
    TQuat4f mMarioRotateQuat;
    TVec3f mRingUp;
    TVec3f _C4;  // unused
    TVec3f mKickVel;
    TVec3f mMarioBindRequestPos;
    TVec3f mMarioUp;
    TVec3f mMarioPos;
    TVec3f mMoment;
    TVec3f mMarioFront;
    TVec3f mDirectionToMario;
    TVec3f mAccelDir;
    f32 mAccelRate;
    f32 mMarioRotateYAngle;
    f32 mMarioOffset;
    f32 mMarioOffsetVelocity;
    s32 mAirTime;
    AudTamakoroBgmCtrl* mBgmCtrl;
    bool mControlDisabled;
    bool mHasTutorial;
};

namespace MR {
    NameObj* createTamakoroWithTutorial(const char*);
};  // namespace MR
