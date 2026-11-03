#pragma once

#include "Game/Player/MarioModule.hpp"
#include "Game/Util/TriangleFilter.hpp"
#include <JSystem/JGeometry/TMatrix.hpp>

class AreaObj;
class CubeCameraArea;
class FloorCode;
class GravityInfo;
class HashSortTable;
class HitInfo;
class HitSensor;
class MarioAbyssDamage;
class MarioActor;
class MarioBlown;
class MarioBump;
class MarioBury;
class MarioClimb;
class MarioCrush;
class MarioDamage;
class MarioDarkDamage;
class MarioFaint;
class MarioFireDamage;
class MarioFireDance;
class MarioFireRun;
class MarioFlip;
class MarioFlow;
class MarioFoo;
class MarioFpView;
class MarioFreeze;
class MarioFrontStep;
class MarioHang;
class MarioMagic;
class MarioModuleTask;
class MarioMove;
class MarioParalyze;
class MarioRabbit;
class MarioRecovery;
class MarioSideStep;
class MarioSkate;
class MarioSlider;
class MarioState;
class MarioStep;
class MarioStick;
class MarioStun;
class MarioSukekiyo;
class MarioSwim;
class MarioTalk;
class MarioTeresa;
class MarioWait;
class MarioWall;
class MarioWarp;
class Triangle;
struct SoundList;

class Mario final : public MarioModule {
public:
    enum SeVersion {
        SeVersion_NORMAL = 0,
        SeVersion_DARK = 1,
    };

    typedef bool (Mario::*Task)(u32);
    Mario(MarioActor*);

    bool postureCtrl(MtxPtr);

    void initMember();

    void updateAndClearStrideParameter();
    void checkKeyLock();
    void updateTimers();
    void updateMorphResetTimer();
    void doExtraServices();
    bool isEnableCheckGround();
    void setGroundNorm(const TVec3f&);
    void checkForceGrounding();
    void updateGroundInfo();
    void fixHeadFrontVecByGravity();
    bool isNonFixHeadVec() const;
    void createMtxDir(MtxPtr, const TVec3f&, const TVec3f&, const TVec3f&);
    void createDirectionMtx(MtxPtr);
    bool createCorrectionMtx(MtxPtr, TVec3f*);
    void createAngleMtx(MtxPtr, bool);
    void slopeTiltHead(TVec3f*);
    void fixFrontVecByGravity();
    void fixFrontVecFromUpSide();
    void fixSideVecFromFrontUp();
    void setSideVec(const TVec3f&);
    void setHeadVec(const TVec3f&);
    void setFrontVec(const TVec3f&);
    void setFrontVecKeepUp(const TVec3f&, f32);
    void setFrontVecKeepUp(const TVec3f&, u32);
    void setFrontVecKeepUp(const TVec3f&);
    void setFrontVecKeepUpAngle(const TVec3f&, f32);
    void setFrontVecKeepSide(const TVec3f&);
    void setHeadAndFrontVecFromRotate(const TVec3f&);
    void forceSetHeadVecKeepSide(const TVec3f&);
    void setGravityVec(const TVec3f&);
    void draw() const;
    void addTrans(const TVec3f&, const char*);
    void setTrans(const TVec3f&, const char*);
    bool isEnableRush() const;
    bool isForceStopRush() const;
    bool isInvincible() const;
    bool isCeiling() const;
    void inputStick();
    void updateSoundCode();
    const TVec3f& getShadowNorm() const;
    const TVec3f& getAirGravityVec() const;
    const TVec3f getAirFrontVec() const;
    const TVec3f* getGravityVec() const;
    void initAfterConst();
    void writeBackPhyisicalVector();
    void update();
    void updateLookOfs();
    void actionMain();
    void touchWater();

    bool isBlendWaitGround() const;
    void checkSpecialWaitAnimation();
    void resetSleepTimer();

    void connectToClimb();

