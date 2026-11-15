#include <nebula_game_api.h>
#include "Game/Enemy/WaterBazooka.hpp"
#include "Game/System/NebulaFrameClock.hpp"
#include "Game/Camera/CameraTargetObj.hpp"
#include "Game/Enemy/ElectricPressureBullet.hpp"
#include "Game/Enemy/MogucchiShooter.hpp"
#include "Game/Enemy/WaterBazookaCapsule.hpp"
#include "Game/LiveActor/HitSensor.hpp"
#include "Game/LiveActor/ModelObj.hpp"
#include "Game/LiveActor/Nerve.hpp"
#include "Game/Map/CollisionParts.hpp"
#include "Game/MapObj/WaterPressureBullet.hpp"
#include "Game/MapObj/WaterPressureBulletHolder.hpp"
#include "Game/Scene/SceneObjHolder.hpp"
#include "Game/Util/ActorCameraUtil.hpp"
#include "Game/Util/ActorMovementUtil.hpp"
#include "Game/Util/ActorSensorUtil.hpp"
#include "Game/Util/ActorSwitchUtil.hpp"
#include "Game/Util/DemoUtil.hpp"
#include "Game/Util/EffectUtil.hpp"
#include "Game/Util/EventUtil.hpp"
#include "Game/Util/PlacementUtil.hpp"
#include "Game/Util/JointController.hpp"
#include "Game/Util/JointUtil.hpp"
#include "Game/Util/LiveActorUtil.hpp"
#include "Game/Util/MathUtil.hpp"
#include "Game/Util/MtxUtil.hpp"
#include "Game/Util/ObjUtil.hpp"
#include "Game/Util/PlayerUtil.hpp"
#include "Game/Util/SceneUtil.hpp"
#include "Game/Util/SequenceUtil.hpp"
#include "Game/Util/SoundUtil.hpp"
#include "Game/Util/StringUtil.hpp"
#include "math_types.hpp"
#include <nebula_geometry.hpp>

#include <nebula_math.h>
#include "Game/System/LumaCore.hpp"

namespace NrvWaterBazooka {
    NEW_NERVE(WaterBazookaNrvWaitForBattle, WaterBazooka, WaitForBattle);
    NEW_NERVE(WaterBazookaNrvWait, WaterBazooka, Wait);
    NEW_NERVE(WaterBazookaNrvAim, WaterBazooka, Aim);
    NEW_NERVE(WaterBazookaNrvAimEnd, WaterBazooka, AimEnd);
    NEW_NERVE(WaterBazookaNrvShot, WaterBazooka, Shot);
    NEW_NERVE(WaterBazookaNrvShotNoMotion, WaterBazooka, ShotNoMotion);
    NEW_NERVE(WaterBazookaNrvTire, WaterBazooka, Tire);
    NEW_NERVE(WaterBazookaNrvDemoCrackCapsule, WaterBazooka, DemoCrackCapsule);
    NEW_NERVE(WaterBazookaNrvDemoAnger, WaterBazooka, DemoAnger);
    NEW_NERVE(WaterBazookaNrvDemoBreakWait, WaterBazooka, DemoBreakWait);
    NEW_NERVE(WaterBazookaNrvDemoBreakSign, WaterBazooka, DemoBreakSign);
    NEW_NERVE(WaterBazookaNrvDemoBreakExplosion, WaterBazooka, DemoBreakExplosion);
    NEW_NERVE(WaterBazookaNrvDemoBreakEnd, WaterBazooka, DemoBreakEnd);
    NEW_NERVE(WaterBazookaNrvWaitForLaugh, WaterBazooka, WaitForLaugh);
    NEW_NERVE(WaterBazookaNrvPanic, WaterBazooka, Panic);
    NEW_NERVE_ONEND(WaterBazookaNrvStorm, WaterBazooka, Storm, Storm);
};  // namespace NrvWaterBazooka

namespace {
    // A water shot is not broken by a spin.
    const bool cShotInvalidSpinKill = false;
    const f32 sElectricBulletSpeed = 12.0f;
    const char* const sDropPointStringTable[] = {"落下点1", "落下点2", "落下点3", "落下点4"};
    const s32 sElectricBulletNum = 5;
};  // namespace

WaterBazooka::WaterBazooka(const char* pName)
    : LiveActor(pName), mShooter(), mCapsule(), mBreakModel(), mDemoActor(), mCameraInfo(), mAlreadyDoneFlag(-1), mLife(3), mShotNum(),
      mCannonCollisionParts(), mJointCtrl(), mIsPinchBGMStarted(), mHasPowerStar(), mIsElectric(), mBullets(), mElectricTime(-1) {
    lumacore::setClass(this, lumacore::kClassId< WaterBazooka >);
    mBaseMtx.identity();
}

