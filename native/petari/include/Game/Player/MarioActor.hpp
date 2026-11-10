#pragma once

#include "Game/Render/Model.hpp"
#include "Game/Player/MarioModelTint.hpp"
#include "Game/LiveActor/Nerve.hpp"
#include "Game/Player/Mario.hpp"
#include "Game/Player/MatrixControl.hpp"
#include "Game/Util/Array.hpp"
#include "Game/Util/Color.hpp"
#include "Game/Util/MapUtil.hpp"
#include "Game/Util/NameIndex.hpp"

class BlackHole;
class CollisionShadow;
class DrawAdaptor;
class FixedPosition;
class FootPrint;
class FurMulti;
class IceStep;
class FireMarioBall;
class Clip;
class Model;
class ModelData;
class PlayerModel;
class SoundAudible;
class GameTexture;
class JetTurtleShadow;
class MarioAnimator;
class MarioConst;
class MarioEffect;
class CollectCounter;
class MarioMessenger;
class MarioNullBck;
class MarioSearchLight;
class MarioParts;
class ModelHolder;
class ModelObj;
class MultiEmitter;
class RushEndInfo;
class TornadoMario;
class Triangle;
class XanimePlayer;
class XanimeResourceTable;
class XjointTransform;
struct StateListHolder;
struct ResTIMG;
struct SmokeEffectEntry;
class MatrixControl;

// What CameraTargetObj::isCameraStateOn asks the player.
enum SPECIAL_STATUS_FOR_CAMERA {
    CameraStatus_FireDance,
    CameraStatus_HeliJump,  // the helicopter jump off a trampled sensor of type 0x1F (jump kind 11)
};

enum PlayerMode {
    PlayerMode_Normal,
    PlayerMode_Invincible,
    PlayerMode_Fire,
    PlayerMode_Ice,
    PlayerMode_Bee,
    PlayerMode_Hopper,
    PlayerMode_Teresa,
    PlayerMode_Foo,
    PlayerMode_Tornado = 9,  // no mode is 8
};

extern bool gIsLuigi;  // set by MarioActor::init2 from MR::isPlayerLuigi

class MarioActor : public LiveActor {
public:
    MarioActor(const char*);

    virtual ~MarioActor();
    virtual void init(const TableIter&);
    virtual void initAfterPlacement();
    virtual void movement();
    virtual void draw() const;
    virtual void calcAnim();
    virtual void calcViewAndEntry();
    virtual void control();
    virtual void calcAndSetBaseMtx();
    virtual void updateHitSensor(HitSensor*);
    virtual void attackSensor(HitSensor*, HitSensor*);
    virtual bool receiveMsgPush(HitSensor*, HitSensor*);
    virtual bool receiveMsgEnemyAttack(u32, HitSensor*, HitSensor*);
    virtual bool receiveMsgTaken(HitSensor*, HitSensor*);
    virtual bool receiveOtherMsg(u32, HitSensor*, HitSensor*);
    virtual const Vec3f& getLastMove() const;
    virtual void getLastMove(Vec3f*) const;
    virtual void getFrontVec(Vec3f*) const;

    void init2(const Vec3f&, const Vec3f&, s32);
    void initAfterOpeningDemo();
    void calcBaseFrontVec(const Vec3f&);
    void playSound(const char*, s32);
    void changeAnimation(const char*, const char* = nullptr);
    void changeAnimationNonStop(const char*);
    void changeAnimationUpper(const char*);
    void stopAnimation(const char*);
    bool isAnimationRun(const char*) const;
    void changeNullAnimation(const char*, s8);
    void clearNullAnimation(s8);
    bool isStopNullAnimation() const;
    void changeGameOverAnimation();
    XjointTransform* getJointCtrl(const char*) const;
    void updateRotationInfo();
    void exeWait();
    // void exeGameOverBlackHole();
    void control2();
    void controlMain();
    void updateBehavior();
    void updateBindRatio();
    bool isInPunchTimerRange() const;
    void updatePunching();
    bool doRush();
    void updateSwingTimer();
    void updateSwingAction();
    bool checkClapCatchStart();
    void updateRealMtx();
    void setUpperRotateY(f32);
    void decLife(u16);
    void decLifeLarge();
    void decLifeMiddle();
    void decLifeSmall();
    void resetWaterLife();
    void updateLife();
    void incLife(u32);
    void changeMaxLife(s32);
    bool doPressing();
    void careMovingObject();
    bool doStun();
    void scaleMtx(MtxPtr);
    void updateBaseScaleMtx();
    void getRealMtx(MtxPtr, const char*) const;
    void getRealPos(const char*, Vec3f*) const;
    MtxPtr getGlobalJointMtx(const char*);
    void calcAnimInMovement();
    void forceSetBaseMtx(MtxPtr);
    void setBlendMtxTimer(u16);
    void getGroundPos(Vec3f* dst) const;
    Vec3f* getShadowPos() const;

    bool isTurning() const;
    bool isJumping() const;
    bool isJumpRising() const;
    bool isPunching() const;
    bool isItemSwinging() const;
    bool isDamaging() const;
    bool isNeedDamageFog() const;
    bool isStaggering() const;
    bool isSleeping() const;
    bool isRefuseTalk() const;

    void jumpHop();
    void calcCenterPos();
    void calcHeadPos();
    void setPress(u8, s32);
    XanimeResourceTable* getResourceTable() const;

    bool isEnableMoveMario() const;
    bool isEnableNerveChange() const;

