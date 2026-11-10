#include "Game/Boss/SkeletalFishBoss.hpp"
#include "Game/System/NebulaFrameClock.hpp"
#include "Game/Util/StringUtil.hpp"
#include "Game/Boss/SkeletalFishBossBattleDirector.hpp"
#include "Game/Boss/SkeletalFishBossRail.hpp"
#include "Game/Boss/SkeletalFishBossRailHolder.hpp"
#include "Game/Boss/SkeletalFishGuardHolder.hpp"
#include "Game/Boss/SkeletalFishJointCalc.hpp"
#include "Game/Boss/SkeletalFishRailControl.hpp"
#include "Game/Camera/CameraTargetArg.hpp"
#include "Game/Camera/CameraTargetMtx.hpp"
#include "Game/LiveActor/ActorCameraInfo.hpp"
#include "Game/LiveActor/HitSensor.hpp"
#include "Game/LiveActor/ModelObj.hpp"
#include "Game/LiveActor/Nerve.hpp"
#include "Game/LiveActor/SensorHitChecker.hpp"
#include "Game/Map/CollisionParts.hpp"
#include "Game/NameObj/NameObjExecuteHolder.hpp"
#include "Game/Scene/SceneFunction.hpp"
#include "Game/Scene/SceneObjHolder.hpp"
#include "Game/System/ResourceHolder.hpp"
#include "Game/Util/ActorMovementUtil.hpp"
#include "Game/Util/ActorSensorUtil.hpp"
#include "Game/Util/ActorShadowUtil.hpp"
#include "Game/Util/ActorSwitchUtil.hpp"
#include "Game/Util/BaseMatrixFollowTargetHolder.hpp"
#include "Game/Util/CameraUtil.hpp"
#include "Game/Util/DemoUtil.hpp"
#include "Game/Util/EffectUtil.hpp"
#include "Game/Util/GravityUtil.hpp"
#include "Game/Util/PlacementUtil.hpp"
#include "Game/Util/JointController.hpp"
#include "Game/Util/JointUtil.hpp"
#include "Game/Util/LiveActorUtil.hpp"
#include "Game/Util/ObjUtil.hpp"
#include "Game/Util/PlayerUtil.hpp"
#include "Game/Util/RailUtil.hpp"
#include "Game/Util/SceneUtil.hpp"
#include "Game/Util/ScreenUtil.hpp"
#include "Game/Util/SoundUtil.hpp"
#include "Game/Render/ModelData.hpp"
#include "Game/Util/StringUtil.hpp"
#include <nebula_geometry.hpp>
#include "Game/Util/Tunable.hpp"

NEBULA_TUNABLE_NAMED(f32, cNum_init_f1, "init.f1", 1000.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_calcAnim_f1, "calcAnim.f1", 1.8325958f);
NEBULA_TUNABLE_NAMED(f32, cNum_damage_f1, "damage.f1", 10.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_damage_f2, "damage.f2", 40.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeSwim_f1, "exeSwim.f1", 5000.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeSwim_f2, "exeSwim.f2", 5000.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeOpenWait_f1, "exeOpenWait.f1", 7000.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeOpenWait_f2, "exeOpenWait.f2", 7000.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeDamage_f1, "exeDamage.f1", 10.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeDamage_f2, "exeDamage.f2", 40.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_exePowerUpDemo_f1, "exePowerUpDemo.f1", 60.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeBreakDemo_f1, "exeBreakDemo.f1", 5.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeBreakDemo_f2, "exeBreakDemo.f2", 80.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_getBodyThickness_f1, "getBodyThickness.f1", 400.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_initShadow_f1, "initShadow.f1", 4000.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_calcGravityCenter_f1, "calcGravityCenter.f1", 0.001f);
NEBULA_TUNABLE_NAMED(f32, cNum_updateBgm_f1, "updateBgm.f1", 4200.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_resetCamera_f1, "resetCamera.f1", 1200.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_endBreakDemo_f1, "endBreakDemo.f1", 1573.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_SkeletalFishBossHead_f1, "SkeletalFishBossHead.f1", 400.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_SkeletalFishBossHead_f2, "SkeletalFishBossHead.f2", 120.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_SkeletalFishBossHead_f3, "SkeletalFishBossHead.f3", 320.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_SkeletalFishBossHead_f4, "SkeletalFishBossHead.f4", 9.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_SkeletalFishBossHead_f5, "SkeletalFishBossHead.f5", 10.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_SkeletalFishBossHead_f6, "SkeletalFishBossHead.f6", 270.0f);

// The step counts of this file's functions, as tunables: "<file>.<function>.<k>".
NEBULA_TUNABLE_NAMED(s32, cStep_exeAppearDemo_1, "exeAppearDemo.1", 280);
NEBULA_TUNABLE_NAMED(s32, cStep_exeDeadDemo_1, "exeDeadDemo.1", 218);
NEBULA_TUNABLE_NAMED(s32, cStep_exeBreakDemo_1, "exeBreakDemo.1", 204);
NEBULA_TUNABLE_NAMED(s32, cStep_exeBreakDemo_2, "exeBreakDemo.2", 206);
NEBULA_TUNABLE_NAMED(s32, cStep_exeBreakDemo_3, "exeBreakDemo.3", 200);


namespace {
    // One collision part per row, following the joint.
    const SkeletalFishBoss::SensorToCollider sColInfo[] = {
        {"Joint01", "BackBone01"},        {"Joint02", "BackBone02"},         {"Joint03", "BackBone03"}, {"Joint04", "BackBone04"},
        {"Joint05", "BackBone05"},        {"Joint06", "BackBone06"},         {"Joint07", "BackBone07"}, {"Joint08", "BackBone08"},
        {"Joint09", "BackBone09"},        {"Joint10", "BackBone10"},         {"Joint11", "BackBone11"}, {"Joint12", "BackFin"},
        {"LeftFinJoint", "LeftFinJoint"}, {"RightFinJoint", "RightFinJoint"}};

    const SkeletalFishBoss::JointToShadow sShadowInfo[] = {{"Shadow00", "Joint02", "SkeletalFishBossShadow"}};

    const SkeletalFishBoss::HitPos sHitPosData[] = {
        {"Wound0", "Head", {26.144243f, 414.56268f, 552.1146f}},     {"Wound1", "Head", {-618.77576f, 55.65544f, 163.1091f}},
        {"Wound10", "Head", {597.2239f, 92.83909f, 188.59679f}},     {"Wound11", "Head", {4.24828f, -134.68741f, 869.9156f}},
        {"Wound2", "Head", {88.67339f, 10.598214f, -206.06902f}},    {"Wound3", "Head", {331.39523f, 37.908966f, 794.4394f}},
        {"Wound4", "Head", {-314.09827f, -16.389574f, 782.438f}},    {"Wound5", "Head", {6.604942f, 104.82322f, 1221.328f}},
        {"Wound8", "Head", {365.95203f, 371.1259f, 93.8099f}},       {"Wound9", "Head", {-262.9857f, 410.83163f, 159.02629f}},
        {"WoundJow0", "Jow", {35.98041f, -300.88702f, 76.61445f}},   {"WoundJow1", "Jow", {-181.04329f, -124.71387f, 593.6195f}},
        {"WoundJow2", "Jow", {475.85388f, -146.99239f, 119.52184f}}, {"WoundJow3", "Jow", {-473.8122f, -146.99239f, 119.52184f}},
        {"WoundJow4", "Jow", {233.05017f, -124.71387f, 575.92737f}}, {"WoundJow5", "Jow", {-4.140523f, -318.03336f, 394.53833f}}};
};  // namespace