void WaterBazooka::init(const TableIter& rIter) {
    MR::initDefaultPos(this, rIter);

    const char* objectName;
    MR::getObjectName(&objectName, rIter);
    mIsElectric = MR::isEqualString("ElectricBazooka", objectName);
    initModelManagerWithAnm(objectName, nullptr, false);

    MR::connectToSceneMapObjStrongLight(this);

    initHitSensor(2);
    MR::addBodyMessageSensorMapObj(this);
    MR::addMessageSensorEnemy(this, "cannon");

    MR::initCollisionParts(this, "Cannon1", getSensor("cannon"), MR::getJointMtx(this, "Cannon1"));
    mCannonCollisionParts = MR::createCollisionPartsFromLiveActor(this, "AllRoot", getSensor("body"), MR::CollisionScaleType_Default);
    MR::validateCollisionParts(mCannonCollisionParts);

    initEffectKeeper(1, nullptr);
    MR::addEffectHitNormal(this, nullptr);

    initSound(4, false);
    MR::invalidateClipping(this);

    MR::initActorCamera(this, rIter, &mCameraInfo);

    mJointCtrl = MR::createJointDelegatorWithNullChildFunc(this, &WaterBazooka::calcJointCannon, "Cannon1");
    MR::initJointTransform(this);
    mBaseMtx.setInline(MR::getJointMtx(this, "Cannon1"));

    initShooter();
    initBazookaCapsule();
    initBreakModel();
    initCameraTarget();
    initBullet(rIter);

    MR::getPlacementArg0NoInit(rIter, &mHasPowerStar);
    if (mHasPowerStar) {
        MR::declarePowerStar(this);
    }

    if (MR::tryRegisterDemoCast(this, rIter)) {
        MR::tryRegisterDemoCast(mShooter, rIter);
        MR::tryRegisterDemoCast(mCapsule, rIter);
        MR::tryRegisterDemoCast(mDemoActor, rIter);
        MR::tryRegisterDemoCast(mBreakModel, rIter);
    }

    MR::startBrk(this, "Damage");
    MR::setBrkFrameAndStop(this, 0.0f);
    MR::declareStarPiece(this, 24);
    MR::addToAttributeGroupSearchTurtle(this);

    MR::useStageSwitchReadA(this, rIter);
    MR::useStageSwitchWriteDead(this, rIter);

    initNerve(GET_NERVE(WaterBazooka, WaterBazookaNrvWait));

    if (MR::isEqualStageName("OceanPhantomCaveGalaxy")) {
        u32 isDone = 0;
        mAlreadyDoneFlag = MR::setupAlreadyDoneFlag("ウォータバズーカ撃破", rIter, &isDone);

        if (isDone != 0) {
            if (MR::isValidSwitchDead(this)) {
                MR::onSwitchDead(this);
            }

            makeActorDead();
            return;
        }
    }

    makeActorAppeared();
}

bool WaterBazooka::receiveMsgPlayerAttack(u32 msg, HitSensor* pSender, HitSensor* pReceiver) {
    if (MR::isMsgStarPieceReflect(msg)) {
        return true;
    }

    if (pReceiver == getSensor("body")) {
        return false;
    }

    if (MR::isDemoActive()) {
        return false;
    }

    if (isNrvDemo()) {
        return false;
    }

    if (MR::isMsgPlayerHipDropFloor(msg) && pReceiver->mType == ATYPE_WATER_BAZOOKA_CAPSULE) {
        if (isElectricLeak() && (!MR::isPlayerDamaging() || MR::isPlayerParalyzing())) {
            return false;
        }

        damageBazooka(pSender, pReceiver);
        return true;
    }

    if (MR::isMsgJetTurtleAttack(msg)) {
        damageBazooka(pSender, pReceiver);
        return true;
    }

    return false;
}

bool WaterBazooka::receiveMsgEnemyAttack(u32 /*msg*/, HitSensor* pSender, HitSensor* pReceiver) {
    if (pReceiver == getSensor("body")) {
        return false;
    }

    if (MR::isDemoActive()) {
        return false;
    }

    if (isNrvDemo()) {
        return false;
    }

    if (pSender->mType == ATYPE_COCO_NUT) {
        damageBazooka(pSender, pReceiver);
        return true;
    }

    return false;
}

bool WaterBazooka::receiveOtherMsg(u32 msg, HitSensor* pSender, HitSensor* pReceiver) {
    if (!MR::isSensorPlayer(pSender)) {
        return false;
    }

    if (pReceiver == getSensor("body")) {
        return false;
    }

    if (MR::isDemoActive()) {
        return false;
    }

    if (isNrvDemo()) {
        return false;
    }

    if (MR::isMsgFloorTouch(msg) || MR::isMsgWallTouch(msg)) {
        if (isElectricLeak() && (!MR::isPlayerHipDropFalling() && !MR::isPlayerHipDropLand())) {
            MR::sendMsgEnemyAttackElectric(pSender, pReceiver);
            return true;
        }

        if (isNerve(GET_NERVE(WaterBazooka, WaterBazookaNrvStorm))) {
            MR::sendMsgEnemyAttackFlip(pSender, pReceiver);
            return true;
        }

        return false;
    }

    return false;
}

