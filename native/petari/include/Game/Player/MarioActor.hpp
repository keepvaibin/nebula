#pragma once

#include "Game/Player/MarioModelTint.hpp"
#include "Game/LiveActor/Nerve.hpp"
#include "Game/Player/Mario.hpp"
#include "Game/Player/MatrixControl.hpp"
#include "Game/Util/Color.hpp"
#include "Game/Util/MapUtil.hpp"

class BlackHole;
class CollisionShadow;
class DrawAdaptor;
class FixedPosition;
class FootPrint;
class FurMulti;
class HashSortTable;
class IceStep;
class FireMarioBall;
class J3DAnmTexPattern;
class J3DModel;
class J3DModelData;
class J3DModelX;
class JAIAudible;
class JUTTexture;
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

enum SPECIAL_STATUS_FOR_CAMERA { CASE_0, CASE_1 };

enum PlayerMode {
    PlayerMode_Normal,
    PlayerMode_Invincible,
    PlayerMode_Fire,
    PlayerMode_Ice,
    PlayerMode_Bee,
    PlayerMode_Hopper,
    PlayerMode_Teresa,
    PlayerMode_Foo,
    PlayerMode_8,
    PlayerMode_Tornado
};

template < int SIZE, class T, class U >
class AudGenericAudible;
class JAUDopplerAudibleChannel;
class JAUDopplerAudibleAbsPos;
typedef AudGenericAudible< 4, JAUDopplerAudibleChannel, JAUDopplerAudibleAbsPos > AudGeneric;

extern bool gIsLuigi;  // (cc68 - 10000)(r13)

class MarioActor : public LiveActor {
public:
    MarioActor(const char*);

    virtual ~MarioActor();
    virtual void init(const JMapInfoIter&);
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
    virtual const TVec3f& getLastMove() const;
    virtual void getLastMove(TVec3f*) const;
    virtual void getFrontVec(TVec3f*) const;

    void init2(const TVec3f&, const TVec3f&, s32);
    void initAfterOpeningDemo();
    void calcBaseFrontVec(const TVec3f&);
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
    void getRealPos(const char*, TVec3f*) const;
    MtxPtr getGlobalJointMtx(const char*);
    void calcAnimInMovement();
    void forceSetBaseMtx(MtxPtr);
    void setBlendMtxTimer(u16);
    void getGroundPos(TVec3f* dst) const;
    TVec3f* getShadowPos() const;

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
    bool isDebugMode() const;

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
    void addSoundObjHolder();

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
    void playEffectTrans(const char*, const TVec3f&);
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
    bool selectWaterInOut(const char*) const;
    void playEffectRT(const char*, const TVec3f&, const TVec3f&);
    void playEffectRTZ(const char*, const TVec3f&, const TVec3f&);
    void playEffectRTW(const char*, const TVec3f&, const TVec3f&);
    void playEffectSRT(const char*, f32, const TVec3f&, const TVec3f&);
    void emitEffectWaterColumn(const TVec3f&, const TVec3f&);
    void endRush(const RushEndInfo*);
    void stopSpinTicoEffect(bool);
    void stopEffectForce(const char*);
    void initBlackHoleOut();  // void ?
    void exeGameOverBlackHole2();
    void initBlink();
    void updateBlink();
    void setBlink(const char*);

    void entryWallWalkMode(const TVec3f&, const TVec3f&);

    HitSensor* getLookTargetSensor() const;
    J3DModelX* getJ3DModel() const;
    J3DModelData* getModelData() const;
    J3DModelX* getSimpleModel() const;
    void swapTexture(const char*, u8) const;
    void copyMaterial(J3DModel*, u16, s32);

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
    MtxPtr getMapBaseMtx() const;
    bool isFastRise() const;
    bool isLongDrop() const;
    bool isFastDrop() const;
    u32 getSpecialMode() const;
    bool isCameraStateOn(SPECIAL_STATUS_FOR_CAMERA) const;
    void getSideVec(TVec3f*) const;
    void getUpVec(TVec3f*) const;
    const TVec3f& getTransForCamera() const;
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
    const TVec3f& getGravityVec() const;
    const TVec3f& getGravityVector() const;
    const TVec3f& getAirGravityVec() const;
    void getGravityVector(TVec3f*) const;
    f32 getGravityRatio() const;
    GravityInfo* getGravityInfo() const;
    u8 getGravityLevel() const;
    bool checkBeeWallStick(TVec3f&);
    bool checkBeeFloorStick(TVec3f&);
    void syncJumpBeeStickMode();
    void updateBeeModeGravity(TVec3f&);
    bool isInZeroGravitySpot() const;
    void updateGravityVec(bool, bool);
    bool checkBeeCeilStick(TVec3f&);
    void updateBeeStickMode(TVec3f&);

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
    void lockOnDPD();
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
    void createIceFloor(const TVec3f&);
    void createIceFloor(const TVec3f&, const TVec3f&);
    void createIceWall(const TVec3f&, const TVec3f&);
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
    f32 calcCylinderToCenter(const AreaObj*, TVec3f*);
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
    void drawWallShade(const TVec3f&, const TVec3f&, f32) const;
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
    bool cylinderPushCheck(const TVec3f&, f32, f32, f32);
    void attackOrPushPolygons();
    void sendWallTouch(HitSensor*, HitSensor*);
    bool sendMsgUpperPunch(HitSensor*);