namespace {
    // The first rail placed for the boss, or null when the stage has none (the holder is made by the first rail).
    SkeletalFishBossRail* findFirstBossRail() {
        if (!MR::isExistSceneObj(SceneObj_SkeletalFishBossRailHolder)) {
            return nullptr;
        }

        const SkeletalFishBossRailHolder* pHolder = MR::getSkeletalFishBossRailHolder();
        return pHolder->mRails.size() > 0 ? pHolder->getInOrder(0) : nullptr;
    }
};  // namespace

namespace {
    NEW_NERVE(SkeletalFishBossNrvSwim, SkeletalFishBoss, Swim);
    NEW_NERVE(SkeletalFishBossNrvOpen, SkeletalFishBoss, Open);
    NEW_NERVE(SkeletalFishBossNrvOpenWait, SkeletalFishBoss, OpenWait);
    NEW_NERVE(SkeletalFishBossNrvClose, SkeletalFishBoss, Close);
    NEW_NERVE(SkeletalFishBossNrvBite, SkeletalFishBoss, Bite);
    NEW_NERVE(SkeletalFishBossNrvDamage, SkeletalFishBoss, Damage);
    NEW_NERVE(SkeletalFishBossNrvDown, SkeletalFishBoss, Down);
    NEW_NERVE(SkeletalFishBossNrvDeadDamage, SkeletalFishBoss, DeadDamage);
    NEW_NERVE(SkeletalFishBossNrvDead, SkeletalFishBoss, Dead);
    NEW_NERVE(SkeletalFishBossNrvAppearWait, SkeletalFishBoss, AppearWait);
    NEW_NERVE(SkeletalFishBossNrvAppearDemo, SkeletalFishBoss, AppearDemo);
    NEW_NERVE(SkeletalFishBossNrvPowerUpDemo, SkeletalFishBoss, PowerUpDemo);
    NEW_NERVE(SkeletalFishBossNrvDeadDemo, SkeletalFishBoss, DeadDemo);
    NEW_NERVE(SkeletalFishBossNrvBreakDemo, SkeletalFishBoss, BreakDemo);
    NEW_NERVE(SkeletalFishBossNrvDemoWait, SkeletalFishBoss, DemoWait);
};  // namespace

SkeletalFishBoss::SkeletalFishBoss(const char* pName) : LiveActor(pName) {
    mJointIndicies = nullptr;
    mPartsArray = nullptr;
    mBossHead = nullptr;
    mScarFlash = nullptr;
    mBreakModel = nullptr;
    mRailControl = new SkeletalFishRailControl();
    mBossDirector = nullptr;
    mLevel = 0;
    mHitsLeft = 3;
    mDamageBrkIndex = 0;
    mCameraTargetMtx = nullptr;
    mPlanetCenter.x = 0.0f;
    mPlanetCenter.y = 0.0f;
    mPlanetCenter.z = 0.0f;
    mSceneFunc = nullptr;
    mCurScene = nullptr;
    mSceneNerve = nullptr;
    mLevelNum = 3;
    mGuardNum = 4;
    mBossInfo = nullptr;
    mGuardHolder = nullptr;
    mBgmType = 0;
    mBgmHoldStep = -1;

    for (u32 i = 0; i < ARRAY_SIZE(mControllers); i++) {
        mControllers[i] = nullptr;
    }

    mRailMtxInv.identity();
    mPlacementMtx.identity();
}

void SkeletalFishBoss::init(const TableIter& rIter) {
    initModelManagerWithAnm("SkeletalFishBoss", nullptr, false);
    MR::initDefaultPos(this, rIter);
    MR::getPlacementArg0NoInit(rIter, &mLevelNum);
    MR::getPlacementArg1NoInit(rIter, &mGuardNum);

    // The boss has at least one level (powerUp reads level 0) and no fewer than 0 guards (both size arrays).
    if (mLevelNum < 1) {
        mLevelNum = 1;
    }

    if (mGuardNum < 0) {
        mGuardNum = 0;
    }

    initLevelStatus();
    mBossInfo->initWithoutIter();
    mPlacementMtx.setInline(MR::getZonePlacementMtx(rIter));
    initSwitch(rIter);
    initJoint();
    initHead();
    initScarFlash();
    initBreakModel();
    initCollision();
    initEffectKeeper(1, "SkeletalFishBoss");
    initSound(4, false);
    initNerve(GET_NERVE_ANON(SkeletalFishBossNrvSwim));
    MR::invalidateClipping(this);
    MR::setClippingTypeSphere(this, cNum_init_f1);
    mBossDirector = new SkeletalFishBossBattleDirector(this);
    MR::joinToGroupArray(this, rIter, nullptr, 32);
    MR::connectToSceneCollisionEnemy(this);

    if (MR::tryRegisterDemoCast(this, rIter)) {
        MR::tryRegisterDemoCast(mBossHead, rIter);
    }

    MR::initLightCtrl(this);
    initShadow();
    initCamera();
    createGuards();
    MR::createNormalBloom();
    MR::addBaseMatrixFollowTarget(this, rIter, nullptr, nullptr);
    MR::declareCameraRegisterVec(this, 0, &mPosition);
    MR::declarePowerStar(this);
    MR::declareStarPiece(this, 0x19);
    MR::createCenterScreenBlur();
    MR::startBck(this, "Swim");
    MR::startBck(mBossHead, "Wait");
    MR::startBrk(this, "Base");
    MR::startBrk(mBossHead, "Base");
    MR::startBva(this, "Normal");
    MR::startBva(mBossHead, "Normal");
}

void SkeletalFishBoss::initAfterPlacement() {
    NameObj::initAfterPlacement();

    // A stage with no boss rail leaves the boss dead: it has nowhere to swim, and appear() and makeActorAppeared() keep it so.
    if (findFirstBossRail() == nullptr) {
        makeActorDead();
        return;
    }

    mLevel = -1;
    powerUp();
    makeActorDead();
    calcPlanetCenterPosition();
    mBossDirector->initiate();
}

void SkeletalFishBoss::makeActorAppeared() {
    if (findFirstBossRail() == nullptr) {
        return;
    }

    LiveActor::makeActorAppeared();
    validateCollision();
    mLevel = -1;
    powerUp();
}

void SkeletalFishBoss::appear() {
    if (findFirstBossRail() == nullptr) {
        return;
    }

    LiveActor::appear();
    MR::hideModel(this);
    setNerve(GET_NERVE_ANON(SkeletalFishBossNrvAppearWait));
}