    bool isIgnoreTriangle(const Triangle*);
    void checkBaseTransBall();
    void createAtField(bool, f32);
    void doSwimmingHitCheck(const HitInfo*, u32);
    void doSpinPunchAroundPolygons();
    void checkMap();
    f32 calcDistToCeil(bool);
    f32 calcDistToCeilOnPress();
    f32 calcDistToCeilHead();
    void fixTransBetweenWall(const TVec3f&, const TVec3f&);
    f32 calcDistWidth();
    void updateCameraPolygon();
    void setCameraPolygon(const Triangle*);
    void checkAllWall(const TVec3f&, f32);
    void calcFrontFloor();
    const TVec3f& getWallNorm() const;
    const TVec3f& getSideWallNorm() const;
    const TVec3f& getFrontWallNorm() const;
    const TVec3f& getBackWallNorm() const;
    const TVec3f& getWallPos() const;
    const Triangle* getWallPolygon() const;
    const Triangle* getGroundPolygon() const;
    void updateFloorCode();
    void updateWallFloorCode();
    void saveLastSafetyTrans();
    void setNotSafetyTimer();
    const TVec3f* getLastSafetyTrans(TVec3f*) const;
    bool checkCurrentFloorCodeSevere(u32) const;
    bool isCurrentFloorSink() const;
    bool isCurrentFloorSand() const;
    bool isCurrentShadowFloorDangerAction() const;
    void checkBaseTransPoint();
    void checkHeadPoint();
    void calcShadowPos();
    bool updateBinderInfo();
    bool isThroughWall(const Triangle*) const;
    bool checkGround();
    CubeCameraArea* getCameraCubeCode() const;
    void updateCubeCode();

    bool isDamaging() const;
    bool damageLarge(const TVec3f&);
    void decDamageAfterTimer();
    bool checkDamage();
    u16 getDamageAfterTimer() const;
    bool damageFloorCheck();
    bool damageWallCheck();
    bool damagePolygonCheck(const Triangle*);
    bool flipLarge(const TVec3f&);
    bool isEnableAddDamage() const;
    bool damage(const TVec3f&);
    bool doAbyssDamage();
    void connectToFireRun();
    bool doFireDanceWithInitialDamage(u8);
    bool doFireObjHitWithInitialDamage();
    bool doNeedleWithInitialDamage(u8);
    bool doNeedleWithInitialDamage(const Triangle*);
    bool doNeedle(const Triangle*);
    bool doFireDance();
    void checkKarikariDamage();
    bool doDarkDamage();
    bool doParalyze();
    bool doFreeze();
    bool requestCrush();
    bool tryCrush();

    bool doFlipWeak(const TVec3f&);
    bool faint(const TVec3f&);

    bool doFlipJump(const TVec3f&);
    bool doFlipBackRoll(const TVec3f&);
    bool doFlipLarge(const TVec3f&);
    bool doFlipRot(const TVec3f&);

    bool doFrontStep();

    void beeMarioOnGround();
    bool beeMarioOnAir();

    bool blown(const TVec3f&);

    bool taskOnEffectCheck(u32);

    bool doFlow();

    void tryStartFoo();

    bool isRising() const;
    void checkWallRiseAndSlipFront();
    void tryJump();
    void tryTurnJump();
    void trySquatJump();
    void tryBackJump();
    void tryTornadoJump();
    void startTornadoCentering(HitSensor*);
    bool taskOnTornadoCentering(u32);
    void trySpinJump(u8);
    void tryForceJumpDelay(const TVec3f&);
    void tryFreeJumpDelay(const TVec3f&);
    void tryForceJump(const TVec3f&, bool);
    void tryForceFreeJump(const TVec3f&);
    void tryForcePowerJump(const TVec3f&, bool);
    void tryFreeJump(const TVec3f&, bool);
    void tryWallJump(const TVec3f&, bool);
    void tryStickJump(const TVec3f&);
    void trySlipUpJump();
    void tryHangSlipUp();
    void tryDrop();
    bool isDigitalJump() const;
    void initActiveJumpVec();
    void initJumpParam();
    bool isEnableFutureJump() const;
    void procJump(bool);
    void checkWallRising();
    void checkWallJumpHit();
    void decideSlipUp();
    void moveWallSlide(f32);
    bool jumpToHipDrop();
    void procHipDrop();
    void doAirWalk();
    void stopJump();
    void cancelTornadoJump();
    void setRocketBooster(const TVec3f&, f32, u16);
    void procRocketBooster();
    bool isSoftLandingFloor() const;
    void checkAndTryForceJump();
    void doLanding();
    void startSlidingTask(u32, f32, u16);
    bool taskOnSlide(u32);
    void incAirWalkTimer();