    void forceGameOver();
    void forceGameOverAbyss();
    void forceGameOverBlackHole();
    void forceGameOverNonStop();
    void forceGameOverSink();

    void updateCameraInfo();
    bool binderFilter(const Triangle*);

    void setPunchHitTimer(u8);
    void initEffect();
    void initMaterialEffect();
    void initCommonEffect();

    void initMember();

    void initDrawAndModel();
    void initBeeMario();
    void initTeresaMario();
    void initHopperMario();
    void initIceMario();
    void initInvincibleMario();
    void initTeresaMarioAnimation();
    void initTornadoMario();
    void initBoneMario();
    void swapTextureInit();
    void initBlur();
    void changeDisplayMode(u8);
    void initModPowerUpModels();
    bool isAllHidden() const;
    void calcViewMainModel();
    void initFace();
    void updateFace();
    void drawIndirect() const;
    void drawIndirectModel() const;
    void drawReflectModel() const;
    void drawModelBlur() const;
    void drawSearchLight() const;
    void drawPreWipe() const;
    void initSearchLight();
    void initThrowing();
    void drawMarioModel() const;

    // Called by drawMarioModel
    void calcViewSearchLight();

    void changeTeresaAnimation(const char*, s32);

    MultiEmitter* playEffect(const char*);
    void playEffectTrans(const char*, const Vec3f&);
    MultiEmitter* playMaterialEffect(const char*);
    MultiEmitter* playCommonEffect(const char*);
    void stopEffect(const char*);
    void stopMaterialEffect(const char*);
    void stopCommonEffect(const char*);
    bool isCommonEffect(const char*) const;
    bool isMaterialEffect(const char*) const;
    s32 getFloorMaterialIndex(u32) const;

    void updateSearchLight();
    void updateThrowing();
    void updateEffect();
    bool checkEffectWaterColumn();

    void bodyClap();
    void playEffectRT(const char*, const Vec3f&, const Vec3f&);
    void playEffectRTZ(const char*, const Vec3f&, const Vec3f&);
    void playEffectRTW(const char*, const Vec3f&, const Vec3f&);
    void playEffectSRT(const char*, f32, const Vec3f&, const Vec3f&);
    void emitEffectWaterColumn(const Vec3f&, const Vec3f&);
    void endRush(const RushEndInfo*);
    void stopSpinTicoEffect(bool);
    void stopEffectForce(const char*);
    void initBlackHoleOut();  // void ?
    void exeGameOverBlackHole2();
    void initBlink();
    void updateBlink();
    void setBlink(const char*);

    void entryWallWalkMode(const Vec3f&, const Vec3f&);

    HitSensor* getLookTargetSensor() const;
    PlayerModel* getModel() const;
    ModelData* getModelData() const;
    PlayerModel* getSimpleModel() const;
    void swapTexture(const char*, u8) const;
    void copyMaterial(Model*, u32, s32);

    void settingRush();

    void resetCondition();
    bool isFixJumpRushSensor(const HitSensor*) const;
    bool isLandEffectRushSensor(const HitSensor*) const;
    void beginRush();

    bool takeSensor(HitSensor*);

    // Defined in MarioActorPunch
    bool isEnableSpinPunch();
    void reactionPunch(HitSensor*);
    bool sendPunch(HitSensor*, bool);
    bool doFreezeAttack(HitSensor*);
    bool trySpinPunch();
    void printHitMark(HitSensor*);
    bool tryPunchAirAfter(HitSensor*);
    bool sendBodyAttack(HitSensor*);
    void doFreezeAttack();

    // Defined in MarioActorCamera
    bool isFastRise() const;
    bool isLongDrop() const;
    bool isFastDrop() const;
    u32 getSpecialMode() const;
    bool isCameraStateOn(SPECIAL_STATUS_FOR_CAMERA) const;
    void getSideVec(Vec3f*) const;
    void getUpVec(Vec3f*) const;
    const Vec3f& getTransForCamera() const;
    void updateTransForCamera();
    void updateForCamera();

    // Defined in MarioActorGameOver
    void forceKill(u32);
    void exeGameOver();
    void exeGameOverAbyss();
    void exeGameOverFire();
    void exeGameOverSink();
    void exeGameOverNonStop();
    void exeTimeWait();

    // Defined in MarioActorGravity
    const Vec3f& getGravityVec() const;
    const Vec3f& getGravityVector() const;
    const Vec3f& getAirGravityVec() const;
    void getGravityVector(Vec3f*) const;
    f32 getGravityRatio() const;
    GravityInfo* getGravityInfo() const;
    u8 getGravityLevel() const;
    bool checkBeeWallStick(Vec3f&);
    bool checkBeeFloorStick(Vec3f&);
    void syncJumpBeeStickMode();
    void updateBeeModeGravity(Vec3f&);
    bool isInZeroGravitySpot() const;
    void updateGravityVec(bool, bool);
    bool checkBeeCeilStick(Vec3f&);
    void updateBeeStickMode(Vec3f&);

    // Defined in MarioActorHand
    void initHand();
    void updateHand();
    void changeHandMaterial();
    void updateHandAtMorph();
    void drawHand() const;
    void drawIndirectHand() const;

    // Defined in MarioActorPad
    void resetPadSwing();
    bool isRequestHipDrop() const;
    bool isRequestRush() const;
    bool isRequestThrow() const;
    bool isRequestSpin() const;
    bool isRequestJump() const;
    bool isRequestJump2P() const;
    bool isKeepJump() const;
    bool isKeepJump2P() const;
    bool checkButtonType(u16, bool) const;
    void getStickValue(f32*, f32*);
    void updateActionTrigger();
    void updateControllerSwing();
    void updateControllerSwingLeft();
    void lockOnPointer();
    bool isRequestSpinJump2P() const;

