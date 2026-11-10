#include "Game/System/NebulaFrameClock.hpp"
#include "Game/Player/MarioActor.hpp"
#include "Game/System/NebulaMods.hpp"
#include "Game/Animation/XanimeCore.hpp"
#include "Game/Camera/CameraTargetArg.hpp"
#include "Game/Enemy/KarikariDirector.hpp"
#include "Game/Gravity/GravityInfo.hpp"
#include "Game/LiveActor/Binder.hpp"
#include "Game/LiveActor/HitSensor.hpp"
#include "Game/Map/CollisionParts.hpp"
#include "Game/Map/HitInfo.hpp"
#include "Game/Map/WaterInfo.hpp"
#include "Game/Player/PlayerModel.hpp"
#include "Game/Player/MarioAnimator.hpp"
#include "Game/Player/MarioConst.hpp"
#include "Game/Player/MarioEffect.hpp"
#include "Game/Player/MarioHolder.hpp"
#include "Game/Player/MarioMessenger.hpp"
#include "Game/Player/MarioNullBck.hpp"
#include "Game/Player/MarioParts.hpp"
#include "Game/Player/MarioShadow.hpp"
#include "Game/Player/MarioSwim.hpp"
#include "Game/Player/ModelHolder.hpp"
#include "Game/Player/RushEndInfo.hpp"
#include "Game/Scene/SceneFunction.hpp"
#include "Game/Screen/GameSceneLayoutHolder.hpp"
#include "Game/Util/ActorMovementUtil.hpp"
#include "Game/Util/ActorSensorUtil.hpp"
#include "Game/Util/AreaObjUtil.hpp"
#include "Game/Util/CameraUtil.hpp"
#include "Game/Util/DemoUtil.hpp"
#include "Game/Util/EffectUtil.hpp"
#include "Game/Util/EventUtil.hpp"
#include "Game/Util/FixedPosition.hpp"
#include "Game/Util/FootPrint.hpp"
#include "Game/Util/PlacementUtil.hpp"
#include "Game/Util/JointUtil.hpp"
#include "Game/Util/LiveActorUtil.hpp"
#include "Game/Util/MapUtil.hpp"
#include "Game/Util/MathUtil.hpp"
#include "Game/Util/ModelUtil.hpp"
#include "Game/Util/MtxUtil.hpp"
#include "Game/Util/ObjUtil.hpp"
#include "Game/Util/ScreenUtil.hpp"
#include "Game/Util/SoundUtil.hpp"
#include "Game/Util/StarPointerUtil.hpp"
#include "Game/System/NebulaArena.hpp"
#include "Game/System/NebulaAchievements.hpp"
#include "Game/System/GameRules.hpp"
#include "Game/Util/Tunable.hpp"
#include "Game/System/LumaCore.hpp"

NEBULA_TUNABLE_NAMED(f32, cNum_MarioActor_f1, "MarioActor.f1", 1000.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_init_f1, "init.f1", 360.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_init_f2, "init.f2", 360.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_init_f3, "init.f3", 360.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_init2_f1, "init2.f1", 100.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_init2_f2, "init2.f2", 40.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_init2_f3, "init2.f3", 60.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_init2_f4, "init2.f4", 70.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_init2_f5, "init2.f5", 0.35f);
NEBULA_TUNABLE_NAMED(f32, cNum_init2_f6, "init2.f6", 1.8f);
NEBULA_TUNABLE_NAMED(f32, cNum_init2_f7, "init2.f7", 52.51f);
NEBULA_TUNABLE_NAMED(f32, cNum_init2_f8, "init2.f8", 39.58f);
NEBULA_TUNABLE_NAMED(f32, cNum_init2_f9, "init2.f9", 76.33f);
NEBULA_TUNABLE_NAMED(f32, cNum_init2_f10, "init2.f10", 15.69f);
NEBULA_TUNABLE_NAMED(f32, cNum_init2_f11, "init2.f11", 88.99f);
NEBULA_TUNABLE_NAMED(f32, cNum_init2_f12, "init2.f12", 35.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_init2_f13, "init2.f13", 60.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_initAfterPlacement_f1, "initAfterPlacement.f1", 70.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_calcBaseFrontVec_f1, "calcBaseFrontVec.f1", 0.99f);
NEBULA_TUNABLE_NAMED(f32, cNum_updateRotationInfo_f1, "updateRotationInfo.f1", 90.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_updateRotationInfo_f2, "updateRotationInfo.f2", 90.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_updateRotationInfo_f3, "updateRotationInfo.f3", 180.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_updateRotationInfo_f4, "updateRotationInfo.f4", 57.2957763672f);
NEBULA_TUNABLE_NAMED(f32, cNum_updateRotationInfo_f5, "updateRotationInfo.f5", 57.2957763672f);
NEBULA_TUNABLE_NAMED(f32, cNum_updateRotationInfo_f6, "updateRotationInfo.f6", 90.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_updateRotationInfo_f7, "updateRotationInfo.f7", 90.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_updateRotationInfo_f8, "updateRotationInfo.f8", 180.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_movement_f1, "movement.f1", 2.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_movement_f2, "movement.f2", 0.5f);
NEBULA_TUNABLE_NAMED(f32, cNum_movement_f3, "movement.f3", 0.1f);
NEBULA_TUNABLE_NAMED(f32, cNum_movement_f4, "movement.f4", 2.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_movement_f5, "movement.f5", 10.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_movement_f6, "movement.f6", 10.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_movement_f7, "movement.f7", 20.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_movement_f8, "movement.f8", 0.5f);
NEBULA_TUNABLE_NAMED(f32, cNum_movement_f9, "movement.f9", 5.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_movement_f10, "movement.f10", 5.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_movement_f11, "movement.f11", 40.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_movement_f12, "movement.f12", 100.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_movement_f13, "movement.f13", 200.f);
NEBULA_TUNABLE_NAMED(f32, cNum_movement_f14, "movement.f14", 5.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_movement_f15, "movement.f15", 40.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_movement_f16, "movement.f16", 0.1f);
NEBULA_TUNABLE_NAMED(f32, cNum_movement_f17, "movement.f17", 200.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_movement_f18, "movement.f18", 0.1f);
NEBULA_TUNABLE_NAMED(f32, cNum_updateBindRatio_f1, "updateBindRatio.f1", 2.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_updateBindRatio_f2, "updateBindRatio.f2", 0.1f);
NEBULA_TUNABLE_NAMED(f32, cNum_updateBindRatio_f3, "updateBindRatio.f3", 0.01f);
NEBULA_TUNABLE_NAMED(f32, cNum_updateBindRatio_f4, "updateBindRatio.f4", 0.01f);
NEBULA_TUNABLE_NAMED(f32, cNum_isInPunchTimerRange_f1, "isInPunchTimerRange.f1", 6.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_isInPunchTimerRange_f2, "isInPunchTimerRange.f2", 20.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_updateSwingTimer_f1, "updateSwingTimer.f1", 0.75f);
NEBULA_TUNABLE_NAMED(f32, cNum_scaleMtx_f1, "scaleMtx.f1", 0.35f);
NEBULA_TUNABLE_NAMED(f32, cNum_calcAnimInMovement_f1, "calcAnimInMovement.f1", 500.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_calcAnimInMovement_f2, "calcAnimInMovement.f2", 700.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_calcAnim_f1, "calcAnim.f1", 40.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_calcAnim_f2, "calcAnim.f2", 10.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_calcAnim_f3, "calcAnim.f3", 140.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_calcAnim_f4, "calcAnim.f4", 5.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_calcAnim_f5, "calcAnim.f5", 70.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_calcAndSetBaseMtx_f1, "calcAndSetBaseMtx.f1", 500.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_calcAndSetBaseMtx_f2, "calcAndSetBaseMtx.f2", 150.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_calcAndSetBaseMtx_f3, "calcAndSetBaseMtx.f3", 0.2f);
NEBULA_TUNABLE_NAMED(f32, cNum_calcAndSetBaseMtx_f4, "calcAndSetBaseMtx.f4", 150.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_calcAndSetBaseMtx_f5, "calcAndSetBaseMtx.f5", 0.2f);
NEBULA_TUNABLE_NAMED(f32, cNum_calcAndSetBaseMtx_f6, "calcAndSetBaseMtx.f6", 0.2f);
NEBULA_TUNABLE_NAMED(f32, cNum_calcAndSetBaseMtx_f7, "calcAndSetBaseMtx.f7", 0.8f);
NEBULA_TUNABLE_NAMED(f32, cNum_calcAndSetBaseMtx_f8, "calcAndSetBaseMtx.f8", 15.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_calcAndSetBaseMtx_f9, "calcAndSetBaseMtx.f9", 0.02f);
NEBULA_TUNABLE_NAMED(f32, cNum_calcAndSetBaseMtx_f10, "calcAndSetBaseMtx.f10", 80.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_calcAndSetBaseMtx_f11, "calcAndSetBaseMtx.f11", 0.2f);
NEBULA_TUNABLE_NAMED(f32, cNum_calcAndSetBaseMtx_f12, "calcAndSetBaseMtx.f12", 0.01f);
NEBULA_TUNABLE_NAMED(f32, cNum_calcAndSetBaseMtx_f13, "calcAndSetBaseMtx.f13", 0.2f);
NEBULA_TUNABLE_NAMED(f32, cNum_calcAndSetBaseMtx_f14, "calcAndSetBaseMtx.f14", 0.2f);
NEBULA_TUNABLE_NAMED(f32, cNum_calcAndSetBaseMtx_f15, "calcAndSetBaseMtx.f15", 0.8f);
NEBULA_TUNABLE_NAMED(f32, cNum_calcAndSetBaseMtx_f16, "calcAndSetBaseMtx.f16", 15.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_calcAndSetBaseMtx_f17, "calcAndSetBaseMtx.f17", 0.02f);
NEBULA_TUNABLE_NAMED(f32, cNum_calcAndSetBaseMtx_f18, "calcAndSetBaseMtx.f18", 160.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_calcAndSetBaseMtx_f19, "calcAndSetBaseMtx.f19", 0.5f);
NEBULA_TUNABLE_NAMED(f32, cNum_calcAndSetBaseMtx_f20, "calcAndSetBaseMtx.f20", 120.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_calcAndSetBaseMtx_f21, "calcAndSetBaseMtx.f21", 160.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_calcAndSetBaseMtx_f22, "calcAndSetBaseMtx.f22", 0.15f);
NEBULA_TUNABLE_NAMED(f32, cNum_calcAndSetBaseMtx_f23, "calcAndSetBaseMtx.f23", 30.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_calcAndSetBaseMtx_f24, "calcAndSetBaseMtx.f24", 2.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_calcAndSetBaseMtx_f25, "calcAndSetBaseMtx.f25", 15.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_calcAndSetBaseMtx_f26, "calcAndSetBaseMtx.f26", 0.15f);
NEBULA_TUNABLE_NAMED(f32, cNum_calcAndSetBaseMtx_f27, "calcAndSetBaseMtx.f27", 30.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_calcAndSetBaseMtx_f28, "calcAndSetBaseMtx.f28", 2.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_calcAndSetBaseMtx_f29, "calcAndSetBaseMtx.f29", 15.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_isJumping_f1, "isJumping.f1", 10.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_calcCenterPos_f1, "calcCenterPos.f1", 40.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_calcCenterPos_f2, "calcCenterPos.f2", 0.8f);
NEBULA_TUNABLE_NAMED(f32, cNum_calcCenterPos_f3, "calcCenterPos.f3", 0.2f);
NEBULA_TUNABLE_NAMED(f32, cNum_calcCenterPos_f4, "calcCenterPos.f4", 60.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_calcCenterPos_f5, "calcCenterPos.f5", 0.9f);
NEBULA_TUNABLE_NAMED(f32, cNum_calcCenterPos_f6, "calcCenterPos.f6", 0.1f);
NEBULA_TUNABLE_NAMED(f32, cNum_calcCenterPos_f7, "calcCenterPos.f7", 100.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_calcCenterPos_f8, "calcCenterPos.f8", 0.8f);
NEBULA_TUNABLE_NAMED(f32, cNum_calcCenterPos_f9, "calcCenterPos.f9", 0.2f);
NEBULA_TUNABLE_NAMED(f32, cNum_calcCenterPos_f10, "calcCenterPos.f10", 20.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_setPress_f1, "setPress.f1", 160.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_updateCameraInfo_f1, "updateCameraInfo.f1", 6.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_updateCameraInfo_f2, "updateCameraInfo.f2", 500.0f);

namespace {
    // The air the player has under water (8).
    NEBULA_TUNABLE(s32, cWaterAir, 8);
    // A hit takes all the player's health.
    NEBULA_TUNABLE(bool, cHitTakesAll, false);
    // A Daredevil comet leaves the player one health.
    NEBULA_TUNABLE(bool, cDarkCometOneHealth, true);
};  // namespace

bool gIsLuigi;