    void mainMove();
    bool isEnableTurn();
    void recordTurnSlipAngle();
    f32 decideInertia(f32);
    f32 decideInertiaOnIce(f32);
    f32 decideInertiaOnSlip(f32);
    void calcShadowDir(const TVec3f&, TVec3f*);
    bool retainMoveDir(f32, f32, TVec3f*);
    void calcMoveDir(f32, f32, TVec3f*, bool);
    bool checkLockOnHoming();
    void doLockOnHoming();
    void fixPositionInTower();

    void check2DMode();
    void calcMoveDir2D(f32, f32, TVec3f*);
    void calcShadowDir2D(const TVec3f&, TVec3f*);
    void stick2Dadjust(f32&, f32&);
    void set2Dmode(bool);
    void beforeJumping2D();
    void afterLanding2D();
    void clear2DStick();

    void set25Dmode(const AreaObj*);
    void update25Dmode();
    void updateAxisFromMode(u8);
    void calcMoveDir25D(f32, f32, TVec3f*);

    bool checkPressDamage();
    bool checkVerticalPress(bool);
    bool checkSidePressPre();
    bool checkSidePress();

    void startRabbitMode();
    void endRabbitMode();

    bool checkWallJumpCode();
    bool doSideStep();

    bool isSkatableFloor() const;
    bool doSkate();

    u32 initSoundTable(SoundList* list);
    void initSound();
    void playSoundJ(const char*, s32);
    void stopSoundJ(const char*, u32);
    void startBas(const char*, bool, f32, f32);
    bool isRunningBas(const char*) const;
    void skipBas(f32);
    void playSoundTeresaFlying();
    void playSoundTrampleCombo(u8);
    void setSeVersion(u32);

    void checkOnimasu(const HitSensor*);
    bool isDossun(const Triangle*) const;
    bool isStageCameraRotate2D() const;
    bool isNoWalkFallOnDossun() const;
    bool isNotReflectGlassGround() const;
    bool isUseAnotherMovingPolygon() const;
    bool isUseFooSpecialGravity(const TVec3f&, TVec3f*) const;
    void updateOnimasu();
    bool isHeadPushEnableArea() const;
    bool isOnimasuBinderPressSkip() const;

    void checkTornado();
    void resetTornado();
    void calcTornadoTilt();
    void reflectWallOnSpinning(const TVec3f&, u16);
    void forceStopTornado();
    void startRotationTask(u32);
    void doSpinWallEffect();
    bool taskOnRotation(u32);

    void sendStateMsg(u32);
    bool updatePosture(MtxPtr);
    void changeStatus(MarioState*);
    void closeStatus(MarioState*);
    u32 getCurrentStatus() const;
    bool isStatusActive(u32) const;

    void checkBeeStick();
    void tryBeeStick(const HitSensor*);

    void startTalk(const LiveActor*);
    void endTalk();
    void setLastNonFixMoveVec(const TVec3f&);

    bool isWalling() const;
    void checkWallStick();
    u8 checkStickWallSide();
    s32 checkStickFrontBack();
    bool fixWallingPosition(bool);
    void fixWallingDir(bool);
    bool fixWallingTop();
    bool checkWallFloorCode(u16) const;
    bool checkWallCode(const char*, bool) const;
    bool checkWallCodeNorm(u16, TVec3f*, bool) const;
    void setWallCancel();
    void keepDistFrontWall();
    bool isEnableStickWall();
    bool fixWallingDist();
    bool isInhibitWall() const;
    void tryWallPunch();

    bool isHanging() const;
    bool fixHangDir(const TVec3f&, TVec3f*);
    bool isEnableBackHang();
    bool isEnableSideHang();
    void checkHang();
    bool isEnableHang();

    bool isSwimming() const;
    bool forceStartSwimAndShoot(const TVec3f&);
    bool forceExitSwim();
    bool checkStartSwim();
    void startSwim();

    bool checkWaterDamage();

    void clearSlope();
    bool isEnableSlopeMove() const;
    void moveSlopeSlide();
    void slopeMove();
    bool taskOnSlipTurn(u32);

    void lockGroundCheck(void*, bool);
    void unlockGroundCheck(void*);
    bool isUseSimpleGroundCheck() const;
    bool checkGroundOnSlope();

    void stopWalk();
    void cancelSquatMode();
    f32 getTargetWalkSpeed() const;
    void decideSquatWalkAnimation();
    void decideWalkSpeed();
    void decideWalkAnimation();
    void doBrakingAnimation();
    void checkWallPush();
    void updateBrakeAnimation();
    void updateWalkSpeed();
    void decideOnIceAnimation();
    void updateOnSand();
    void updateOnPoison();
    void updateOnWater();