bool WaterBazooka::isFirstShoot() const {
    if (MR::isFirstStep(this)) {
        return isNerve(GET_NERVE(WaterBazooka, WaterBazookaNrvShot));
    }

    return false;
}

bool WaterBazooka::isTired() const {
    return isNerve(GET_NERVE(WaterBazooka, WaterBazookaNrvTire));
}

bool WaterBazooka::isPanic() const {
    return isNerve(GET_NERVE(WaterBazooka, WaterBazookaNrvPanic)) || isNerve(GET_NERVE(WaterBazooka, WaterBazookaNrvStorm));
}

bool WaterBazooka::isBazookaPinch() const {
    return mLife <= 1;
}

bool WaterBazooka::isBazookaLifeOut() const {
    return mLife <= 0;
}

void WaterBazooka::exeWaitForBattle() {
    if (MR::isFirstStep(this)) {
        MR::tryStartBck(this, "Wait");
    }

    mBaseMtx.setInline(MR::getJointMtx(this, "Cannon1"));

    bool start = !MR::isValidSwitchA(this) || MR::isOnSwitchA(this) || MR::isStageStateScenarioOpeningCamera();

    if (start) {
        setNerve(GET_NERVE(WaterBazooka, WaterBazookaNrvWait));
    }
}

void WaterBazooka::exeWait() {
    if (MR::isFirstStep(this) && isBazookaPinch() && !MR::isBrkPlaying(this, "PowerUp")) {
        MR::startBrk(this, "PowerUp");
    }

    if (mShooter->isLaughed()) {
        setNerve(GET_NERVE(WaterBazooka, WaterBazookaNrvWaitForLaugh));
        return;
    }

    if (MR::isGreaterStep(this, 0) && !MR::isPlayerHidden()) {
        setNerve(GET_NERVE(WaterBazooka, WaterBazookaNrvAim));
        return;
    }

    if (tryWaitForBattle()) {
    }
}

void WaterBazooka::exeAim() {
    if (MR::isFirstStep(this)) {
        MR::tryStartBck(this, "ShotStart");
    }

    MR::startLevelSound(this, "SE_EM_LV_WATERBAZ_TURN");

    if (mIsElectric && MR::isGreaterEqualStep(this, 10)) {
        MR::startLevelSound(this, "SE_EM_LV_ELECBAZ_PRE_SHOOT");
    }

    if (aimAtMario()) {
        setNerve(GET_NERVE(WaterBazooka, WaterBazookaNrvAimEnd));
        return;
    }

    if (MR::isGreaterEqualStep(this, NebulaFrameClock::stepsOfSeconds(1))) {
        setNerve(GET_NERVE(WaterBazooka, WaterBazookaNrvAimEnd));
    }
}

void WaterBazooka::exeAimEnd() {
    if (MR::isFirstStep(this)) {
        MR::startBck(this, "SwingStop");
        MR::startSound(this, "SE_EM_WATERBAZ_TURN_END");
    }

    if (mIsElectric) {
        MR::startLevelSound(this, "SE_EM_LV_ELECBAZ_PRE_SHOOT");
    }

    if (MR::isBckStopped(this)) {
        if (isBazookaPinch() && !mIsElectric) {
            setNerve(GET_NERVE(WaterBazooka, WaterBazookaNrvShotNoMotion));
        } else {
            setNerve(GET_NERVE(WaterBazooka, WaterBazookaNrvShot));
        }
    }
}

void WaterBazooka::exeShot() {
    if (MR::isFirstStep(this)) {
        MR::startBck(this, "Shot");
        mShotNum++;
    }

    if (mIsElectric) {
        MR::startLevelSound(this, "SE_EM_LV_ELECBAZ_PRE_SHOOT");
    }

    if (MR::isStep(this, NebulaFrameClock::stepsOfMilliseconds(500))) {
        MR::startSound(this, "SE_EM_WATERBAZ_SHOT");
        tryShotBullet();
    }

    if (MR::isBckStopped(this)) {
        if (mShotNum >= 3) {
            mShotNum = 0;
            setNerve(GET_NERVE(WaterBazooka, WaterBazookaNrvTire));
            return;
        }

        if (isBazookaPinch() && !mIsElectric) {
            setNerve(GET_NERVE(WaterBazooka, WaterBazookaNrvShotNoMotion));
        } else {
            setNerve(GET_NERVE(WaterBazooka, WaterBazookaNrvWaitForLaugh));
        }
    }
}

void WaterBazooka::exeShotNoMotion() {
    if (MR::isFirstStep(this)) {
        mShotNum++;
        MR::startBck(this, "ShortShot");
        MR::startSound(this, "SE_EM_WATERBAZ_SHOT");
        tryShotBullet();
    }

    aimAtMario();

    if (mShooter->isLaughed()) {
        setNerve(GET_NERVE(WaterBazooka, WaterBazookaNrvWaitForLaugh));
        return;
    }

    if (MR::isStep(this, NebulaFrameClock::stepsOfMilliseconds(500))) {
        if (mShotNum >= 5) {
            mShotNum = 0;
            setNerve(GET_NERVE(WaterBazooka, WaterBazookaNrvTire));
            return;
        }

        setNerve(GET_NERVE(WaterBazooka, WaterBazookaNrvShotNoMotion));
    }
}