    // Defined in MarioActorDefensiveMsg
    bool tryAttackMsg(u32, const HitSensor*, bool*);
    bool tryVectorAttackMsg(u32, const TVec3f&);
    bool cylinderHitCheck(const HitSensor*, TVec3f*);

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
    void tryPullTrans(TVec3f*, const TVec3f&);
    bool releaseThrowMemoSensor();
    void tryReleaseWithMsg(u32);
    void tryTornadoPull(HitSensor*);
    void tryReleaseBombTeresa();

    void runTeresaBaseAnimation();
    void updateTeresaAnimation();
    void getThrowVec(TVec3f*) const;

    const MarioConst& getConst() const {
        return *mConst;
    }

    inline u32 getHealth() const {
        return mHealth;
    }

    inline void setB90(bool param1) {
        _B90 = param1;
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

    // Only used in isSleeping() to make it match
    inline bool IsMarioAnimationRun(const char* pAnimName) const {
        return mMario->isAnimationRun(pAnimName);
    }

    inline bool IsMarioSwimming() const {
        return mMario->isSwimming();
    }

    inline void MarioStopWalk() const {
        mMario->stopWalk();
    }

    inline const bool isBeeMarioOnFur() const {
        return mMario->getMovementStates()._1 && strcmp("Fur", MR::getWallCodeString(mMario->getGroundPolygon())) == 0 && mBeeWallWalk == 0;
    }

    inline const bool is481or482On() const {
        return _482 || _481;
    }

    inline Mario* getMario() const {
        return mMario;
    }

    inline bool isBeeWallWalk() const {
        return mBeeWallWalk != 0;
    }

    struct FBO {
        u32 _0;
        u32 _4;
    };

    u8 _8C;
    MarioModelTint mAlphaTint;
    MarioModelTint mGhostTints[0x40];
    MarioModelTint mLightTint;
    u8 _1A1;
    f32 _1A4;
    u16 _1A8;
    u16 _1AA;
    f32 _1AC;
    Color8 _1B0;
    u8 _1B4;
    bool _1B5;
    CollectCounter* _1B8;
    MarioMessenger* _1BC;
    bool _1C0;
    bool _1C1;
    bool _1C2;
    bool _1C3;
    u16 _1C4;
    u16 _1C6;
    f32 _1C8;
    f32 _1CC;
    u8 _1D0;
    u8 _1D1;
    f32 _1D4;
    union {
        FBO* mRasterBuffers[2];
        struct {
            FBO* _1D8;
            FBO* _1DC;
        };
    };
    bool _1E0;
    bool _1E1;
    u8 _1E2;
    f32 _1E4;
    u16 _1E8;
    f32 _1EC;
    TVec3f _1F0;
    TVec3f _1FC;
    f32 _208;
    const AreaObj* _20C;
    u8 _210;
    u8 _211;
    CollisionShadow* _214;
    DrawAdaptor* _218;
    DrawAdaptor* _21C;
    DrawAdaptor* _220;
    DrawAdaptor* mDrawSearchLight;
    DrawAdaptor* _228;
    DrawAdaptor* _22C;
    Mario* mMario;
    MarioAnimator* mMarioAnim;
    MarioEffect* mMarioEffect;
    MarioConst* mConst;
    TVec3f _240;
    TVec3f _24C;
    TVec3f _258;
    TVec3f mLastMove;
    TVec3f _270;
    TVec3f _27C;
    TVec3f _288;
    TVec3f _294;
    TVec3f _2A0;
    TVec3f _2AC;
    TVec3f _2B8;
    TVec3f _2C4;
    TVec3f mUpVec;
    TVec3f _2DC;
    TVec3f _2E8;
    TVec3f mCameraTrans;
    TVec3f _300;
    TVec3f _30C;
    TVec3f _318;
    TVec3f _324;
    u16 _330;  // a timer
    u16 _332;
    u16 _334;
    u16 _336;
    u16 _338;
    TVec3f _33C;
    TVec3f _348;
    TVec3f _354;
    TVec3f _360;
    GravityInfo* mGravityInfo;
    bool _370;
    f32 mGravityRatio;
    u16 _378;
    u32 _37C;
    u32 mHealth;
    u32 mWaterLife;
    u32 _388;
    u16 _38C;
    u32 _390;
    u32 _394;
    u32 _398;
    u8 _39C;
    u8 _39D;
    u8 _39E;
    u32 _3A0;
    u32 _3A4;
    u16 _3A8;
    u16 _3AA;
    u16 _3AC;
    f32 _3B0;
    TVec3f _3B4;
    bool _3C0;
    bool _3C1;
    TVec3f _3C4;
    u16 _3D0;
    u16 _3D2;
    u16 mPlayerMode;
    u16 _3D6;
    u16 _3D8;
    u16 _3DA;
    u16 _3DC;
    u8 _3DE;
    u8 _3DF;
    u32 mMaxHealth;
    bool _3E4;
    bool _3E5;
    bool _3E6;
    bool _3E7;
    bool _3E8;
    TMtx34f _3EC;
    u32 _41C;
    u32 _420;
    HitSensor* _424;
    HitSensor* _428[4];
    TVec3f _438[4];

    union {
        struct {
            u32 _468;
            HitSensor* _46C;
            HitSensor* _470;
        };

        TVec3f _468Vec;
    };

    HitSensor* _474;
    f32 _478;
    u32 _47C;
    bool _480;
    u8 _481;
    bool _482;
    bool _483;
    TVec3f _484;
    f32 _490;
    FixedPosition* _494;
    FixedPosition* _498;
    FixedPosition* _49C;
    FixedPosition* mSearchLightThrowPos;
    HitSensor* _4A4;  // used in calcAnimInMovement()
    HitSensor* _4A8;
    f32 _4AC;
    f32 _4B0;
    f32 _4B4;
    TVec3f _4B8;
    TVec3f _4C4;
    HitSensor* _4D0[128];
    bool _6D0;
    f32 _6D4;
    f32 _6D8;
    HitSensor* _6DC[64];
    u16 _7DC;  // _6DC count
    u16 _7DE;
    u16 _7E0;
    u8 _7E2;
    HitSensor* _7E4[64];
    bool _8E4[64];
    HitSensor* _924;
    HitSensor* _928;
    u32 _92C;
    u32 _930;
    bool _934;
    bool _935;
    TVec3f _938;
    u8 _944;
    u8 _945;
    u16 _946;
    u16 _948;
    u16 _94A;
    u16 _94C;
    u16 _94E;
    bool _950;
    u8 _951;
    HitSensor* _954[8];
    u8 _974;  // _954 count
    TVec3f _978;
    f32 _984;
    u8 _988;
    u8 _989;
    TornadoMario* mTornadoMario;
    u8 _990;
    LiveActor* _994;  // probably some subclass of LiveActor
    MarioSearchLight* mSearchLight;
    u32 mSearchLightTimer;
    JetTurtleShadow* _9A0;
    MarioParts* _9A4;
    f32 _9A8;
    f32 _9AC;
    f32 _9B0;
    u16 _9B4;
    XanimePlayer* _9B8;
    XanimeResourceTable* _9BC;
    ModelHolder* _9C0;
    MarioParts* _9C4;
    ModelHolder* _9C8;
    f32 _9CC;
    f32 _9D0;
    HitSensor* _9D4;
    TVec3f _9D8;
    ModelHolder* _9E4;
    MarioParts* _9E8;
    FurMulti* _9EC;
    u8 _9F0;
    u8 mBeeWallWalk;
    u16 _9F2;
    TVec3f _9F4;
    ModelHolder* _A00;
    ModelHolder* _A04;
    u8 _A08;
    u8 _A09;
    u8 mCurrModel;
    u8 _A0B;
    u8 _A0C;
    ModelHolder* _A10;
    J3DModelX* _A14;
    TVec3f _A18;
    u8 _A24;
    u8 _A25;
    J3DModelX* mModels[6];
    ModelHolder* _A40;
    ModelHolder* _A44;
    ModelHolder* _A48;
    ModelHolder* _A4C;
    ModelHolder* _A50;
    ModelHolder* _A54;
    bool _A58;
    u8 _A59;
    u8 _A5A;
    u8 _A5B;
    ModelHolder* _A5C;
    u8 _A60;
    bool _A61;
    bool _A62;
    u32 _A64;
    f32 _A68;
    u16 _A6C;
    u8 _A6E;
    union {
        struct {
            Mtx* _A70[8];
            Mtx* _A90[8];
        };
        Mtx* mBlurMatrices[2][8];
    };
    TMtx34f _AB0;
    TMtx34f _AE0;
    u16 _B10;
    u16 _B12;
    u16 _B14;
    TVec3f _B18;
    union {
        TVec2f mScreenBoxMin;
        struct {
            f32 _B24;
            f32 _B28;
        };
    };
    union {
        TVec2f mScreenBoxMax;
        struct {
            f32 _B2C;
            f32 _B30;
        };
    };
    union {
        TVec2f mScreenBoxPos;
        struct {
            f32 _B34;
            f32 _B38;
        };
    };
    union {
        TVec2f mScreenBoxSize;
        struct {
            f32 _B3C;
            f32 _B40;
        };
    };
    void* _B44;
    FootPrint* _B48;
    IceStep** _B4C;
    u16 _B50;  // _B4C count
    FireMarioBall* _B54[3];
    u16 _B60;
    ResTIMG** _B64;
    u8 _B68;
    u16 _B6A;
    const void** _B6C;
    u16 _B70;
    u8 _B72;
    u16 _B74;
    J3DAnmTexPattern* mEyeRes;
    JUTTexture* _B7C;
    union {
        JUTTexture* mMaskTextures[2];
        struct {
            JUTTexture* _B80;
            JUTTexture* _B84;
        };
    };
    u16 _B88;
    MarioNullBck* mNullAnimation;
    bool _B90;
    bool _B91;
    s8 _B92;
    u16 _B94;
    u8 _B96;
    union {
        u32 _B98;
        struct {
            unsigned mSmoke : 1;
            unsigned mSnow : 1;
            unsigned _2 : 1;
            unsigned mSlopeSlip : 1;
            unsigned mIsStationary : 1;
            unsigned mBeeWind : 1;
        } mEffectFlags;
    };
    u16 _B9C;
    u16 _B9E;
    SmokeEffectEntry** _BA0;
    HashSortTable* _BA4;
    ModelObj* _BA8;
    TVec3f _BAC;
    TVec3f _BB8;
    u16 _BC4;
    TMtx34f _BC8;
    TMtx34f _BF8;
    Mtx* _C28;
    TMtx34f _C2C;
    TMtx34f _C5C;
    TMtx34f _C8C;
    TMtx34f _CBC;
    TMtx34f _CEC;
    TMtx34f _D1C;
    TMtx34f _D4C;
    TMtx34f _D7C;
    TMtx34f _DAC;
    TMtx34f _DDC;
    TMtx34f _E0C;
    TMtx34f _E3C;
    TMtx34f _E6C;
    u16 mBlendMtxTimer;
    f32 _EA0;
    bool _EA4;
    bool _EA5;
    bool _EA6;
    TMtx34f _EA8;
    TVec3f _ED8;
    const char* _EE4;
    bool mSuperKinokoCollected;
    bool mPowerupCollected;
    bool mTransforming;
    bool _EEB;
    bool _EEC;
    bool _EED;
    bool _EEE;
    u16 _EF0;
    u16 _EF2;
    u16 _EF4;
    u16 _EF6;
    u32 _EF8;
    u32 _EFC;
    bool _F00;
    u32 _F04;
    u32 _F08;
    bool _F0C;
    u8 _F0D;
    u16 _F0E;
    u16 _F10;
    u16 _F12;
    u16 _F14;
    u16 _F16;
    u16 _F18;
    u16 _F1A;
    u16 _F1C;
    u16 _F1E;
    bool _F20;
    bool _F21;
    HitSensor* _F24;
    u16 _F28;
    

    union {
        u32 _F2C;
        TVec3f _F2CVec;
    };

    u8 _F38;

    union {
        AudGeneric* _F3C;
        TVec3f* _F3CVec;
    };

    u16 _F40;
    u16 _F42;
    bool _F44;
    HitSensor* _F48;
    BlackHole* mBlackHole;
    TVec3f mBlackHolePosition;
    TVec3f mBlackHoleRotateAxis;
    TVec3f mPosRelativeToBlackHole;
    bool _F74;
    TVec3f mCamPos;
    TVec3f mCamDirX;
    TVec3f mCamDirY;
    TVec3f mCamDirZ;
    TVec3f _FA8;
    const Nerve* _FB4;
    u16 _FB8;  // a timer
    MatrixControl* _FBC;
    MatrixControl* _FC0;
    MatrixControl* _FC4;
    MatrixValueGetter* _FC8;
    bool _FCC;
    bool _FCD;
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