    // Defined in MarioActorParts
    void init2D();
    void initParts();
    void updateBeeWingAnimation();
    void updateTornado();
    void updateTakingPosition();
    const HitSensor* getCarrySensor() const;
    void changeSpecialModeAnimation(const char*);
    void updateSpecialModeAnimation();
    void initFireBall();
    void shootFireBall();
    void showFreezeModel();
    void hideFreezeModel();
    void updateFairyStar();
    void update2D();
    void updateThrowVector();
    void createIceFloor(const Vec3f&);
    void createIceFloor(const Vec3f&, const Vec3f&);
    void createIceWall(const Vec3f&, const Vec3f&);
    void updateBaseMtxTeresa(MtxPtr);
    bool finalizeFreezeModel();
    void offTakingFlag();

    // Defined in MarioActorSensor
    void setupSensors();
    void doTrampleJump(HitSensor*);
    void trampleJump(f32, f32);
    bool sendMsgToSensor(HitSensor*, u32);
    void resetSensorCount();
    void recordScoutingObject(HitSensor*);
    void updateScouter();
    void initScouter();
    void initForJump();

    // Defined in MarioActorShadow
    void initShadow();
    void calcViewReflectionModel();
    void calcViewWallShadowModel();
    void drawShadow() const;
    void decideShadowMode();
    void calcViewSilhouetteModel();
    f32 calcCylinderToCenter(const AreaObj*, Vec3f*);
    void calcViewBlurModel();
    void calcViewFootPrint();
    void drawSilhouette() const;

    // Defined in MarioActorSpecialDraw
    void initScreenBox();
    bool isUseScreenBox() const;
    void calcScreenBoxRange();
    void captureScreenBox() const;
    void writeBackScreenBox() const;
    void calc1stPersonView();
    void hideBeeFur();
    void calcFogLighting();
    void resetFog();
    void updateAlphaList(u8);
    void updateSimpleAlphaList(u8);
    void updateReflectAlphaList(u8);
    void updateLightList(const Color8&, const Color8&, const Color8&, f32);
    void createRainbowMesh();
    void drawScreenBlend() const;
    void updateRandomTexture(f32);
    void drawWallShade(const Vec3f&, const Vec3f&, f32) const;
    void drawSpinInhibit() const;
    void drawColdWaterDamage() const;
    void setRasterScroll(s32, s32, s32);
    void updateRasterScroll();
    void drawRasterScroll(f32, s16, f32) const;
    void drawMosaic() const;
    void drawLifeUp() const;
    void calcSpinEffect();
    void drawSpinEffect() const;
    void drawSphereMask() const;
    void initDarkMask();
    void updateDarkMask(u16);
    void showBeeFur();
    bool drawDarkMask() const;

    // Defined in MarioActorMatrix
    void initActionMatrix();
    bool isActionOk(const char*) const;
    u8 selectAction(const char*) const;
    bool selectAutoBind(const char*, u8*) const;
    bool selectCustomEffectSpinHitSound(const char*) const;
    bool selectSpinCatchInRush(const char*) const;
    bool selectHomingInSuperHipDrop(const char*) const;
    bool selectNotHomingSensor(const HitSensor*) const;
    bool selectInvalidMovingCollision(const char*) const;
    bool selectQuickResetBeeWallGravity(const char*) const;
    bool selectJumpRushSensor(const char*) const;
    bool selectDamageFireColor(const char*) const;
    bool selectWaterInOutEffect(const char*) const;
    bool selectOnDamageRush(const char*) const;
    bool selectTiltPress(const HitSensor*) const;
    bool selectHandyRush(const HitSensor*) const;
    bool selectRecoverFlyMeter(const HitSensor*) const;
    bool selectHideFlyMeter(const HitSensor*) const;
    bool selectTeresaThru(const HitSensor*) const;
    bool selectRebindTimer(const HitSensor*) const;
    bool selectHipDropRush(const HitSensor*) const;
    bool selectPushOff(const HitSensor*) const;
    bool selectWaterInOutRush(const HitSensor*) const;
    bool selectLandEffect(const HitSensor*) const;
    bool selectNoShadow(const HitSensor*) const;
    bool selectDamagePop(const HitSensor*) const;
    f32 getFaceLookHeight(const char*) const;

    // Defined in MarioActorMorph
    void setPlayerMode(u16, bool);
    void resetPlayerModeOnDamage();
    void resetPlayerModeOnNoDamage();
    void updatePlayerMode();
    void touchSensor(HitSensor*);
    void initMorphStringTable();
    const char* changeMorphString(const char*) const;

    // Defined in MarioActorOffensiveMsg
    void attackOrPushSensor(HitSensor*, f32);
    void attackOrPushSensorInDamage(HitSensor*, f32);
    void attackOrPushSensorInRush(HitSensor*, f32);
    void tryAddClapCoin(HitSensor*);
    bool tryTornadoAttack(HitSensor*);
    bool isUnderTarget(HitSensor*);
    bool tryHipDropAttack(HitSensor*);
    bool checkAndTryTrampleAttack(HitSensor*, f32, bool);
    bool tryTrampleAttack(HitSensor*);
    bool cylinderHorizontalCheck(HitSensor*);
    bool tryJetAttack(HitSensor*);
    void tryCounterJetAttack(HitSensor*);
    bool tryGetItem(HitSensor*);
    bool cylinderPushCheck(HitSensor* pSensor, f32 radius, f32 width, f32 height);
    bool cylinderPushCheck(const Vec3f&, f32, f32, f32);
    void attackOrPushPolygons();
    void sendWallTouch(HitSensor*, HitSensor*);
    bool sendMsgUpperPunch(HitSensor*);