void WaterBazooka::exeTire() {
    if (MR::isFirstStep(this)) {
        MR::startBck(this, "Tire");
    }

    if (mShooter->isLaughed()) {
        setNerve(GET_NERVE(WaterBazooka, WaterBazookaNrvWaitForLaugh));
        return;
    }

    if (MR::isStep(this, NebulaFrameClock::stepsOfSeconds(2))) {
        setNerve(GET_NERVE(WaterBazooka, WaterBazookaNrvWait));
    }
}

void WaterBazooka::exeDemoCrackCapsule() {
    if (MR::isFirstStep(this)) {
        tryJumpBackPlayerFromBazooka();
        MR::startBck(this, "1stDamage");
        mShooter->hitShock();
    }

    if (MR::isStep(this, 27)) {
        MR::startSound(this, "SE_EV_MOGUBAZ_ANGRY1");
    }

    if (!MR::isDemoActiveRegistered(this)) {
        Vec3f starPieceSpawnPos;
        Vec3f up;
        MR::calcUpVec(&up, this);
        starPieceSpawnPos.scaleAdd(600.0f, up, mPosition);
        MR::appearStarPiece(this, starPieceSpawnPos, 8, 25.0f, 40.0f, false);
        MR::startSound(this, "SE_OJ_STAR_PIECE_BURST");
        MR::validateCollisionParts(mCapsule);
        setNerve(GET_NERVE(WaterBazooka, WaterBazookaNrvWait));
    }
}

void WaterBazooka::exeDemoAnger() {
    if (MR::isFirstStep(this)) {
        tryJumpBackPlayerFromBazooka();
        MR::startBck(this, "2ndDamage");
        mShooter->anger();
        startBrk("PowerUp");
    }

    if (MR::isStep(this, 20)) {
        MR::startSound(this, "SE_EV_MOGUBAZ_ANGRY2");
    }

    if (mIsElectric) {
        if (MR::isFirstStep(this)) {
            MR::emitEffect(this, "AngrySign");
        } else if (MR::isStep(this, NebulaFrameClock::stepsOfSeconds(1))) {
            MR::deleteEffect(this, "AngrySign");
        } else if (MR::isStep(this, NebulaFrameClock::stepsOfMilliseconds(1500))) {
            MR::emitEffect(this, "Angry");
        }

        if (MR::isLessStep(this, NebulaFrameClock::stepsOfSeconds(1))) {
            MR::startLevelSound(this, "SE_EM_LV_ELECBAZ_PRE_ELEC_LEAK");
        } else if (MR::isGreaterEqualStep(this, NebulaFrameClock::stepsOfMilliseconds(1500))) {
            MR::startLevelSound(this, "SE_EM_LV_ELECBAZ_ELEC_LEAK");
        }
    }

    if (!MR::isDemoActiveRegistered(this)) {
        if (mIsElectric) {
            MR::deleteEffect(this, "Angry");
            mElectricTime = 0;
        }

        Vec3f starPieceSpawnPos;
        Vec3f up;
        MR::calcUpVec(&up, this);
        starPieceSpawnPos.scaleAdd(600.0f, up, mPosition);
        MR::appearStarPiece(this, starPieceSpawnPos, 16, 25.0f, 40.0f, false);
        MR::startSound(this, "SE_OJ_STAR_PIECE_BURST");
        MR::validateCollisionParts(mCapsule);
        setNerve(GET_NERVE(WaterBazooka, WaterBazookaNrvWait));
    }
}

void WaterBazooka::exeDemoBreakWait() {
    if (MR::isFirstStep(this)) {
        MR::offUpdateCollisionParts(this);
        if (MR::isPlayerInBind()) {
            MR::endBindAndPlayerJumpWithRollLanding(this, *MR::getPlayerVelocity(), 0);
        }
    }

    if (MR::isStep(this, 5)) {
        setNerve(GET_NERVE(WaterBazooka, WaterBazookaNrvDemoBreakSign));
    }
}

void WaterBazooka::exeDemoBreakSign() {
    if (MR::isFirstStep(this)) {
        MR::startBck(this, "Down");
        tryJumpBackPlayerFromBazooka();
        if (mIsElectric) {
            MR::deleteEffect(this, "Angry");
            MR::deleteEffect(this, "AngrySign");
        }
    }

    MR::tryRumblePadWeak(this, PLAYER_1);
    MR::startLevelSound(this, "SE_EM_LV_WATERBAZ_PRE_EXPLODE");
    if (MR::isGreaterEqualStep(this, NebulaFrameClock::stepsOfSeconds(2))) {
        MR::startLevelSound(this, "SE_EM_LV_WATERBAZ_BREAK_FLASH");
    }

    if (MR::isBckStopped(this)) {
        setNerve(GET_NERVE(WaterBazooka, WaterBazookaNrvDemoBreakExplosion));
    }
}