void SkeletalFishBoss::kill() {
    LiveActor::kill();
    invalidateCollision();
    mScarFlash->kill();
}

void SkeletalFishBoss::control() {
    ActorCameraInfo info = ActorCameraInfo();

    if (MR::isEventCameraActive(&info, "デモ終了後カメラ") && MR::isGreaterEqualStep(this, NebulaFrameClock::stepsOfSeconds(1))) {
        MR::endGlobalEventCamera("デモ終了後カメラ", -1, true);
    }

    if (!isNerve(GET_NERVE_ANON(SkeletalFishBossNrvDead)) && !isNerve(GET_NERVE_ANON(SkeletalFishBossNrvAppearWait))) {
        bool isInDemo = isNerve(GET_NERVE_ANON(SkeletalFishBossNrvAppearDemo)) || isNerve(GET_NERVE_ANON(SkeletalFishBossNrvPowerUpDemo)) ||
                        isNerve(GET_NERVE_ANON(SkeletalFishBossNrvDeadDemo));

        if (!isInDemo) {
            mRailControl->update();
            mRailControl->getPos(&mPosition, 0.0f);
            updateCollision();
            mGuardHolder->movement();
            updateBgm();
        }

        mBossDirector->tryColumnCollision(mBossHead->getSensor("body"));
    }
}

void SkeletalFishBoss::calcAnim() {
    LiveActor::calcAnim();
    Vec3f cameraDir;
    MR::getCameraInvViewMtx().getZDir(cameraDir);
    cameraDir.negate();

    if (!MR::isNoCalcAnim(this)) {
        for (u32 i = 0; i < ARRAY_SIZE(::sShadowInfo); i++) {
            const JointToShadow* pShadow = &::sShadowInfo[i];
            Pos3f jointMtx(MR::getJointMtx(this, pShadow->mJointName));
            Vec3f position;
            jointMtx.getTrans(position);
            Vec3f gravity;
            MR::calcGravityVector(this, position, &gravity, nullptr, 0);

            if (cameraDir.dot(gravity) < Angle::cosRadian(cNum_calcAnim_f1)) {
                MR::invalidateShadow(this, pShadow->mName);
            } else {
                MR::validateShadow(this, pShadow->mName);
            }

            MR::setShadowDropPosition(this, pShadow->mName, position);
            MR::setShadowDropDirection(this, pShadow->mName, gravity);
        }
    }

    mBossHead->updateCollisionMtx();
}

bool SkeletalFishBoss::calcJoint(Pos3f* pJointPos, const JointControllerInfo& rInfo) {
    if (mJointIndicies[rInfo.mJoint->getJntNo()] == -1) {
        return false;
    }

    SkeletalFishJointCalc::calcJoint(pJointPos, &mRailMtxInv, mRailControl, rInfo);
    return true;
}

void SkeletalFishBoss::damage(const HitSensor* /*pSensor*/, const Vec3f& rStarPieceOffs) {
    if (isEnableToBeDamaged()) {
        MR::shakeCameraStrong();
        playDamageBrk();

        mHitsLeft--;

        if (mHitsLeft == 1 && mLevel + 1 == mLevelNum) {
            MR::emitEffect(this, "SkeltalDamageLast");
        }

        if (mHitsLeft <= 0) {
            if (mLevel + 1 == mLevelNum) {
                setNerve(GET_NERVE_ANON(SkeletalFishBossNrvDeadDamage));
            } else if (MR::isDemoExist("スカルシャークデモ")) {
                setNerve(GET_NERVE_ANON(SkeletalFishBossNrvDown));
            }
        } else {
            u32 numStarPiece = (mLevel + 1 == mLevelNum) ? 15 : 10;
            MR::appearStarPiece(this, rStarPieceOffs, numStarPiece, cNum_damage_f1, cNum_damage_f2, false);
            MR::startSound(this, "SE_OJ_STAR_PIECE_BURST_W_F");
            setNerve(GET_NERVE_ANON(SkeletalFishBossNrvDamage));
        }
    }
}

void SkeletalFishBoss::exeSwim() {
    if (MR::isFirstStep(this)) {
        if (!MR::isBckPlaying(this, "Swim")) {
            MR::startBckWithInterpole(this, "Swim", 60);
            MR::startBckWithInterpole(mBossHead, "Wait", 60);
        }

        mGuardHolder->validate();
    }

    MR::startLevelSound(mBossHead, "SE_BM_LV_SKL_BOSS_SWIM_FAR");
    MR::startLevelSound(mBossHead, "SE_BM_LV_SKL_BOSS_SWIM_NEAR");
    Vec3f mouthPos;
    getMouthSensorCenterPos(mouthPos, cNum_exeSwim_f1);

    bool isClose = mouthPos.distance(*MR::getPlayerPos()) < cNum_exeSwim_f2;

    if (isClose) {
        setNerve(GET_NERVE_ANON(SkeletalFishBossNrvOpen));
    }
}

void SkeletalFishBoss::exeOpen() {
    if (MR::isFirstStep(this)) {
        MR::startBck(mBossHead, "Open");
        MR::startSound(mBossHead, "SE_BM_SKL_BOSS_MOUTH_OPEN");
    }

    MR::startLevelSound(mBossHead, "SE_BM_LV_SKL_BOSS_SWIM_FAR");
    MR::startLevelSound(mBossHead, "SE_BM_LV_SKL_BOSS_SWIM_NEAR");

    if (MR::isBckStopped(mBossHead)) {
        MR::startBck(mBossHead, "OpenWait");
        setNerve(GET_NERVE_ANON(SkeletalFishBossNrvOpenWait));
    }
}

void SkeletalFishBoss::exeOpenWait() {
    Vec3f mouthPos;
    getMouthSensorCenterPos(mouthPos, cNum_exeOpenWait_f1);
    bool isClose = mouthPos.distance(*MR::getPlayerPos()) < cNum_exeOpenWait_f2;

    if (!isClose) {
        setNerve(GET_NERVE_ANON(SkeletalFishBossNrvClose));
    } else {
        MR::startLevelSound(mBossHead, "SE_BM_LV_SKL_BOSS_SWIM_FAR");
        MR::startLevelSound(mBossHead, "SE_BM_LV_SKL_BOSS_SWIM_NEAR");
    }
}

void SkeletalFishBoss::exeClose() {
    if (MR::isFirstStep(this)) {
        MR::startBck(mBossHead, "Close");
        MR::startSound(mBossHead, "SE_BM_SKL_BOSS_MOUTH_CLOSE");
    }

    MR::startLevelSound(mBossHead, "SE_BM_LV_SKL_BOSS_SWIM_FAR");
    MR::startLevelSound(mBossHead, "SE_BM_LV_SKL_BOSS_SWIM_NEAR");

    if (MR::isBckStopped(mBossHead)) {
        MR::startBck(mBossHead, "Wait");
        setNerve(GET_NERVE_ANON(SkeletalFishBossNrvSwim));
    }
}