    // Defined in MarioActorDefensiveMsg
    bool tryAttackMsg(u32, const HitSensor*, bool*);
    bool tryVectorAttackMsg(u32, const Vec3f&);
    bool cylinderHitCheck(const HitSensor*, Vec3f*);

    // Defined in MarioActorRushMsg
    void addRushSensor(HitSensor*, bool);
    bool tryStandardRush();
    void checkPriorRushTarget();
    HitSensor* getNearestRushTarget(bool) const;
    HitSensor* getNearestJumpTarget() const;
    HitSensor* getHighPriorityTarget() const;
    HitSensor* getRescueTarget() const;
    bool tryStartRush(bool);
    bool tryJumpRush();
    void tryRushInRush();

    // Defined in MarioActorTakeMsg
    void memorizeSensorThrow(HitSensor*);
    bool tryThrow();
    void tryReleaseDirect(const HitSensor*);
    void damageDropThrowMemoSensor();
    void rushDropThrowMemoSensor();
    void trySetLockOnTarget(HitSensor*);
    void tryCoinPull();
    void tryCoinPullInRush();
    void flushCoinPull();
    bool tryCoinPullOne(HitSensor*);
    void tryPullTrans(Vec3f*, const Vec3f&);
    bool releaseThrowMemoSensor();
    void tryReleaseWithMsg(u32);
    void tryTornadoPull(HitSensor*);
    void tryReleaseBombTeresa();

    void runTeresaBaseAnimation();
    void updateTeresaAnimation();
    void getThrowVec(Vec3f*) const;

    const MarioConst& getConst() const {
        return *mConst;
    }

    inline u32 getHealth() const {
        return mHealth;
    }

    inline void setAnimationFrozen(bool frozen) {
        mAnimationFrozen = frozen;
    }
    inline const Mario::MovementStates& getMovementStates() const {
        return mMario->mMovementStates;
    }

    inline const Mario::DrawStates& getDrawStates() const {
        return mMario->mDrawStates;
    }

    inline const Mario::DrawStates& getPrevDrawStates() const {
        return mMario->mPrevDrawStates;
    }

    inline bool IsMarioSwimming() const {
        return mMario->isSwimming();
    }

    inline void MarioStopWalk() const {
        mMario->stopWalk();
    }

    inline const bool isBeeMarioOnFur() const {
        return mMario->getMovementStates().onGround && MR::getWallCodeIndex(mMario->getGroundPolygon()) == CollisionWallCode_Fur && mBeeWallWalk == 0;
    }

    inline const bool isHidden() const {
        return mHidden || mHiddenByDemo;
    }

    /// @brief Whether this stage loaded what the player mode needs: a level brings a power-up's model with the item that gives it.
    bool canTakePowerUp(s32 playerMode) const;

    inline Mario* getMario() const {
        return mMario;
    }

    inline bool isBeeWallWalk() const {
        return mBeeWallWalk != 0;
    }