void WaterBazooka::exeDemoBreakExplosion() {
    if (MR::isFirstStep(this)) {
        MR::hideModel(this);
        MR::invalidateCollisionParts(this);
        MR::invalidateCollisionParts(mCannonCollisionParts);
        MR::invalidateHitSensors(this);
        MR::deleteEffect(this, "DamageSmokeS1");
        MR::deleteEffect(this, "DamageSmokeS2");
        MR::deleteEffect(this, "DamageSmokeL1");
        MR::deleteEffect(this, "DamageSmokeL2");
        mShooter->mIsShownWhileHostHidden = true;
        mShooter->explosion();
        mCapsule->kill();
        mBreakModel->makeActorAppeared();
        MR::invalidateClipping(mBreakModel);
        MR::startBck(mBreakModel, "Break");
        MR::startBrk(mBreakModel, "Break");
        MR::tryRumblePadStrong(this, PLAYER_1);
        MR::shakeCameraNormal();
        MR::startSound(this, "SE_EM_WATERBAZ_EXPLODE");
        MR::startSound(this, "SE_EV_MOGUBAZ_DEAD");
    }

    if (MR::isStep(this, 53)) {
        if (mHasPowerStar) {
            Vec3f spawnPos;
            MR::copyJointPos(this, "Cockpit", &spawnPos);
            MR::startAfterBossBGM();
            MR::requestAppearPowerStar(this, spawnPos);
        } else {
            MR::startSystemSE("SE_SY_READ_RIDDLE_S");
        }
    }

    if (MR::isBckStopped(mBreakModel)) {
        mBreakModel->kill();
        setNerve(GET_NERVE(WaterBazooka, WaterBazookaNrvDemoBreakEnd));
    }
}

void WaterBazooka::exeDemoBreakEnd() {
    if (MR::isStep(this, NebulaFrameClock::stepsOfSeconds(1))) {
        if (MR::isValidSwitchDead(this)) {
            MR::onSwitchDead(this);
        }

        kill();
    }
}

void WaterBazooka::exeWaitForLaugh() {
    if (mShooter->isLaughed()) {
        mShotNum = 0;
    } else {
        setNerve(GET_NERVE(WaterBazooka, WaterBazookaNrvWait));
    }
}

void WaterBazooka::exePanic() {
    if (MR::isFirstStep(this)) {
        MR::tryStartBck(this, "Wait");
        mShotNum = 0;
    }

    aimAtMario();

    bool playerOn = MR::isOnPlayer(getSensor("cannon")) || mCapsule->isPlayerOnCapsule();

    if (!playerOn && MR::isOnGroundPlayer()) {
        setNerve(GET_NERVE(WaterBazooka, WaterBazookaNrvWait));
        return;
    }

    if (MR::isStep(this, NebulaFrameClock::stepsOfSeconds(3))) {
        mShooter->stormStart();
        startBrk("Spin");
        MR::startSound(this, "SE_EV_MOGUBAZ_PRE_STORM");
    }

    if (MR::isGreaterEqualStep(this, NebulaFrameClock::stepsOfSeconds(3))) {
        MR::startLevelSound(this, "SE_EM_LV_WATERBAZ_PRE_STORM");
    }

    if (MR::isStep(this, NebulaFrameClock::stepsOfSeconds(5))) {
        setNerve(GET_NERVE(WaterBazooka, WaterBazookaNrvStorm));
    }
}

void WaterBazooka::exeStorm() {
    if (MR::isFirstStep(this)) {
        mShooter->storm();
        MR::emitEffect(this, "Spin");
    }

    Vec3f pos;
    Vec3f side;
    mBaseMtx.getTrans(pos);
    mBaseMtx.getXDir(side);

    f32 deg = MR::calcNerveEaseOutValue(this, NebulaFrameClock::stepsOfMilliseconds(2500), 25.0f, 0.0f);
    MR::rotateVecDegree(&side, mGravity, deg);
    MR::makeMtxSideUpPos(&mBaseMtx, side, -mGravity, pos);

    MR::startLevelSound(this, "SE_EM_LV_WATERBAZ_STORM", (deg * 100.0f) / 25.0f);

    if (MR::isStep(this, NebulaFrameClock::stepsOfMilliseconds(1500))) {
        MR::deleteEffect(this, "Spin");
    }

    if (MR::isStep(this, NebulaFrameClock::stepsOfMilliseconds(2500))) {
        setNerve(GET_NERVE(WaterBazooka, WaterBazookaNrvWait));
    }
}

void WaterBazooka::endStorm() {
    if (MR::isEffectValid(this, "Spin")) {
        MR::deleteEffect(this, "Spin");
    }
}

void WaterBazooka::makeActorDead() {
    mShooter->makeActorDead();
    mCapsule->makeActorDead();
    mBreakModel->makeActorDead();
    mDemoActor->makeActorDead();
    mCannonCollisionParts->mIsValid = false;
    LiveActor::makeActorDead();
}