void SkeletalFishBoss::exeBite() {
    if (MR::isFirstStep(this)) {
        MR::startBck(mBossHead, "Bite");
    }

    MR::startLevelSound(mBossHead, "SE_BM_LV_SKL_BOSS_SWIM_FAR");
    MR::startLevelSound(mBossHead, "SE_BM_LV_SKL_BOSS_SWIM_NEAR");

    if (MR::isBckStopped(mBossHead)) {
        MR::startBck(mBossHead, "Wait");
        setNerve(GET_NERVE_ANON(SkeletalFishBossNrvSwim));
    }
}

void SkeletalFishBoss::exeDamage() {
    if (MR::isFirstStep(this)) {
        startDamageAnim();
        MR::startSound(mBossHead, "SE_BM_SKL_BOSS_DAMAGE_S");
        MR::startSystemSE("SE_SY_VS_BOSS_DAMAGE_1");
        mGuardHolder->invalidate();
    }

    MR::startLevelSound(mBossHead, "SE_BM_LV_SKL_BOSS_SWIM_FAR");
    MR::startLevelSound(mBossHead, "SE_BM_LV_SKL_BOSS_SWIM_NEAR");

    if (MR::isBckStopped(this)) {
        if (mHitsLeft == 2) {
            MR::startBrk(this, "Damage1Color");
            MR::startBrk(mBossHead, "Damage1Color");
        } else if (mHitsLeft == 1) {
            MR::startBrk(this, "Damage2Color");
            MR::startBrk(mBossHead, "Damage2Color");
        }

        setNerve(GET_NERVE_ANON(SkeletalFishBossNrvSwim));
        MR::appearStarPiece(this, mPosition, 6, cNum_exeDamage_f1, cNum_exeDamage_f2, false);
        MR::startSound(this, "SE_OJ_STAR_PIECE_BURST_W_F");
    }
}

void SkeletalFishBoss::exeDown() {
    if (MR::isFirstStep(this)) {
        startDamageAnim();
        MR::startSound(mBossHead, "SE_BM_SKL_BOSS_DAMAGE_L");
        MR::startSystemSE("SE_SY_VS_BOSS_DAMAGE_3");
        mGuardHolder->invalidate();

        if (mLevel == 1) {
            MR::startBva(this, "Crack");
            MR::startBva(mBossHead, "Crack");
        }
    }

    MR::startLevelSound(mBossHead, "SE_BM_LV_SKL_BOSS_SWIM_FAR");
    MR::startLevelSound(mBossHead, "SE_BM_LV_SKL_BOSS_SWIM_NEAR");

    if (MR::isStep(this, NebulaFrameClock::stepsOfSeconds(2))) {
        stopScene("スカルシャークパワーアップ", GET_NERVE_ANON(SkeletalFishBossNrvPowerUpDemo), &SkeletalFishBoss::startPowerUpDemo);
    }
}

void SkeletalFishBoss::exeDeadDamage() {
    if (MR::isFirstStep(this)) {
        startDamageAnim();
        MR::startSound(mBossHead, "SE_BM_SKL_BOSS_DAMAGE_LAST");
        MR::startSystemSE("SE_SY_VS_BOSS_LAST_HIT");
        mGuardHolder->invalidate();
        mGuardHolder->killAll();
        mBossHead->mIsEyeLightOn = false;
    }

    MR::startLevelSound(mBossHead, "SE_BM_LV_SKL_BOSS_SWIM_FAR");
    MR::startLevelSound(mBossHead, "SE_BM_LV_SKL_BOSS_SWIM_NEAR");

    if (MR::isStep(this, NebulaFrameClock::stepsOfSeconds(2))) {
        MR::stopStageBGM(30);
        stopScene("スカルシャーク死亡", GET_NERVE_ANON(SkeletalFishBossNrvDeadDemo), &SkeletalFishBoss::startDeadDemo);
    }
}

void SkeletalFishBoss::exeDead() {
    if (MR::isFirstStep(this)) {
        Mtx pos;

        if (MR::tryFindNamePos("マリオ再セット位置2", pos)) {
            MR::setPlayerBaseMtx(pos);
        }

        resetCamera();
    } else {
        ActorCameraInfo info = ActorCameraInfo();
        if (!MR::isEventCameraActive(&info, "デモ終了後カメラ")) {
            kill();
        }
    }
}

void SkeletalFishBoss::exeAppearWait() {
    if (!MR::isPlayerInBind() && !MR::isEventCameraActive()) {
        stopScene("スカルシャーク出現", GET_NERVE_ANON(SkeletalFishBossNrvAppearDemo), &SkeletalFishBoss::startAppearDemo);
    }
}

void SkeletalFishBoss::exeAppearDemo() {
    if (MR::isFirstStep(this)) {
        MR::showModel(this);
        MR::startBck(this, "BattleStart");
        MR::startBck(mBossHead, "BattleStart");
        MR::stopStageBGM(60);
        mBossDirector->appearBirdLouse();
        mGuardHolder->invalidate();
    }

    if (MR::isStep(this, cStep_exeAppearDemo_1)) {
        MR::startStageBGM("MBGM_BOSS_09_A", false);
    }

    MR::startLevelSound(mBossHead, "SE_BM_LV_SKL_BOSS_SWIM_APPEAR");

    if (MR::isBckStopped(this)) {
        if (!MR::isPlayingStageBgm()) {
            MR::startStageBGM("MBGM_BOSS_09_A", false);
        }

        mBgmHoldStep = NebulaFrameClock::stepsOfSeconds(2);
        mBossDirector->killBirdLouse();
        endAppearDemo();
    }
}

void SkeletalFishBoss::exePowerUpDemo() {
    if (MR::isFirstStep(this)) {
        MR::showModel(this);
        powerUp();
        mGuardHolder->invalidate();
        mGuardHolder->forceKillAll();
        mRailControl->mSpeed = cNum_exePowerUpDemo_f1;

        if (mLevel == 1) {
            MR::startBck(this, "PowerUp1");
            MR::startBck(mBossHead, "PowerUp1");
            mBossDirector->startPowerUpDemo1();
        } else {
            MR::startBck(this, "PowerUp2");
            MR::startBck(mBossHead, "PowerUp2");
            mBossDirector->startPowerUpDemo2();
        }

        mBossDirector->appearBirdLouse();

        if (mLevel > 1) {
            MR::stopStageBGM(30);
        }
    }

    if (mLevel == 2 && MR::isStep(this, NebulaFrameClock::stepsOfSeconds(4))) {
        MR::startAtmosphereSE("SE_BM_SKL_GUARD_COME_FRONT");
    }

    MR::startLevelSound(mBossHead, "SE_BM_LV_SKL_BOSS_SWIM_POW_UP");

    if (MR::isBckStopped(this)) {
        if (mLevel > 1) {
            MR::startStageBGM("MBGM_BOSS_09_B", false);
        }

        mBgmHoldStep = NebulaFrameClock::stepsOfSeconds(2);
        mBossDirector->killBirdLouse();
        endPowerUpDemo();
    }
}