MarioActor::MarioActor(const char* pName) : LiveActor(pName), mFlashFogColor(0xFFFFFFFF) {
    lumacore::setClass(this, lumacore::kClassId< MarioActor >);
    initMember();
    mMario = new Mario(this);
    mRushSensorCount = 0;
    mTakeCount = 0;
    const u32 startingHealth = static_cast< u32 >(GameRules::playerHealth());
    mMaxHealth = startingHealth;
    mHealth = startingHealth;
    mWaterLife = ::cWaterAir;

    if (MR::isPlayerLuigi()) {
        mMaxHealth = startingHealth;
        mHealth = startingHealth;
    }

    if (MR::isGalaxyDarkCometAppearInCurrentStage() && ::cDarkCometOneHealth) {
        mMaxHealth = 1;
        mHealth = 1;
    }

    init2D();
    mTrampleCombo = 0;
    mFrameCount = 0;
    mLastRushEndFrame = 0;
    mLifeDamageQueue = 0;
    mStunTimer = 0;
    mPressTimer = 0;
    mPressEndTimer = 0;
    mPressSwellTimer = 0;
    mFloorMaterialHold = 0;
    mGravityBlendTimer = 0;
    mPunchHitTimer = 0;
    mSpinIntervalTimer = 0;
    mDamageFlashTimer = 0;
    mSpinTicoTimer = 0;
    mSpinStarTimer = 0;
    mSpinStarEndTimer = 0;
    mThrowVecTimer = 0;
    mPlayerModeTimer = 0;
    mMorphTimer = 0;
    mMorphEndTimer = 0;
    mItemPickupBlock = 0;
    mBombTeresaTimer = 0;
    mMovementTimer = 0;
    mSpinEffectRadius = 0.0f;
    mSuperKinokoCollected = false;
    mPowerupCollected = false;
    mTransforming = false;
    mAirPunchHitCount = 0;
    mPressKind = 0;
    mPressScale = 1.0f;

    mPressAxis.zero();

    mTakeCandidate = 0;
    mThrowTarget = 0;
    mThrowMemoSensor = 0;
    mRushSensor = nullptr;
    mLastRushSensor = 0;
    mTaking = 0;
    mHiddenByDemo = 0;
    mHidden = 0;
    mMorphHidden = 0;
    mModHidden = false;

    mThrowProbePos.zero();

    mNullAnimationKind = 0;
    mPlayerMode = 0;
    mPrevPlayerMode = 0;
    mMorphFlashDone = 0;
    mMorphAnimDone = 0;
    mSwingPermission = 1;
    mScoutSensor = 0;

    mScoutPrevRange = 0.0f;
    mScoutRange = 0.0f;


    mBeeWallWalk = 0;
    mBeeStickInhibit = false;
    mInDemo = false;
    mBaseMtxForced = false;
    mAttacking = false;
    mTrampling = false;

    NebulaMtxIdentity(mForcedBaseMtx.mMtx);
    NebulaMtxIdentity(mBaseMtx.mMtx);

    mWallShadePos.zero();
    mWallShadeNormal.zero();

    mBlackHole = nullptr;
    mWallShadeDist = cNum_MarioActor_f1;

    mBlackHolePosition.zero();
    mBlackHoleRotateAxis.zero();
    mPosRelativeToBlackHole.zero();

    mModels = nullptr;
    mModPowerUpModels = nullptr;
    mNumModPowerUps = 0;
    mModForm = 0;
    mPrevModForm = 0;

    mPreWipeOn = false;



    mPreWipeTimer = 0;
    mFootPrint = nullptr;
    mInRush = false;
    mSwingInhibitTimer = 0;
    mTakenSensor = 0;
    mMorphItemSensor = nullptr;
    mClapCatchStart = false;
    mBlendMtxTimer = 0;

    mBlendMtxRate = 1.0f;

    mRushEndFront.zero();

    mRushEndFrontMode = 0;
    mRemoteDemo = false;
    mAnimationFrozen = false;
    mForceKilled = false;
    mGameOverKind = 0;
    mMudWalkTimer = 0;
    mFlashFogTimer = 0;
    mFlashFogRate = 0.0f;

    mFlashFogColor.set(0xFF, 0xFF, 0xFF, 0);

    mFlashFogOn = false;
    mTrampleJumpStep = 0;

    mRushButton = 2;
    mSpinButton = 2;



    mJumpButton = 3;

    mHipDropButton = 7;

    resetPadSwing();


    mMainSpinBlock = 0x3C;
    mSubSpinBlock = 0x3C;

    mSwingTrigger = 0;
    mThrowAimSensor = 0;
    mThrowAimHold = 0;

    NebulaMtxIdentity(mFootRMtx.mMtx);
    NebulaMtxIdentity(mCapMtx.mMtx);

    mBlueFireDamage = 0;
    mDarkMaskOn = false;
    mCareMovingObject = false;
}

MarioActor::~MarioActor() {
    delete mMario;
}

void MarioActor::init(const TableIter& rInfo) {
    s32 initialAnimation = -1;
    if (!MR::isValidInfo(rInfo)) {
        return;
    }

    Vec3f position, rotation, scale;

    MR::getPlacementTrans(rInfo, &position);
    MR::getPlacementRotate(rInfo, &rotation);
    MR::getPlacementScale(rInfo, &scale);

    rotation.x = MR::repeat(rotation.x, 0.0f, cNum_init_f1);
    rotation.y = MR::repeat(rotation.y, 0.0f, cNum_init_f2);
    rotation.z = MR::repeat(rotation.z, 0.0f, cNum_init_f3);

    if (MR::isExistPlacementArg(rInfo)) {
        MR::getPlacementArg0NoInit(rInfo, &initialAnimation);
    }

    init2(position, rotation, initialAnimation);
}

void MarioActor::init2(const Vec3f& rA, const Vec3f& rB, s32 initialAnimation) {
    mInitializing = 1;
    gIsLuigi = false;
    if (MR::isPlayerLuigi()) {
        gIsLuigi = true;
    }

    mPosition.set(rA);
    mRotation.set(rB);
    mScale.set(Vec3f(1.0f, 1.0f, 1.0f));
    mMario->setHeadAndFrontVecFromRotate(mRotation);
    mMario->mGravityFixSideVec = mMario->mSideVec;
    updateBaseScaleMtx();
    mShadowRotate = mRotation;
    initDrawAndModel();

    mGlobalJointMtx = new Mtx[MR::getJointNum(this)];

    MR::connectToScene(this, MR::MovementType_Player, MR::CalcAnimType_Player, MR::DrawBufferType_Player, MR::DrawType_Player);
    MR::initLightCtrlForPlayer(this);
    mMarioAnim = new MarioAnimator(this);
    mMarioEffect = new MarioEffect(this);
    mCollisionShadow = new CollisionShadow(cNum_init2_f1, cNum_init2_f2);
    mConst = new MarioConst();
    if (gIsLuigi) {
        mConst->changeTable(1);
    }

    mMario->initAfterConst();
    mGravityInfo = new GravityInfo();
    mGravityRatio = 1.0f;
    initNerve(GET_NERVE(MarioActor, MarioActorNrvWait));
    mWaitNextNerve = 0;
    mWaitTimer = 0;
    initActionMatrix();
    initBinder(cNum_init2_f3, 0.0f, 8);
    mBinderOffset.x = 0.0f;
    mBinderOffset.y = cNum_init2_f4;
    mBinderOffset.z = 0.0f;
    mBinder->mIsMoveWithCollision = false;
    MR::setBinderOffsetVec(this, &mBinderOffset, false);

    mBinder->setTriangleFilter(MR::createTriangleFilterDelegator(this, &MarioActor::binderFilter));

    mBinder->mIsSkipFirstCheck = true;
    initEffect();
    MR::invalidateClipping(this);

    mGravityVec.set(0.0f, -1.0f, 0.0f);

    mRawGravityVec = mGravityVec;
    mGravityJumpTimer = 0;
    mFirstMorphCameraOn = 0;
    mFirstMorphCameraEnd = 0;

    mLastMove.zero();
    mPrevControlPos = mPosition;
    calcCenterPos();
    initSound(0x10, 0);
    addToSoundObjHolder();
    initParts();
    initMorphStringTable();
    MR::declareGlobalEventCameraAbyss("奈落カメラ");
    MR::declareBlackHoleCamera("ブラックホール");
    MR::declareGlobalEventCameraDead("昇天カメラ", cNum_init2_f5, 0x78, 0x3C);
    MR::declareGlobalEventCamera("水中フォロー");
    MR::declareGlobalEventCamera("水中プラネット");
    MR::declareGlobalEventCamera("水上フォロー");
    MR::declareGlobalEventCamera("引き戻し");
    MR::declareEventCameraProgrammable("変身初出カメラ");
    mBeePoseHeadPos = mPosition;
    mBeePoseTrans.zero();
    mBeePoseVelocity.zero();
    setupSensors();
    MR::getMarioHolder()->setMarioActor(this);
    mMessenger = new MarioMessenger(getSensor("dummy"));
    mCameraUpTarget = mMario->mHeadVec;
    mUpVec = mCameraUpTarget;
    mCameraUpTimer = 0;
    mCameraLiftTimer = 0;
    MR::setGameCameraTargetToPlayer();
    mWallShadeOn = 0;
    mFootPrint = new FootPrint("足跡", 0x100, -1);
    mFootPrint->setTexture(MR::getTexFromArc("Footprint.bti", this));
    switch (initialAnimation) {
    case 1:
        mMario->changeAnimation("基本");
        break;
    case 2:
        mMario->changeAnimationNonStop("ウォークイン");
        break;
    default:
        mMario->changeAnimation("ステージインA");
        break;
    }

    updateTransForCamera();
    mBindOn = 1;
    mBindRatio = 0.0f;
    mControlVelocity.zero();
    mMovementMove.zero();
    mLastPushMove.zero();
    mHandFixedPos = new FixedPosition(this, "HandR", Vec3f(0.0f, 0.0f, 0.0f), Vec3f(0.0f, 0.0f, 0.0f));
    mHandHoldFixedPos = new FixedPosition(this, "HandR", Vec3f(cNum_init2_f6, cNum_init2_f7, cNum_init2_f8), Vec3f(cNum_init2_f9, cNum_init2_f10, cNum_init2_f11));
    mTakingFixedPos = 0;
    mBodyRadius = cNum_init2_f12;
    mBodyHeight = cNum_init2_f13;
    mBodyUp.set(0.0f, 1.0f, 0.0f);
    mBodyDown = -mBodyUp;
    mHidden = true;
    appear();
    mHidden = false;  // do we change this to control appearances?
    mScreenBlurOn = 0;
    mScreenBlurGrow = 0.0f;
    mScreenBlurTrailGrow = 0.0f;
    mScreenBlurAlpha = 0;
    mScreenBlurTrailAlpha = 0;
    mReflectSimpleAlpha = 0;
    mWaterReflectAlpha = 0;
    mRippleKey = new u8[32];  // the frame take's key
    mRasterScrollAmp = 0.0f;
    mRasterScrollPhase = 0;
    mRasterScrollWave = 0.0f;
    mMoveFrontVec.set(1.0f, 0.0f, 0.0f);

    mInitializing = 0;  // is this to indicate that we are in the process of initialization?
}

void MarioActor::initAfterPlacement() {
    updateGravityVec(true, true);
    mMario->mAirGravityVec = mGravityVec;
    mMario->mHeadVec = -mGravityVec;
    mMario->mPostureUpVec = -mGravityVec;
    mCameraUpTarget = mMario->mHeadVec;
    mUpVec = mCameraUpTarget;
    mBinderOffset = mGravityVec * -cNum_initAfterPlacement_f1;
    calcCenterPos();
    MR::updateHitSensorsAll(this);
    mBeePoseDir = getGravityVector();
    mBaseMtxSet = true;
    mBeePoseGravity = getGravityVector();
    updateCameraInfo();
    calcBaseFrontVec(-mGravityVec);
}

void MarioActor::initAfterOpeningDemo() {
    mMario->changeAnimationNonStop("ウォークイン");
    mFrameCount = 0;
}

void MarioActor::calcBaseFrontVec(const Vec3f& rVec) {
    Vec3f cross, j(0.0f, 1.0f, 0.0f);
    f32 y = j.dot(rVec);
    if (y < -cNum_calcBaseFrontVec_f1) {
        mBaseFrontVec.set(0.0f, 0.0f, 1.0f);
    } else {
        f32 cosine;
        if (MR::makeAxisAndCosignVecToVec(&cross, &cosine, rVec, j)) {
            Vec3f k(0.0f, 0.0f, 1.0f);
            Mtx transformation;
            NebulaMtxRotAxisRad(transformation, &cross, -MR::acos(cosine));
            NebulaMtxMultVecSR(transformation, &k, &mBaseFrontVec);
            MR::normalize(&mBaseFrontVec);
        } else {
            mBaseFrontVec.set(0.0f, 0.0f, 1.0f);
        }
    }
}

void MarioActor::playSound(const char* pName, s32 duration) {
    mMario->playSound(pName, duration);
}

void MarioActor::changeAnimation(const char* pName, const char* pNewName) {
    mMario->changeAnimation(pName, pNewName);
}

void MarioActor::changeAnimationNonStop(const char* pName) {
    mMario->changeAnimationNonStop(pName);
}

void MarioActor::changeAnimationUpper(const char* pName) {
    if (mMario->mTargetWalkSpeedIndex == 0 && isAnimationRun("基本")) {
        mMario->changeAnimation(pName);
        return;
    }

    mMario->changeAnimationUpper(pName);
}

