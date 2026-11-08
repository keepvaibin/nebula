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
    virtual void hitWall(const Vec3f&, HitSensor*);
    virtual void hitPoly(u8, const Vec3f&, HitSensor*);
    virtual bool passRing(const HitSensor*);
    virtual f32 getBlurOffset() const {
        return mBlurOffset;
    }

    virtual void draw3D() const;
    virtual f32 getStickY() const;
    virtual const Vec3f& getGravityVec() const;
    virtual void addVelocity(const Vec3f& rVelocity);
    virtual void addVelocity(const Vec3f& rVelocity, f32);

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
    void setDamage(const Vec3f&, u16);
    void updateUnderWater();
    bool startJet(u32);
    void addDamage(const Vec3f&);
    void addFaint(const Vec3f&);
    bool getWaterAreaInfo(WaterInfo*, const Vec3f&, Vec2f*);
    void decOxygen(u16 amount);
    void incOxygen();
    void incLife();
    void decLife();
    f32 calcRingAcc();
    void hitPunch(const Vec3f& rPunchDir);
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
    f32 checkUnderWaterFull(const Vec3f&);
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
                mUpStrokeAllowed = 1;
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

    inline Vec3f getTouchedWallNorm() const {
        return getPlayer()->mWallNorm;
    }

    inline bool isSpinning() const {
        return mSpinTimer || mSpinDashTimer;
    }

    inline bool isOnWaterSurface() const {
        return mIsOnSurface || mIsSwimmingAtSurface;
    }

    MarineSnow* mMarineSnow;
    u8 mHoldOnSurface;  // kept on the surface: no sinking; nothing sets it
    bool mIsOnSurface;
    bool mEnteredWater;
    bool mSurfaceStartAnim;  // the surface's first-move animation is due
    bool mPaddleJumpReady;  // the surface paddle is ready to jump
    bool mStrokeQueued;  // an A press waits for the stroke's connect window
    bool mUpStrokeAllowed;  // the upward stroke's angle applies (after 30 idle frames)
    bool mWallHit;  // a wall was hit: the bottom check runs
    bool mIsSwimmingAtSurface;
    bool mIsSinking;
    bool mSlideOut;  // a slide polygon or wall: the swim ends in a slide
    u32 mStateTimer;
    u32 mSurfacePaddleHoldTimer;
    u16 mActionLockTimer;
    u16 mSinkTimer;
    u16 mSwimYAccTimer;
    u16 mZSinkTimer;
    u16 mJumpDiveTimer;
    u16 mDashTimer;
    u16 mSurfaceStickTimer;  // after a dive or a sink: frames before the stick steers at the surface again
    u16 mBuoyancyDelayTimer;
    u16 mIdleWaitTimer;
    u16 mWaveFlowTimer;
    u16 mJetCooldownTimer;
    u16 mFloorContactTimer;
    u16 mWaterWallTimer;  // frames the water wall pushes straight out
    f32 mSurfaceOffset;
    f32 mStickInputYInertia;
    f32 mStickInputXInertia;
    f32 mForwardSpeed;
    f32 mBuoyancySpeed;
    f32 mVerticalAngle;
    Vec3f mFrontVec;
    Vec3f mUpVec;
    bool mSpinPunchHit;  // the spin hit something: the spin dash stops its animation
    u16 mSpinTimer;
    u16 mSpinDashTimer;
    u8 mJetKind;  // the jet's kind (startJet)
    u16 mJetTimer;
    u16 mRingDashTimer;
    u16 mRingDashChargeTimer;
    u16 mRingDashMaxDuration;
    f32 mRingDashSpeedScale;
    f32 mBlurOffset;
    bool mPaddleReleased;  // A was let go since the last paddle
    u8 mEntryKind;  // how he went in: 0 walking, 1 jumping, 4 damaged
    u8 mNextAction;
    u8 mEffectState;
    Vec3f mKnockbackVel;
    u8 mDamageType;
    u8 mLargeDamage;  // the next damage is large (damageLarge, flipLarge)
    u16 mKnockbackTimer;
    Mtx34f mUpperBodyJointMtx;
    f32 mCurrentTiltX;
    f32 mCurrentTiltY;
    u16 mDrownTimer;
    u16 mOxygen;
    u16 mOxygenWarningTimer;
    u16 mOxygenDecreaseDelay;
    u16 mColdWaterDamageInterval;
    WaterInfo mWaterInfo;
    s32 mSwimState;
    Vec3f mAreaFollowMtxPos;
    Vec3f mSurfacePos;
    Vec3f mSurfaceNorm;
    Vec3f mWaterEdgePos;  // the water area's edge (the reflection, the water wall)
    Vec3f mWaterStreamVel;
    Vec3f mWaterStreamDir;
    f32 mWaterDepth;
    f32 mDistToFloor;
    f32 mDistanceToWaterSurface;
    u16 mWallStickTimer;
    u8 mDeepWater;  // the surface is over 300 below (the walk and the floor effects)
    f32 mWaterDistanceTarget;
    f32 mSurfaceDepth;  // the distance to the surface along the gravity
};