void SkeletalFishBoss::exeDeadDemo() {
    if (MR::isFirstStep(this)) {
        MR::startBck(this, "Down");
        MR::startBrk(this, "Down");
        MR::startBck(mBossHead, "Down");
        MR::startBrk(mBossHead, "Down");
        MR::startSound(mBossHead, "SE_BM_SKL_BOSS_MOUTH_OPEN");
        MR::setImageEffectControlAuto();
        mGuardHolder->invalidate();
        mBossDirector->appearBirdLouse();
    }

    if (MR::isStep(this, NebulaFrameClock::stepsOfMilliseconds(500))) {
        MR::startSound(mBossHead, "SE_BM_SKL_BOSS_BODY_SOLID");
    }

    if (MR::isLessStep(this, cStep_exeDeadDemo_1)) {
        MR::startLevelSound(mBossHead, "SE_BM_LV_SKL_BOSS_SWIM_DIE");
    }

    if (MR::isBckStopped(this)) {
        mBossDirector->killBirdLouse();
        MR::overlayWithPreviousScreen(2);
        setNerve(GET_NERVE_ANON(SkeletalFishBossNrvBreakDemo));
    }
}

void SkeletalFishBoss::exeBreakDemo() {
    if (MR::isFirstStep(this)) {
        MR::hideModel(this);
        mBossHead->kill();
        mBreakModel->makeActorAppeared();
        MR::requestMovementOn(mBreakModel);
        MR::startBck(mBreakModel, "Break");
        MR::startBrk(mBreakModel, "Break");
        MR::startSound(mBossHead, "SE_BM_SKL_BOSS_EXPLODE");
        mGuardHolder->invalidate();
        mBossDirector->appearBirdLouse();
        MR::tryRumblePadMiddle(this, PLAYER_1);
    }

    if (MR::isStep(this, cStep_exeBreakDemo_1)) {
        MR::tryRumblePadStrong(this, PLAYER_1);
    }

    if (MR::isStep(this, cStep_exeBreakDemo_2)) {
        MR::startSound(mBossHead, "SE_BM_SKL_BOSS_EXPLODE_2");
    }

    if (MR::isStep(this, 0)) {
        MR::startCenterScreenBlur(60, cNum_exeBreakDemo_f1, 0x50, 5, 30);
    }

    if (MR::isStep(this, cStep_exeBreakDemo_3)) {
        MR::startCenterScreenBlur(60, cNum_exeBreakDemo_f2, 0x50, 5, 30);
    }

    if (MR::isBckStopped(mBreakModel)) {
        MR::startAfterBossBGM();
        mBossDirector->killBirdLouse();
        endBreakDemo();
        setNerve(GET_NERVE_ANON(SkeletalFishBossNrvDead));
    }
}

void SkeletalFishBoss::exeDemoWait() {
    if (MR::tryStartDemo(this, mCurScene)) {
        (this->*mSceneFunc)();
        MR::requestMovementOn(this);
        MR::requestMovementOn(mBossHead);
        MR::requestMovementOn(mScarFlash);
        mBossDirector->pauseOffCast();
        setNerve(mSceneNerve);
    }
}

void SkeletalFishBoss::notifyAttack(SkeletalFishGuard* pGuard) {
    mGuardHolder->notifyAttack(pGuard);
}

LiveActor* SkeletalFishBoss::getCurrentRail() {
    return mRailControl->mRail;
}

f32 SkeletalFishBoss::getBodyThickness() const {
    return cNum_getBodyThickness_f1;
}

SkeletalFishBossHead* SkeletalFishBoss::getHeadActor() const {
    return mBossHead;
}

void SkeletalFishBoss::calcAndSetBaseMtx() {
    bool isInDemo = isNerve(GET_NERVE_ANON(SkeletalFishBossNrvAppearDemo)) || isNerve(GET_NERVE_ANON(SkeletalFishBossNrvPowerUpDemo)) ||
                    isNerve(GET_NERVE_ANON(SkeletalFishBossNrvDeadDemo));

    if (isInDemo) {
        MR::setBaseTRMtx(this, mPlacementMtx);
    } else {
        for (u32 i = 0; i < ARRAY_SIZE(mControllers); i++) {
            mControllers[i]->registerCallBack();
        }

        Pos3f railMtx;
        mRailControl->getMtx(&railMtx, 0.0f);
        MR::setBaseTRMtx(this, railMtx);
        mRailMtxInv.setInline(railMtx);
        mRailMtxInv.invert(mRailMtxInv);
    }
}

void SkeletalFishBoss::initLevelStatus() {
    mBossInfo = new SkeletalFishBossInfo(this, mLevelNum, mGuardNum, "ボス戦パラメータ");
}

void SkeletalFishBoss::initJoint() {
    mJointIndicies = new s32[MR::getJointNum(this)];

    for (u32 i = 0; i < MR::getJointNum(this); i++) {
        mJointIndicies[i] = -1;
    }

    for (s32 i = 0; i < ARRAY_SIZE(mControllers); i++) {
        const MR::FormattedName jointName("Joint%02d", i);
        mControllers[i] = MR::createJointDelegatorWithNullChildFunc(this, &SkeletalFishBoss::calcJoint, jointName.c_str());
        mJointIndicies[MR::getJointIndex(this, jointName.c_str())] = i;
    }
}

void SkeletalFishBoss::initCollision() {
    mPartsArray = new CollisionParts*[ARRAY_SIZEU(::sColInfo)];

    for (u32 i = 0; i < ARRAY_SIZEU(::sColInfo); i++) {
        mPartsArray[i] = MR::createCollisionPartsFromLiveActor(this, ::sColInfo[i].mColliderName, mBossHead->getSensor("body"),
                                                               MR::getJointMtx(this, ::sColInfo[i].mJointName), MR::CollisionScaleType_Default);
        MR::validateCollisionParts(mPartsArray[i]);
    }
}

void SkeletalFishBoss::initHead() {
    mBossHead = new SkeletalFishBossHead(this);
    mBossHead->initWithoutIter();
}

void SkeletalFishBoss::initScarFlash() {
    mScarFlash = new SkeletalFishBossScarFlash(this);
    mScarFlash->initWithoutIter();
}

void SkeletalFishBoss::initBreakModel() {
    mBreakModel = new ModelObj("壊れモデル", "SkeletalFishBossBreak", mPlacementMtx.mMtx, MR::DrawBufferType_Enemy, -2, -2, false);
    mBreakModel->initWithoutIter();
    MR::initLightCtrl(mBreakModel);
    mBreakModel->makeActorDead();
    MR::invalidateClipping(mBreakModel);
}

void SkeletalFishBoss::initSwitch(const TableIter& rIter) {
    if (MR::useStageSwitchReadAppear(this, rIter)) {
        MR::syncStageSwitchAppear(this);
    } else if (MR::useStageSwitchReadA(this, rIter)) {
        MR::listenStageSwitchOnA(this, MR::Functor(this, &SkeletalFishBoss::appear));
    }
}