void MarioActor::stopAnimation(const char* pName) {
    mMario->stopAnimation(pName);
}

bool MarioActor::isAnimationRun(const char* pName) const {
    return mMario->isAnimationRun(pName);
}

void MarioActor::changeNullAnimation(const char* pName, s8 num) {
    mNullAnimation->appear();
    MR::startBck(mNullAnimation, pName);
    mNullAnimationKind = num;
}

void MarioActor::clearNullAnimation(s8 num) {
    mNullAnimationKind = num;
    mNullAnimation->kill();
}

bool MarioActor::isStopNullAnimation() const {
    if (!MR::isBckStopped(mNullAnimation)) {
        return MR::isDead(mNullAnimation);
    }

    return true;
}

void MarioActor::changeGameOverAnimation() {
    int animation = 0;
    if (mMario->isStatusActive(MarioStatus_FpView)) {
        mMario->closeStatus(nullptr);
    }

    if (mMario->isAnimationRun("前方小ダメージ")) {
        animation = 0;
    }

    if (mMario->isAnimationRun("後方小ダメージ")) {
        animation = 0;
    }

    if (mMario->isAnimationRun("ファイアラン前兆")) {
        animation = 0;
    }

    if (mMario->isAnimationRun("炎のランナー")) {
        animation = 0;
    }

    if (mMario->isAnimationRun("電気ダメージ")) {
        animation = 1;
    }

    if (mMario->isAnimationRun("電気ダメージ終了")) {
        animation = 1;
    }

    if (mMario->isAnimationRun("炎ダメージ")) {
        animation = 2;
    }

    if (mMario->isAnimationRun("ファイアダンス")) {
        animation = 2;
    }

    if (mMario->isAnimationRun("中ダメージ")) {
        animation = 3;
    }

    if (mMario->isAnimationRun("中ダメージ空中")) {
        animation = 3;
    }

    if (mMario->isAnimationRun("中ダメージ着地")) {
        animation = 3;
    }

    if (mMario->isAnimationRun("中後ダメージ")) {
        animation = 4;
    }

    if (mMario->isAnimationRun("中後ダメージ空中")) {
        animation = 4;
    }

    if (mMario->isAnimationRun("中後ダメージ着地")) {
        animation = 4;
    }

    if (mMario->isAnimationRun("落下")) {
        animation = 5;
    }

    if (mMario->isAnimationRun("空中ふんばり")) {
        animation = 5;
    }

    if (mMario->isAnimationRun("つぶれ")) {
        animation = 6;
    }

    if (mMario->isAnimationRun("つぶれ復帰")) {
        animation = 6;
    }

    if (mMario->isAnimationRun("氷結") || mMario->isStatusActive(MarioStatus_Freeze)) {
        animation = -1;
    }

    if (mMario->isSwimming()) {
        animation = 7;
    }

    if (isNerve(GET_NERVE(MarioActor, MarioActorNrvGameOverSink))) {
        animation = 8;
    }

    if (MR::getKarikariClingNum() && mMario->getMovementStates().onGround) {
        animation = 4;
    }

    if (mGameOverKind == 2) {
        animation = 9;
    }

    if (mGameOverKind == 1) {
        animation = 10;
    }

    switch (animation) {
    case 0:
        mMario->changeAnimationNonStop("座りダウン");
        break;
    case 1:
        mMario->changeAnimationNonStop("感電ダウン");
        break;
    case 2:
        mMario->changeAnimationNonStop("炎ダウン");
        break;
    case 3:
        mMario->changeAnimationNonStop("仰向けダウン");
        break;
    case 4:
        mMario->changeAnimationNonStop("俯せダウン");
        break;
    case 5:
        if (mMario->getMovementStates().onGround) {
            mMario->changeAnimationNonStop("座りダウン");
        } else {
            mMario->changeAnimationNonStop("奈落ダウン");
        }

        break;
    case 6:
        mMario->changeAnimationNonStop("つぶれダウン");
        break;
    case 7:
        mMario->changeAnimationNonStop("水泳ダウン");
        break;
    case 8:
        mMario->changeAnimationNonStop("埋まりダウン");
        break;
    case 9:
        mMario->changeAnimationNonStop("レース負け");
        break;
    case 10:
        mMario->changeAnimationNonStop("水中レース負け");
        break;
    }

    mAnimationFrozen = true;
    stopEffect("無敵中");
    mBlurState = 0;
}

XjointTransform* MarioActor::getJointCtrl(const char* pName) const {
    XanimeCore* pCore = mMarioAnim->mXanimePlayer->mCore;
    return pCore->getJointTransform(MR::getJointIndex(this, pName));
}

void MarioActor::updateRotationInfo() {
    Rot3f baseRot;
    MR::multMtx(baseRot, mBaseCorrectionMtx, getBaseMtx());
    MR::makeRTFromMtxPtr(nullptr, &mRotation, baseRot, true);
    if (mRotation.z > cNum_updateRotationInfo_f1 && mRotation.x > cNum_updateRotationInfo_f2) {
        f32 diff = cNum_updateRotationInfo_f3 - mRotation.y;
        mRotation.z = 0.0f;
        mRotation.y = diff;
        mRotation.x = 0.0f;
    }

    baseRot.getEuler(mBaseEuler);
    mBaseEuler.mult(cNum_updateRotationInfo_f4);
    baseRot.getEuler(mFrontEuler);
    mFrontEuler.mult(cNum_updateRotationInfo_f5);
    if (MR::isSameDirection(mGravityVec, mMario->mFrontVec)) {
        mShadowRotate = mRotation;
    } else {
        Pos3f shadowMtx;
        Vec3f up = -mGravityVec;
        MR::makeMtxUpFront(&shadowMtx, up, mMario->mFrontVec);
        MR::makeRTFromMtxPtr(nullptr, &mShadowRotate, shadowMtx, true);
        if (mShadowRotate.z > cNum_updateRotationInfo_f6 && mShadowRotate.x > cNum_updateRotationInfo_f7) {
            f32 diff = cNum_updateRotationInfo_f8 - mShadowRotate.y;
            mShadowRotate.z = 0.0f;
            mShadowRotate.y = diff;
            mShadowRotate.x = 0.0f;
        }
    }
}

void MarioActor::exeWait() {
    if (mWaitTimer != 0) {
        mWaitTimer--;
        if (mWaitTimer == 0) {
            setNerve(mWaitNextNerve);
            mWaitNextNerve = nullptr;
        }
    }
}

void MarioActor::movement() {
    mTakeCandidate = nullptr;
    mMovementTimer++;
    mSwingTrigger = 0;
    NebulaMtxCopy(mViewMtx.toMtxPtr(), mPrevViewMtx.toMtxPtr());
    NebulaMtxCopy(MR::getCameraViewMtx(), mViewMtx.toMtxPtr());
    updateCameraInfo();
    mHomingSensor = 0;
    mPushSensorAngle = HALF_PI / cNum_movement_f1;
    LiveActor::movement();
    Vec3f moved(mPosition - mControlPos);
    Vec3f pushMove(moved - mVelocity);
    mMovementMove = moved;
    Vec3f prevPushMove(mLastPushMove);
    mLastPushMove = pushMove;
    if (MR::isOppositeDirection(mLastPushMove, prevPushMove)) {
        f32 pushLength = mLastPushMove.length();
        f32 prevPushLength = prevPushMove.length();
        if (!MR::isNearZero(pushLength) && !MR::isNearZero(prevPushLength) && MR::isNearZero(pushLength - prevPushLength, 1.0f)) {
            mPosition -= mLastPushMove * cNum_movement_f2;
        }
    }

    if (pushMove.length() > cNum_movement_f3) {
        if (!(getMovementStates().squat)) {
            if (!MR::isNearZero(mVelocity)) {
                f32 diffMag = (mControlPos - mPrevControlPos).length();
                f32 vMag = mVelocity.length();
                if (pushMove.length() > cNum_movement_f4 * (diffMag + vMag)) {
                    mMario->stopWalk();
                }
            }
        }

        if (getMovementStates().slopeSlide && mVelocity.length() < moved.length()) {
            if (moved.dot(getGravityVec()) < -0.0f) {
                Vec3f slideVel;
                MR::vecKillElement(mVelocity, getGravityVec(), &slideVel);
                if (MR::isNearZero(slideVel)) {
                    MR::vecKillElement(moved, getGravityVec(), &slideVel);
                }

                slideVel.setLength(moved.length());  // needs to be inlined
                mMario->push(slideVel);
                if (mMario->mAirTimer <= 2) {
                    f32 scale = pushMove.length();
                    if (scale > cNum_movement_f5) {
                        scale = cNum_movement_f6;
                    }

                    mMario->mJumpVec += -getGravityVec() * scale;
                }
            }
        } else if (getMovementStates().slopeSlide) {
            Vec3f slopeDir(mMario->mSlopeMoveVec);
            MR::normalizeOrZero(&slopeDir);
            Vec3f offSlope;
            f32 elementA = MR::vecKillElement(moved, slopeDir, &offSlope);
            f32 elementB = MR::vecKillElement(mVelocity, slopeDir, &offSlope);
            if (mVelocity.length() > cNum_movement_f7 && elementA < elementB * cNum_movement_f8) {
                if (mMario->isAnimationRun("坂すべり下向きあおむけ")) {
                    mMario->push(mMario->mFrontVec * cNum_movement_f9);
                } else if (mMario->isAnimationRun("坂すべり上向きうつぶせ")) {
                    mMario->push(mMario->mFrontVec * -cNum_movement_f10);
                }

                mMario->mActionStates.slopePushed = true;
            }
        }

        if (getMovementStates().jumping && mBeeWallWalk == 0) {
            if (pushMove.dot(getGravityVec()) < -cNum_movement_f11) {
                Vec3f probeStart(mPosition - getGravityVec() * cNum_movement_f12);
                Vec3f floorHit;
                Triangle* pTmp = mMario->getTmpPolygon();

                if (MR::getFirstPolyOnLineToMap(&floorHit, pTmp, probeStart, getGravityVec() * cNum_movement_f13)) {
                    Vec3f hitOffGravity;
                    if (MR::vecKillElement(floorHit - (mPosition), getGravityVec(), &hitOffGravity) < -cNum_movement_f14 && pTmp->mParts && !pTmp->mParts->mStillCount &&
                        getMovementStates().launchKind != 1) {
                        mPosition = floorHit;
                        mMario->mPosition = mPosition;
                        mMario->stopJump();
                        mMario->changeAnimation("基本", "落下");
                        mMario->updateGroundInfo();
                    }
                }
            } else if (pushMove.dot(getGravityVec()) > cNum_movement_f15) {
                if (mMario->mCeilPolygon->isValid()) {
                    if (MR::isSensorPressObj(mMario->mCeilPolygon->mSensor)) {
                        mMario->mDrawStates.pressed = true;
                    }
                } else {
                    const u32 stop = mBinder->mPlaneNum;
                    for (int i = 0; i < stop; i++) {
                        if (MR::isSensorPressObj(mBinder->getPlane(i)->mParentTriangle.mSensor)) {
                            mMario->mDrawStates.pressed = true;
                        }
                    }
                }

                if ((mMario->mShadowPos - mPosition).dot(getGravityVec()) < 0.0f) {
                    bool eject = true;
                    CollisionParts* parts = mMario->mShadowPolygon->mParts;
                    if (parts && !mMario->mShadowPolygon->mParts->mStillCount) {
                        Vec3f shadowLocal, shadowPrev, partsMove;
                        NebulaMtxMultVec(parts->mInvBaseMatrix.toMtxPtr(), &mMario->mShadowPos, &shadowLocal);
                        NebulaMtxMultVec(parts->mPrevBaseMatrix.toMtxPtr(), &shadowLocal, &shadowPrev);
                        partsMove = mMario->mShadowPos - shadowPrev;
                        if (partsMove.dot(pushMove) > 0.0f) {
                            eject = false;
                        }
                    }

                    if (eject) {
                        mPosition = mMario->mShadowPos;
                        mMario->mJumpVec.zero();
                        mMario->mLookOfsBody.zero();
                        if (!mMario->mOnimasuSensor && (getDrawStates().pressed || getPrevDrawStates().pressed)) {
                            Vec3f pressAxis(pushMove);
                            MR::normalizeOrZero(&pressAxis);
                            mPressAxis = pressAxis;
                            setPress(0, 0);
                            mPressScale = cNum_movement_f16;
                        }
                    }
                }
            }
        } else if (mMario->getMovementStates().onGround) {
            const u32 stop = mBinder->mPlaneNum;
            bool eject = false;
            for (u32 i = 0; i < stop; i++) {
                const Triangle* plane = &mBinder->getPlane(i)->mParentTriangle;
                if (!MR::isSensorPressObj(plane->mSensor)) {
                    continue;
                }

                if (mMario->mGroundNorm.dot(*plane->getNormal(0)) > 0.0f) {
                    if (mMario->mCeilDist < cNum_movement_f17) {
                        eject = true;
                    } else if (plane->getNormal(0)->dot(moved) < 0.0f) {
                        eject = true;
                    } else {
                        eject = false;
                    }
                } else if (plane->getNormal(0)->dot(moved) < 0.0f) {
                    eject = true;
                } else if (mMario->mOnimasuSensor) {
                    if (!MR::isWallCodeNoAction(plane) && !mMario->isOnimasuBinderPressSkip()) {
                        mPressAxis = mMario->mGroundNorm;
                        mMario->mActionStates.onimasuWrapped = false;
                        *mMario->mCeilPolygon = *plane;
                        setPress(2, 0);
                        mPressScale = cNum_movement_f18;
                    }

                    eject = true;
                }
            }

            if (eject) {
                Vec3f offGround;
                f32 element = MR::vecKillElement(moved, mMario->mGroundNorm, &offGround);
                mPosition -= mMario->mGroundNorm * element;
            }
        }
    }

    if (mMario->mDrawStates.updated) {
        mMario->mDrawStates.updated = false;
        mMarioAnim->MarioAnimator::update();
    }

    mMario->recordRelativePosition();
    updateTransForCamera();
    calcAnimInMovement();
    mRushJustEnded = false;
    mMario->mWalkingResist = 0.0f;
    mMoveFrontVec = mMario->mFrontVec;
}