void WaterBazooka::kill() {
    mDemoActor->kill();
    if (mAlreadyDoneFlag >= 0) {
        MR::updateAlreadyDoneFlag(mAlreadyDoneFlag, 1);
    }

    LiveActor::kill();
}

void WaterBazooka::control() {
    setCameraTargetMtx();
    switchShowOrHide();

    if (!MR::isStageStateScenarioOpeningCamera()) {
        if (MR::isEqualStageName("OceanPhantomCaveGalaxy")) {
            if (MR::isExecScenarioStarter()) {
                MR::playLevelMarioPinchBGM(mIsPinchBGMStarted);
                mIsPinchBGMStarted = true;
            } else {
                if (MR::isNearPlayer(this, 5000.0f)) {
                    MR::playLevelMarioPinchBGM(mIsPinchBGMStarted);
                    mIsPinchBGMStarted = true;
                }
            }
        }
    }

    updateElectricLeak();
    if (getSmokeLevel() != -1) {
        MR::startLevelSound(this, "SE_EM_LV_WATERBAZ_SMOKE");
    }

    tryPanic();
}

void WaterBazooka::calcAndSetBaseMtx() {
    LiveActor::calcAndSetBaseMtx();
    mJointCtrl->registerCallBack();
}

void WaterBazooka::startDemoCrackCapsule() {
    MR::requestStartDemoRegisteredMarioPuppetable(this, nullptr, nullptr, "カプセル破壊");
    mCapsule->crackCapsule();
    MR::invalidateCollisionParts(mCapsule);
    setNerve(GET_NERVE(WaterBazooka, WaterBazookaNrvDemoCrackCapsule));
}

void WaterBazooka::startDemoAnger() {
    if (MR::isDemoPartExist(this, "乗組員怒り")) {
        MR::requestStartDemoRegisteredMarioPuppetable(this, nullptr, nullptr, "乗組員怒り");
    }

    mCapsule->crackCapsule();
    MR::invalidateCollisionParts(mCapsule);
    setNerve(GET_NERVE(WaterBazooka, WaterBazookaNrvDemoAnger));
}

void WaterBazooka::startDemoBreakCapsule() {
    MR::requestStartDemoRegisteredMarioPuppetable(this, nullptr, nullptr, "完全破壊");
    mShooter->panicDeath();
    mCapsule->breakCapsule();
    MR::invalidateCollisionParts(mCapsule);
    setNerve(GET_NERVE(WaterBazooka, WaterBazookaNrvDemoBreakWait));
}

bool WaterBazooka::aimAtMario() {
    if (MR::isStageStateScenarioOpeningCamera()) {
        return true;
    }

    Vec3f side;
    Vec3f cannonPos;
    mBaseMtx.getXDir(side);
    mBaseMtx.getTrans(cannonPos);

    Vec3f aimPos;
    aimPos.scaleAdd(100.0f, -mGravity, *MR::getPlayerPos());

    Vec3f aim;
    aim.sub(aimPos, cannonPos);
    MR::normalize(&aim);
    const f32 turnCos = MR::cosDegree(1.2f);
    const Vec3f& rGravity = mGravity;
    MR::turnVecToVecCos(&side, side, aim, turnCos, rGravity);

    Vec3f sideOnPlane;
    MR::turnVecToPlane(&sideOnPlane, side, -mGravity);
    MR::clampVecAngleDeg(&side, sideOnPlane, 15.0f);

    if (side.dot(-mGravity) > 0.0f) {
        MR::turnVecToPlane(&side, side, -mGravity);
    }

    MR::makeMtxSideUpPos(&mBaseMtx, side, -mGravity, cannonPos);

    Vec3f side2;
    mBaseMtx.getXDir(side2);
    return side2.angle(aim) * _180_PI <= 2.0f;
}

void WaterBazooka::switchShowOrHide() {
    if (isNerve(GET_NERVE(WaterBazooka, WaterBazookaNrvDemoBreakExplosion)) || isNerve(GET_NERVE(WaterBazooka, WaterBazookaNrvDemoBreakEnd))) {
        MR::hideModel(this);
        return;
    }

    if (MR::isJudgedToClipFrustum(mPosition, 800.0f)) {
        if (!MR::isHiddenModel(this)) {
            MR::hideModel(this);
        }
    } else {
        if (MR::isHiddenModel(this)) {
            MR::showModel(this);
        }
    }
}