void SkeletalFishBoss::createGuards() {
    mGuardHolder = new SkeletalFishGuardHolder(this, mGuardNum, "スカルシャークガード管理");
}

void SkeletalFishBoss::initShadow() {
    MR::initShadowController(this, 1);

    for (u32 i = 0; i < ARRAY_SIZE(::sShadowInfo); i++) {
        const JointToShadow& rShadow = ::sShadowInfo[i];
        const char* pName = rShadow.mName;
        MtxPtr pMtx = MR::getJointMtx(this, rShadow.mJointName);
        MR::addShadowVolumeFlatModel(this, pName, rShadow.mShadowName, pMtx);
        MR::setShadowDropLength(this, pName, cNum_initShadow_f1);
    }

    MR::excludeCalcShadowToSensorAll(this, mBossHead->getSensor("body"));
}

void SkeletalFishBoss::initCamera() {
    ActorCameraInfo cameraInfo = ActorCameraInfo();
    const char* fileName = "SkeletalFishBossBattleStart.canm";
    MR::declareEventCameraAnim(&cameraInfo, "スカルシャーク出現", MR::getResourceHolder(this)->mFileInfoTable->getRes(fileName));
    fileName = "SkeletalFishBossPowerUp1.canm";
    MR::declareEventCameraAnim(&cameraInfo, "スカルシャークパワーアップ", MR::getResourceHolder(this)->mFileInfoTable->getRes(fileName));
    fileName = "SkeletalFishBossPowerUp2.canm";
    MR::declareEventCameraAnim(&cameraInfo, "スカルシャークパワーアップ２", MR::getResourceHolder(this)->mFileInfoTable->getRes(fileName));
    fileName = "SkeletalFishBossDown.canm";
    MR::declareEventCameraAnim(&cameraInfo, "スカルシャーク死亡", MR::getResourceHolder(this)->mFileInfoTable->getRes(fileName));
    MR::declareEventCameraProgrammable("デモ終了後カメラ");
    mCameraTargetMtx = new CameraTargetMtx("カメラターゲットダミー");
    mCameraTargetMtx->mMatrix.setInline(mPlacementMtx);
}

void SkeletalFishBoss::validateCollision() {
    for (u32 i = 0; i < ARRAY_SIZEU(::sColInfo); i++) {
        MR::validateCollisionParts(mPartsArray[i]);
    }

    SkeletalFishBossHead* head = getHeadActor();
    MR::validateCollisionParts(head->mHeadCollision);
    MR::validateCollisionParts(head->mJawCollision);
}

void SkeletalFishBoss::invalidateCollision() {
    for (u32 i = 0; i < ARRAY_SIZEU(::sColInfo); i++) {
        MR::invalidateCollisionParts(mPartsArray[i]);
    }

    SkeletalFishBossHead* head = getHeadActor();
    MR::invalidateCollisionParts(head->mHeadCollision);
    MR::invalidateCollisionParts(head->mJawCollision);
}

void SkeletalFishBoss::getMouthSensorCenterPos(Vec3f& rPos, f32 forward) const {
    Pos3f mtx(MR::getJointMtx(this, "Head"));
    rPos.z = forward;
    rPos.x = 0.0f;
    rPos.y = 0.0f;
    mtx.mult(rPos, rPos);
}

void SkeletalFishBoss::updateCollision() {
    for (u32 i = 0; i < ARRAY_SIZEU(::sColInfo); i++) {
        mPartsArray[i]->setMtx();
    }

    SkeletalFishBossHead* head = getHeadActor();
    head->mHeadCollision->setMtx();
    head->mJawCollision->setMtx();
}

void SkeletalFishBoss::powerUp() {
    mLevel++;
    mHitsLeft = mBossInfo->getLevelStatus(mLevel)->mEnergyLevel;
    resetRail();
}

void SkeletalFishBoss::startDamageAnim() {
    MR::startBck(this, "Damage");
    MR::startBck(mBossHead, "Damage");
    mScarFlash->appear();
}

void SkeletalFishBoss::calcPlanetCenterPosition() {
    SkeletalFishBossRail* pRail = findFirstBossRail();

    // The planet centre comes from the first two points of the first rail; fewer points keep the centre at the origin.
    if (pRail == nullptr || MR::getRailPointNum(pRail) < 2) {
        return;
    }

    Vec3f firstPnt;
    MR::calcRailPointPos(&firstPnt, pRail, 0);
    Vec3f secondPnt;
    MR::calcRailPointPos(&secondPnt, pRail, 1);
    calcGravityCenter(&mPlanetCenter, firstPnt, secondPnt);
}

bool SkeletalFishBoss::calcGravityCenter(Vec3f* pOut, const Vec3f& rFirstPoint, const Vec3f& rSecondPoint) {
    Vec3f firstGravityCenter(rFirstPoint);
    Vec3f firstGravityVector;
    MR::calcGravityVector(this, firstGravityCenter, &firstGravityVector, nullptr, 0);
    firstGravityVector.negate();

    Vec3f secondGravityCenter(rSecondPoint);
    Vec3f secondGravityVector;
    MR::calcGravityVector(this, secondGravityCenter, &secondGravityVector, nullptr, 0);
    secondGravityVector.negate();

    f32 firstGravitySquared = firstGravityVector.squared();
    f32 secondGravitySquared = secondGravityVector.squared();
    f32 dotProduct = firstGravityVector.dot(secondGravityVector);
    f32 dotProductSquared = dotProduct * dotProduct;

    Vec3f distanceVector(firstGravityCenter - secondGravityCenter);
    Vec3f reversedDistanceVector(secondGravityCenter - firstGravityCenter);
    f32 dotFirst = distanceVector.dot(secondGravityVector);
    f32 dotSecond = reversedDistanceVector.dot(firstGravityVector);

    if (((firstGravitySquared * secondGravitySquared) - dotProductSquared) < cNum_calcGravityCenter_f1) {
        return false;
    }

    f32 scaleFactor =
        (1.0f / (firstGravitySquared * secondGravitySquared - dotProductSquared)) * (secondGravitySquared * dotSecond + dotProduct * dotFirst);
    pOut->set(firstGravityCenter + firstGravityVector * scaleFactor);
    return true;
}

void SkeletalFishBoss::resetRail() {
    SkeletalFishBossInfo::LevelStatus* lvl = mBossInfo->getLevelStatus(mLevel);
    SkeletalFishBossRail* rail = MR::getSkeletalFishBossRailHolder()->getByID(lvl->mRailIDLevel);

    // Nothing to swim on: the rail stays as it is.
    if (rail == nullptr) {
        return;
    }

    mRailControl->setRailActor(rail, nullptr, true);
    mRailControl->mSpeed = mBossInfo->getLevelStatus(mLevel)->mSpeedLevel;
}