void MarioActor::control() {
    if (mSuperKinokoCollected) {
        if (MR::tryStartDemoWithoutCinemaFrame(this, "マリオスーパー化")) {  // 6-up camera
            mSuperKinokoCollected = false;
            const s32 lifeUpHealth = GameRules::lifeUpHealth();
            if (lifeUpHealth != 0) {
                changeMaxLife(lifeUpHealth);
            }
            MR::stopAnimFrame(this);
            MR::requestPowerUpHPMeter();
            mMario->startPadVib("マリオ[変身]");
            MR::startSystemSE("SE_SY_SUPER_KINOKO_GET");
            mMorphEndTimer = 0x5a;
        }
    } else if (mPowerupCollected) {
        if (MR::tryStartDemoWithoutCinemaFrame(this, "マリオ変身")) {
            mPowerupCollected = false;
            mTransforming = true;
            if (mPlayerMode == 6) {
                MR::requestMovementOn(mTeresaModel);
                changeTeresaAnimation("change", -1);
            }

            mMorphTimer = 0x40;
            MR::stopAnimFrame(this);
            playEffect("変身");
            mMario->startPadVib("マリオ[変身]");
        }
    }

    control2();
    mControlPos = mPosition;
}

void MarioActor::control2() {
    if ((mMorphTimer && mPlayerMode) || mMorphEndTimer) {
        mMario->mPushVec.zero();
        mMario->mPushVelocity.zero();
        mClapCoinCount = 0;
        mRushSensorCount = 0;
        mVelocity.zero();
        mLastMove.zero();
        mPrevControlPos = mPosition;
        if (getMovementStates().onGround && !MR::isSameMtx(mMario->mShadowPolygon->getBaseMtx()->toMtxPtr(), mMario->mShadowPolygon->getPrevBaseMtx()->toMtxPtr())) {
            mMario->mPosition = mPosition;
            mMario->checkEnforceMove();
            mPosition = mMario->mPosition;
        }
    } else {
        if (mDamageFlashTimer) {
            mDamageFlashTimer--;
        }

        controlMain();
        if (!mInDemo) {
            if (!mBindOn) {
                MR::offBind(this);
            } else {
                MR::onBind(this);
            }
        }

        mControlVelocity = mVelocity;
        if (getDrawStates().enforcedMove) {
            mBinder->mIsMovingReaction = false;
        } else {
            mBinder->mIsMovingReaction = true;
        }

        if (mMario->isDamaging()) {
            mTakenSensor = 0;
        }
    }
}

void MarioActor::controlMain() {
    if (mRemoteDemo) {
        mClapCoinCount = 0;
        mRushSensorCount = 0;
    }

    updateActionTrigger();
    updateControllerSwing();
    updateControllerSwingLeft();
    update2D();
    if (mPreWipeOn) {
        mPreWipeTimer++;
    }

    if (!isEnableMoveMario()) {
        mClapCoinCount = 0;
        mRushSensorCount = 0;
        careMovingObject();
    } else {
        mVelocity.zero();
        updateBehavior();
        updateRotationInfo();
        updateTakingPosition();
        updateSearchLight();
        updateThrowing();
        updateBeeWingAnimation();
        updateFairyStar();
        mRushSensorCount = 0;
    }
}

void MarioActor::updateBehavior() {
    updateLife();
    updatePlayerMode();
    mFrameCount++;
    if (mSwingInhibitTimer) {
        mSwingInhibitTimer--;
    }

    if (mFloorMaterialHold) {
        mFloorMaterialHold--;
    }

    if (mItemPickupBlock) {
        mItemPickupBlock--;
    }

    Vec3f vec = mPosition;
    vec -= mPrevControlPos;
    mLastMove = vec;
    mPrevControlPos = mPosition;
    updateBindRatio();
    updateEffect();
    if (mBombTeresaTimer && !--mBombTeresaTimer) {
        mMario->stopAnimationUpper("ハンマー投げ回転中");
        mMario->stopAnimation("ハンマー投げ回転中");
    }

    updatePunching();
    if (!doPressing() && !doStun() && !doRush()) {
        updateGravityVec(false, false);
        if (!tryJumpRush()) {
            checkPriorRushTarget();
            if (!tryThrow() && !tryStandardRush()) {
                if (getMovementStates().onGround) {
                    initForJump();
                }

                updateSwingTimer();
                updateSwingAction();
                updateThrowVector();
                lockOnPointer();
                if (!checkClapCatchStart()) {
                    mClapCoinCount = 0;
                    mMario->Mario::update();
                    updateForCamera();
                    updateTornado();
                    tryCoinPull();
                    mMario->mDrawStates.updated = true;
                }
            }
        }
    }
}

void MarioActor::updateBindRatio() {
    if (!mInRush && !MR::isNearZero(mControlVelocity - mLastMove)) {
        f32 mag = mControlVelocity.length();
        if (mag / (mControlVelocity - mLastMove).length() < cNum_updateBindRatio_f1) {
            mBindRatio += cNum_updateBindRatio_f2;
        } else {
            mBindRatio -= cNum_updateBindRatio_f3;
        }
    } else if (!MR::isNearZero(mControlVelocity)) {
        mBindRatio -= cNum_updateBindRatio_f4;
    }

    mBindRatio = MR::clamp(mBindRatio, 0.0f, 1.0f);
}

bool MarioActor::isInPunchTimerRange() const {
    bool ret = false;
    if (mPunchTimerSensor) {
        ret = MR::isInRange(mPunchFrame, cNum_isInPunchTimerRange_f1, cNum_isInPunchTimerRange_f2);
    }

    return ret;
}

void MarioActor::updatePunching() {
    if (mPunchHitTimer) {
        bool canSpinring = true;
        mPunchHitTimer--;
        mPunchFrame++;
        if (!mSpinIntervalTimer && !getMovementStates().spinBlocked) {
            canSpinring = false;
        }

        if (canSpinring && !mMario->isSwimming() && !mPunchHitTimer && selectAction("スピン回復エフェクト") == true) {
            playEffect("スピンリング");
        }

        bool canPunch = isInPunchTimerRange();
        if (!mMario->isSwimming()) {
            canPunch = true;
        }

        if (canPunch) {
            const Mario::MovementStates& moveStates = getMovementStates();
            if (moveStates.frontWall || moveStates.sideWall || moveStates.backWall) {
                mMario->tryWallPunch();
            }
        }
    }

    if (mMario->isAnimationRun("ハンマー投げリリース") && mMario->getMovementStates().onGround && !mStunTimer && !mMario->mMoveLockTimer && mMario->Mario::isStickOn()) {
        mMario->stopAnimation(nullptr);
    }
}

bool MarioActor::doRush() {
    if (mInRush) {
        tryCoinPull();
        tryRushInRush();
        if (!mInRush) {
            return false;
        }

        updateGravityVec(false, false);
        if (!checkClapCatchStart() && mClapCoinCount) {
            bodyClap();
        }

        mClapCoinCount = 0;
        mMario->mPosition = mPosition;
        mMario->updateGroundInfo();
        mMario->updateCubeCode();
        mMario->decDamageAfterTimer();
        // Those can end the rush (what it held let go, a place that ends it): nothing is held from here.
        if (!mInRush) {
            return false;
        }

        if (selectWaterInOutRush(mRushSensor)) {
            WaterInfo waterInfo;
            if (!MR::getWaterAreaObj(&waterInfo, mPosition)) {
                mMario->forceExitSwim();
            }
        } else if (!selectWaterInOutEffect(mRushSensor->mHost->mName)) {
            s32 initial = mMario->mSwim->mSwimState;
            mMario->mSwim->checkWaterCube(false);
            if (mMario->mSwim->mSwimState != initial) {
                if ((u32)mMario->mSwim->mSwimState <= MarioSwim::SWIM_STATE_ENTERING && (u32)initial - 2 <= 1) {
                    playEffectRT("水面ジャンプ水柱", mMario->mSwim->mSurfacePos, mMario->mSwim->mSurfaceNorm);
                    emitEffectWaterColumn(mMario->mSwim->mSurfacePos, mMario->mSwim->mSurfaceNorm);
                    // SWIM_STATE_UNDERWATER and SWIM_STATE_SURFACE
                } else if ((u32)initial <= 1 && (u32)(mMario->mSwim->mSwimState - MarioSwim::SWIM_STATE_UNDERWATER) <= 1) {
                    playEffectRT("水面ジャンプ水柱", -mMario->mMoveFrontVec, mMario->mSwim->mSurfaceNorm);
                    emitEffectWaterColumn(mMario->mSwim->mSurfacePos, mMario->mSwim->mSurfaceNorm);
                }

                if (initial == 2) {
                    mWaterLife = ::cWaterAir;
                    MR::getGameSceneLayoutHolder()->changeLifeMeterModeGround();
                    mMario->forceExitSwim();
                } else if (initial == 0) {
                    MR::getGameSceneLayoutHolder()->changeLifeMeterModeWater();
                }
            }
        }

        if (mInRush && mMario->isForceStopRush()) {
            bool received = mRushSensor->receiveMessage(ACTMES_RUSH_DAMAGE_CANCEL, getSensor("body"));
            if (mInRush && received) {
                RushEndInfo endInfo(nullptr, RushEndInfo::Mode_Release, Vec3f(0.0f, 0.0f, 0.0f), false, 0);
                endRush(&endInfo);
            }
        }

        mMarioAnim->setHoming();
        updateTornado();
        if (mInRush && selectRecoverFlyMeter(mRushSensor)) {
            for (u32 i = 0; i < 4; i++) {
                mMario->incAirWalkTimer();
            }
        }

        updateSwingTimer();
        return true;
    } else {
        return false;
    }
}

void MarioActor::updateSwingTimer() {
    if (mInRush) {
        stopSpinTicoEffect(false);
    } else {
        if (mSpinTicoTimer) {
            mSpinTicoTimer--;
        }

        if (mSpinStarTimer && --mSpinStarTimer == 6) {
            mSpinStarEndTimer = 5;
        }

        if (mSpinStarEndTimer && --mSpinStarEndTimer == 0) {
            mMario->startPadVib((u32)0);
            mMario->playSound("スピン回復終了");
            Color8 fogColor(80, 128, 200, 0);
            mFlashFogTimer = 0xf;
            mFlashFogRate = cNum_updateSwingTimer_f1;
            mFlashFogColor = fogColor;
            mFlashFogOn = true;
        }

        if (mSpinIntervalTimer) {
            if (--mSpinIntervalTimer == 0x18) {
                selectAction("スピン回復エフェクト");
                stopEffectForce("スピンリング");
            }

            if (mSpinIntervalTimer == 0xd) {
                mSpinStarTimer = 0x13;
            }
        }
    }
}