    bool doRecovery();

    bool isSliderFloor() const;
    bool checkSliderMode() const;
    void startSlider();

    void checkStep();
    void startStep(const TVec3f&);

    void checkBump();
    void startBump(const TVec3f&);

    void checkEnforceMove();
    void checkEnforceMoveInner();
    void recordRelativePosition();
    void invalidateRelativePosition();
    TPos3f* getMoveBaseMtx() const;
    u32 moveRelativePosition(u32);
    u32 moveRelativePositionWall();
    void recordLastGround();
    void getLastGroundPos(TVec3f*) const;
    const TVec3f* getLastGroundEdgeNrm(u32) const;
    u32 getLastGroundEdgeIndex(const TVec3f&, const TVec3f&) const;
    void pushedByReaction();
    void addReaction(const TVec3f&);
    void tryPushToVelocity();
    void push(const TVec3f&);
    void push2(const TVec3f&);
    void powerAreaMove();
    void powerRailMove();
    void recordJumpEnforceMove();
    void doEnforceJump(f32);
    void pushedByWind();

    void delTask(MarioModuleTask*);
    void execTask();
    void drawTask() const;
    void initTask();
    bool isActiveTask(Task);
    bool isActiveTaskID(u32);
    bool pushTask(Task, u32);
    void popTask(Task);
    void callExtraTasks(u32);
    void startHandy();
    bool taskOnHipDropBlurHopper(u32);
    bool taskOnHipDropBlur(u32);
    bool taskOnHipDropSlide(u32);
    bool taskOnFreezeEnd(u32);
    void startFreezeEnd();
    bool taskOnHandy(u32);
    void startHipDropBlur();
    void startHipDropSlide(const HitSensor*);
    void startJumpDropSlide(const HitSensor*);

    void startTeresaMode();
    bool getHitWallNorm(TVec3f*);
    void resetTeresaMode();
    void doTeresaReflection(const TVec3f&, bool);
    void startTeresaDisappear();

    void stopPunch();
    void startMagic();

    bool doObjWarp(LiveActor*);
    bool doPointWarp(const TVec3f&, const TVec3f&, s32);
    bool isVisibleRecoveryWarpBubble() const;
    void doCubeWarp();
    bool doPointWarpRecovery(const TVec3f&, const TVec3f&);

    bool isDisableStayHere() const;
    bool isDisableFpViewMode() const;
    void tryFpViewMode();

    void stick2DadjustGround(f32&, f32&);
    void calcDir2D(f32, f32, TVec3f*);