void SkeletalFishBoss::updateBgm() {
    s32 curType = mBgmHoldStep;

    if (curType < 0 || (mBgmHoldStep = curType - 1, curType - 1 < 0)) {
        mBgmHoldStep = -1;
        s32 bgType, fadeTime;

        if (MR::calcDistanceToPlayer(this) <= cNum_updateBgm_f1) {
            bgType = 2;
            fadeTime = NebulaFrameClock::stepsOfSeconds(1);
        } else {
            bgType = 1;
            fadeTime = NebulaFrameClock::stepsOfSeconds(2);
        }

        if (bgType != mBgmType) {
            MR::setStageBGMState(bgType, fadeTime);
        }

        mBgmType = bgType;
    }
}

LiveActor* SkeletalFishBoss::getCurrentBossRail() {
    return mRailControl->mRail;
}

void SkeletalFishBoss::stopScene(const char* pName, const Nerve* pNerve, SceneFunc func) {
    setNerve(GET_NERVE_ANON(SkeletalFishBossNrvDemoWait));
    mSceneFunc = func;
    mCurScene = pName;
    mSceneNerve = pNerve;
}

void SkeletalFishBoss::startCamera(const char* pCameraName) {
    ActorCameraInfo cameraInfo = ActorCameraInfo();
    CameraTargetArg target(nullptr, mCameraTargetMtx, nullptr, nullptr);
    MR::startEventCamera(&cameraInfo, pCameraName, target, 0);
}

void SkeletalFishBoss::resetCamera() {
    MR::startGlobalEventCameraTargetPlayer("デモ終了後カメラ", 0);
    Pos3f playerMtx(MR::getPlayerBaseMtx());
    Vec3f position;
    playerMtx.getTrans(position);
    Vec3f targetPosition;
    playerMtx.getYDir(targetPosition);
    Vec3f frontDirection;
    Vec3f cameraTarget = position + targetPosition * cNum_resetCamera_f1;
    targetPosition = cameraTarget;
    playerMtx.getZDir(frontDirection);
    MR::setProgrammableCameraParam("デモ終了後カメラ", position, targetPosition, frontDirection, true);
}

void SkeletalFishBoss::playDamageBrk() {
    const MR::FormattedName brkName("Damage%dColor", mDamageBrkIndex);
    MR::startBrk(this, brkName.c_str());
    MR::startBrk(mBossHead, brkName.c_str());
    mDamageBrkIndex++;
}

void SkeletalFishBoss::startAppearDemo() {
    MR::requestMovementOn(MR::getSceneObj< SensorHitChecker >(SceneObj_SensorHitChecker));
    Mtx namePosMtx;

    if (MR::tryFindNamePos("マリオ再セット位置1", namePosMtx)) {
        MR::setPlayerBaseMtx(namePosMtx);
    }

    MR::hidePlayer();
    startCamera("スカルシャーク出現");

    if (!MR::isNormalBloomOn()) {
        MR::turnOnNormalBloom();
        MR::setNormalBloomIntensity(0xD5);
        MR::setNormalBloomThreshold(0);
        MR::setNormalBloomBlurIntensity1(0x2A);
        MR::setNormalBloomBlurIntensity2(0x15);
    }
}

void SkeletalFishBoss::endAppearDemo() {
    Mtx namePosMtx;

    if (MR::tryFindNamePos("マリオ再セット位置1", namePosMtx)) {
        MR::setPlayerBaseMtx(namePosMtx);
    }

    MR::showPlayer();
    const char* cameraName = "スカルシャーク出現";
    ActorCameraInfo cameraInfo = ActorCameraInfo();
    MR::endEventCamera(&cameraInfo, cameraName, false, 0);
    resetCamera();

    if (cameraName != nullptr) {
        MR::endDemo(this, cameraName);
    }

    MR::startBck(this, "Swim");
    MR::startBck(mBossHead, "Wait");
    resetRail();
    setNerve(GET_NERVE_ANON(SkeletalFishBossNrvSwim));
}

void SkeletalFishBoss::startPowerUpDemo() {
    MR::requestMovementOn(MR::getSceneObj< SensorHitChecker >(SceneObj_SensorHitChecker));
    MR::overlayWithPreviousScreen(2);
    MR::hidePlayer();

    if (!mLevel) {
        startCamera("スカルシャークパワーアップ");
    } else {
        startCamera("スカルシャークパワーアップ２");
    }
}

void SkeletalFishBoss::endPowerUpDemo() {
    Mtx namePosMtx;

    if (MR::tryFindNamePos("マリオ再セット位置2", namePosMtx)) {
        MR::setPlayerBaseMtx(namePosMtx);
    }

    MR::showPlayer();

    const char* eventCameraName;

    if (mLevel == 1) {
        eventCameraName = "スカルシャークパワーアップ";
        ActorCameraInfo cameraInfo = ActorCameraInfo();
        MR::endEventCamera(&cameraInfo, eventCameraName, false, 0);
        resetCamera();
        mBossDirector->endPowerUpDemo1();
    } else {
        eventCameraName = "スカルシャークパワーアップ２";
        ActorCameraInfo cameraInfo = ActorCameraInfo();
        MR::endEventCamera(&cameraInfo, eventCameraName, false, 0);
        resetCamera();
        mBossDirector->endPowerUpDemo2();
    }

    if ("スカルシャークパワーアップ" != nullptr) {
        MR::endDemo(this, "スカルシャークパワーアップ");
    }

    resetRail();
    MR::startBck(this, "Swim");
    MR::startBck(mBossHead, "Wait");
    mGuardHolder->forceAppearAll();
    setNerve(GET_NERVE_ANON(SkeletalFishBossNrvSwim));
}

void SkeletalFishBoss::startDeadDemo() {
    MR::overlayWithPreviousScreen(2);
    MR::hidePlayer();
    startCamera("スカルシャーク死亡");
}

void SkeletalFishBoss::endBreakDemo() {
    const char* cameraName = "スカルシャーク死亡";
    ActorCameraInfo info = ActorCameraInfo();
    MR::endEventCamera(&info, cameraName, false, 0);
    resetCamera();

    if (cameraName != nullptr) {
        MR::endDemo(this, cameraName);
    }

    MR::showPlayer();
    mBreakModel->makeActorDead();
    MR::hideModel(this);
    Vec3f offset(0.0f, cNum_endBreakDemo_f1, 0.0f);
    mPlacementMtx.mult(offset, offset);
    MR::requestAppearPowerStar(this, offset);
}

bool SkeletalFishBoss::isEnableToBeDamaged() const {
    return !isNerve(GET_NERVE_ANON(SkeletalFishBossNrvDamage)) && !isNerve(GET_NERVE_ANON(SkeletalFishBossNrvDown)) &&
           !isNerve(GET_NERVE_ANON(SkeletalFishBossNrvDeadDamage)) && !isNerve(GET_NERVE_ANON(SkeletalFishBossNrvDead)) &&
           !isNerve(GET_NERVE_ANON(SkeletalFishBossNrvAppearDemo)) && !isNerve(GET_NERVE_ANON(SkeletalFishBossNrvPowerUpDemo)) &&
           !isNerve(GET_NERVE_ANON(SkeletalFishBossNrvDeadDemo));
}