void MarioActor::updateSwingAction() {
    if (isJumping() && mPlayerMode != 6 && !mMario->isStatusActive(MarioStatus_Foo)) {
        mSpinIntervalTimer = 0;
    }

    bool requestRush = isRequestRush();
    bool requestSpinJump2P = false;
    if (!requestRush) {
        requestRush = isRequestSpinJump2P();
        requestSpinJump2P = requestRush;
    }

    mSwingTrigger = requestRush;
    if (mSpinIntervalTimer) {
        if (!requestRush) {
            return;
        }

        if (!isPunching()) {
            if (mSpinTicoTimer) {
                mSpinTicoTimer += 0x96;
            } else {
                mSpinTicoTimer += 0x12c;
            }
        }

        if (!requestRush) {
            return;
        }

        tryReleaseBombTeresa();
        return;
    }

    bool canRush = true;
    if (!requestRush) {
        return;
    }

    if (mMario->isAnimationRun("壁はじき")) {
        canRush = false;
    }

    if (isJumping() && mMario->mAirControlInhibitTimer) {
        canRush = false;
    }

    if (mPlayerMode == 9) {
        canRush = false;
    }

    if (mMario->getCurrentStatus() == 1) {
        canRush = false;
    }

    if (mMario->isSwimming()) {
        canRush = false;
    }

    if (mMario->isStatusActive(MarioStatus_Foo)) {
        canRush = false;
    }

    if (mMario->isStatusActive(MarioStatus_RecoveryOrWarp)) {
        canRush = false;
    }

    if (mTakeCount) {
        canRush = false;
    }

    if (mMario->isStatusActive(MarioStatus_Damage)) {
        canRush = false;
    }

    if (mRemoteDemo) {
        canRush = false;
    }

    if (mInDemo) {
        canRush = false;
    }

    if (!canRush) {
        return;
    }

    u8 action = selectAction("スピンアタック");
    switch (action) {
    case 1: {
        NebulaAchievements::countStat(NebulaStat_Spins, 1);
        bool didSpinPunch = true;
        if (!mMario->mMovementStates.tornado && isJumping() && !mMario->isDamaging() && !mMario->mMovementStates.spinBlocked) {
            bool subSpin = false;
            if (mSubSpinTrigger) {
                subSpin = true;
            }

            mMario->trySpinJump(subSpin);
            setPunchHitTimer(0x19);
            tryReleaseBombTeresa();
            if (requestSpinJump2P) {
                MR::start2PJumpAssistSound();
            }
        } else if (!getMovementStates().tornado && !mMario->isAnimationRun("地上ひねり")) {
            const char* pLastAnimationName = mMarioAnim->mXanimePlayerUpper->getCurrentAnimationName();
            if (mPlayerMode == 4) {
                if (!mMario->isAnimationRun("ハチスピン")) {
                    didSpinPunch = trySpinPunch();
                }
            } else {
                didSpinPunch = trySpinPunch();
            }

            mAirPunchHitCount = 0;
            if (pLastAnimationName != mMarioAnim->mXanimePlayerUpper->getCurrentAnimationName()) {
                mMario->playSound("パンチ風切り");
            }
        }

        if (mPlayerMode == 4) {
            if (isJumping()) {
                if (!mMario->isAnimationRun("ハチスピン空中")) {
                    mMario->playSound("声スピン");
                    mMario->playSound("スピンジャンプ");
                }

                mMario->changeAnimation("ハチスピン空中");
            } else if (getMovementStates().squat || mBeeWallWalk != 0) {
                mMario->changeAnimation("サマーソルト");
            } else {
                mMario->changeAnimation("ハチスピン");
            }
        }

        if (didSpinPunch) {
            mSpinIntervalTimer = mConst->getTable()->mSpinIntervalTime + 0x22;
        }

        break;
    }
    case 2:
        if (isEnableSpinPunch() && !mMario->isSwimming()) {
            shootFireBall();
        }

        break;
    case 3:
        if (isEnableSpinPunch()) {
            doFreezeAttack();
            setPunchHitTimer(0x1e);
        }

        break;
    case 4: {
        if (mMario->mTeresaWallThroughTimer != 0) {
            break;
        }

        mMario->startTeresaDisappear();
        const MarioConstTable* pConstants = mConst->getTable();
        mSpinIntervalTimer = pConstants->mTeresaWallThroughTime + pConstants->mSpinIntervalTime;
        break;
    }
    case 5:
        if (!isEnableSpinPunch()) {
            break;
        }

        if (isJumping()) {
            mMario->changeAnimation("ハチスピン空中");
        } else {
            if (getMovementStates().squat || mBeeWallWalk != 0) {
                mMario->changeAnimation("サマーソルト");  // Summersault
            } else {
                mMario->changeAnimation("ハチスピン");
            }
        }

        mSpinIntervalTimer = mConst->getTable()->mSpinIntervalTime + 0x22;
        break;
    }
}

bool MarioActor::checkClapCatchStart() {
    if (mClapCatchStart) {
        mClapCatchStart = false;
        tryCoinPullInRush();
        return true;
    } else {
        return false;
    }
}

void MarioActor::updateRealMtx() {
    updateBaseScaleMtx();
    NebulaMtxCopy(MR::getJointMtx(this, "HandL"), mHandLMtx.toMtxPtr());
    NebulaMtxCopy(MR::getJointMtx(this, "HandR"), mHandRMtx.toMtxPtr());
    NebulaMtxCopy(MR::getJointMtx(this, "Face0"), mFaceMtx.toMtxPtr());
    getRealMtx(mFootRMtx.toMtxPtr(), "FootR");
    getRealMtx(mCapMtx.toMtxPtr(), "CapPosition");
    getRealMtx(mHipMtx.toMtxPtr(), "Hip");
    getRealMtx(mSpine2Mtx.toMtxPtr(), "Spine2");
    Vec3f spinePos;
    MR::extractMtxTrans(mSpine2Mtx.toMtxPtr(), &spinePos);
    if (MR::isNan(spinePos)) {
        MR::getJointMtx(this, "Spine2");
        getBaseMtx();
    }

    bool notStatus12 = true;
    if (mMario->isStatusActive(MarioStatus_FpView)) {
        notStatus12 = false;
    }

    if (!notStatus12) {
        return;
    }

    Vec3f capPos, hipPos;
    MR::extractMtxTrans(mCapMtx.toMtxPtr(), &capPos);
    MR::extractMtxTrans(mHipMtx.toMtxPtr(), &hipPos);
    mBodyUp = capPos - hipPos;
    mBodyDown = hipPos - capPos;
    if (MR::normalizeOrZero(&mBodyUp)) {
        mBodyUp = mMario->mHeadVec;
    }

    if (MR::normalizeOrZero(&mBodyDown)) {
        mBodyDown = -mMario->mHeadVec;
    }

    if (mMario->getMovementStates().squat && mMario->mMovementStates.onGround) {
        mBodyDown = -mMario->mHeadVec;
    }

    mHeadPos = capPos;
}

void MarioActor::setUpperRotateY(f32 angle) {
    mMarioAnim->setUpperRotateY(angle);
}

void MarioActor::decLife(u16 amt) {
    if (getMovementStates().invincible) {
        return;
    }

    if (mLifeDamageQueue > 0 && amt > mLifeDamageQueue) {
        return;
    }

    if (amt) {
        mLifeDamageQueue = amt;
        return;
    }

    if (NebulaMods::fire(NEBULA_MOD_EVENT_PLAYER_DAMAGE, 1) == 0) {
        mLifeDamageQueue = 0;
        return;
    }

    if (mHealth) {
        mHealth = ::cHitTakesAll ? 0 : mHealth - 1;
    }

    mLifeDamageQueue = 0;
    // A Life Up's extra health gone, the most falls back to what the player starts with.
    const u32 startingHealth = static_cast< u32 >(GameRules::playerHealth());
    if (mMaxHealth <= startingHealth || mHealth > startingHealth) {
        return;
    }

    mMaxHealth = startingHealth;
}

void MarioActor::decLifeLarge() {
    decLife(0);
}

void MarioActor::decLifeMiddle() {
    decLife(0);
}

void MarioActor::decLifeSmall() {
    decLife(0);
}

void MarioActor::resetWaterLife() {
    mWaterLife = ::cWaterAir;
}

void MarioActor::updateLife() {
    if (!mLifeDamageQueue) {
        return;
    }

    if (--mLifeDamageQueue != 0) {
        return;
    }

    if (!mHealth) {
        return;
    }

    mHealth = ::cHitTakesAll ? 0 : mHealth - 1;
}

void MarioActor::incLife(u32 amt) {
    if (isEnableNerveChange() && !mLifeGainBlocked) {
        const u32 health = getHealth();
        if (mHealth != mMaxHealth) {
            mMario->playSound("ライフ回復");
        }

        mHealth += amt;
        if (mHealth >= mMaxHealth) {
            mHealth = mMaxHealth;
        }

        if (health == 1 && mMarioAnim->isAnimationStop()) {
            mMarioAnim->mXanimePlayer->changeTrackAnimation(3, "ノーマルウエイト");
            if (mMario->mRunningBas && !mMario->mRunningDamageWait) {
                mMario->startBas(nullptr, false, 0.0f, 0.0f);
                setBlink(nullptr);
            }
        }
    }
}

void MarioActor::changeMaxLife(s32 max) {
    mMaxHealth = max;
    while (mHealth != max) {
        if (mHealth > max) {
            decLife(0);
        } else if (mHealth < max) {
            incLife(1);
        } else {
            break;
        }
    }
}

bool MarioActor::doPressing() {
    if (!mPressTimer) {
        return false;
    }

    switch (mPressKind) {
    case 0:
        if (!mMario->checkVerticalPress(false)) {
            if (--mPressTimer == 0x1e && !mHealth && isEnableNerveChange()) {
                setNerve(GET_NERVE(MarioActor, MarioActorNrvGameOver));
            }

            if (!mPressTimer) {
                mMario->changeAnimation("つぶれ解除");
                mBindOn = true;
            }
        }

        break;
    case 2:
    case 3:
        if (--mPressTimer == 0x1e && !mHealth && isEnableNerveChange()) {
            setNerve(GET_NERVE(MarioActor, MarioActorNrvGameOver));
        }

        if (!mPressTimer) {
            mMario->changeAnimation("つぶれ解除");
            mBindOn = true;
        }

        break;
    case 4:
        if (mPressTimer > 2) {
            mPressTimer--;
        }

        mMario->mMovementStates.frontWall = false;
        mMario->mMovementStates.backWall = false;
        mMario->mMovementStates.sideWall = false;
        break;
    }

    if (mPressKind != 1) {
        careMovingObject();
    }

    mMarioAnim->setSpeed(0.0f);
    mMario->mPushVec.zero();
    mMario->mPushVelocity.zero();
    resetSensorCount();
    return true;
}

void MarioActor::careMovingObject() {
    if (mCareMovingObject) {
        return;
    }

    if (getMovementStates().onGround && !MR::isSameMtx(mMario->mShadowPolygon->getBaseMtx()->toMtxPtr(), mMario->mShadowPolygon->getPrevBaseMtx()->toMtxPtr())) {
        mMario->mPosition = mPosition;
        mMario->checkEnforceMove();
        mPosition = mMario->mPosition;
    }

    mMario->mPosition = mPosition;
    mMario->mVelocity.zero();
    mMario->powerAreaMove();
    mMario->powerRailMove();
    mMario->mPosition += mMario->mVelocity;
    mMario->mVelocity.zero();
    mPosition = mMario->mPosition;
    mMario->checkGround();
    mMario->updateFloorCode();
}

bool MarioActor::doStun() {
    if (mStunTimer) {
        mMario->mDrawStates.updated = true;
        resetSensorCount();
        if (getMovementStates().onGround && !MR::isSameMtx(mMario->mShadowPolygon->getBaseMtx()->toMtxPtr(), mMario->mShadowPolygon->getPrevBaseMtx()->toMtxPtr())) {
            mMario->mPosition = mPosition;
            mMario->checkEnforceMove();
            mPosition = mMario->mPosition;
        }

        if (!--mStunTimer) {
            f32 stickX, stickY;
            getStickValue(&stickX, &stickY);
            if (MR::isNearZero(stickX) && MR::isNearZero(stickY) && !getMovementStates().squat) {
                mMario->stopWalk();
            }
        }

        mMario->inputStick();
        return true;
    } else {
        return false;
    }
}

void MarioActor::scaleMtx(MtxPtr pRawMtx) {
    Vec3f i, j, k;
    f32 scalar = cNum_scaleMtx_f1 * (1.0f - mPressScale) + 1.0f;
    MR::extractMtxXYZDir(pRawMtx, &i, &j, &k);
    f32 elementX = MR::vecKillElement(i, mPressAxis, &i);
    f32 elementY = MR::vecKillElement(j, mPressAxis, &j);
    f32 elementZ = MR::vecKillElement(k, mPressAxis, &k);
    elementZ *= mPressScale;
    elementX *= mPressScale;
    elementY *= mPressScale;
    i.scale(scalar);
    j.scale(scalar);
    k.scale(scalar);
    i += mPressAxis * elementX;
    j += mPressAxis * elementY;
    k += mPressAxis * elementZ;
    pRawMtx[0][0] = i.x;
    pRawMtx[1][0] = i.y;
    pRawMtx[2][0] = i.z;
    pRawMtx[0][1] = j.x;
    pRawMtx[1][1] = j.y;
    pRawMtx[2][1] = j.z;
    pRawMtx[0][2] = k.x;
    pRawMtx[1][2] = k.y;
    pRawMtx[2][2] = k.z;
}

void MarioActor::updateBaseScaleMtx() {
    if (mMario->mMovementStates.scaleBody) {
        NebulaMtxScale(mBaseScaleMtx.toMtxPtr(), mScale.x, mScale.y, mScale.z);
    } else {
        NebulaMtxIdentity(mBaseScaleMtx.toMtxPtr());
    }
}

void MarioActor::getRealMtx(MtxPtr pMtx, const char* pName) const {
    MR::multMtx(pMtx, mBaseScaleMtx.mMtx, MR::getJointMtx(this, pName));
    Vec3f lookOfs(mMario->mLookOfs);
    MR::addTransMtx(pMtx, lookOfs);
}

void MarioActor::getRealPos(const char* pName, Vec3f* pVec) const {
    Mtx mtx;
    getRealMtx(mtx, pName);
    MR::extractMtxTrans(mtx, pVec);
}

MtxPtr MarioActor::getGlobalJointMtx(const char* pName) {
    u32 idx = MR::getJointIndex(this, pName);
    getRealMtx(mGlobalJointMtx[idx], pName);
    return mGlobalJointMtx[idx];
}