    // instruction comments to make it easier to identify each bit
    struct MovementStates {
        // Code reads these as words (mMovementStates_HIGH_WORD and the others): fields from the least significant value
        // bit up in each word.
        unsigned _1F : 1;
        unsigned digitalJump : 1;  // _1E
        unsigned _1D : 1;
        unsigned _1C : 1;
        unsigned _1B : 1;
        unsigned _1A : 1;        // extrwi rX, rX, 1, 26
        unsigned _19 : 1;        // extrwi rX, rX, 1, 25
        unsigned _18 : 1;        // rlwinm rX, rX, 0, 25, 23
        unsigned _17 : 1;        // extrwi rX, rX, 1, 23
        unsigned debugMode : 1;  // _16
        unsigned _15 : 1;
        unsigned _14 : 1;
        unsigned _13 : 1;
        unsigned _12 : 1;
        unsigned _11 : 1;
        unsigned _10 : 1;  // rlwinm rX, rX, 0, 17, 15
        unsigned _F : 1;
        unsigned _E : 1;
        unsigned _D : 1;
        unsigned _C : 1;
        unsigned _B : 1;
        unsigned _A : 1;  // extrwi rX, rX, 1, 10
        unsigned _9 : 1;
        unsigned _8 : 1;
        unsigned _7 : 1;
        unsigned _6 : 1;
        unsigned _5 : 1;
        unsigned _4 : 1;
        unsigned turning : 1;  // _3
        unsigned _2 : 1;
        unsigned _1 : 1;       // extrwi rX, rX, 1, 1
        unsigned jumping : 1;  // _0 (srwi rX, rX, 31)
        unsigned _3E : 2;  // clrrwi rX, rX, 2
        unsigned _3D : 1;
        unsigned _3C : 1;
        unsigned _3B : 1;
        unsigned _3A : 1;
        unsigned _39 : 1;
        unsigned _38 : 1;  // rlwimi rX, rX, 7, 24, 24
        unsigned _37 : 1;
        unsigned _36 : 1;
        unsigned _35 : 1;
        unsigned _34 : 1;
        unsigned _33 : 1;
        unsigned _32 : 1;
        unsigned _31 : 1;
        unsigned _30 : 1;
        unsigned _2F : 1;
        unsigned _2E : 1;
        unsigned _2D : 1;
        unsigned _2C : 1;
        unsigned _2B : 1;
        unsigned _2A : 1;
        unsigned _29 : 1;
        unsigned _28 : 1;
        unsigned _27 : 1;
        unsigned _26 : 1;
        unsigned _25 : 1;
        unsigned _24 : 1;
        unsigned _23 : 1;  // extrwi rX, rX, 1, 3
        unsigned _22 : 1;
        unsigned _21 : 1;
        unsigned _20 : 1;
    };
    struct DrawStates {
        // Code reads these as a word (_1C_WORD masks): fields from the least significant value bit up, so each has
        // the bit those masks name.
        unsigned _1F : 1;
        unsigned _1E : 1;
        unsigned _1D : 1;
        unsigned _1C : 1;
        unsigned _1B : 1;
        unsigned _1A : 1;
        unsigned _19 : 1;
        unsigned _18 : 1;
        unsigned _17 : 1;
        unsigned _16 : 1;
        unsigned _15 : 1;
        unsigned _14 : 1;
        unsigned _13 : 1;
        unsigned mIsUnderwater : 1;
        unsigned _11 : 1;
        unsigned _10 : 1;
        unsigned _F : 1;
        unsigned _E : 1;
        unsigned _D : 1;
        unsigned _C : 1;
        unsigned _B : 1;
        unsigned _A : 1;
        unsigned _9 : 1;
        unsigned _8 : 1;
        unsigned _7 : 1;
        unsigned _6 : 1;
        unsigned _5 : 1;
        unsigned _4 : 1;
        unsigned _3 : 1;
        unsigned _2 : 1;
        unsigned _1 : 1;
        unsigned _0 : 1;
    };

    inline const MovementStates& getMovementStates() const {
        return mMovementStates;
    }

    inline const DrawStates& getDrawStates() const {
        return mDrawStates;
    }

    inline const DrawStates& getPrevDrawStates() const {
        return mPrevDrawStates;
    }

    const TVec3f& getUpVec() const {
        return _1FC;
    }

    const TVec3f& getFrontVec() const {
        return mFrontVec;
    }

    const TVec3f& getJumpVec() const {
        return mJumpVec;
    }

    inline void setJumpVec(const TVec3f& rVec) {
        mJumpVec = rVec;
    }

    inline void resetInline() {
        _74C = 0.0f;
        _750 = 0;
        _754 = 0;
    }

    inline void set3BC(u16 val) {
        _3BC = val;
    }

    inline void set41E(u16 val) {
        _41E = val;
    }

    union {
        MovementStates mMovementStates;
        struct {
            u32 mMovementStates_LOW_WORD;
            u32 mMovementStates_HIGH_WORD;
        };
    };

    union {
        MovementStates _10;
        struct {
            u32 _10_LOW_WORD;
            u32 _10_HIGH_WORD;
        };
    };

    union {
        DrawStates mDrawStates;
        u32 mDrawStates_WORD;
    };
    union {
        DrawStates _1C;
        u32 _1C_WORD;
    };
    union {
        MovementStates _20;
        struct {
            u32 _20_LOW_WORD;
            u32 _20_HIGH_WORD;
        };
    };
    u32 _28;
    u32 _2C;

    DrawStates mPrevDrawStates;
    // FAKE
    u32 _34;
    // NOT FAKE