void WaterBazooka::updateElectricLeak() {
    if (mIsElectric && mElectricTime >= 0 && !isNrvDemo() && isBazookaPinch()) {
        if (isElectricLeakSign() && !MR::isEffectValid(this, "AngrySign")) {
            MR::emitEffect(this, "AngrySign");
            MR::deleteEffect(this, "Angry");
        } else if (isElectricLeak() && !MR::isEffectValid(this, "Angry")) {
            MR::emitEffect(this, "Angry");
            MR::deleteEffect(this, "AngrySign");
        }

        if (isElectricLeakSign()) {
            MR::startLevelSound(this, "SE_EM_LV_ELECBAZ_PRE_ELEC_LEAK");
        } else if (isElectricLeak()) {
            MR::startLevelSound(this, "SE_EM_LV_ELECBAZ_ELEC_LEAK");
        }

        if (mElectricTime < NebulaFrameClock::stepsOfSeconds(10)) {
            mElectricTime++;
        } else {
            mElectricTime = 0;
            MR::deleteEffect(this, "Angry");
        }
    }
}

void WaterBazooka::damageBazooka(HitSensor* pSender, HitSensor* pReceiver) {
    startBrk("Damage");
    MR::emitEffectHitBetweenSensors(this, pSender, pReceiver, 0.0f, nullptr);

    if (mLife == 3) {
        startDemoCrackCapsule();
        MR::emitEffect(this, "DamageSmokeS1");
    } else if (mLife == 2) {
        startDemoAnger();
        MR::emitEffect(this, "DamageSmokeS2");
        MR::emitEffect(this, "DamageSmokeL1");
    } else if (mLife == 1) {
        startDemoBreakCapsule();
        MR::emitEffect(this, "DamageSmokeL2");
    }

    mLife--;
}

bool WaterBazooka::calcJointCannon(Pos3f* pMtx, const JointControllerInfo& /*rJointInfo*/) {
    pMtx->setInline(mBaseMtx);
    return true;
}

s32 WaterBazooka::getSmokeLevel() const {
    if (mLife == 4) {
        return 0;
    }

    if (mLife == 3) {
        return 1;
    }

    if (mLife == 2) {
        return 2;
    }

    if (mLife == 1) {
        return 3;
    }

    return -1;
}

bool WaterBazooka::tryWaitForBattle() {
    if (MR::isStageStateScenarioOpeningCamera()) {
        return false;
    }

    if (!MR::isValidSwitchA(this)) {
        return false;
    }

    if (MR::isValidSwitchA(this) && MR::isOnSwitchA(this)) {
        return false;
    }

    setNerve(GET_NERVE(WaterBazooka, WaterBazookaNrvWaitForBattle));
    return true;
}

bool WaterBazooka::tryPanic() {
    if (isNrvDemo()) {
        return false;
    }

    if (isNerve(GET_NERVE(WaterBazooka, WaterBazookaNrvPanic)) || isNerve(GET_NERVE(WaterBazooka, WaterBazookaNrvStorm))) {
        return false;
    }

    bool playerOn = MR::isOnPlayer(getSensor("cannon")) || mCapsule->isPlayerOnCapsule();

    if (!playerOn) {
        return false;
    }

    setNerve(GET_NERVE(WaterBazooka, WaterBazookaNrvPanic));
    return true;
}

bool WaterBazooka::isNrvDemo() const {
    return isNerve(GET_NERVE(WaterBazooka, WaterBazookaNrvDemoCrackCapsule)) || isNerve(GET_NERVE(WaterBazooka, WaterBazookaNrvDemoAnger)) ||
           isNerve(GET_NERVE(WaterBazooka, WaterBazookaNrvDemoBreakWait)) || isNerve(GET_NERVE(WaterBazooka, WaterBazookaNrvDemoBreakSign)) ||
           isNerve(GET_NERVE(WaterBazooka, WaterBazookaNrvDemoBreakExplosion)) || isNerve(GET_NERVE(WaterBazooka, WaterBazookaNrvDemoBreakEnd));
}

bool WaterBazooka::isElectricLeakSign() const {
    if (!mIsElectric) {
        return false;
    }

    return mElectricTime >= NebulaFrameClock::stepsOfSeconds(5) && mElectricTime < NebulaFrameClock::stepsOfSeconds(7);
}

bool WaterBazooka::isElectricLeak() const {
    if (!mIsElectric) {
        return false;
    }

    return mElectricTime >= NebulaFrameClock::stepsOfSeconds(7) && mElectricTime < NebulaFrameClock::stepsOfSeconds(10);
}

void WaterBazooka::initShooter() {
    mShooter = new MogucchiShooter(this, "ウォーターバズーカ乗組員モグッチ");
    mShooter->initFixedPosition("Cockpit");
    mShooter->initWithoutIter();
}

void WaterBazooka::initBazookaCapsule() {
    mCapsule = new WaterBazookaCapsule(this, "ウォーターバズーカのカプセル");
    mCapsule->initFixedPosition("Top");
    mCapsule->initWithoutIter();
}

void WaterBazooka::initBreakModel() {
    if (mIsElectric) {
        mBreakModel = MR::createModelObjEnemy("エレクトリックバズーカ壊れモデル", "ElectricBazookaBreak", getBaseMtx());
    } else {
        mBreakModel = MR::createModelObjEnemy("ウォーターバズーカ壊れモデル", "WaterBazookaBreak", getBaseMtx());
    }

    mBreakModel->initWithoutIter();
    MR::invalidateClipping(mBreakModel);
    mBreakModel->makeActorDead();
}