void MarioActor::calcAnimInMovement() {
    if (mHealth != 0 && (mMario->isDamaging() || mMario->getDamageAfterTimer())) {
        mFlashFogTimer = 0;
        mFogWaveTimer++;
    } else if (mFlashFogTimer != 0) {
        mFlashFogTimer--;
    }

    mBaseMtxSet = true;

    updateBlink();
    calcSpinEffect();

    switch (mCurrModel) {
    case 4:
        mInvincibleModel->movement();
        mInvincibleHandL->movement();
        mInvincibleHandR->movement();

        MR::setBrkFrame(mInvincibleHandL, MR::getBrkFrame(mInvincibleModel));
        MR::setBrkFrame(mInvincibleHandR, MR::getBrkFrame(mInvincibleModel));
    }

    if (mMorphEndTimer != 0) {
        mMorphEndTimer--;

        if (mMorphEndTimer == 0) {
            MR::endDemo(this, "マリオスーパー化");
            MR::releaseAnimFrame(this);


            MR::explainLifeUpIfAtFirst();
        }
    }

    if (mMorphTimer != 0) {
        if (mPlayerMode == 6 && mMario->mVerticalSpeed < mConst->getTable()->mTeresaWaitHeight) {
            mPosition -= getGravityVec();
        }

        bool val = true;
        switch (mPlayerMode) {
        case PlayerMode_Bee:
            val = MR::isOnGameEventFlagBeeMarioAtFirst();
            break;
        case PlayerMode_Teresa:
            val = MR::isOnGameEventFlagTeresaMarioAtFirst();
            break;
        case PlayerMode_Hopper:
            val = MR::isOnGameEventFlagHopperMarioAtFirst();
            break;
        case PlayerMode_Fire:
            val = MR::isOnGameEventFlagFireMarioAtFirst();
            break;
        case PlayerMode_Ice:
            val = MR::isOnGameEventFlagIceMarioAtFirst();
            break;
        case PlayerMode_Foo:
            val = MR::isOnGameEventFlagFlyingMarioAtFirst();
            break;
        }

        if (val == false) {
            // Mario faces the item that gave the form, when one did (a morph can end it first).
            Vec3f frontVec;
            if (mMorphItemSensor != nullptr) {
                MR::calcFrontVec(&frontVec, mMorphItemSensor->mHost);
                mMario->setFrontVecKeepUp(frontVec);
            } else {
                MR::calcFrontVec(&frontVec, this);
            }

            if (mFirstMorphCameraOn == 0) {
                MR::zoomInTargetGameCamera();
                CameraTargetArg camera(nullptr, nullptr, nullptr, this);
                MR::startGlobalEventCamera("変身初出カメラ", camera, 60);

                Vec3f vec(mCenterPos);

                f32 scale = cNum_calcAnimInMovement_f1;
                if (mPlayerMode == 6) {
                    scale = cNum_calcAnimInMovement_f2;
                }

                if (getMovementStates().hipDrop && !getMovementStates().onGround) {
                    vec += mMario->mJumpVec;
                }

                MR::setProgrammableCameraParam("変身初出カメラ", vec, vec + frontVec * scale, -mGravityVec, true);
            }

            mFirstMorphCameraOn = 1;
        }

        if (--mMorphTimer == 0) {
            if (mTransforming) {
                MR::endDemo(this, "マリオ変身");
                mTransforming = false;
            }

            MR::releaseAnimFrame(this);
            mMorphAnimDone = 1;

            // The first transformation's explanation, by the mode.
            switch (mPlayerMode) {
            case PlayerMode_Invincible:
                MR::explainInvincibleMarioIfAtFirst();
                break;
            case PlayerMode_Fire:
                MR::explainFireMarioIfAtFirst();
                break;
            case PlayerMode_Ice:
                MR::explainIceMarioIfAtFirst();
                break;
            case PlayerMode_Bee:
                MR::explainBeeMarioIfAtFirst();
                break;
            case PlayerMode_Hopper:
                MR::explainHopperMarioIfAtFirst();
                break;
            case PlayerMode_Teresa:
                MR::explainTeresaMarioIfAtFirst();
                break;
            case PlayerMode_Foo:
                MR::explainFlyingMarioIfAtFirst();
                break;
            }

            if (mFirstMorphCameraOn != 0) {
                mFirstMorphCameraEnd = 10;
            }

            mFirstMorphCameraOn = 0;
        }

        mMorphFlashDone = 1;
    } else {
        mMario->updateMorphResetTimer();
    }

    if (mFirstMorphCameraEnd != 0 && --mFirstMorphCameraEnd == 0) {
        MR::zoomOutTargetGameCamera();
        MR::endGlobalEventCamera("変身初出カメラ", 60, true);
    }
}

void MarioActor::forceSetBaseMtx(MtxPtr pMtx) {
    mBaseMtxForced = true;
    mBaseMtxSet = true;
    NebulaMtxCopy(pMtx, mForcedBaseMtx);
    MR::extractMtxTrans(pMtx, &mCameraTrans);
    if (mHidden) {
        MR::extractMtxTrans(pMtx, &mPosition);
    }

    MR::extractMtxZDir(pMtx, &mCameraFrontVec);
    MR::extractMtxYDir(pMtx, &mUpVec);
    MR::extractMtxXDir(pMtx, &mCameraSideVec);
    MR::updateHitSensorsAll(this);
    mMario->invalidateRelativePosition();
    mMario->mSlopeMoveVec.zero();
    mMario->mMovementStates.slopeSlide = 0;
}

void MarioActor::calcAnim() {
    CollisionShadow* pShadow;
    if (mHidden != 0) {
        mBlendMtxTimer = 0;
        return;
    }

    if (mMorphFlashDone != 0) {
        u16 val = (mMorphTimer & 7) < (mMorphTimer >> 3) ? mPrevPlayerMode : mPlayerMode;
        if (mMorphTimer != 0 && mPlayerMode == 0) {
            if ((s32)val != (s32)mPlayerMode && mPrevPlayerMode == 6) {
                if (26 < mMorphTimer) {
                    mTeresaModel->appear();
                }

                mMorphHidden = 1;
                updateHand();
                updateFace();
                return;
            }

            if (mPrevPlayerMode == 6) {
                mTeresaModel->kill();
                mMorphHidden = 0;
            }
        }

        // A mod's power-up: a body of its own, or the look of one of the game's.
        const u16 valForm = (mMorphTimer & 7) < (mMorphTimer >> 3) ? mPrevModForm : mModForm;
        const u16 look = valForm != 0 ? valForm : val;
        const s32 modSlot = static_cast< s32 >(valForm) - NebulaMods::cFirstPowerUp;
        const bool ownModel = valForm != 0 && modSlot >= 0 && modSlot < mNumModPowerUps && mModels[6 + modSlot] != nullptr;
        if (ownModel) {
            changeDisplayMode(6 + modSlot);
        } else switch (NebulaMods::powerUpLook(look)) {
        case 1:
            changeDisplayMode(4);
            mInvincibleModel->calcAnim();

            if (mMorphAnimDone != 0) {
                mBlurState = 2;
            }

            break;
        case 4:
            changeDisplayMode(2);
            break;
        case 5:
            mMorphFlashDone = 0;
            changeDisplayMode(5);
            break;
        case 3:
            changeDisplayMode(3);
            break;

        default:
            changeDisplayMode(0);
            MR::startBtp(this, "ColorChange");
            MR::setBtpFrameAndStop(this, static_cast< f32 >(NebulaMods::powerUpColorFrame(look)));
            break;
        }

        if (mMorphFlashDone != 0) {
            changeHandMaterial();
        }
    }

    if (mMario->isStatusActive(MarioStatus_Paralyze)) {
        if (mCurrModel != 1) {
            mDisplayModel = mCurrModel;
        }

        if ((mFrameCount & 3) != 0) {
            changeDisplayMode(1);
        } else {
            changeDisplayMode(mDisplayModel);
        }
    } else if (mCurrModel == 1 && mHealth != 0) {
        changeDisplayMode(mDisplayModel);
    }

    mMorphFlashDone = 0;
    mMorphAnimDone = 0;

    calcAndSetBaseMtx();
    mMarioAnim->switchMirrorMode();
    mMarioAnim->calc();

    if (mMorphTimer == 0) {
        switch (mPlayerMode) {
        case 4:
            mBeeModel->mPosition.set(mPosition);

            MR::updateModelDiffMaterials(mBeeModel);
            break;
        case 3:
            mIceModel->mPosition.set(mPosition);

            MR::updateModelDiffMaterials(mIceModel);
            MR::updateModelDiffMaterials(mIceHandL);
            MR::updateModelDiffMaterials(mIceHandR);
            break;
        case 1:
            mInvincibleModel->mPosition.set(mPosition);

            MR::updateModelDiffMaterials(mInvincibleModel);
            MR::updateModelDiffMaterials(mInvincibleHandL);
            MR::updateModelDiffMaterials(mInvincibleHandR);
            break;
        case 5:
            mHopperModel->mPosition.set(mPosition);

            MR::updateModelDiffMaterials(mHopperModel);
            break;
        default:
            if (mCurrModel >= 6 && mModPowerUpModels[mCurrModel - 6] != nullptr) {
                mModPowerUpModels[mCurrModel - 6]->mPosition.set(mPosition);
                MR::updateModelDiffMaterials(mModPowerUpModels[mCurrModel - 6]);
            } else if (mCurrModel != 1) {
                MR::updateModelDiffMaterials(this);
            }

            break;
        }
    } else if (mCurrModel == 0) {
        MR::updateModelDiffMaterials(this);
    }

    if (MR::isHiddenModel(this) && mHidden == 0 && !mModHidden) {
        MR::showModel(this);
    }

    updateRealMtx();

    if (!mMario->mMovementStates.tornado || mMario->mTornadoTimer <= 1) {
        if (mBeeWallWalk != 0) {
            mCollisionShadow->setMode(0);
        } else {
            mCollisionShadow->setMode(2);
        }

        mCollisionShadow->mLength = cNum_calcAnim_f1 + (mMario->mShadowPos - mPosition).dot(mGravityVec);
        mCollisionShadow->mReferencePos = mMario->mShadowPos;

        Vec3f leftVec;
        getRealPos("HandL", &leftVec);

        Vec3f rightVec;
        getRealPos("HandR", &rightVec);

        Vec3f volumeUp(rightVec - leftVec);

        f32 length = volumeUp.length();
        MR::normalizeOrZero(&volumeUp);

        Vec3f cross;
        cross.cross(volumeUp, mGravityVec);
        MR::normalizeOrZero(&cross);

        volumeUp.cross(mGravityVec, cross);

        if (MR::normalizeOrZero(&volumeUp)) {
            volumeUp = mMario->mSideVec;
        }

        if (mMario->isSwimming()) {
            pShadow = mCollisionShadow;
            pShadow->mVolumeUp = volumeUp;
            pShadow->mVolumeHeight = cNum_calcAnim_f2 + length;
            pShadow->mVolumeWidth = cNum_calcAnim_f3;
        } else {
            pShadow = mCollisionShadow;
            pShadow->mVolumeUp = volumeUp;
            pShadow->mVolumeHeight = cNum_calcAnim_f4 + length;
            pShadow->mVolumeWidth = cNum_calcAnim_f5;
        }

        Vec3f vec;
        if (mMario->isStatusActive(MarioStatus_Hang)) {
            getRealPos("Center", &vec);
        } else {
            getRealPos("All_Root", &vec);
        }

        mCollisionShadow->create(vec, mGravityVec, mMario->mFrontVec);
    }

    if (mBaseMtxStale == 0) {
        mMario->updateLookOfs();
    }

    updateHand();
    updateFace();
    calcScreenBoxRange();

    if (mSpinIntervalTimer || mMainSpinHeld || mSubSpinHeld || mMario->mMovementStates.spinBlocked) {
    } else {
    }

    if (mCurrModel != 0) {
        NebulaMtxCopy(MR::getJointMtx(getModel(), "HandL0"), MR::getJointMtx(mModels[0], "HandL0"));
        NebulaMtxCopy(MR::getJointMtx(getModel(), "HandR0"), MR::getJointMtx(mModels[0], "HandR0"));
        NebulaMtxCopy(MR::getJointMtx(getModel(), "FootL"), MR::getJointMtx(mModels[0], "FootL"));
        NebulaMtxCopy(MR::getJointMtx(getModel(), "FootR"), MR::getJointMtx(mModels[0], "FootR"));
        NebulaMtxCopy(MR::getJointMtx(getModel(), "Center"), MR::getJointMtx(mModels[0], "Center"));
        NebulaMtxCopy(MR::getJointMtx(getModel(), "All_Root"), MR::getJointMtx(mModels[0], "All_Root"));
        NebulaMtxCopy(MR::getJointMtx(getModel(), "Spine1"), MR::getJointMtx(mModels[0], "Spine1"));
        NebulaMtxCopy(MR::getJointMtx(getModel(), "Hip"), MR::getJointMtx(mModels[0], "Hip"));
        NebulaMtxCopy(MR::getJointMtx(getModel(), "Face0"), MR::getJointMtx(mModels[0], "Face0"));

        NebulaMtxCopy(getModel()->getBaseTRMtx(), mModels[0]->getBaseTRMtx());
    }

    updateRasterScroll();
}