    f32 _38;
    f32 _3C;
    TVec3f _40;
    TVec3f _4C;
    TVec3f _58;
    TMtx34f _64;
    TMtx34f _94;
    TMtx34f _C4;
    TMtx34f _F4;
    f32 _124;
    f32 _128;
    u32 _12C;
    TVec3f mPosition;
    TVec3f _13C;
    TVec3f _148;
    TVec3f _154;
    TVec3f mVelocity;
    TVec3f _16C;
    TVec3f mVelocityAfter;
    TVec3f _184;
    TVec3f _190;
    TVec3f _19C;
    TVec3f _1A8;
    TVec3f _1B4;
    TVec3f mStickPos;
    TVec3f mWorldPadDir;
    TVec3f mAirGravityVec;
    TVec3f _1E4;
    TVec3f mHeadVec;
    TVec3f _1FC;
    TVec3f mFrontVec;
    TVec3f _214;
    TVec3f _220;
    TVec3f _22C;
    TVec3f _238;
    TVec3f _244;
    TVec3f _250;
    TVec3f _25C;
    TVec3f _268;
    u8 _274;
    f32 mWalkSpeed;
    f32 mPrevAnimFrame;
    f32 _280;
    TVec3f _284;
    TVec3f _290;
    TVec3f _29C;
    TVec3f _2A8;
    f32 _2B4;
    TVec3f _2B8;
    TVec3f _2C4;
    f32 _2D0;
    TVec3f mJumpVec;
    TVec3f _2E0;
    TVec3f _2EC;
    TVec3f _2F8;
    TVec3f _304;
    TVec3f mSideVec;
    TVec3f mShadowPos;
    TVec3f _328;
    TVec3f _334;
    f32 _340;
    TVec3f _344;  // left/right?
    TVec3f _350;
    TVec3f _35C;
    TVec3f _368;
    TVec3f _374;
    TVec3f _380;
    TVec3f _38C;
    TVec3f _398;
    TVec3f _3A4;
    TVec3f _3B0;
    u16 _3BC;
    u16 _3BE;
    u16 _3C0;
    u16 _3C2;
    u16 _3C4;
    u16 _3C6;
    u16 _3C8;
    u16 _3CA;
    u16 _3CC;
    u16 _3CE;  // a timer
    u16 _3D0;
    u16 _3D2;
    u16 _3D4;
    TVec3f _3D8;
    TVec3f _3E4;
    f32 _3F0;
    f32 _3F4;
    u16 _3F8;
    u16 _3FA;
    u16 _3FC;
    u16 _3FE;
    u16 _400;
    u16 _402;
    u16 _404;
    u16 _406;
    u16 _408;
    u16 _40A;
    u16 _40C;
    u16 _40E;
    u16 _410;
    u16 _412;
    u16 _414;  // a timer
    u16 mMorphResetTimer;
    u16 _418;
    u16 _41A;
    u16 _41C;
    u16 _41E;
    u16 _420;
    u16 _422;
    u16 _424;
    u16 _426;
    u16 _428;
    u16 _42A;
    u16 _42C;
    u32 _430;
    u16 _434;  // a timer
    u16 _436;
    u16 _438;
    u16 _43A;
    TVec3f _43C;  // front?
    TVec3f _448;
    f32 _454;
    TriangleFilterDelegator< Mario >* _458;
    Triangle* _45C;
    Triangle* _460;
    Triangle* mGroundPolygon;
    Triangle* _468;
    Triangle* _46C;
    Triangle* _470;
    Triangle* _474;
    Triangle* mTmpPolygon;
    Triangle* _47C;
    Triangle* _480;
    Triangle* _484;
    f32 mVerticalSpeed;
    TVec3f mGroundPos;
    TVec3f _498;
    TVec3f _4A4;
    TVec3f _4B0;
    TVec3f _4BC;
    Triangle* _4C8;
    Triangle* mFrontWallTriangle;
    Triangle* mBackWallTriangle;
    Triangle* mSideWallTriangle;
    Triangle* _4D8;
    Triangle* _4DC;
    f32 _4E0;
    f32 _4E4;
    TVec3f _4E8;
    TVec3f _4F4;

    // FAKE
    TVec3f _500;
    // NOT FAKE

    TVec3f _50C;
    TVec3f _518;
    f32 _524;
    f32 _528;
    f32 _52C;
    f32 _530;
    u16 _534;
    f32 _538;
    f32 _53C;
    f32 mYAngleOffset;
    u16 _544;
    f32 _548;
    TVec3f _54C;
    u32 _558;

    /// FAKE
    u32 _55C;
    u32 _560;
    // NOT FAKE

    s32 _564;
    CubeCameraArea* _568;
    AreaObj* _56C;
    u8 _570;
    void* _574;
    u32 _578;
    Triangle* _57C[0x20];
    const HitSensor* _5FC;
    TVec3f _600;
    u8 _60C;
    bool _60D;
    u8 _60E;
    u8 _60F;
    u8 _610;
    u8 _611;
    f32 _614;
    f32 _618;
    f32 _61C;
    f32 _620;
    bool _624;
    TVec3f _628;
    f32 _634;

