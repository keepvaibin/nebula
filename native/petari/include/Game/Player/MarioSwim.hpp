#pragma once

#include "Game/Map/WaterInfo.hpp"
#include "Game/Player/Mario.hpp"
#include "Game/Player/MarioState.hpp"

struct AreaInfo;
class HitInfo;
class MarioActor;
class MarineSnow;

class MarioSwim : public MarioState {
public:
    enum WaterExitAction {
        EXIT_ACTION_NONE = 0,
        EXIT_ACTION_JUMP = 1,        // Shallow water jump
        EXIT_ACTION_POWER_JUMP = 2,  // Spin jump or paddle jump
        EXIT_ACTION_FALL = 3,

        EXIT_ACTION_SLIDE = 5,
        EXIT_ACTION_SURFACE = 6
    };
    enum SwimState { SWIM_STATE_NONE = 0, SWIM_STATE_ENTERING = 1, SWIM_STATE_UNDERWATER = 2, SWIM_STATE_SURFACE = 3 };

    MarioSwim(MarioActor*);

    virtual void init();
    virtual bool start();
    virtual bool close();
    virtual bool update();
    virtual bool notice();
    virtual void hitWall(const TVec3f&, HitSensor*);
    virtual void hitPoly(u8, const TVec3f&, HitSensor*);
    virtual bool passRing(const HitSensor*);
    virtual f32 getBlurOffset() const {
        return mBlurOffset;
    }

    virtual void draw3D() const;
    virtual f32 getStickY() const;
    virtual const TVec3f& getGravityVec() const;
    virtual void addVelocity(const TVec3f& rVelocity);
    virtual void addVelocity(const TVec3f& rVelocity, f32);

    bool checkWaterCube(bool);
    void onSurface();
    void doJetJump(u8);
    void updateLifeByTime();
    bool surfacePaddle();
    void flowOnWave(f32);
    bool checkWaterBottom();
    void spin();
    void decideVelocity();
    void procBuoyancy();
    void decideAnimation();
    void decideEffect(bool);
    void updateTilt();
    void jet();
    void pushedByWaterWall();
    void setDamage(const TVec3f&, u16);
    void updateUnderWater();
    bool startJet(u32);
    void addDamage(const TVec3f&);
    void addFaint(const TVec3f&);
    bool getWaterAreaInfo(WaterInfo*, const TVec3f&, TVec2f*);
    void decOxygen(u16 amount);
    void incOxygen();
    void incLife();
    void decLife();
    f32 calcRingAcc();
    void hitPunch(const TVec3f& rPunchDir);
    f32 getSurface() const;
    f32 getWaterEdgeDist() const {
        if (isOnWaterSurface()) {
            return -1.0f;
        }

        return mDistanceToWaterSurface;
    }

    bool tryJetAttack(HitSensor*);
    void dropJet(bool);
    void updateOxygenWatch();
    void doDecLifeByCold();
    void startSpinDash();
    void ringDash();
    void resetJet();
    void forceStopSpin();
    void resetAndFixPose();
    f32 checkUnderWaterFull(const TVec3f&);
    void hitHead(const HitInfo*);

    f32 calcWaterEdgeDist() const {
        if (isOnWaterSurface()) {
            return -1.0f;
        }

        return mDistanceToWaterSurface;
    };

    static inline f32 getWorthlessNumber() {
        return 0.523598790169f;
    }

    /* The worthlesser variable, as its name suggests, has no purpose.
       It achieves nothing in life. Its existence is completely devoid of value.
       Nevertheless, the compiler left evidence of this forgettable variable behind.
       As a result, we know that it existed, but we do not know how precisely it achieved nothing.

       Summary:
       worthlesser does not impact the behavior of this function.

     */

    inline void funReferenceTime(bool& worthlesser) {
        if (mVerticalAngle > 1.57079637051f) {
            worthlesser = true;
        } else {
            if (mIdleWaitTimer > 0x1E) {
                _1E = 1;
            }

            worthlesser = false;
            if (getStickY() > 0.0f) {
                f32 stickY = getStickY();
                // Note: The binary does not tell us whether this comparison is > or <.
                // It is not == because that generates an fcmpu instruction, not fcmpo.
                // It is not <= or >= because those generate cror instructions.
                if (getWorthlessNumber() + getWorthlessNumber() * stickY < mVerticalAngle

                    // This comparison needs to be present for the compiler to optimize the condition
                    // accurately.
                    && worthlesser) {
                    return;
                }
            }
        }

        worthlesser = true;
    }

    inline TVec3f getPlayer380() const {
        return getPlayer()->_380;
    }

    inline bool check7Aand7C() const {
        return mSpinTimer || mSpinDashTimer;
    }

    inline bool isOnWaterSurface() const {
        return mIsOnSurface || mIsSwimmingAtSurface;
    }

    MarineSnow* mMarineSnow;
    u8 _18;
    bool mIsOnSurface;
    bool mEnteredWater;
    bool _1B;
    bool _1C;
    bool _1D;
    bool _1E;
    bool _1F;
    bool mIsSwimmingAtSurface;
    bool mIsSinking;
    bool _22;
    u32 mStateTimer;
    u32 mSurfacePaddleHoldTimer;
    u16 mActionLockTimer;
    u16 mSinkTimer;
    u16 mSwimYAccTimer;
    u16 mZSinkTimer;
    u16 mJumpDiveTimer;
    u16 mDashTimer;
    u16 _38;
    u16 mBuoyancyDelayTimer;
    u16 mIdleWaitTimer;
    u16 mWaveFlowTimer;
    u16 mJetCooldownTimer;
    u16 mFloorContactTimer;
    u16 _44;
    f32 mSurfaceOffset;
    f32 mStickInputYInertia;
    f32 mStickInputXInertia;
    f32 mForwardSpeed;
    f32 mBuoyancySpeed;
    f32 mVerticalAngle;
    TVec3f mFrontVec;
    TVec3f mUpVec;
    bool _78;
    u16 mSpinTimer;
    u16 mSpinDashTimer;
    f32 _80;
    f32 _84;
    u8 _88;
    u16 mJetTimer;
    u16 mRingDashTimer;
    u16 mRingDashChargeTimer;
    u16 mRingDashMaxDuration;
    f32 mRingDashSpeedScale;
    f32 mBlurOffset;
    bool _9C;
    u8 _9D;
    u8 mNextAction;
    u8 mEffectState;
    TVec3f mKnockbackVel;
    u8 mDamageType;
    u8 _AD;
    u16 mKnockbackTimer;
    TMtx34f mUpperBodyJointMtx;
    f32 mCurrentTiltX;
    f32 mCurrentTiltY;
    u16 mDrownTimer;
    u16 mOxygen;
    u16 mOxygenWarningTimer;
    u16 mOxygenDecreaseDelay;
    u16 mColdWaterDamageInterval;
    WaterInfo mWaterInfo;
    s32 mSwimState;
    TVec3f _148;
    TVec3f mAreaFollowMtxPos;
    TVec3f mSurfacePos;
    TVec3f mSurfaceNorm;
    TVec3f _178;
    TVec3f mWaterStreamVel;
    TVec3f mWaterStreamDir;
    f32 mWaterDepth;
    f32 _1A0;
    f32 mDistToFloor;
    f32 _1A8;
    f32 mDistanceToWaterSurface;
    u16 mWallStickTimer;
    u8 _1B2;
    f32 mWaterDistanceTarget;
    f32 _1B8;
};