void MarioActor::calcAndSetBaseMtx() {
    if (mBaseMtxSet == false) {
        mBaseMtxStale = true;

        return;
    }

    mBaseMtxStale = false;
    mBaseMtxSet = false;

    Mtx34f mtx;
    mtx.set(getModel()->getBaseTRMtx());

    bool hostSetBase = false;
    if (mInRush) {
        hostSetBase = mRushSensor->receiveMessage(ACTMES_UPDATE_BASEMTX, getSensor("body"));
        if (hostSetBase) {
            Vec3f frontVec;
            Vec3f headVec;
            MR::extractMtxYDir(getBaseMtx(), &headVec);
            MR::normalizeOrZero(&headVec);
            mMario->setHeadVec(headVec);

            MR::extractMtxZDir(getBaseMtx(), &frontVec);
            MR::normalizeOrZero(&frontVec);
            mMario->setFrontVecKeepUp(frontVec);
            mMario->mPrevMoveFrontVec = frontVec;
        }
    }

    if (mInDemo || mBaseMtxForced) {
        if (mBaseMtxForced) {
            NebulaMtxCopy(mForcedBaseMtx.mMtx, getBaseMtx());

            Vec3f frontVec;
            Vec3f headVec;
            MR::extractMtxYDir(getBaseMtx(), &headVec);
            mMario->setHeadVec(headVec);

            MR::extractMtxZDir(getBaseMtx(), &frontVec);
            mMario->setFrontVecKeepUp(frontVec);

            mMario->mWalkSpeed = 0.0f;
            mMario->mTargetWalkSpeedIndex = 0;
            mMario->stopJump();
            mMario->mPrevMoveFrontVec = mMario->mFrontVec;

            if (mMario->isSwimming()) {
                mMario->mSwim->resetAndFixPose();
            }

            hostSetBase = true;
            if (mInDemo) {
                mDemoBaseKept = true;
            }
        } else if (mDemoBaseKept == 1) {
            return;
        }
    }

    Mtx receivedMtx;
    if (hostSetBase) {
        Vec3f vec(*getPosition());
        Vec3f* pPosition = &mPosition;
        MR::extractMtxTrans(getBaseMtx(), pPosition);
        mMario->invalidateRelativePosition();

        mRushMove = *getPosition() - vec;
        NebulaMtxCopy(getBaseMtx(), receivedMtx);

        if (mMario->mMovementStates.mode2D) {
            mMario->m2DPlanePos = *pPosition;
        }

        updateGravityVec(true, true);
        mMario->updateGroundInfo();
    }

    if (mInRush && !hostSetBase) {
        Vec3f vec;
        MR::extractMtxTrans(getBaseMtx(), &vec);
        mRushMove = *getPosition() - vec;
    }

    Pos3f baseMtx;
    NebulaMtxIdentity(baseMtx);

    mMario->createAngleMtx(baseMtx, hostSetBase);

    if (mDemoBaseKept == 1) {
        getRealPos("CapPosition", &mHeadPos);

        if ((mHeadPos - mPosition).length() > cNum_calcAndSetBaseMtx_f1) {
            NebulaMtxCopy(getBaseMtx(), baseMtx);
            MR::setMtxTrans(baseMtx, 0.0f, 0.0f, 0.0f);
        }
    }

    NebulaMtxConcat(baseMtx, MR::tmpMtxRotYRad(mMario->mYAngleOffset), baseMtx);

    if (mPlayerMode == 6) {
        updateBaseMtxTeresa(baseMtx);
    }

    bool isAir;
    if (!hostSetBase && mPlayerMode == 4 && !mMario->isStatusActive(MarioStatus_Stick) && !mMario->isStatusActive(MarioStatus_SideStep) &&
        !mMario->isStatusActive(MarioStatus_Bury) && !getMovementStates().slopeSlide && !getMovementStates().squat &&
        ((isAir = getMario()->getMovementStates().hipDrop && getMario()->getMovementStates().jumping), !isAir)) {
        mBeePoseGravity = mMario->mAirGravityVec;
        Vec3f vec(-getGravityVector() * getConst().getTable()->mBeePoseHeadToFootLength);
        MtxPtr pTranslation = MR::tmpMtxTrans(-vec);
        Vec3f vec4(mBeePoseHeadPos - (mPosition - mBeePoseGravity * getConst().getTable()->mBeePoseHeadToFootLength));
        Vec3f vec5;
        f32 dot = 0.0f;
        if (!MR::normalizeOrZero(&vec4)) {
            NebulaVecCrossProduct(&mMario->mFrontVec, &vec4, &vec5);
            if (!MR::normalizeOrZero(&vec5)) {
                dot = MR::acosEx(mBeePoseGravity.dot(vec4));
                const MarioConstTable* pTable = getConst().getTable();
                f32 max = pTable->mBeePoseDelayAngleAir;
                if (getMario()->getMovementStates().onGround) {
                    max = pTable->mBeePoseDelayAngleGround;
                }

                dot = MR::clamp(dot, 0.0f, max);
            }
        }

        Mtx rotation;
        NebulaMtxRotAxisRad(rotation, &vec5, dot);
        NebulaMtxConcat(pTranslation, baseMtx, baseMtx);
        NebulaMtxConcat(rotation, baseMtx, baseMtx);
        NebulaMtxConcat(MR::tmpMtxTrans(vec), baseMtx, baseMtx);
        Vec3f poseAccel(mBeePoseGravity * getConst().getTable()->mBeePoseDelayAccel);
        MR::vecKillElement(poseAccel, mBeePoseDir, &poseAccel);
        mBeePoseVelocity = mBeePoseVelocity + poseAccel;
        mBeePoseHeadPos = mBeePoseHeadPos + mBeePoseVelocity;
        mBeePoseDir = mBeePoseHeadPos - (mPosition - mBeePoseGravity * getConst().getTable()->mBeePoseHeadToFootLength);
        Vec3f poseOffset(mBeePoseDir);
        Vec3f poseDir(mBeePoseDir);
        MR::normalizeOrZero(&poseDir);
        f32 dot2 = MR::acosEx(mBeePoseGravity.dot(poseDir));
        f32 val = getConst().getTable()->mBeePoseLimitAngleAir;
        if (mMario->getMovementStates().onGround) {
            val = getConst().getTable()->mBeePoseLimitAngleGround;
        }

        if (dot2 > val) {
            Vec3f poseAxis;
            NebulaVecCrossProduct(&mBeePoseGravity, &poseDir, &poseAxis);
            if (MR::normalizeOrZero(&poseAxis)) {
                mBeePoseDir = mBeePoseGravity;
            } else {
                Mtx rotation;
                NebulaMtxRotAxisRad(rotation, poseAxis, val);
                NebulaMtxMultVec(rotation, &mBeePoseGravity, &mBeePoseDir);
            }
        }

        mBeePoseDir.setLength(getConst().getTable()->mBeePoseHeadToFootLength);
        const MarioConstTable* table = getConst().getTable();
        if (poseOffset.length() > table->mBeePoseHeadToFootLength) {
            mBeePoseVelocity += -(poseOffset - mBeePoseDir);
        }

        mBeePoseHeadPos = (mPosition - mBeePoseGravity * getConst().getTable()->mBeePoseHeadToFootLength) + mBeePoseDir;
        MR::normalizeOrZero(&mBeePoseDir);
        table = getConst().getTable();
        if (mBeePoseVelocity.length() > table->mBeePoseDelaySpeedLimit) {
            mBeePoseVelocity.setLength(getConst().getTable()->mBeePoseDelaySpeedLimit);
        }

        f32 friction = getConst().getTable()->mBeePoseFrictionStop;
        if (mMario->mTargetWalkSpeedIndex == 0 && !isJumping()) {
            friction = getConst().getTable()->mBeePoseFrictionMove;
        }

        mBeePoseVelocity.scale(friction);
        if (!MR::isSameDirection(mBeePoseDir, mMario->mFrontVec)) {
            MR::makeMtxUpFront(&baseMtx, -mBeePoseDir, mMario->getFrontVec());
        } else if (!MR::isSameDirection(mMario->mPostureUpVec, mMario->getFrontVec())) {
            MR::makeMtxUpFront(&baseMtx, mMario->mPostureUpVec, mMario->getFrontVec());
        } else {
            MR::makeMtxUpFront(&baseMtx, mMario->mHeadVec, mMario->getFrontVec());
        }

        if (mMario->mTargetWalkSpeedIndex > 2 || isJumping()) {
            mBeePoseTrans = mBeePoseTrans * getConst().getTable()->mBeePoseTransBlendingRatioMove +
                   (mBeePoseHeadPos - *getPosition()) * (1.0f - getConst().getTable()->mBeePoseTransBlendingRatioMove);
            MR::setMtxTrans(baseMtx, mBeePoseTrans.x, mBeePoseTrans.y, mBeePoseTrans.z);
        } else {
            mBeePoseTrans.scale(getConst().getTable()->mBeePoseTransBlendingRatioStop);
            MR::setMtxTrans(baseMtx, mBeePoseTrans.x, mBeePoseTrans.y, mBeePoseTrans.z);
        }
    } else {
        mBeePoseVelocity.zero();
        mBeePoseTrans.zero();
        mBeePoseHeadPos = mPosition;
        mBeePoseGravity = getGravityVector();
    }

    MR::addTransMtx(baseMtx, *getPosition());

    if (hostSetBase) {
        Mtx inverse;
        NebulaMtxInverse(baseMtx, inverse);
        NebulaMtxConcat(inverse, receivedMtx, mBaseCorrectionMtx.mMtx);
        XjointTransform* pJoint = mMarioAnim->getXanimePlayer()->getCore()->getJointTransform(0);
        pJoint->mBaseMtx = mBaseCorrectionMtx.mMtx;
    } else {
        Mtx correction;
        Vec3f offset;
        mMario->createCorrectionMtx(correction, &offset);
        mMarioAnim->getXanimePlayer()->getCore()->getJointTransform(1)->mTranslateOffset = offset;
        NebulaMtxCopy(correction, mBaseCorrectionMtx.mMtx);
        XjointTransform* pJoint = mMarioAnim->getXanimePlayer()->getCore()->getJointTransform(0);
        pJoint->mBaseMtx = mBaseCorrectionMtx.mMtx;
    }

    if (mPressTimer != 0) {
        mPressEndTimer = 30;
        switch (mPressKind) {
        case 0:
        case 2:
            if (mPressTimer > 15) {
                f32 ceil = mMario->calcDistToCeil(false);
                f32 pressCeil = mMario->calcDistToCeilOnPress();
                f32 scale = MR::clamp(ceil / cNum_calcAndSetBaseMtx_f2, cNum_calcAndSetBaseMtx_f3, 1.0f);
                if (mPressScale > scale) {
                    mPressScale = scale;
                } else {
                    scale = MR::clamp(pressCeil / cNum_calcAndSetBaseMtx_f4, cNum_calcAndSetBaseMtx_f5, 1.0f);
                    if (mPressScale > scale) {
                        mPressScale = scale;
                    }

                    MR::setMtxTrans(baseMtx, mPosition.x, mPosition.y, mPosition.z);
                    if (mMario->mFloorCode != 27) {
                        mCareMovingObject = true;
                    }
                }
            } else {
                mPressScale = cNum_calcAndSetBaseMtx_f6 + cNum_calcAndSetBaseMtx_f7 * (15 - mPressTimer) / cNum_calcAndSetBaseMtx_f8;
            }

            if (mPressSwellTimer != 0) {
                mPressScale += cNum_calcAndSetBaseMtx_f9 * mPressSwellTimer;
            }

            break;
        case 1:
        case 3:
            if (mPressTimer > 15) {
                f32 width = mMario->calcDistWidth();
                mPosition = mMario->mPosition;
                f32 scale = MR::clamp(width / cNum_calcAndSetBaseMtx_f10, cNum_calcAndSetBaseMtx_f11, 1.0f);
                if (mPressScale > scale) {
                    mPressScale = scale;
                } else {
                    mPressScale -= cNum_calcAndSetBaseMtx_f12;
                    mPressScale = MR::clamp(mPressScale, cNum_calcAndSetBaseMtx_f13, 1.0f);
                }
            } else {
                mPressScale = cNum_calcAndSetBaseMtx_f14 + cNum_calcAndSetBaseMtx_f15 * (15 - mPressTimer) / cNum_calcAndSetBaseMtx_f16;
            }

            if (mPressSwellTimer != 0) {
                mPressScale += cNum_calcAndSetBaseMtx_f17 * mPressSwellTimer;
            }

            break;
        case 4:
            f32 height = cNum_calcAndSetBaseMtx_f18 * (1.0f - MR::cos(cNum_calcAndSetBaseMtx_f19 * ((mPressTimer / cNum_calcAndSetBaseMtx_f20) * FloatUtil::PI())));
            mPressScale = height / cNum_calcAndSetBaseMtx_f21;
            break;
        }
    } else if (mPressEndTimer != 0) {
        mPressEndTimer--;
        switch (mPressKind) {
        case 0:
        case 2:
            mPressScale = 1.0f + (cNum_calcAndSetBaseMtx_f22 * mPressEndTimer / cNum_calcAndSetBaseMtx_f23) * MR::sin(cNum_calcAndSetBaseMtx_f24 * (mPressEndTimer * FloatUtil::PI()) / cNum_calcAndSetBaseMtx_f25);
            break;
        case 1:
        case 3:
            mPressScale = 1.0f + (cNum_calcAndSetBaseMtx_f26 * mPressEndTimer / cNum_calcAndSetBaseMtx_f27) * MR::sin(cNum_calcAndSetBaseMtx_f28 * (mPressEndTimer * FloatUtil::PI()) / cNum_calcAndSetBaseMtx_f29);
            break;
        }
    } else if (!getMovementStates().scaleBody) {
        mPressScale = 1.0f;
    }

    if (mPressSwellTimer != 0) {
        mPressSwellTimer--;
    }

    u16 val = mBlendMtxTimer;
    if (val != 0) {
        f32 remaining = 1.0f - mBlendMtxRate;
        remaining *= static_cast< f32 >(val - 1) / val;
        mBlendMtxRate = 1.0f - remaining;

        MR::blendMtx(mtx, baseMtx, mBlendMtxRate, baseMtx);
        mBlendMtxTimer--;
    }

    NebulaMtxCopy(baseMtx, mBaseMtx.mMtx);

    if (mPressScale != 1.0f) {
        scaleMtx(baseMtx);
    }

    NebulaMtxCopy(baseMtx, getModel()->mBaseTransformMtx);
    getModel()->mBaseScale = mScale;

    mBaseMtxForced = false;
}