    u8 _638;
    TVec3f mSide2D;
    TVec3f mUp2D;
    TVec3f mNormal2D;
    TVec3f _660;
    bool _66C;
    TVec3f _670;
    TVec3f _67C;

    TVec3f _688;
    TVec3f _694;
    TVec3f _6A0;
    u8 _6AC;
    u8 _6AD;
    TVec3f _6B0;
    TVec3f _6BC;
    TVec2f _6C8;
    TVec3f _6D0;
    TVec3f _6DC;
    TVec3f _6E8;
    TVec3f _6F4;
    TVec3f _700;
    TVec3f _70C;
    f32 _718;
    u8 mTargetWalkSpeedIndex;
    u8 _71D;
    u8 _71E;
    u8 _71F;
    const char* _720;
    const char* _724;
    const char* _728;
    f32 _72C;
    HitSensor* _730;
    u8 mIceAnimFoot;
    u8 mSinkTimer;
    f32 _738;
    TVec3f _73C;
    u8 mPoisonTimer;
    f32 _74C;
    u32 _750;
    u32 _754;
    MarioWall* mWall;
    TVec3f _75C;
    MarioStick* mStick;
    u16 _76C;
    f32 _770;
    u16 _774;
    MarioRabbit* mRabbit;
    MarioFoo* mFoo;
    MarioSukekiyo* mSukekiyo;
    MarioBury* mBury;
    MarioWait* mWait;
    MarioClimb* mClimb;
    TVec3f _790;
    MarioHang* mHang;
    MarioRecovery* mRecovery;
    MarioWarp* mWarp;
    MarioFlip* mFlip;
    MarioSideStep* mSideStep;
    MarioFrontStep* mFrontStep;
    MarioSkate* mSkate;
    MarioTalk* mTalk;
    MarioTeresa* mTeresa;
    MarioDamage* mDamage;
    TVec3f _7C4;
    u16 _7D0;
    TVec3f _7D4;
    Triangle* _7E0;
    TMtx34f _7E4;
    TVec3f _814;
    Triangle* _820;
    TMtx34f _824;
    MarioFlow* mFlow;
    MarioFireDamage* mFireDamage;
    MarioFireDance* mFireDance;
    MarioFireRun* mFireRun;
    MarioParalyze* mParalyze;
    MarioStun* mStun;
    MarioCrush* mCrush;
    MarioFreeze* mFreeze;
    MarioAbyssDamage* mAbyssDamage;
    MarioDarkDamage* mDarkDamage;
    MarioFaint* mFaint;
    MarioBlown* mBlown;
    MarioSwim* mSwim;
    MarioSlider* mSlider;
    MarioStep* mStep;
    MarioBump* mBump;
    MarioMagic* mMagic;
    bool _898;
    MarioFpView* mFpView;
    MarioMove* mMove;
    TVec3f _8A4;
    union {
        struct {
            TVec3f _8B0;
            TVec3f _8BC;
        };
        TVec3f mRelativeWallPos[2];
    };
    Triangle* _8C8;
    Triangle* _8CC[2];
    HitSensor* _8D4;
    HitSensor* _8D8;
    TVec3f _8DC;
    Triangle* _8E8;
    u8 _8EC;

    // FAKE?
    f32 _8F0;
    // NOT FAKE

    f32 _8F4;

    TVec3f _8F8;
    TVec3f _904;
    // NOT FAKE

    TVec3f _910;
    TVec3f _91C;
    TVec3f _928;
    f32 _934;
    TVec3f _938;
    TVec3f _944;
    TVec3f _950;
    FloorCode* _95C;
    u16 _960;
    u16 _962;
    u16 _964[3];
    u16 _96A;

    HashSortTable* _96C;  // Sounds
    const char* _970;     // Sounds or somthing
    MarioModuleTask* _974;

    // Fake
    u32 _978;
    // Not fake

    MarioState* _97C;
    MarioState* _980;

    Task _984[0xb];
    u32 _A08[11];
    u16 _A34;
    uintptr_t _A38;
    u16 _A3C;
    TVec3f _A40;
    TVec3f _A4C;
    TVec3f _A58;
    f32 _A64;
    HitSensor* _A68;
    u8 _A6C[0x20];  // animations
    TVec3f _A8C[9];
};