    u8 mInitializing;  // true from init2's start to its end (calcCenterPos waits)
    MarioModelTint mAlphaTint;
    static const u32 cBlurPoseNum = 8;  // afterimages in the blur trail, one taken every 3 updates; also its tint levels
    MarioModelTint mGhostTints[cBlurPoseNum * cBlurPoseNum];  // [age][pose slot] (createRainbowMesh)
    MarioModelTint mLightTint;
    u8 mFpViewHidden;  // the first-person view hides the model (calc1stPersonView)
    f32 mFogRate;  // the fog's strength for updateLightList (calcFogLighting)
    u16 mFogWaveTimer;  // frames for the damage fog's wave
    u16 mFlashFogTimer;  // frames left of the spin's flash fog (updateSwingTimer)
    f32 mFlashFogRate;  // the flash fog's peak
    Color8 mFlashFogColor;  // the flash fog's color
    u8 mBlueFireDamage;  // the fire damage's smoke is blue (selectDamageFireColor; MarioDamage)
    bool mFlashFogOn;  // the flash fog's color applies
    CollectCounter* mTrampleCounter;  // the consecutive-trample counter
    MarioMessenger* mMessenger;  // the shared-group message requests
    bool mBaseMtxSet;  // a base matrix was set this frame (calcAndSetBaseMtx)
    bool mBaseMtxStale;  // no base matrix this frame: the blur and the animation wait
    bool mDarkMaskOn;  // the dark mask is up (initDarkMask)
    u16 mDarkMaskTimer;  // the dark mask's frames, its radius
    u16 mScreenBlurOn;  // the screen blur draws (drawScreenBlend); nothing sets it
    f32 mScreenBlurGrow;  // the screen layer's overscan (drawFullScreenBlur)
    f32 mScreenBlurTrailGrow;  // the blur trail's overscan
    u8 mScreenBlurAlpha;  // the screen layer's alpha
    u8 mScreenBlurTrailAlpha;  // the blur trail's alpha
    u8* mRippleKey;  // the frame ripple's key
    bool mCareMovingObject;  // calcAndSetBaseMtx asks careMovingObject to run (check)
    bool mSwingTrigger; // a spin asked for this frame (isRequestRush or a 2P spin jump)
    u8 mRasterScrollSet;  // setRasterScroll ran this frame
    f32 mRasterScrollAmp;  // the raster scroll's amplitude
    u16 mRasterScrollPhase;  // the raster scroll's phase
    f32 mRasterScrollWave;  // the raster scroll's wavelength
    Vec3f mWallShadePos;  // the wall shade's point
    Vec3f mWallShadeNormal;  // the wall shade's normal
    f32 mWallShadeDist;  // the wall shade's distance (1000 = none)
    const AreaObj* mWallShadeArea;  // the area the wall shade came from
    u8 mWallShadeAreaKind;  // 0 cube, 1 cylinder
    CollisionShadow* mCollisionShadow;  // the collision shadow
    DrawAdaptor* mShadowDrawer;  // draws drawShadow
    Mario* mMario;
    MarioAnimator* mMarioAnim;
    MarioEffect* mMarioEffect;
    MarioConst* mConst;
    Vec3f mGravityVec;  // the gravity applied (smoothed from mRawGravityVec)
    Vec3f mRawGravityVec;  // the gravity system's vector; zero in a zero-gravity spot
    Vec3f mBaseFrontVec;  // the front kept across gravity turns (calcBaseFrontVec)
    Vec3f mLastMove;
    Vec3f mPrevControlPos;  // the position at the last control (control2, updateBehavior)
    Vec3f mMovementMove;  // movement's move this frame (MarioCollision's wall check)
    Vec3f mLastPushMove;  // movement's push back (half undone next frame)
    Vec3f mControlPos;  // the position at control
    Vec3f mCenterPos; // the Spine1 joint
    Vec3f mHeadPos;  // the CapPosition joint
    Vec3f mBinderOffset;  // the binder's offset from the position (setBinderOffsetVec)
    Vec3f mUpVec;
    Vec3f mCameraFrontVec;  // the front for the camera (?)
    Vec3f mCameraSideVec;  // the side for the camera (?)
    Vec3f mCameraTrans;
    Vec3f mCameraUpTarget;  // the head vector the camera's up turns to
    Vec3f mPrevHeadVec;  // last frame's head vector (updateGravityVec)
    Vec3f mBaseEuler;  // degrees (updateRotationInfo) (?)
    Vec3f mFrontEuler;  // degrees (updateRotationInfo) (?)
    u16 mCameraUpTimer;  // frames left of the camera up's turn
    u16 mCameraLiftTimer;  // counts to 100 and back: the camera's lift along the head (updateTransForCamera)
    u16 mGravityJumpTimer;  // frames left of the gravity change's jump hold (updateGravityVec)
    u16 mFirstMorphCameraOn;  // the first power-up's programmable camera is running
    u16 mFirstMorphCameraEnd;  // frames until that camera ends
    Vec3f mBeePoseHeadPos;  // the bee pose's lagging head point
    Vec3f mBeePoseTrans;  // the bee pose's blended offset
    Vec3f mBeePoseVelocity;  // the bee pose's lag velocity
    Vec3f mBeePoseDir;  // the bee pose's head-to-foot direction
    GravityInfo* mGravityInfo;
    bool mGravitySameAhead;  // the gravity 100 ahead is the gravity here (Mario's camera turn)
    f32 mGravityRatio;
    u16 mMovementTimer; // movement frames since init
    u32 mFrameCount;  // frames since the opening demo (updateBehavior)
    u32 mHealth;
    u32 mWaterLife;
    u32 mLifeDamageQueue;  // damage queued against the life (decLife, updateLife)
    u16 mStunTimer;  // frames left of the stun: no jump or hip drop (doStun)
    u32 mPressTimer;  // frames left of the press (setPress, doPressing)
    u32 mPressEndTimer;  // frames of the bounce after a press
    u32 mPressSwellTimer;  // frames of the swell added to the press scale
    u8 mPressKind;  // the press's kind (setPress)
    u8 mGameOverKind;  // the game over's kind (forceKill)
    u16 mFloorMaterialHold;  // frames the floor material keeps the message's choice (receiveOtherMsg)
    u16 mGravityBlendTimer;  // frames left of the gravity offset's blend
    u16 mItemPickupBlock;  // frames with no item pickup after a throw
    f32 mPressScale;  // the press's scale along mPressAxis (scaleMtx)
    Vec3f mPressAxis;  // the press's axis (scaleMtx)
    bool mRemoteDemo;  // a remote demo drives him (pad reads stop; readyRemoteDemo)
    Vec3f mRushEndFront;  // the front a rush ends facing
    u16 mRushEndFrontMode;  // 0 keep the base's, 1 keep the up, more: the turn's frames
    u16 mPlayerMode;
    u16 mPrevPlayerMode;  // the form before the morph (setPlayerMode)
    u16 mMorphTimer;  // frames left of the morph flash
    u16 mMorphEndTimer;  // frames left after a morph from control
    u16 mPlayerModeTimer;  // frames left of the form (setPlayerMode, updatePlayerMode)
    u8 mMorphAnimDone;  // the morph animation finished (calcAnimInMovement)
    u8 mMorphFlashDone;  // the morph flash finished
    u32 mMaxHealth;
    bool mLifeGainBlocked;  // no life gain (incLife); nothing sets it
    bool mPunchSensorOn;  // the punch's sensor is on (updateHitSensor)
    bool mPunchTimerSensor;  // the punch sensor counts from its timer
    bool mPolygonAttackDone;  // attackOrPushPolygons hit this frame
    Mtx34f mBaseMtx;  // the base matrix as set (calcAndSetBaseMtx) (?)
    HitSensor* mTakenSensor;  // the sensor he holds
    MR::GrowTable< HitSensor*, 4 > mTakeSensors;  // what he holds, mTakeCount of them
    MR::GrowTable< Vec3f, 4 > mTakeRotations;     // each one's rotation less his
    u32 mTakeCount;
    HitSensor* mTakeCandidate;    // the best sensor to take
    HitSensor* mThrowTarget;