void WaterBazooka::initCameraTarget() {
    mDemoActor = new CameraTargetDemoActor(MR::getJointMtx(this, "Cockpit"), "バズーカカメラターゲット");
    mDemoActor->initWithoutIter();
    setCameraTargetMtx();
}

void WaterBazooka::initBullet(const TableIter& rIter) {
    if (mIsElectric) {
        mBullets = new ElectricPressureBullet*[::sElectricBulletNum];

        for (s32 idx = 0; idx < ::sElectricBulletNum; idx++) {
            mBullets[idx] = new ElectricPressureBullet("エレクトリックバズーカの弾");
            mBullets[idx]->initWithoutIter();
            MR::tryRegisterDemoCast(mBullets[idx], rIter);
        }
    } else {
        MR::createSceneObj(SceneObj_WaterPressureBulletHolder);
    }
}

bool WaterBazooka::tryShotBullet() {
    LiveActor* bullet;
    if (mIsElectric) {
        bullet = selectBulletElectric();
    } else {
        bullet = MR::getSceneObj< WaterPressureBulletHolder >(SceneObj_WaterPressureBulletHolder)->callEmptyBullet();
    }

    if (bullet == nullptr) {
        return false;
    }

    Pos3f posMtx;
    calcGunPointFromCannon(&posMtx);
    if (mIsElectric) {
        static_cast< ElectricPressureBullet* >(bullet)->shotElectricBullet(this, posMtx, ::sElectricBulletSpeed);
    } else {
        static_cast< WaterPressureBullet* >(bullet)->shotWaterBullet(
            this, posMtx, 40.0f, true, true, ::cShotInvalidSpinKill, &mCameraInfo);
    }

    return true;
}

ElectricPressureBullet* WaterBazooka::selectBulletElectric() {
    for (s32 idx = 0; idx < ::sElectricBulletNum; idx++) {
        if (MR::isDead(mBullets[idx])) {
            return mBullets[idx];
        }
    }

    return nullptr;
}

bool WaterBazooka::tryJumpBackPlayerFromBazooka() const {
    bool playerOn = MR::isOnPlayer(getSensor("cannon")) || mCapsule->isPlayerOnCapsule();

    if (!playerOn && !mCapsule->isPlayerOnCapsule()) {
        return false;
    }

    Vec3f dropPoint;
    calcNearDropPoint(&dropPoint);
    Vec3f jumpDir;
    jumpDir.sub(dropPoint, *MR::getPlayerPos());
    Vec3f rej;
    rej.killElement(jumpDir, mGravity);
    Vec3f rej2 = -rej;

    MR::offBind(MR::getPlayerDemoActor());
    MR::unlockPlayerAnimation();
    MR::forceFlyPlayer(dropPoint, rej2, MR::calcDistanceToPlayer(dropPoint) / 20.0f);
    return true;
}

void WaterBazooka::calcNearDropPoint(Vec3f* pPos) const {
    f32 nearestDistance = FLOAT_MAX;
    Vec3f nearestPoint;
    for (u32 idx = 0; idx < ARRAY_SIZEU(::sDropPointStringTable); idx++) {
        Vec3f pos;
        if (MR::tryFindNamePos(::sDropPointStringTable[idx], &pos, nullptr) && MR::isNearPlayer(pos, nearestDistance)) {
            nearestPoint.set(pos);
            nearestDistance = MR::calcDistanceToPlayer(nearestPoint);
        }
    }

    if (nearestDistance != FLOAT_MAX) {
        pPos->set(nearestPoint);
        return;
    }

    Vec3f toPlayer;
    toPlayer.sub(*MR::getPlayerPos(), mPosition);
    toPlayer.orthogonalize(mGravity);
    MR::normalizeOrZero(&toPlayer);

    if (MR::isNearZero(toPlayer)) {
        MR::getPlayerFrontVec(&toPlayer);
        toPlayer.negate();
    }

    pPos->scaleAdd(800.0f, toPlayer, mPosition);
}

void WaterBazooka::calcGunPointFromCannon(Pos3f* pMtx) {
    Pos3f mtx(MR::getJointMtx(this, "Cannon1"));
    Vec3f side;
    mtx.getXDir(side);
    Vec3f pos;
    mtx.getTrans(pos);
    pos.scaleAdd(550.0f, side, pos);
    pMtx->setInline(mtx);
    Vec3f up;
    mtx.getYDir(up);
    Vec3f front;
    mtx.getZDir(front);
    MR::makeMtxFrontUpPos(pMtx, side, up, pos);
}

void WaterBazooka::setCameraTargetMtx() {
    Pos3f mtx(mShooter->getBaseMtx());

    mtx.setTrans(mPosition);
    Vec3f up;
    MR::calcUpVec(&up, this);
    Vec3f front;
    mtx.getZDir(front);
    MR::makeMtxUpFrontPos(&mtx, up, front, mPosition);
    mDemoActor->setTargetMtx(mtx);
}