void MarioActor::setBlendMtxTimer(u16 frames) {
    if (frames == 0) {
        mBlendMtxRate = 1.0f;
    } else {
        mBlendMtxRate = 0.0f;
    }

    mBlendMtxTimer = frames;
}

void MarioActor::getGroundPos(Vec3f* pDst) const {
    *pDst = mMario->mGroundPos;
}

Vec3f* MarioActor::getShadowPos() const {
    return &mMario->mShadowPos;
}

bool MarioActor::isTurning() const {
    return getMovementStates().turning;
}

bool MarioActor::isJumping() const {
    if (mInRush) {
        return mRushMove.dot(getGravityVec()) < -cNum_isJumping_f1;
    }

    if (mPlayerMode == 6 && mMario->mVerticalSpeed < mConst->getTable()->mTeresaDropDownHeight) {
        return false;
    }

    if (mMario->isWalling()) {
        return true;
    }

    return mMario->mMovementStates.jumping && !mMario->mMovementStates.onGround;
}

bool MarioActor::isJumpRising() const {
    return mMario->mMovementStates.jumping && mMario->isRising();
}

bool MarioActor::isPunching() const {
    if (mMario->isStatusActive(MarioStatus_Magic)) {
        return true;
    }

    if (mMario->mSwim->isSpinning()) {
        return true;
    }

    if (mPunchSensorOn) {
        return true;
    }

    if (!mMario->isAnimationTerminate(nullptr) &&
        (mMario->isAnimationRun("空中ひねり") || mMario->isAnimationRun("アイスひねり") || mMario->isAnimationRun("アイスひねり空中") ||
         mMario->isAnimationRun("アイスひねり静止") || mMario->isAnimationRun("アイスひねり移動"))) {
        return true;
    }

    return false;
}

bool MarioActor::isItemSwinging() const {
    if (mMario->isAnimationRun("テニスショット左") || mMario->isAnimationRun("テニスショット右") || mMario->isAnimationRun("テニスショット中")) {
        return true;
    } else {
        return false;
    }
}

bool MarioActor::isNeedDamageFog() const {
    if (!isEnableNerveChange()) {
        return false;
    }

    if (MR::isDemoActive()) {
        return false;
    }

    if (mInDemo) {
        return false;
    }

    return mMario->getDamageAfterTimer() != 0;
}

bool MarioActor::isDamaging() const {
    return mMario->isDamaging() != false;
}

bool MarioActor::isStaggering() const {
    return mMario->isStatusActive(MarioStatus_Flip) != false;
}

bool MarioActor::isSleeping() const {
    return mMario->isAnimationRun("特殊ウエイト1B") || mMario->isAnimationRun("特殊ウエイト1A");
}

bool MarioActor::isRefuseTalk() const {
    if (!isEnableNerveChange()) {
        return true;
    }

    if (mMario->isStatusActive(MarioStatus_Wait)) {
        return true;
    }

    if (mMario->isStatusActive(MarioStatus_Hang)) {
        return true;
    }

    if (mMario->isStatusActive(MarioStatus_RecoveryOrWarp)) {
        return true;
    }

    if (mMario->isDamaging()) {
        return true;
    }

    if (mMario->mMoveLockTimer != 0) {
        return true;
    }

    bool isNormal = !mMario->isPlayerModeTeresa();
    if (isNormal) {
        if (mBombTeresaTimer != 0) {
            return true;
        }

        if (mMario->getMovementStates().jumping) {
            return true;
        }

        if (!mMario->getMovementStates().onGround) {
            return true;
        }

        if (mMario->mLandTimer < 5) {
            return true;
        }

        if (mMario->getMovementStates().slopeSlide) {
            return true;
        }

        if (mMario->getDrawStates().atEdge) {
            return true;
        }

        if (mMario->mTurnReadyTimer != 0) {
            return true;
        }
    }

    if (mTakenSensor != nullptr) {
        return true;
    }

    return mTaking;
}

void MarioActor::jumpHop() {
    if (!isJumping()) {
        return;
    }

    if (!mMario->isRising()) {
        mMario->cutGravityElementFromJumpVec(true);

        mMario->mJumpVec += -mGravityVec * getConst().getTable()->mClapJumpBonusFalling;
    } else {
        f32 val = -mMario->cutGravityElementFromJumpVec(true);
        if (val < getConst().getTable()->mHopLimit) {
            val += getConst().getTable()->mClapJumpBonusRising;
            if (val > getConst().getTable()->mHopLimit) {
                val = getConst().getTable()->mHopLimit;
            }
        }

        mMario->mJumpVec += -mGravityVec * val;
    }

    if (mMario->mJumpKind == 5) {
        mMario->mJumpKind = 0;
        mMario->mMovementStates.squat = false;

        changeAnimation(nullptr, "落下");
    }

    if (mMario->mJumpKind == 11) {
        mMario->mJumpKind = 0;
    }

    mMario->mJumpStartPos = mMario->mPosition;
}

void MarioActor::calcCenterPos() {
    if (mInRush || mInitializing) {
        MR::copyJointPos(this, "Spine1", &mCenterPos);

        return;
    }

    Vec3f centerDir;
    if (mMario->isStatusActive(MarioStatus_Wall)) {
        centerDir = mMario->mWallTopHead;
    } else {
        if (mMario->isStatusActive(MarioStatus_Hang)) {
            MR::copyJointPos(this, "Spine1", &mCenterPos);

            return;
        }

        if (mMario->isSwimming()) {
            centerDir = mBodyUp;
        } else {
            centerDir = -getGravityVec();
        }
    }

    f32 centerHeight;
    if (mMario->getMovementStates().squat) {
        f32 radius = mBinder->mRadius;
        f32 height = cNum_calcCenterPos_f1;
        centerHeight = cNum_calcCenterPos_f2 * radius + cNum_calcCenterPos_f3 * height;
        mBinder->mRadius = centerHeight;
    } else {
        f32 radius = mBinder->mRadius;
        f32 height = cNum_calcCenterPos_f4;
        centerHeight = cNum_calcCenterPos_f5 * radius + cNum_calcCenterPos_f6 * height;
        mBinder->mRadius = centerHeight;
    }

    if (mPlayerMode == PlayerMode_Teresa) {
        f32 radius = mBinder->mRadius;
        f32 height = cNum_calcCenterPos_f7;
        f32 teresaRadius = cNum_calcCenterPos_f8 * radius + cNum_calcCenterPos_f9 * height;
        centerHeight = cNum_calcCenterPos_f10 + teresaRadius;
        mBinder->mRadius = teresaRadius;
    }

    mCenterPos = mPosition + centerDir * centerHeight;
}

void MarioActor::calcHeadPos() {
    Mtx34f mtx;
    const char* pName = "CapPosition";
    getRealMtx(mtx, pName);
    MR::extractMtxTrans(mtx, &mHeadPos);
}

void MarioActor::setPress(u8 kind, s32 frames) {
    if (mPressTimer != 0) {
        return;
    }

    if (kind == 0 && mMario->calcDistToCeil(false) >= cNum_setPress_f1) {
        return;
    }

    if (getMario()->mActionStates.onimasuWrapped) {
        return;
    }

    if (mPressKind == 0 || mPressKind == 2) {
        // Missing instruction
        *mMario->mPressFloorPolygon = *mMario->mGroundPolygon;
        *mMario->mPressCeilPolygon = *mMario->mCeilPolygon;
    }

    if (mMario->isStatusActive(MarioStatus_Blown)) {
        mMario->closeStatus(nullptr);
    }

    mMario->startPadVib("マリオ[つぶれ]");

    MR::forceDeleteEffectAll(this);

    mVelocity.zero();

    if (frames == 0) {
        frames = 240;
    }

    mPressTimer = frames;

    mMario->mMovementStates.hipDrop = false;
    mMario->mMovementStates.tornado = false;

    mMario->stopJump();

    mMario->mMovementStates.onGround = true;

    mMario->mLookOfs.zero();
    mMario->mLookOfsBody.zero();
    mMario->mLookOfsSink.zero();

    mMario->mVerticalSpeed = 0.0f;

    mMario->stopWalk();

    mMarioAnim->setSpeed(0.0f);

    mAnimationFrozen = true;
    mPressKind = kind;
    mBindOn = false;

    mMario->playSound("プレスダメージ");
    mMario->playSound("声大ダメージ");

    mWaitNextNerve = GET_NERVE(MarioActor, MarioActorNrvGameOver);
    mWaitTimer = 60;

    if (mPressKind != 4) {
        mPressTimer = 1000;
    }

    for (u32 idx = 0; idx < 6; idx++) {
        decLife(0);
    }

    MR::startStarPointerModeDemoMarioDeath(this);
}

XanimeResourceTable* MarioActor::getResourceTable() const {
    return mMarioAnim->mResourceTable;
}

bool MarioActor::isEnableMoveMario() const {
    if (isNerve(GET_NERVE(MarioActor, MarioActorNrvWait))) {
        return true;
    }

    if (isNerve(GET_NERVE(MarioActor, MarioActorNrvNoRush))) {
        return true;
    }

    if (isNerve(GET_NERVE(MarioActor, MarioActorNrvGameOverNonStop))) {
        return true;
    }

    return false;
}

bool MarioActor::isEnableNerveChange() const {
    if (isNerve(GET_NERVE(MarioActor, MarioActorNrvWait))) {
        return true;
    }

    if (isNerve(GET_NERVE(MarioActor, MarioActorNrvNoRush))) {
        return true;
    }

    return false;
}

void MarioActor::forceGameOver() {
    if (!isEnableNerveChange()) {
        return;
    }

    setNerve(GET_NERVE(MarioActor, MarioActorNrvGameOver));
}

void MarioActor::forceGameOverAbyss() {
    if (!isEnableNerveChange()) {
        return;
    }

    setNerve(GET_NERVE(MarioActor, MarioActorNrvGameOverAbyss));
}

void MarioActor::forceGameOverBlackHole() {
    if (!isEnableNerveChange()) {
        return;
    }

    setNerve(GET_NERVE(MarioActor, MarioActorNrvGameOverBlackHole));
}

void MarioActor::forceGameOverNonStop() {
    if (!isEnableNerveChange()) {
        return;
    }

    setNerve(GET_NERVE(MarioActor, MarioActorNrvGameOverNonStop));
}

void MarioActor::forceGameOverSink() {
    if (!isEnableNerveChange()) {
        return;
    }

    setNerve(GET_NERVE(MarioActor, MarioActorNrvGameOverSink));
}

void MarioActor::updateCameraInfo() {
    mCameraCut = false;

    if (MR::diffAngleAbs(mCamDirZ, MR::getCamZdir()) >= PI / cNum_updateCameraInfo_f1) {
        mCameraCut = true;
    }

    if ((mCamPos - MR::getCamPos()).length() > cNum_updateCameraInfo_f2) {
        mCameraCut = true;
    }

    if (mCameraCut) {
        mMario->changeAnimationInterpoleFrame(0);
        stopSpinTicoEffect(true);
        mBlendMtxTimer = 0;
    }

    mCamPos = MR::getCamPos();
    mCamDirX = MR::getCamXdir();
    mCamDirY = MR::getCamYdir();
    mPrevCamDirZ = mCamDirZ;
    mCamDirZ = MR::getCamZdir();
}

bool MarioActor::binderFilter(const Triangle* pTriangle) {
    if (mPressCeilingSensor != nullptr && pTriangle->mSensor == mPressCeilingSensor) {
        return true;
    }

    return false;
}

void MarioActor::setPunchHitTimer(u8 punchHitTime) {
    if (mPunchHitTimer == 0) {
        mPunchFrame = 0;
        mAirPunchHitCount = 0;
    }

    mPunchHitTimer = punchHitTime;
}