SkeletalFishBossHead::SkeletalFishBossHead(LiveActor* pActor)
    : PartsModel(pActor, "スカルシャーク頭", "SkeletalFishBossHeadA", nullptr, MR::DrawBufferType_Enemy, false), mIsEyeLightOn(true) {
    initFixedPosition("Head");
    // The body and one sensor per wound.
    initHitSensor(1 + ARRAY_SIZE(::sHitPosData));
    MR::addHitSensorAtJointEnemy(this, "body", "Head", 8, cNum_SkeletalFishBossHead_f1, Vec3f(0.0f, -cNum_SkeletalFishBossHead_f2, cNum_SkeletalFishBossHead_f3));

    for (u32 i = 0; i < ARRAY_SIZE(::sHitPosData); i++) {
        Vec3f offset(::sHitPosData[i].mOffset);

        offset.scale(cNum_SkeletalFishBossHead_f4 / cNum_SkeletalFishBossHead_f5);
        MR::addHitSensorAtJointEnemyAttack(this, ::sHitPosData[i].mName, ::sHitPosData[i].mSensorName, 8, cNum_SkeletalFishBossHead_f6, offset);
    }

    MR::initLightCtrl(this);
    mHeadCollisionMtx.identity();
    mHeadCollision = MR::createCollisionPartsFromLiveActor(this, "Head", getSensor("body"), mHeadCollisionMtx.toMtxPtr(), MR::CollisionScaleType_Default);
    MR::validateCollisionParts(mHeadCollision);
    mJawCollisionMtx.identity();
    mJawCollision = MR::createCollisionPartsFromLiveActor(this, "Jow", getSensor("body"), mJawCollisionMtx.toMtxPtr(), MR::CollisionScaleType_Default);
    MR::validateCollisionParts(mJawCollision);
    MR::addToAttributeGroupSearchTurtle(this);
    createSubModel();
}

void SkeletalFishBossHead::movement() {
    PartsModel::movement();

    if (mIsEyeLightOn) {
        if (MR::isConnectToDrawTemporarily(this) && !MR::isHiddenModel(this)) {
            if (!MR::isConnectToDrawTemporarily(mLightModels[0])) {
                for (s32 i = 0; i < 2; i++) {
                    MR::connectToDrawTemporarily(mLightModels[i]);
                    MR::connectToDrawTemporarily(mBloomModels[i]);
                }
            }
        } else if (MR::isConnectToDrawTemporarily(mLightModels[0])) {
            for (s32 i = 0; i < 2; i++) {
                MR::disconnectToDrawTemporarily(mLightModels[i]);
                MR::disconnectToDrawTemporarily(mBloomModels[i]);
            }
        }
    } else if (MR::isConnectToDrawTemporarily(mLightModels[0])) {
        for (s32 i = 0; i < 2; i++) {
            MR::disconnectToDrawTemporarily(mLightModels[i]);
            MR::disconnectToDrawTemporarily(mBloomModels[i]);
        }
    }
}

void SkeletalFishBossHead::calcAnim() {
    PartsModel::calcAnim();

    if (MR::isConnectToDrawTemporarily(this)) {
        for (s32 i = 0; i < 2; i++) {
            mLightModels[i]->calcAnim();
            mBloomModels[i]->calcAnim();
        }
    }
}

bool SkeletalFishBossHead::receiveMsgPlayerAttack(u32 msg, HitSensor* pSender, HitSensor* pReceiver) {
    if (MR::isMsgJetTurtleAttack(msg)) {
        SkeletalFishBoss* boss = static_cast< SkeletalFishBoss* >(mHost);
        boss->damage(pReceiver, pSender->mPosition);
        return true;
    }

    return MR::isMsgStarPieceReflect(msg);
}

void SkeletalFishBossHead::attackSensor(HitSensor* pSender, HitSensor* pReceiver) {
    if (!MR::isSensor(pSender, "body")) {
        return;
    }

    LiveActor* pHost = mHost;
    bool curFlag = pHost->isNerve(GET_NERVE_ANON(SkeletalFishBossNrvDeadDamage)) || pHost->isNerve(GET_NERVE_ANON(SkeletalFishBossNrvDead));

    if (curFlag) {
        return;
    }

    if (MR::isSensorPlayer(pReceiver)) {
        curFlag = pHost->isNerve(GET_NERVE_ANON(SkeletalFishBossNrvAppearDemo)) || pHost->isNerve(GET_NERVE_ANON(SkeletalFishBossNrvPowerUpDemo)) ||
                  pHost->isNerve(GET_NERVE_ANON(SkeletalFishBossNrvDeadDemo));

        if (curFlag) {
            return;
        }

        if (pHost->isNerve(GET_NERVE_ANON(SkeletalFishBossNrvDamage))) {
            MR::sendMsgPush(pReceiver, pSender);
        } else {
            MR::sendMsgPush(pReceiver, pSender);

            if (pHost->isNerve(GET_NERVE_ANON(SkeletalFishBossNrvOpenWait))) {
                pHost->setNerve(GET_NERVE_ANON(SkeletalFishBossNrvBite));
            }
        }
    } else if (!MR::isSensorEnemyAttack(pSender) && MR::isSensorEnemy(pReceiver)) {
        MR::sendMsgEnemyAttack(pReceiver, pSender);
    }
}

void SkeletalFishBossHead::updateCollisionMtx() {
    mHeadCollisionMtx.setInline(MR::getJointMtx(this, "Head"));
    mJawCollisionMtx.setInline(MR::getJointMtx(this, "Jow"));
}

void SkeletalFishBossHead::createSubModel() {
    const char* coneMdls[2] = {"LightConeLeft", "LightConeRight"};

    for (s32 i = 0; i < 2; i++) {
        mLightModels[i] = new ModelObj("眼光", "SkeletalFishBossLight", MR::getJointMtx(this, coneMdls[i]), MR::DrawBufferType_Enemy, -1, -1, false);
        mLightModels[i]->initWithoutIter();
        MR::invalidateClipping(mLightModels[i]);

        mBloomModels[i] = new ModelObj("眼光ブルーム", "SkeletalFishBossLightBloom", MR::getJointMtx(this, coneMdls[i]),
                                       MR::DrawBufferType_BloomModel, -1, -1, false);
        mBloomModels[i]->initWithoutIter();
        MR::invalidateClipping(mBloomModels[i]);
    }
}

SkeletalFishBossScarFlash::SkeletalFishBossScarFlash(LiveActor* pActor)
    : PartsModel(pActor, "スカルシャーク傷跡エフェクトモデル", "SkeletalFishBossScarFlash", nullptr, MR::DrawBufferType_Enemy, false) {
    initFixedPosition("Head");
}

void SkeletalFishBossScarFlash::init(const TableIter& rIter) {
    PartsModel::init(rIter);
    makeActorDead();
}

void SkeletalFishBossScarFlash::appear() {
    LiveActor::appear();
    MR::startBck(this, "Damage");
    MR::startBrk(this, "Damage");
}

void SkeletalFishBossScarFlash::control() {
    if (MR::isBckStopped(this)) {
        kill();
    }
}
