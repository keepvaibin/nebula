#pragma once

#include "Game/Map/CollisionCode.hpp"
#include "Game/Map/HitInfo.hpp"
#include "Game/Player/MarioModule.hpp"
#include "Game/Util/Array.hpp"
#include "Game/Util/NameIndex.hpp"
#include "Game/Util/TriangleFilter.hpp"
#include <nebula_geometry.hpp>

class AreaObj;
class CubeCameraArea;
class FloorCode;
class GravityInfo;
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
    void setGroundNorm(const Vec3f&);
    void checkForceGrounding();
    void updateGroundInfo();
    void fixHeadFrontVecByGravity();
    bool isNonFixHeadVec() const;
    void createMtxDir(MtxPtr, const Vec3f&, const Vec3f&, const Vec3f&);
    void createDirectionMtx(MtxPtr);
    bool createCorrectionMtx(MtxPtr, Vec3f*);
    void createAngleMtx(MtxPtr, bool);
    void slopeTiltHead(Vec3f*);
    void fixFrontVecByGravity();
    void fixFrontVecFromUpSide();
    void fixSideVecFromFrontUp();
    void setSideVec(const Vec3f&);
    void setHeadVec(const Vec3f&);
    void setFrontVec(const Vec3f&);
    void setFrontVecKeepUp(const Vec3f&, f32);
    void setFrontVecKeepUp(const Vec3f&, u32);
    void setFrontVecKeepUp(const Vec3f&);
    void setFrontVecKeepUpAngle(const Vec3f&, f32);
    void setFrontVecKeepSide(const Vec3f&);
    void setHeadAndFrontVecFromRotate(const Vec3f&);
    void forceSetHeadVecKeepSide(const Vec3f&);
    void setGravityVec(const Vec3f&);
    void draw() const;
    void addTrans(const Vec3f&);
    void setTrans(const Vec3f&);
    bool isEnableRush() const;
    bool isForceStopRush() const;
    bool isInvincible() const;
    bool isCeiling() const;
    void inputStick();
    void updateSoundCode();
    const Vec3f& getShadowNorm() const;
    const Vec3f& getAirGravityVec() const;
    const Vec3f getAirFrontVec() const;
    const Vec3f* getGravityVec() const;
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
    void fixTransBetweenWall(const Vec3f&, const Vec3f&);
    f32 calcDistWidth();
    void checkAllWall(const Vec3f&, f32);
    void calcFrontFloor();
    const Vec3f& getWallNorm() const;
    const Vec3f& getSideWallNorm() const;
    const Vec3f& getFrontWallNorm() const;
    const Vec3f& getBackWallNorm() const;
    const Vec3f& getWallPos() const;
    const Triangle* getWallPolygon() const;
    const Triangle* getGroundPolygon() const;
    void updateFloorCode();
    void updateWallFloorCode();
    void saveLastSafetyTrans();
    void setNotSafetyTimer();
    const Vec3f* getLastSafetyTrans(Vec3f*) const;
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
    bool damageLarge(const Vec3f&);
    void decDamageAfterTimer();
    bool checkDamage();
    u16 getDamageAfterTimer() const;
    bool damageFloorCheck();
    bool damageWallCheck();
    bool damagePolygonCheck(const Triangle*);
    bool flipLarge(const Vec3f&);
    bool isEnableAddDamage() const;
    bool damage(const Vec3f&);
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

    bool doFlipWeak(const Vec3f&);
    bool faint(const Vec3f&);

    bool doFlipJump(const Vec3f&);
    bool doFlipBackRoll(const Vec3f&);
    bool doFlipLarge(const Vec3f&);
    bool doFlipRot(const Vec3f&);

    bool doFrontStep();

    void beeMarioOnGround();
    bool beeMarioOnAir();

    bool blown(const Vec3f&);

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
    void tryForceJumpDelay(const Vec3f&);
    void tryFreeJumpDelay(const Vec3f&);
    void tryForceJump(const Vec3f&, bool);
    void tryForceFreeJump(const Vec3f&);
    void tryForcePowerJump(const Vec3f&, bool);
    void tryFreeJump(const Vec3f&, bool);
    void tryWallJump(const Vec3f&, bool);
    void tryStickJump(const Vec3f&);
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
    void setRocketBooster(const Vec3f&, f32, u16);
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
    void calcShadowDir(const Vec3f&, Vec3f*);
    bool retainMoveDir(f32, f32, Vec3f*);
    void calcMoveDir(f32, f32, Vec3f*, bool);
    bool checkLockOnHoming();
    void doLockOnHoming();
    void fixPositionInTower();

    void check2DMode();
    void calcMoveDir2D(f32, f32, Vec3f*);
    void calcShadowDir2D(const Vec3f&, Vec3f*);
    void stick2Dadjust(f32&, f32&);
    void set2Dmode(bool);
    void beforeJumping2D();
    void afterLanding2D();
    void clear2DStick();

    void set25Dmode(const AreaObj*);
    void update25Dmode();
    void updateAxisFromMode(u8);
    void calcMoveDir25D(f32, f32, Vec3f*);

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

    u32 initSoundTable(const SoundList* list);
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
    bool isUseFooSpecialGravity(const Vec3f&, Vec3f*) const;
    void updateOnimasu();
    bool isHeadPushEnableArea() const;
    bool isOnimasuBinderPressSkip() const;

    void checkTornado();
    void resetTornado();
    void calcTornadoTilt();
    void reflectWallOnSpinning(const Vec3f&, u16);
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
    void setLastNonFixMoveVec(const Vec3f&);

    bool isWalling() const;
    void checkWallStick();
    u8 checkStickWallSide();
    s32 checkStickFrontBack();
    bool fixWallingPosition(bool);
    void fixWallingDir(bool);
    bool fixWallingTop();
    bool checkWallFloorCode(u16) const;
    bool checkWallCode(u16 code, bool frontBackOnly) const;
    bool checkWallCodeNorm(u16, Vec3f*, bool) const;
    void setWallCancel();
    void keepDistFrontWall();
    bool isEnableStickWall();
    bool fixWallingDist();
    bool isInhibitWall() const;
    void tryWallPunch();

    bool isHanging() const;
    bool fixHangDir(const Vec3f&, Vec3f*);
    bool isEnableBackHang();
    bool isEnableSideHang();
    void checkHang();
    bool isEnableHang();

    bool isSwimming() const;
    bool forceStartSwimAndShoot(const Vec3f&);
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
    void startStep(const Vec3f&);

    void checkBump();
    void startBump(const Vec3f&);

    void checkEnforceMove();
    void checkEnforceMoveInner();
    void recordRelativePosition();
    void invalidateRelativePosition();
    Pos3f* getMoveBaseMtx() const;
    u32 moveRelativePosition(u32);
    u32 moveRelativePositionWall();
    void recordLastGround();
    void getLastGroundPos(Vec3f*) const;
    const Vec3f* getLastGroundEdgeNrm(u32) const;
    u32 getLastGroundEdgeIndex(const Vec3f&, const Vec3f&) const;
    void pushedByReaction();
    void addReaction(const Vec3f&);
    void tryPushToVelocity();
    void push(const Vec3f&);
    void push2(const Vec3f&);
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
    bool getHitWallNorm(Vec3f*);
    void resetTeresaMode();
    void doTeresaReflection(const Vec3f&, bool);
    void startTeresaDisappear();

    void stopPunch();
    void startMagic();

    bool doObjWarp(LiveActor*);
    bool doPointWarp(const Vec3f&, const Vec3f&, s32);
    bool isVisibleRecoveryWarpBubble() const;
    void doCubeWarp();
    bool doPointWarpRecovery(const Vec3f&, const Vec3f&);

    bool isDisableStayHere() const;
    bool isDisableFpViewMode() const;
    void tryFpViewMode();

    void stick2DadjustGround(f32&, f32&);
    void calcDir2D(f32, f32, Vec3f*);

    // His movement this frame; mPrevMovementStates is last frame's.
    struct MovementStates {
        unsigned invincible : 1;  // isInvincible; nothing in the source sets it (listed)
        unsigned waitStickNeutral : 1;  // a wall or ledge move holds off the next wall stick until the stick is let go
        unsigned damaged : 1;  // a damage hit is pending (damage, a rush ending in a hit)
        unsigned sideWall : 1;  // a wall at his side (checkAllWall)
        unsigned backWall : 1;  // a wall behind him (checkAllWall)
        unsigned baseRotated : 1;  // the base he stands on turned this frame (moveRelativePosition)
        unsigned wallJumpQueued : 1;  // an A press starts the wall jump (procJump); nothing sets it (listed)
        unsigned frontLedge : 1;  // the floor past the front wall is above the hit (calcFrontFloor)
        unsigned groundMissing : 1;  // the last ground check found no ground
        unsigned jumpHeld : 1;  // the jump button is held since takeoff (procJump clears it on release)
        unsigned beeFly : 1;  // the bee is flying (beeMarioOnAir)
        unsigned turnStickNeutral : 1;  // the stick went neutral while a turn was ready (mainMove)
        unsigned tornado : 1;  // the spin-tornado jump
        unsigned digitalJump : 1;  // a fixed-height jump (isDigitalJump)
        unsigned forceGroundOnce : 1;  // the next ground check reports ground once (MarioBlown)
        unsigned spinStartWithSquat : 1;  // like squat, a hard stop starts the start spin (decideInertia); nothing sets it (listed)
        unsigned hipDrop : 1;  // the hip drop
        unsigned squat : 1;  // squatting
        unsigned wallJump : 1;  // a wall, stick or rush jump is rising (slip-up checks)
        unsigned frontWall : 1;  // a wall straight ahead (checkAllWall)
        unsigned binderHit : 1;  // the binder touched the map this frame (updateBinderInfo)
        unsigned wallSlipUp : 1;  // sliding up a wall (decideSlipUp)
        unsigned jumpChain : 1;  // the last jump can chain into the next (tryJump, JumpConnectTime)
        unsigned turnBrake : 1;  // the turn brake (mainMove)
        unsigned turning : 1;
        unsigned shadowFound : 1;  // the shadow ray found a floor (calcShadowPos)
        unsigned onGround : 1;
        unsigned jumping : 1;  // in the air
        unsigned launchKind : 2;  // 1 after a rush launch (lands on the shadow), 2 after a scenario starter (endRush)
        unsigned scaleBody : 1;  // the actor's scale goes into the base matrix (crush, black hole)
        unsigned stick25DLatched : 1;  // the 2.5D section latched the stick (update25Dmode)
        unsigned mode25D : 1;  // a 2.5D section (set25Dmode)
        unsigned frontFloorFound : 1;  // a floor to stand on past the front wall (calcFrontFloor)
        unsigned jumpBuffered : 1;  // a jump pressed in the air, taken on landing (isEnableFutureJump)
        unsigned mode2D : 1;  // a 2D section (check2DMode)
        unsigned simpleGroundCheck : 1;  // the simple ground check is in use
        unsigned onSkatableFloor : 1;
        unsigned onIce : 1;  // ice walking and inertia (decideOnIceAnimation, decideInertiaOnIce); nothing sets it (listed)
        unsigned sideWallCorner : 1;  // the side wall hit is at a corner or an edge
        unsigned frontWallGlancing : 1;  // a wall ahead, not head-on (checkAllWall)
        unsigned hangAllowed : 1;  // set on the ground, cleared under a ceiling (checkHang)
        unsigned hangBlocked : 1;  // set on the ground, cleared once rising or 20 frames in the air: no hang or wall stick yet
        unsigned forcedLaunch : 1;  // a forced rush launch or power jump
        unsigned freeJumpDelay : 1;  // a free jump waits for its frame (tryFreeJumpDelay)
        unsigned forceJumpDelay : 1;  // a forced jump waits for its frame (tryForceJumpDelay)
        unsigned blown : 1;  // blown away (blown)
        unsigned spinBlocked : 1;  // no spin until a tornado sensor or the next jump (flips, blows, warps, mud jumps)
        unsigned slipUpBlocked : 1;  // this jump can't start a wall slip-up (forced jumps; a jump begun at a wall)
        unsigned slipUpJumped : 1;  // the slip-up jump was taken; cleared on the ground
        unsigned fainted : 1;
        unsigned squatWalk : 1;  // the squat walk is moving (decideSquatWalkAnimation)
        unsigned slopeFloorFound : 1;  // a floor down the slope (checkMap)
        unsigned slopeSlide : 1;  // sliding on a slope (slopeMove)
        unsigned inputLocked : 1;  // pad input is off (checkKeyLock, warps, rush ends; isInputDisable)
        unsigned squatLocked : 1;  // no squat until the squat button is let go (after jumps)
        unsigned noDropSpeedCap : 1;  // with jump kind 0xD, no cap on the fall speed (procJump)
    };
    // Action flags kept across frames; mPrevActionStates is last frame's.
    struct ActionStates {
        unsigned jump2P : 1;  // the second player's assisted jump
        unsigned jetJumped : 1;  // the swim jet jump (MarioSwim::doJetJump)
        unsigned onimasuWrapped : 1;  // the Onimasu wrap was taken (updateOnimasu)
        unsigned steepGravityStop : 1;  // the gravity turned steeply on the ground: mainMove holds for a frame
        unsigned spinJumpLand : 1;  // a spin jump's landing may swing (doLanding)
        unsigned crushRequested : 1;  // a crush is pending (requestCrush)
        unsigned jumpOnEnforcer : 1;  // the jump started on an enforced-move floor (recordJumpEnforceMove)
        unsigned circular2D : 1;  // a circular 2D section (check2DMode)
        unsigned rushElectricDamage : 1;  // a rush ended in electric damage
        unsigned inTower : 1;  // inside a tower area (updateCubeCode)
        unsigned heavySteeringTurn : 1;  // HeavySteeringCube: the slow turn
        unsigned heavySteeringStick : 1;  // HeavySteeringCube: the stick dead zone
        unsigned baseRotatedAboutUp : 1;  // the base turned about the up axis (moveRelativePosition)
        unsigned squatEnded : 1;  // a squat or hang just ended; the wait animation restarts
        unsigned wallJumpHit : 1;  // the wall jump's hit was handled (checkWallJumpHit)
        unsigned holdButtonAfterRush : 1;  // the rush button is still held from the rush (isRequestRush, isRequestThrow)
        unsigned uTurn : 1;  // a large turn at full turn time (mainMove)
        unsigned stickWasNeutral : 1;  // the stick was neutral (retainMoveDir)
        unsigned retainInAir : 1;  // the move direction may be kept in the air (retainMoveDir)
        unsigned rollLanding : 1;  // the rush asked for a rolling landing
        unsigned swimExited : 1;  // swimming or flying just ended; cleared on the ground (checkStartSwim)
        unsigned planeCollision : 1;  // inside a PlaneCollisionCube
        unsigned groundCheckSimple : 1;  // the locked ground check is the simple one (lockGroundCheck)
        unsigned groundCheckLocked : 1;  // an owner locked the ground check (lockGroundCheck)
        unsigned gravityTurned : 1;  // the gravity changed direction (updateGravityVec, endRush; 2D clears it on the ground)
        unsigned recoveryLand : 1;  // the recovery's landing (doLanding consumes it)
        unsigned swingEffectPending : 1;  // the swing effect plays next frame (MarioEffect)
        unsigned rushFireDamage : 1;  // a rush ended in fire or acid damage
        unsigned zeroStickNextFrame : 1;  // inputStick reads a zero stick once (mainMove's move lock)
        unsigned spinHipDrop : 1;  // the spin hip drop (jumpToHipDrop)
        unsigned gravityFrontTurnPending : 1;  // the gravity turned about his front axis; HitStates gets it next frame
        unsigned gravityFlipPending : 1;  // the gravity flipped; HitStates gets it next frame
        unsigned gravityChangeSeen : 1;  // the gravity has changed since the last reset (setGravityVec)
        unsigned jumpTakeoff : 1;  // a jump has started (tryJump)
        unsigned slopePushed : 1;  // pushed while sliding (MarioActor::movement); checkGroundOnSlope passes it to HitStates
        unsigned fooFlying : 1;  // the Foo flight ended in the air (isFlying)
        unsigned remoteDemoSkipped : 1;  // the remote demo found him busy (readyRemoteDemo)
    };
    // This frame's results (cleared each frame); mPrevDrawStates is last frame's.
    struct DrawStates {
        unsigned teresaInWind : 1;  // the Boo is in wind (MarioTeresa::checkWind)
        unsigned pressed : 1;  // pressed by the map or an object (checkVerticalPress, checkSidePress)
        unsigned onWaterFloor : 1;  // on a water floor (floor codes 20-23)
        unsigned teresaDrop : 1;  // the Boo's drop (MarioTeresa::procDrop)
        unsigned keepJumpParam : 1;  // initJumpParam keeps the jump's parameters (enforced and squat jumps)
        unsigned updated : 1;  // Mario::update ran this frame (or the stun stood in for it)
        unsigned onFur : 1;  // the ground's wall code is Fur (the bee sticks)
        unsigned edgePushBack : 1;  // pushed back from a ledge with no wall behind (checkGround)
        unsigned wallSlope : 1;  // a steep wall beside the ground: mWallSlope is set (checkAllWall)
        unsigned retainMoveDirRan : 1;  // retainMoveDir ran this frame
        unsigned headHit : 1;  // the head hit a ceiling point (checkHeadPoint)
        unsigned hipDropLand : 1;  // the hip drop or slider landing (isHipDropLand)
        unsigned inShallowWater : 1;  // on a shallow-water floor (floor code 23)
        unsigned inWater : 1;  // in water (the swim's body check or a water floor)
        unsigned postureSet : 1;  // postureCtrl set the posture this frame
        unsigned wallDamageCheck : 1;  // inside damageWallCheck
        unsigned wallGravity : 1;  // mWallNorm is set: the wall's gravity applies (checkAllWall)
        unsigned moveDirKept : 1;  // the move direction is kept from the last frame (retainMoveDir)
        unsigned onSlipFloor : 1;  // the ground is a slip floor (updateGroundInfo)
        unsigned enforcedMove : 1;  // an enforced move holds him: no jumps (checkEnforceMoveInner)
        unsigned atEdge : 1;  // held back at a ledge or a danger floor's edge
        unsigned baseTransPoint : 1;  // on a base-trans point (checkBaseTransPoint)
        unsigned endTornado : 1;  // the tornado ends (procJump, jumpToHipDrop, forceStopTornado)
        unsigned stickLocked : 1;  // the stick is locked (checkKeyLock)
        unsigned binderFloorHit : 1;  // the binder touched a floor (updateBinderInfo)
        unsigned homingLockOn : 1;  // a homing target is locked (checkLockOnHoming)
        unsigned slopeRunUp : 1;  // running up a slope against it (slopeMove)
        unsigned wallJumpForbidden : 1;  // no triangle jump here (camera area 2, ForbidTriangleJumpCube)
        unsigned wallInhibited : 1;  // no wall moves here (camera area 1; isInhibitWall)
        unsigned snappedToGround : 1;  // the velocity was pulled to the ground (checkGround)
    };
    // This frame's collision, push and gravity results (cleared each frame).
    struct HitStates {
        unsigned blown : 1;  // blown this frame (MarioBlown; damageFloorCheck)
        unsigned groundEvenSlope : 1;  // three or more ground probes share the slope (checkGround)
        unsigned groundSameSensor : 1;  // a ground probe hit the ground polygon's sensor (checkGround)
        unsigned landed : 1;  // landed this frame (doLanding)
        unsigned groundAllProbes : 1;  // every ground probe hit (checkGround)
        unsigned enforcedMove : 1;  // an enforced move this frame (checkEnforceMoveInner)
        unsigned ceilingNear : 1;  // the ceiling is within 160
        unsigned safetySaved : 1;  // the last safe position was saved (saveLastSafetyTrans)
        unsigned binderPushed : 1;  // the binder pushed him off an edge or a corner (updateBinderInfo)
        unsigned slopePushed : 1;  // pushed while sliding (from ActionStates.slopePushed)
        unsigned jump2PRequest : 1;  // the second player asked for a jump
        unsigned jump2PDelayed : 1;  // the second player's delayed jump fires
        unsigned jet : 1;  // the Foo or swim jet fired
        unsigned launchedFar : 1;  // the rush launched him far (endRush)
        unsigned jumpForbidden : 1;  // inside a ForbidJumpCube
        unsigned trampleBlocked : 1;  // doTrampleJump returns at once; nothing sets it (listed)
        unsigned karikariClinging : 1;  // Karikari cling to him
        unsigned teresaAccel : 1;  // the Boo accelerates (MarioTeresa::checkAccel)
        unsigned swimLifeDrain : 1;  // the swim drains his life (MarioSwim::updateLifeByTime)
        unsigned gravityFrontTurn : 1;  // the gravity turned about his front axis
        unsigned gravityFlip : 1;  // the gravity flipped
        unsigned gravityChangeSeen : 1;  // from ActionStates.gravityChangeSeen
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

    const Vec3f& getUpVec() const {
        return mPostureUpVec;
    }

    const Vec3f& getFrontVec() const {
        return mFrontVec;
    }

    const Vec3f& getJumpVec() const {
        return mJumpVec;
    }

    inline void setJumpVec(const Vec3f& rVec) {
        mJumpVec = rVec;
    }

    inline void resetInline() {
        mCorrectionAngle = 0.0f;
        mCorrectionEaseTimer = 0;
        mCorrectionTurnTimer = 0;
    }

    inline void setAirTimer(u16 val) {
        mAirTimer = val;
    }

    inline void setDamageAfterTimer(u16 val) {
        mDamageAfterTimer = val;
    }

    MovementStates mMovementStates;
    ActionStates mActionStates;
    DrawStates mDrawStates;
    HitStates mHitStates;
    MovementStates mPrevMovementStates;
    ActionStates mPrevActionStates;
    DrawStates mPrevDrawStates;

    f32 mPrevStickPower;  // last frame's stick power (inputStick keeps it before the new one)
    static constexpr f32 kFloorDotLimit = -0.515f;  // gravity against a normal below this marks a floor (updateBinderInfo, checkGround)
    Mtx34f m2DRotateMtx;  // the 2D section's rotation, applied to its side, normal and up (stick2Dadjust)
    Vec3f mPosition;
    Vec3f mLookOfs;  // the camera's offset from the position: body and sink offsets (updateLookOfs)
    Vec3f mLookOfsBody;  // the body's offset (a wall push, the swim surface, a step down); a move cancels it (addTrans)
    Vec3f mLookOfsSink;  // the offset of sinking into sand or quicksand
    Vec3f mVelocity;
    Vec3f mPrevVelocity;  // the velocity at the frame's start (updateAndClearStrideParameter)
    Vec3f mVelocityAfter;
    Vec3f mRelativeMove;  // the moving ground's shift this frame (moveRelativePosition)
    Vec3f mAreaMoveVelocity;  // a power area's push (powerAreaMove)
    Vec3f mRailMoveVelocity;  // a power rail's push (powerRailMove)
    Vec3f mPreSlopeVelocity;  // the velocity before the slope began
    Vec3f mStickPos;
    Vec3f mWorldPadDir;
    Vec3f mAirGravityVec;
    Vec3f mHeadVec;
    Vec3f mPostureUpVec;  // the body's up, eased toward the head (postureCtrl)
    Vec3f mFrontVec;
    Vec3f mFrontOnShadow;  // the front laid on the shadow's plane (calcShadowDir)
    Vec3f mTurnFrontVec;  // the front a turn slip turns away from
    Vec3f mMoveDirOnShadow;  // the move direction laid on the shadow's plane
    Vec3f mPrevMoveDirOnShadow;  // last frame's move direction on the shadow's plane
    Vec3f mPrevTurnCross;  // last frame's front cross the move direction
    Vec3f mPrevStickDir;  // last frame's stick direction
    Vec3f mBinderHitPos;  // where the binder last hit (updateBinderInfo); the Boo's wall-hit effect
    Vec3f mBinderHitNorm;  // that hit's normal
    u8 mTurnLeft;  // the turn goes counter-clockwise about the head
    f32 mWalkSpeed;
    f32 mPrevAnimFrame;
    f32 mSlopeSideSpeed;  // a slope slide's sideways speed, eased (SlopeSideMoveInertia)
    Vec3f mSlopeSideDir;  // a slope slide's sideways direction
    Vec3f mGravityFixSideVec;  // the side kept while the head and front follow a gravity change (fixHeadFrontVecByGravity)
    Vec3f mRetainGroundNorm;  // the ground normal when the move direction was last kept
    f32 mRetainStickAngle;  // the stick's angle when the move direction was last kept
    Vec3f mSlipTurnMove;  // the last move at a turn slip, added back scaled (InertiaSlipTurn)
    Vec3f mWallSlope;  // the slope of the wall touched (checkAllWall)
    f32 mWalkingResist;  // how much a floor slows walking (setWalkingResist), 0 none
    Vec3f mJumpVec;
    Vec3f mJumpFrontVec;  // the jump's direction across gravity (initActiveJumpVec)
    Vec3f mJumpGravityVec;  // the gravity when the jump began
    Vec3f mHipDropJumpVec;  // the jump vector a hip drop keeps
    Vec3f mForceJumpVec;  // a forced jump's vector, taken a frame later (tryForceJumpDelay)
    Vec3f mSideVec;
    Vec3f mShadowPos;
    Vec3f mMoveFrontVec;  // this frame's move front (the kept one, or the front)
    Vec3f mPrevMoveFrontVec;  // the move front kept for the next frame
    Vec3f mFixSideVec;  // the side kept while the front follows a gravity change (fixFrontVecByGravity)
    Vec3f mPushVec;  // pushes this frame (push, wind): taken into the jump or velocity (tryPushToVelocity)
    Vec3f mPushVelocity;  // pushes added to the velocity as they are (push2)
    Vec3f mGroundNorm;  // the ground's normal (setGroundNorm)
    Vec3f mGroundDown;  // the ground normal's opposite
    Vec3f mWallNorm;  // the touched wall's normal (checkAllWall)
    Vec3f mWallGravityVec;  // the wall normal's opposite, the gravity while swimming at a wall
    Vec3f mMoveUpVec;  // the up the stick's screen direction is read against, eased (calcMoveDir)
    Vec3f mCeilNorm;  // the normals of ceilings touched, summed and normalised (isCeiling)
    Vec3f mCeilDistNorm;  // the nearest ceiling's normal (calcDistToCeil); the Boo's wall bounce reads it
    u16 mAirTimer;  // frames off the ground in a jump or hip drop
    u16 mJumpTimer;  // frames since the jump began (the trample-top open time)
    u16 mDropWaitTimer;  // frames left of the drop wait (DropWaitTime)
    u16 mSlopeTimer;  // frames of a slope slide (mMovementStates.slopeSlide)
    u16 mSlopeSteerInhibitTimer;  // frames the stick doesn't steer a slope slide
    u16 mEdgePushTimer;  // frames pushed back from a ledge (mDrawStates.edgePushBack); past 8 the push goes sideways
    u16 mJump2PDelayTimer;  // frames before a second player's jump request is taken
    u16 mBackJumpTimer;  // frames into a jump, for the back-jump limit (BackJumpLimitFrame)
    u16 mBackAirWalkTimer;  // frames left of a backward air walk
    u16 mLandTimer;  // frames since landing (JumpConnectTime)
    u16 mTurnSlipTimer;  // frames left of a turn slip (TurnSlipTime)
    u16 mTurnReadyTimer;  // frames left to start a turn jump (TurnReadyTime)
    u16 mWeakTurnTimer;  // frames of a weak turn (WeakTurnTime)
    Vec3f mTurnCross;  // the last turn's axis: the front cross the stick direction
    Vec3f mTurnSlipFrontVec;  // the front when the turn slip began
    f32 mHandScaleX;  // the hand's x scale (MarioAnimator::setHand); resetTornado sets 1
    f32 mSpinInertiaBlend;  // blend toward the start-spin inertia
    u16 mReflectSlipTimer;  // frames of slip after a spin hit a wall
    u16 mStartSpinTimer;  // frames left of the start spin (StartSpinTime)
    u16 mSlopeDashTimer;  // frames of a slope dash (SlopeDashAccelTime)
    u16 mAirWalkTimer;  // frames left of the air walk after a spin (AirWalkTime); the bee's flight
    u16 mSlowStartTimer;  // frames left of the slow start (SlowStartTime)
    u16 mBeeGravityTimer;  // frames of the bee's weak gravity (BeeGravityPowerTime)
    u16 mWeakStickIgnoreTimer;  // frames a weak forward stick is ignored after a turn
    u16 mMoveDirRetainTimer;  // frames the move direction is kept after the stick moved
    u16 mLedgeFallTimer;  // frames since slipping off a ledge hold
    u16 mMorphResetTimer;
    u16 mTeresaWallThroughTimer;  // frames left of the Boo's pass through a wall (TeresaWallThroughTime)
    u16 mSlipGraceTimer;  // frames on a steep floor before it slips
    u16 mDamageAfterTimer;  // frames left after damage (decDamageAfterTimer)
    u16 mMoveLockTimer;  // frames movement is held (a throw, a recovery, a warp)
    u16 mHipDropStillTimer;  // frames a hip drop hasn't moved; at 15 it ends
    u16 mZeroGravityTimer;  // frames of no gravity (a tornado jump, a hip drop's start)
    u16 mTornadoBoostTimer;  // frames left of the tornado jump's boost (TornadoBoostTimer)
    u16 mAirControlInhibitTimer;  // frames of a jump with no air control and no spin
    u16 mInvincibleJump;  // the jump began invincible at a run
    u16 mFireShotCount;  // fireballs or freeze shots this jump (3 at most)
    u32 mJumpKind;  // 0 to 2 the triple jump's steps; 8 tornado; 0xB trample top; 0xC, 0xD the special jumps
    u16 mItemDashTimer;  // frames left of an item or special dash (ItemDashTimer)
    u16 mDashChanceTimer;  // frames to take a special dash (noticeDashChance)
    u16 mSlipInhibitTimer;  // frames the ground isn't marked slippery after leaving water
    Vec3f mPrevFrontVec;  // the front at the frame's start
    Vec3f mRocketBoostVec;  // a rocket booster's push, added to the jump and decayed (setRocketBooster)
    f32 mRocketBoostDecay;  // the booster push kept each frame
    TriangleFilterDelegator< Mario >* mShadowFilter;  // which polygons the shadow ray ignores
    Triangle* mShadowPolygon;  // the polygon the shadow ray hits (below along gravity)
    Triangle* mSlopeFloorPolygon;  // on a slope: the floor below a point ahead along the ground normal
    Triangle* mGroundPolygon;
    Triangle* mLastGroundPolygon;  // the ground when it was last recorded (recordLastGround)
    Triangle* mTmpPolygon;
    Triangle* mFrontFloorPolygon;  // the floor ahead (calcFrontFloor)
    Triangle* mPressFloorPolygon;  // the floor when pressed (setPress)
    Triangle* mPressCeilPolygon;  // the ceiling when pressed (setPress)
    f32 mVerticalSpeed;
    Vec3f mGroundPos;
    Vec3f mSlopeFloorPos;  // where that slope ray hit
    Vec3f mFrontFloorPos;  // the floor's edge ahead, where a hang takes him (calcFrontFloor)
    Vec3f mJumpStartPos;  // where the jump began (landing compares the height)
    Vec3f mLastGroundLocalPos;  // the last ground position in that polygon's own space (recordLastGround)
    Triangle* mCeilPolygon;  // the ceiling (calcDistToCeil)
    Triangle* mFrontWallTriangle;
    Triangle* mBackWallTriangle;
    Triangle* mSideWallTriangle;
    Triangle* mStepPolygon;  // a wall low enough to step onto (checkAllWall)
    Triangle* mBumpPolygon;  // a wall to bump against (checkAllWall)
    f32 mFrontFloorHeight;  // the floor ahead's height above the wall's foot (calcFrontFloor), 400 none
    f32 mFrontWallDist;  // the distance to the wall ahead
    Vec3f mFrontWallPos;  // where the front wall was hit (checkAllWall)
    Vec3f mBackWallPos;  // where the back wall was hit

    Vec3f mSideWallPos;  // where the side wall was hit

    Vec3f mStepHitPos;  // where the step wall was hit
    Vec3f mBumpHitPos;  // where the bump wall was hit
    f32 mPrevStickAngle;  // last frame's stick angle
    f32 mStickAngle;  // the stick's angle (inputStick)
    f32 mYAngleOffset;
    u16 mTornadoTimer;  // frames of the tornado spin (TornadoTimeAir)
    f32 mTornadoTilt;  // the tornado's tilt, eased (TornadoTiltNear)
    Vec3f mTornadoTiltUp;  // the tornado's tilted up, eased toward the head
    u32 mJumpFrame;  // the actor's frame count when the jump left the ground (procJump); a wall jump within 6


    AreaObj* mArrivedWarpCube;  // the warp cube he came out of: entering it doesn't warp back (doCubeWarp)
    void* mGroundCheckOwner;  // who locked the ground check (lockGroundCheck)
    MR::InlineVector< Triangle, 32 > mTouchedWalls;  // walls touched this frame
    const HitSensor* mOnimasuSensor;  // the nearest Onimasu block's sensor (checkOnimasu)
    Vec3f mOnimasuLocalPos;  // his position in that block's space (updateOnimasu)
    u8 mOnimasuFound;  // a block was found this frame
    bool mCameraTurnHold;  // the move direction holds through a camera turn (45 degrees with the stick held), a bump or a climb
    u8 m2DStickLockMode;  // which 2D stick lock is on (stick2Dadjust)
    u8 m2DStickLocked;  // a 2D stick lock was taken
    u8 m2DLockX;  // the 2D stick's x is locked
    u8 m2DLockY;  // the 2D stick's y is locked
    f32 m2DStickX;  // the 2D stick's x after adjustment (set2Dmode reads its sign)
    f32 m2DLockStickX;  // the stick x when the lock began
    f32 m2DLockStickY;  // the stick y when the lock began
    bool m2DUpside;  // the 2D section's side (set2Dmode's mode)

    Vec3f mSide2D;
    Vec3f mUp2D;
    Vec3f mNormal2D;
    Vec3f m2DGroundNorm;  // the ground normal in the 2D plane
    bool m2DStickHold;  // the held 2D stick is kept while the stick doesn't turn back
    Vec3f m2DPrevStick;  // the 2D stick when the hold began
    Vec3f m2DHeldStick;  // the 2D stick being held

    Vec3f m2DPlanePos;  // a point on the 2D plane (writeBackPhyisicalVector)
    Vec3f m2DCubePos;  // the 2D area cube's position (check2DMode)
    Vec3f m2DPlaneNormal;  // the 2D plane's normal
    u8 m25DMode;  // the 2.5D axis mode (update25Dmode)
    Vec3f m25DSideVec;  // the 2.5D stick's x axis (updateAxisFromMode)
    Vec3f m25DDownVec;  // the 2.5D stick's y axis, negated
    Vec2f m25DStick;  // the stick when the 2.5D mode last changed
    Vec3f m25DNormal;  // the 2.5D plane's normal
    Vec3f m25DUp;  // the 2.5D plane's up (set25Dmode)
    Vec3f m25DAcross;  // the 2.5D plane's normal cross its up
    Vec3f mTowerCenter;  // the tower's centre he walks around (fixPositionInTower)
    Vec3f mTowerAxis;  // the tower's axis (fixPositionInTower, calcMoveDir)
    Vec3f mHipDropSlideGravity;  // the gravity when the hip-drop slide began (taskOnHipDropSlide)
    f32 mTowerRadius;  // the tower's radius (updateCubeCode)
    u8 mTargetWalkSpeedIndex;
    u8 mPrevWalkSpeedIndex;  // mTargetWalkSpeedIndex as of the animator's last update (MarioAnimator::update)
    u8 mBrakeFirstTimer;  // frames left of the first brake (BrakeFirstTimer)
    u8 mBrakeTimer;  // frames left of the brake animation (updateBrakeAnimation)
    const char* mHipDropStartAnim;  // the hip drop's start animation for this form (jumpToHipDrop)
    const char* mHipDropAnim;  // the hip drop's fall animation
    const char* mHipDropLandAnim;  // the hip drop's landing animation
    f32 mCeilDist;  // the distance to the ceiling (calcDistToCeil); under 200 he is pressed
    HitSensor* mCeilSensor;  // the ceiling's sensor (calcDistToCeil)
    u8 mIceAnimFoot;
    u8 mSinkTimer;
    f32 mRippleHeight;  // the water ripples' height above the ground
    Vec3f mRippleRotate;  // the water ripples' rotation
    u8 mPoisonTimer;
    f32 mCorrectionAngle;  // the draw's turn about the head (createCorrectionMtx)
    u32 mCorrectionEaseTimer;  // frames left easing that turn back to 0
    u32 mCorrectionTurnTimer;  // frames left easing that turn to half a circle
    MarioWall* mWall;
    Vec3f mWallTopHead;  // the head on a wall's top (fixWallingTop)
    MarioStick* mStick;
    u16 mBeeGravityReviveTimer;  // frames left of the bee's returning gravity (BeeGravityReviveTime)
    f32 mBeeUpDownSpeed;  // the bee's vertical speed, eased (BeeAccelRatio, BeeUpDownKiller)
    u16 mBeeFlyInhibitTimer;  // frames the bee can't fly after a rush
    MarioRabbit* mRabbit;
    MarioFoo* mFoo;
    MarioSukekiyo* mSukekiyo;
    MarioBury* mBury;
    MarioWait* mWait;
    MarioClimb* mClimb;
    Vec3f mBumpGravityVec;  // the gravity a bump uses (startBump)
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
    Vec3f mDamageDir;  // the last damage's direction (damage), taken into the swim's damage
    u16 mKarikariTimer;  // frames left of the Karikari swarm's damage hold (checkKarikariDamage)
    Vec3f mSafetyPos;  // a safe spot being proven: the ground's centre leaned toward him (saveLastSafetyTrans)
    Triangle* mSafetyPolygon;  // that spot's polygon
    Mtx34f mSafetyBaseMtx;  // that polygon's base matrix then
    Vec3f mLastSafetyPos;  // the last proven safe spot (getLastSafetyTrans)
    Triangle* mLastSafetyPolygon;  // its polygon
    Mtx34f mLastSafetyBaseMtx;  // its polygon's base matrix then
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
    bool mFpViewChangeFailed;  // the first-person view couldn't open this frame (isFpViewChangingFailure)
    MarioFpView* mFpView;
    MarioMove* mMove;
    Vec3f mRelativePos;  // his position in the ground polygon's space (recordRelativePosition)
    Vec3f mRelativeWallPos[2];  // his position in each wall's space (recordRelativePosition)
    Triangle* mRelativePolygon;  // the polygon he moves with
    Triangle* mRelativeWallPolygons[2];  // the walls he moves with
    HitSensor* mRelativeSensor;  // the moving ground's sensor (moveRelativePosition)
    HitSensor* mJumpEnforceSensor;  // the sensor the jump took its push from (recordJumpEnforceMove)
    Vec3f mEnforceJumpVec;  // the ground's push added to the jump (doEnforceJump)
    Triangle* mJumpEnforcePolygon;  // the polygon the jump took its push from

    f32 mSlipPower;  // how far into a slip, 0 to 10 (slopeMove)

    f32 mSlopeDashSpeed;  // a slope dash's speed factor (SlopeDashSpeedFactor)

    Vec3f mSlopeMoveVec;  // the slope's push this frame (slopeMove)
    Vec3f mSlopeDownDir;  // down the slope, across gravity

    Vec3f mSlopeSlideVec;  // a slope slide's move, eased (moveSlopeSlide)
    Vec3f mWindVec;  // the wind on him (pushedByWind)
    Vec3f mReactionVec;  // reactions added (addReaction), decayed each frame
    static constexpr f32 kReactionDecay = 0.8f;  // the reaction kept each frame (pushedByReaction)
    Vec3f mPrevMtxUp;  // last frame's draw up (createMtxDir eases within a limit)
    Vec3f mPrevMtxFront;  // last frame's draw front
    Vec3f mPrevMtxSide;  // last frame's draw side
    FloorCode* mFloorCodeReader;  // reads polygons' floor and wall codes
    u16 mFloorCode;  // the ground's code as he uses it (a steep floor reads slip)
    u16 mPolygonFloorCode;  // the ground polygon's own code (updateFloorCode)
    u16 mWallFloorCodes[3];  // the front, back and side walls' codes
    u16 mNotSafetyTimer;  // frames the spot isn't taken as safe (setNotSafetyTimer)

    MR::NameIndex mSounds;  // a sound's name to its index in soundlist
    const char* mRunningBas;  // the sound animation running (isRunningBas)
    bool mRunningDamageWait;  // mRunningBas is "DamageWait": set with it in startBas
    MarioModuleTask* mTaskHead;  // the first module task (execTask, delTask)


    MarioState* mCurrentState;  // the state on top (changeStatus); each state holds the one under it
    MarioState* mNoticedState;  // the state last asked for (changeStatus; MarioState::getNoticedStatus)

    Task mExtraTasks[0xb];  // the extra tasks called each frame (callExtraTasks)
    u32 mExtraTaskFlags[11];  // which call kinds each extra task runs for
    u16 mTornadoCenteringTimer;  // frames left pulling a tornado jump to its centre
    HitSensor* mTornadoCenteringSensor;  // the tornado jump's centre
    u16 mSlideTimer;  // frames left of a slide task (taskOnSlide)
    Vec3f mSlideVec;  // the slide's move
    Vec3f mHipDropSlideSensorStart;  // where the followed sensor was when the slide began
    Vec3f mHipDropSlideSide;  // the hip-drop slide's side
    f32 mHipDropSlideSpeed;  // the hip-drop slide's speed
    HitSensor* mHipDropSlideSensor;  // what the hip-drop slide follows
    u8 mIndexedAnimeOn[MarioIndexedAnime_Count];  // set by MarioModule::changeAnimation(name, anime), cleared once it stops
};