    HitSensor* mThrowMemoSensor;  // the sensor he threw (releaseThrowMemoSensor)
    f32 mLockOnAngle;  // the lock-on candidate's angle (trySetLockOnTarget)
    u32 mThrowVecTimer;  // the throw vector's frames (updateThrowVector)
    bool mTaking;  // he is taking a sensor (tryGetItem, takeSensor)
    u8 mHiddenByDemo;  // hidden (isAllHidden, the shadow, the silhouette) (?)
    bool mHidden;  // the model is hidden (init2, the black hole)
    bool mMorphHidden;  // the model is hidden during the morph
    bool mModHidden;  // a plugin hid the model (show_model): it stays hidden; he still moves, animates and plays effects
    Vec3f mThrowProbePos;  // the throw vector's probe point (updateThrowVector)
    f32 mTakingWobble;  // the taken item's wobble phase
    FixedPosition* mTakingFixedPos;  // the hand point the taken item follows
    FixedPosition* mHandFixedPos;  // the right hand's point
    FixedPosition* mHandHoldFixedPos;  // the right hand's hold point
    FixedPosition* mSearchLightThrowPos;
    HitSensor* mMorphItemSensor;  // the power-up item being taken (tryGetItem)
    HitSensor* mHomingSensor;  // the sensor below nearest the gravity this frame: the hip drop's homing fallback
    f32 mPushSensorAngle;  // the nearest push sensor's angle this frame
    f32 mBodyRadius;  // the body cylinder's radius
    f32 mBodyHeight;  // the body cylinder's height
    Vec3f mBodyUp;  // the body cylinder's up (updateRealMtx)
    Vec3f mBodyDown;  // the body cylinder's down
    MR::GrowTable< HitSensor*, 128 > mPullSensors;  // items being pulled; null entries are free
    bool mClapCatchStart;  // the clap catch starts (checkClapCatchStart)
    f32 mSpinEffectRadius;  // the spin effect's radius
    f32 mSpinEffectHeight;  // the spin effect's height above the center
    MR::GrowTable< HitSensor*, 64 > mClapCoins;  // the coins the clap caught
    u32 mClapCoinCount;  // entries in mClapCoins
    MR::GrowTable< HitSensor*, 64 > mRushSensors;  // this frame's rush sensors, mRushSensorCount of them
    MR::GrowTable< bool, 64 > mRushSensorNear;  // per rush sensor: near enough without the radius check
    HitSensor* mRushSensor; // the ride's sensor while in a rush
    HitSensor* mLastRushSensor;  // the sensor of the last rush
    u32 mLastRushEndFrame;  // mFrameCount when that rush ended
    u32 mRushSensorCount;  // entries in mRushSensors
    bool mInRush;  // in a rush
    bool mRushJustEnded;  // a rush ended this frame: pad requests wait (endRush)
    Vec3f mRushMove;  // the rush's last move (calcAndSetBaseMtx)
    u8 mPunchHitTimer;  // frames left of the punch hit
    u8 mPunchFrame;  // frames since the punch began
    u16 mSpinIntervalTimer;  // frames until the next spin
    u16 mSpinTicoTimer;  // the spin's Tico effect frames
    u16 mSpinStarTimer;  // the fairy star's frames after a spin
    u16 mSpinStarEndTimer;  // frames until the spin star effect ends
    MR::GrowTable< HitSensor*, 8 > mAirPunchHits;  // sensors the air punch hit
    u32 mAirPunchHitCount;  // entries in mAirPunchHits
    Vec3f mControlVelocity;  // the velocity at control (updateBindRatio)
    f32 mBindRatio;  // how far the up follows the ground or wall normal
    u8 mTrampleJumpStep;  // the trample jump's height step (0-2)
    u8 mTrampleCombo;  // consecutive tramples (the counter and the combo sound)
    TornadoMario* mTornadoMario;
    u8 mTornadoEnd;  // the tornado ends (updateTornado)
    LiveActor* mSpinTico;  // the spin's Tico
    MarioSearchLight* mSearchLight;
    u32 mSearchLightTimer;
    JetTurtleShadow* mTurtleShadow;  // the shell's shadow model
    MarioParts* mTeresaModel;  // the Boo skin
    f32 mTeresaAlpha;  // the Boo skin's fade
    f32 mTeresaTilt;  // the Boo's tilt blend
    f32 mTeresaFlyWeight;  // the Boo's fly track weight
    u16 mTeresaBlinkTimer;  // frames until the Boo blinks
    XanimePlayer* mTeresaAnimPlayer;  // the Boo's animation player
    XanimeResourceTable* mTeresaAnimTable;  // the Boo's animation table
    ModelHolder* mIceModel;  // the ice form's model
    MarioParts* mFreezeModel;  // the freeze ice
    ModelHolder* mInvincibleModel;  // the invincible form's model
    f32 mScoutPrevRange;  // the scouter's range at the last record
    f32 mScoutRange;  // the scouter's range
    HitSensor* mScoutSensor;  // the scouter's target
    ModelHolder* mBeeModel;  // the bee form's model
    MarioParts* mBeeWing;  // the bee's wings
    FurMulti* mBeeFur;  // the bee's fur
    u8 mBeeWingAnim;  // the bee wing animation playing
    u8 mBeeWallWalk;
    u16 mBeeStickInhibit;  // frames with no bee stick
    Vec3f mBeePoseGravity;  // the gravity the bee pose follows
    ModelHolder* mHopperModel;  // the hopper form's model
    ModelHolder* mBoneModel;  // the bone form's model
    u8 mShadowMode;  // the shadow and reflection mode (decideShadowMode)
    u8 mReflectAlpha;  // the reflection's alpha step
    u8 mCurrModel;
    u8 mDisplayModel;  // the model shown (changeDisplayMode)
    u8 mWallShadeOn;  // the wall shade draws
    ModelHolder* mSimpleModelHolder;  // the shadow model's holder
    PlayerModel* mSimpleModel;  // the shadow model (getSimpleModel)
    Vec3f mShadowRotate;
    u8 mReflectSimpleAlpha;  // the simple reflection's alpha
    u8 mWaterReflectAlpha;  // the water reflection's alpha
    PlayerModel** mModels;  // the game's six, then each mod power-up's whole-body model (null when it has none)
    ModelHolder** mModPowerUpModels;
    s32 mNumModPowerUps;  // the power-ups registered when the models were made
    ModelHolder* mIceHandL;  // the ice form's left hand
    ModelHolder* mIceHandR;  // the ice form's right hand
    ModelHolder* mHandL;  // the left hand's poses
    ModelHolder* mHandR;  // the right hand's poses
    ModelHolder* mInvincibleHandL;  // the invincible form's left hand
    ModelHolder* mInvincibleHandR;  // the invincible form's right hand
    bool mHandsHidden;  // the hands are hidden this frame (updateHand)
    u8 mHandPoseL;  // the left hand's pose shown (0xFF none)
    u8 mHandPoseR;  // the right hand's pose shown (0xFF none)
    u8 mFacePoseCount;  // the face's poses
    ModelHolder* mFaceModel;  // the face's poses
    u8 mFacePose;  // the face's pose shown
    bool mPreWipeOn;  // the pre-wipe fill is up (MarioAccess): the model is not drawn
    u16 mPreWipeTimer;  // the pre-wipe fill's frames: its alpha
    u8 mBlurState;  // the model blur: 0 off, 1 asked, 2 on, 3-5 fading out (calcViewBlurModel)
    ModelPose mBlurPoses[2][cBlurPoseNum];
    Mtx34f mPrevViewMtx;  // last frame's camera view (drawModelBlur)
    Mtx34f mViewMtx;  // this frame's camera view
    u16 mBlurPoseSet;  // the blur pose set drawn (0 or 1)
    u16 mBlurPoseIndex;  // the next blur pose (below cBlurPoseNum)
    Vec3f mReflectPos;  // the reflection model's position (calcScreenBoxRange)
    Vec2f mScreenBoxMin;   // the model's box on the screen (calcScreenBoxRange)
    Vec2f mScreenBoxMax;
    Vec2f mScreenBoxPos;   // the box captured, clipped to the screen
    Vec2f mScreenBoxSize;
    void* mScreenBoxKey;  // the frame read's key
    FootPrint* mFootPrint;  // the footprints
    static const u32 cIceStepNum = 20;
    IceStep** mIceSteps;  // the ice form's steps, reused in turn
    u16 mIceStepIndex;  // the next ice step
    FireMarioBall* mFireBalls[3];  // the fire form's balls
    ResTIMG** mSwapTextures;  // the model's textures (swapTextureInit)
    u8 mEyeLidReset;  // the eyelid came from the eye resource: the blink restarts (updateBlink)
    u16 mEyeLidFrame;  // the eyelid texture's frame (swapTexture EyeLid_v)
    const void** mSwapImages;  // the swap textures' images by slot
    u32 mEyeLidTexBase;  // the first eyelid texture's index
    u8 mBlinkFrame;  // frames left of the blink
    u16 mBlinkTimer;  // frames until the next blink
    Clip* mEyeRes;
    GameTexture* mBlurTexture; // the full-screen blur's target
    GameTexture* mMaskTextures[2];  // the mask's two buffers
    u16 mMaskIndex;  // the mask texture drawn (0 or 1)
    MarioNullBck* mNullAnimation;
    bool mAnimationFrozen;  // animation changes are ignored (game over, press, freeze, talk, demo)
    bool mForceKilled;  // forceKill ran (MarioAccess)
    s8 mNullAnimationKind;  // the null animation's number (changeNullAnimation); below 0 a take pose
    u16 mBombTeresaTimer;  // frames of the bomb Boo spin (receiveOtherMsg): no talk
    u8 mMudWalkTimer;  // frames until the mud walk's tracks go back
    struct EffectFlags {
        unsigned mSmoke : 1;
        unsigned mSnow : 1;
        unsigned mSlopeSlip : 1;
        unsigned mIsStationary : 1;
        unsigned mBeeWind : 1;
    } mEffectFlags;  // the level effects playing
    u16 mCommonEffectCount;  // the common effects
    u16 mSmokeEntryCount;  // entries in mSmokeEntries
    SmokeEffectEntry** mSmokeEntries;  // the smoke effects by material
    MR::NameIndex mMaterialEffects;  // a material effect's name to its index in cMaterialEffectTable
    ModelObj* mWaterColumn;  // the water column model
    Vec3f mWaterSurfacePos;  // the water surface effects' point (playEffectRTW)
    Vec3f mWaterSurfaceRot;  // the water surface effects' rotation
    u16 mDamageFlashTimer;  // frames of the full-screen damage flash (blue when swimming drains life)
    Mtx34f mModelUnbaseMtx;  // undoes the model's base and scale (calcViewMainModel); the shadows put it back
    Mtx34f mBaseScaleMtx;  // the scale (updateBaseScaleMtx)
    Mtx* mGlobalJointMtx;  // each joint's world matrix (getGlobalJointMtx)
    Mtx34f mHandLMtx;  // the HandL joint
    Mtx34f mHandRMtx;  // the HandR joint
    Mtx34f mFaceMtx;  // the Face0 joint
    Mtx34f mFootRMtx;  // the FootR joint (the crest effect's)
    Mtx34f mCapMtx;  // the CapPosition joint
    Mtx34f mHipMtx;  // the Hip joint
    Mtx34f mSpine2Mtx;  // the Spine2 joint (the bee wings)
    Mtx34f mTurtleShadowMtx;  // the shell shadow's matrix
    Mtx34f mBaseCorrectionMtx;  // the base matrix's correction (calcAndSetBaseMtx)
    u16 mBlendMtxTimer;
    f32 mBlendMtxRate;  // the base blend's progress (setBlendMtxTimer)
    bool mInDemo;  // a demo holds him (MarioAccess)
    bool mBaseMtxForced;  // forceSetBaseMtx set the base this frame
    bool mDemoBaseKept;  // the demo's base was taken: it stays until the demo ends
    Mtx34f mForcedBaseMtx;  // the base forceSetBaseMtx set
    bool mSuperKinokoCollected;
    bool mPowerupCollected;
    bool mTransforming;
    bool mSwingPermission; // false: no spin
    bool mActionHeld;  // the action input was down last frame
    bool mActionTrigger; // the action button went down this frame
    u16 mSwingInhibitTimer; // frames left with no spin
    bool mMainSpinTrigger;  // the main controller's shake starts a spin this frame
    bool mSubSpinTrigger;  // the sub controller's shake starts a spin this frame
    u16 mHipDropButton;  // the hip drop's button kind (checkButtonType); nothing sets it
    u16 mRushButton;  // the rush and throw's button kind; nothing sets it
    u16 mSpinButton;  // the spin's button kind; nothing sets it
    u16 mJumpButton;  // the jump's button kind; nothing sets it
    u16 mMainSpinBlock;  // frames the main shake can't start a spin (after a jump or fire)
    u16 mSubSpinBlock;  // frames the sub shake can't start a spin
    bool mMainSpinHeld;  // the main shake was on last frame
    bool mSubSpinHeld;  // the sub shake was on last frame
    HitSensor* mThrowAimSensor;  // the scouted target a throw goes to (updateScouter)
    u16 mThrowAimHold;  // frames the target stays after the scout loses it
    


    Vec3f mMoveFrontVec;  // the front at the end of the last movement (MarioAccess's throw fallback)
    bool mBindOn;  // the binder is on (control2)
    HitSensor* mPressCeilingSensor;  // the ceiling's sensor that pressed him: binderFilter skips it (MarioPress)
    BlackHole* mBlackHole;
    Vec3f mBlackHolePosition;
    Vec3f mBlackHoleRotateAxis;
    Vec3f mPosRelativeToBlackHole;
    bool mCameraCut;  // the camera jumped this frame (30 degrees or 500)
    Vec3f mCamPos;
    Vec3f mCamDirX;
    Vec3f mCamDirY;
    Vec3f mCamDirZ;
    Vec3f mPrevCamDirZ;  // last frame's camera z (Mario's front fix)
    const Nerve* mWaitNextNerve;  // the nerve exeWait goes to when mWaitTimer ends
    u16 mWaitTimer;  // frames left of exeWait
    MatrixControl* mMorphActionTable;  // the form's action table (isActionOk)
    MatrixControl* mAutoBindTable;  // the auto bind table
    MatrixControl* mMessageEffectTable;  // the message reply effects table
    MatrixValueGetter* mFaceHeightTable;  // the characters' heights (getFaceLookHeight)
    bool mAttacking;  // an attack is being sent: enemy attacks wait (receiveMsgEnemyAttack)
    bool mTrampling;  // a trample is being sent: enemy attacks wait
};

// header defined since these actually get defined in CamHeliEffector
// and in sinit of NameObjFactory
namespace NrvMarioActor {
    NEW_NERVE(MarioActorNrvWait, MarioActor, Wait);
    NEW_NERVE(MarioActorNrvGameOver, MarioActor, GameOver);
    NEW_NERVE(MarioActorNrvGameOverAbyss, MarioActor, GameOverAbyss);
    NEW_NERVE(MarioActorNrvGameOverAbyss2, MarioActor, GameOverAbyss);
    NEW_NERVE(MarioActorNrvGameOverFire, MarioActor, GameOverFire);
    NEW_NERVE(MarioActorNrvGameOverBlackHole, MarioActor, GameOverBlackHole2);
    NEW_NERVE(MarioActorNrvGameOverNonStop, MarioActor, GameOverNonStop);
    NEW_NERVE(MarioActorNrvGameOverSink, MarioActor, GameOverSink);
    NEW_NERVE(MarioActorNrvTimeWait, MarioActor, TimeWait);
    NEW_NERVE(MarioActorNrvNoRush, MarioActor, Wait);
};  // namespace NrvMarioActor
