#include "Game/Enemy/Unizo.hpp"
#include "Game/System/NebulaFrameClock.hpp"
#include "Game/Enemy/AnimScaleController.hpp"
#include "Game/Enemy/WalkerStateBindStarPointer.hpp"
#include "Game/LiveActor/Binder.hpp"
#include "Game/LiveActor/HitSensor.hpp"
#include "Game/LiveActor/ModelManager.hpp"
#include "Game/LiveActor/ModelObj.hpp"
#include "Game/LiveActor/Nerve.hpp"
#include "Game/Map/WaterInfo.hpp"
#include "Game/Scene/SceneFunction.hpp"
#include "Game/Util/ActorMovementUtil.hpp"
#include "Game/Util/ActorSensorUtil.hpp"
#include "Game/Util/ActorShadowUtil.hpp"
#include "Game/Util/ActorStateUtil.hpp"
#include "Game/Util/ActorSwitchUtil.hpp"
#include "Game/Util/AreaObjUtil.hpp"
#include "Game/Util/CameraUtil.hpp"
#include "Game/Util/EffectUtil.hpp"
#include "Game/Util/LiveActorUtil.hpp"
#include "Game/Util/MapUtil.hpp"
#include "Game/Util/MathUtil.hpp"
#include "Game/Util/MtxUtil.hpp"
#include "Game/Util/ObjUtil.hpp"
#include "Game/Util/PlayerUtil.hpp"
#include "Game/Util/SceneUtil.hpp"
#include "Game/Util/SoundUtil.hpp"
#include "Game/Util/StarPointerUtil.hpp"
#include "Game/Util/StringUtil.hpp"
#include "Game/Util/VectorUtil.hpp"
#include "Game/Util/Tunable.hpp"

NEBULA_TUNABLE_NAMED(f32, cNum_setQuatXYZ_f1, "setQuatXYZ.f1", 0.5f);
NEBULA_TUNABLE_NAMED(f32, cNum_setQuatXYZ_f2, "setQuatXYZ.f2", 0.5f);
NEBULA_TUNABLE_NAMED(f32, cNum_setQuatXYZ_f3, "setQuatXYZ.f3", 0.5f);
NEBULA_TUNABLE_NAMED(f32, cNum_setQuatXYZ_f4, "setQuatXYZ.f4", 0.5f);
NEBULA_TUNABLE_NAMED(f32, cNum_setQuatXYZ_f5, "setQuatXYZ.f5", 0.5f);
NEBULA_TUNABLE_NAMED(f32, cNum_setQuatXYZ_f6, "setQuatXYZ.f6", 0.5f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeFireDown_f1, "exeFireDown.f1", 10.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeFireDown_f2, "exeFireDown.f2", 40.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_updateVelocity_f1, "updateVelocity.f1", 180.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_updateVelocity_f2, "updateVelocity.f2", 2.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_updateInfluence_f1, "updateInfluence.f1", 100.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_updateInfluence_f2, "updateInfluence.f2", 100.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_updateInfluence_f3, "updateInfluence.f3", 100.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_updateInfluence_f4, "updateInfluence.f4", 100.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_doJump_f1, "doJump.f1", 2.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_doFireDown_f1, "doFireDown.f1", 10.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_doFireDown_f2, "doFireDown.f2", 40.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_doBreak_f1, "doBreak.f1", 10.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_doBreak_f2, "doBreak.f2", 40.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_startWallHitSound_f1, "startWallHitSound.f1", 100.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_startWallHitSound_f2, "startWallHitSound.f2", 100.0f);

// The numbers this file's functions give the game's motion and animation calls, as tunables: "<file>.<function>.<call>.<k>".
NEBULA_TUNABLE_NAMED(f32, cNum_updateInfluence_reboundVelocityFromCollision_1, "updateInfluence.reboundVelocityFromCollision.1", 0.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_updateInfluence_reboundVelocityFromCollision_2, "updateInfluence.reboundVelocityFromCollision.2", 1.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_updateInfluence_reboundVelocityFromCollision_3, "updateInfluence.reboundVelocityFromCollision.3", 0.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_updateInfluence_reboundVelocityFromCollision_4, "updateInfluence.reboundVelocityFromCollision.4", 0.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_updateInfluence_reboundVelocityFromCollision_5, "updateInfluence.reboundVelocityFromCollision.5", 1.0f);

namespace {
    // rQuat = Z * X * Y of the half-angle axis turns in rRot (radians).
    inline void setQuatXYZ(Quat4f& rQuat, const Vec3f& rRot) {
        Quat4f quatX(sin(rRot.x * cNum_setQuatXYZ_f1), 0.0f, 0.0f, cos(rRot.x * cNum_setQuatXYZ_f2));
        Quat4f quatY(0.0f, sin(rRot.y * cNum_setQuatXYZ_f3), 0.0f, cos(rRot.y * cNum_setQuatXYZ_f4));
        Quat4f quatZ(0.0f, 0.0f, sin(rRot.z * cNum_setQuatXYZ_f5), cos(rRot.z * cNum_setQuatXYZ_f6));

        rQuat.mult(quatZ, quatX);
        rQuat.mult(rQuat, quatY);
    }

    NEBULA_TUNABLE(f32, sChaseRadius, 1500.0f);
    NEBULA_TUNABLE(f32, sChaseDegree, 90.0f);
    NEBULA_TUNABLE(f32, sChaseCycle, 20.0f);
    NEBULA_TUNABLE(f32, sRollHeightSea, 30.0f);
    NEBULA_TUNABLE(f32, sRollCycle, 60.0f);
    NEBULA_TUNABLE(f32, sAccelerate, 0.1f);
    NEBULA_TUNABLE(f32, sChaseSpeedMax, 4.0f);
    NEBULA_TUNABLE(f32, sSpinSpeedMax, 6.0f);
    NEBULA_TUNABLE(f32, sTotalSpeedMax, 12.0f);
    NEBULA_TUNABLE(f32, sBrakeRate, 0.9f);
    NEBULA_TUNABLE(f32, sBrakeRateChase, 0.96f);
    NEBULA_TUNABLE(f32, sGravityRateSea, 0.15f);
    NEBULA_TUNABLE(f32, sGravityRateLand, 0.8f);
    NEBULA_TUNABLE(f32, sGravityRateGround0, 0.2f);
    NEBULA_TUNABLE(f32, sGravityRateGround1, 0.8f);
    NEBULA_TUNABLE(f32, sDriftSpeedRate, 0.02f);
    NEBULA_TUNABLE(f32, sStopSpeed, 0.5f);
    NEBULA_TUNABLE(f32, sReboundFloor, 0.6f);
    NEBULA_TUNABLE(f32, sReboundEnemy, 0.2f);
    NEBULA_TUNABLE(f32, sReboundPlayerV, 5.0f);
    NEBULA_TUNABLE(f32, sReboundPlayerH, 1.0f);
    NEBULA_TUNABLE(f32, sRadiusScale, 1.0f);
    NEBULA_TUNABLE(f32, sJumpHeight, 70.0f);
    NEBULA_TUNABLE(s32, sCollidePlayerCount, 120);
    NEBULA_TUNABLE(s32, sCollideEnemyCount, 60);
    NEBULA_TUNABLE(s32, sSpinAnimationCount, 50);
    NEBULA_TUNABLE(s32, sRollSoundTime, 20);
    NEBULA_TUNABLE(f32, sRollSoundSpeed, 2.0f);
    NEBULA_TUNABLE(f32, sLandSoundLandSpeed, 2.0f);
    NEBULA_TUNABLE(f32, sWallHitSoundSpeed, 3.0f);
    const s32 sUnizoCollisionSePlayTime = NebulaFrameClock::stepsOfMilliseconds(500);
    NEBULA_TUNABLE(f32, sSurfaceEffectMinLimit, 40.0f);
    NEBULA_TUNABLE(f32, sSurfaceEffectMaxLimit, 200.0f);
    NEBULA_TUNABLE(f32, sSurfaceEffectMaxRunLimit, 280.0f);
    NEBULA_TUNABLE(f32, sSurfaceEffectRunSpeed, 5.0f);
    NEBULA_TUNABLE(f32, sFireDownVertical, 22.0f);
    NEBULA_TUNABLE(f32, sFireDownHorizontal, 6.0f);
    NEBULA_TUNABLE(f32, sFireDownAttenuate, 1.0f);
    NEBULA_TUNABLE(f32, sFireDownGravity, 1.1f);
    NEBULA_TUNABLE(s32, sFireDownStarPiece, 34);
    NEBULA_TUNABLE(f32, sDamageFloorVertical, 30.0f);
    NEBULA_TUNABLE(s32, sDamageFloorFrame, 50);
    NEBULA_TUNABLE(s32, sJumpNow, 1);
    NEBULA_TUNABLE(s32, sStarPieceNum, 6);
    NEBULA_TUNABLE(f32, sRollHeightLand, 0.0f);
    NEBULA_TUNABLE(s32, sStartSearch, 0);
    NEBULA_TUNABLE(f32, sSurfaceEffectMinRunLimit, 0.0f);
    NEBULA_TUNABLE(f32, sBodyRadius, 126.36f);  // binder and roll radius; the body sensor sits this high
    NEBULA_TUNABLE(f32, sSensorRadius, 115.2f);
    NEBULA_TUNABLE(f32, sShadowRadius, 106.36f);
    NEBULA_TUNABLE(s32, sBlinkCycle, 200);

    inline f32 toDeg(f32 x) {
        return _180_PI * x;
    }
};  // namespace

namespace NrvUnizo {
    NEW_NERVE(UnizoNrvWait, Unizo, Wait);
    NEW_NERVE(UnizoNrvJump, Unizo, Jump);
    NEW_NERVE(UnizoNrvChase, Unizo, Chase);
    NEW_NERVE(UnizoNrvAttack, Unizo, Attack);
    NEW_NERVE(UnizoNrvCollidePlayer, Unizo, CollidePlayer);
    NEW_NERVE(UnizoNrvCollideEnemy, Unizo, CollideEnemy);
    NEW_NERVE(UnizoNrvBreak, Unizo, Break);
    NEW_NERVE(UnizoNrvFireDown, Unizo, FireDown);
    NEW_NERVE(UnizoNrvJumpDown, Unizo, JumpDown);
    NEW_NERVE_ONEND(UnizoNrvPointing, Unizo, Pointing, Pointing);
};  // namespace NrvUnizo

Unizo::Unizo(const char* pName)
    : LiveActor(pName), mType(TypeSea), mFrame(0), mBlinkCount(0), mBlinkFrame(0), mSpinAnimTimer(0), mRadiusScale(sRadiusScale), mBreakModel(nullptr),
      mAnimScaleController(nullptr), mBindStarPointerState(nullptr), mJumpCount(0), mGravityRate(sGravityRateSea), mRollHeight(sRollHeightSea),
      mRollSoundTimer(0) {
    mBaseMtx.identity();
    mRippleMtx.identity();
    mQuat.set(0.0f, 0.0f, 0.0f, 1.0f);
}

void Unizo::initType() {
    if (MR::isEqualStringCase(mName, "ウニゾー")) {
        mType = TypeSea;
    } else if (MR::isEqualStringCase(mName, "陸ウニゾー")) {
        mType = TypeLand;
    } else if (MR::isEqualStringCase(mName, "浅瀬ウニゾー")) {
        mType = TypeShoal;
    }
}

void Unizo::init(const TableIter& rIter) {
    initType();

    if (mType == TypeSea) {
        initModelManagerWithAnm("Unizo", nullptr, false);
        mBreakModel = new ModelObj("ウニゾー壊れモデル", "UnizoBreak", nullptr, MR::DrawBufferType_Enemy, -2, -2, false);
        mBreakModel->initWithoutIter();
        mBreakModel->makeActorDead();
    } else if (mType == TypeLand) {
        mRollHeight = sRollHeightLand;
        mGravityRate = sGravityRateLand;
        initModelManagerWithAnm("UnizoLand", nullptr, false);
        mBreakModel = new ModelObj("陸ウニゾー壊れモデル", "UnizoLandBreak", nullptr, MR::DrawBufferType_Enemy, -2, -2, false);
        mBreakModel->initWithoutIter();
        mBreakModel->makeActorDead();
        MR::initFur(this);
    } else if (mType == TypeShoal) {
        initModelManagerWithAnm("UnizoShoal", nullptr, false);
        mBreakModel = new ModelObj("浅瀬ウニゾー壊れモデル", "UnizoShoalBreak", nullptr, MR::DrawBufferType_Enemy, -2, -2, false);
        mBreakModel->initWithoutIter();
        mBreakModel->makeActorDead();
    }

    MR::initDefaultPos(this, rIter);
    MR::makeMtxTR(mBaseMtx, this);
    mRadiusScale = sRadiusScale;
    mScale.set< f32 >(sRadiusScale, sRadiusScale, sRadiusScale);
    MR::connectToSceneEnemy(this);
    MR::initLightCtrl(this);
    initHitSensor(1);
    MR::addHitSensor(this, "Body", ATYPE_UNIZO, 8, sSensorRadius * mRadiusScale, Vec3f(0.0f, sBodyRadius * mRadiusScale, 0.0f));
    MR::initStarPointerTarget(this, sSensorRadius * mRadiusScale, Vec3f(0.0f, sBodyRadius * mRadiusScale, 0.0f));
    initBinder(sBodyRadius * mRadiusScale, sBodyRadius * mRadiusScale, 8);
    MR::initShadowVolumeSphere(this, sShadowRadius * mRadiusScale);
    MR::initShadowVolumeSphere(mBreakModel, sShadowRadius * mRadiusScale);
    initEffectKeeper(4, nullptr);
    if (mType == TypeShoal) {
        MR::setEffectHostMtx(this, "Ripple", mRippleMtx);
    }

    initSound(4, false);
    initNerve(GET_NERVE(Unizo, UnizoNrvWait));
    MR::onCalcGravity(this);
    MR::startBtp(this, "Blink");
    mBlinkFrame = MR::getRandom(100, sBlinkCycle);
    MR::addToAttributeGroupSearchTurtle(this);
    MR::declareStarPiece(this, sStarPieceNum);
    mAnimScaleController = new AnimScaleController(nullptr);
    mAnimScaleController->setParamTight();
    mBindStarPointerState = new WalkerStateBindStarPointer(this, mAnimScaleController);
    MR::setGroupClipping(this, rIter, 16);
    makeActorAppeared();
    MR::useStageSwitchSleep(this, rIter);
}

void Unizo::initAfterPlacement() {
    MR::trySetMoveLimitCollision(this);
}

void Unizo::control() {
    mFrame++;
    if (mSpinAnimTimer > 0) {
        mSpinAnimTimer--;
    }

    if (!MR::isNearZero(mVelocity)) {
        updateRotate();
    }

    MR::turnMtxToYDirDegree(&mBaseMtx, -mGravity, 1.0f);
    mBaseMtx.setTrans(mPosition);
    mAnimScaleController->update();
    updateSurfaceEffect();
    tryPointBind();
}

void Unizo::appear() {
    setNerve(GET_NERVE(Unizo, UnizoNrvWait));
    LiveActor::appear();
    mJumpCount = sJumpNow;
    MR::validateShadowAll(this);
    MR::validateShadowAll(mBreakModel);
}

// The placement rotation becomes the roll quaternion; mRotation is not used after this.
void Unizo::makeActorAppeared() {
    MR::showModel(this);
    mBaseMtx.setTrans(mPosition);
    LiveActor::makeActorAppeared();
    setQuatXYZ(mQuat, mRotation);
    mQuat.normalize();
    mRotation.zero();
    mModelManager->calcView();
    mModelManager->calcView();
}

void Unizo::kill() {
    makeActorDead();
}

void Unizo::makeActorDead() {
    MR::validateHitSensors(this);
    MR::validateClipping(this);
    MR::validateClipping(mBreakModel);
    mBreakModel->makeActorDead();
    LiveActor::makeActorDead();
}

void Unizo::startClipped() {
    LiveActor::startClipped();
    deleteEffect();
}

// The ball rolls by mQuat about its center, lifted and squashed by the scale controller, bobbing while it moves.
void Unizo::calcAndSetBaseMtx() {
    Pos3f mtx;
    mtx.identity();
    mtx.setQuat(mQuat);
    Vec3f position(mPosition);
    Vec3f quatUp;
    mtx.getYDir(quatUp);
    position.scaleAdd(-(sBodyRadius * mRadiusScale), quatUp, position);
    Vec3f baseUp;
    mBaseMtx.getYDir(baseUp);
    position.scaleAdd(sBodyRadius * mRadiusScale * mAnimScaleController->mScale.y, baseUp, position);

    f32 rollHeight = mRadiusScale * (mRollHeight * MR::abs(MR::sin(mFrame / sRollCycle)));
    Vec3f horizontalVelocity = MR::getVelocityHorizon(this);
    f32 rate = horizontalVelocity.length() / sChaseSpeedMax;
    rollHeight *= rate >= 1.0f ? 1.0f : rate;
    position.scaleAdd(rollHeight, mGravity, position);
    MR::scaleMtxToLocalMtx(mtx, mBaseMtx, mAnimScaleController->mScale);
    mtx.setTrans(position);
    MR::setBaseTRMtx(this, mtx);
}

void Unizo::exeWait() {
    if (MR::isFirstStep(this)) {
        MR::startBtp(this, "Normal");
    }

    updateBlink();
    updateInfluence();
    if (mJumpCount == 0 && MR::isNearPlayer(this, sChaseRadius) && !MR::isPlayerDamaging() && MR::isBindedGround(this)) {
        doJump();
        setNerve(GET_NERVE(Unizo, UnizoNrvJump));
    }
}

void Unizo::exeJump() {
    if (MR::isStep(this, sStartSearch)) {
        MR::startBck(this, "Search");
        MR::startBtp(this, "Angry");
        MR::startSound(this, "SE_EM_UNIZO_NEEDLE");
    }

    if (MR::isGreaterEqualStep(this, sChaseCycle)) {
        updateVelocity();
    }
    updateInfluence();
    if (MR::isBckStopped(this)) {
        setNerve(GET_NERVE(Unizo, UnizoNrvChase));
    }
}

void Unizo::exeChase() {
    updateVelocity();
    updateInfluence();
    if (mType == TypeLand) {
        MR::emitEffect(this, "SearchSmoke");
    }

    if (!MR::isNearPlayer(this, sChaseRadius)) {
        deleteEffect();
        setNerve(GET_NERVE(Unizo, UnizoNrvWait));
    }
}

void Unizo::exeAttack() {
    updateInfluence();
    if (MR::isFirstStep(this)) {
        MR::startBck(this, "Attack");
        MR::startSound(this, "SE_EM_UNIZO_NEEDLE");
        MR::startSound(this, "SE_EM_UNIZO_STICK");
    }

    if (MR::isBckStopped(this)) {
        setNerve(GET_NERVE(Unizo, UnizoNrvCollidePlayer));
    }
}

void Unizo::exeCollidePlayer() {
    updateInfluence();
    if (MR::isGreaterStep(this, sCollidePlayerCount)) {
        setNerve(GET_NERVE(Unizo, UnizoNrvChase));
    }
}

void Unizo::exeCollideEnemy() {
    if (MR::isFirstStep(this)) {
        mBlinkCount = MR::isBtpStopped(this) ? 2 : 0;
    }

    updateInfluence();
    if (MR::isBtpStopped(this) && mBlinkCount > 0) {
        mBlinkCount--;
        MR::startBtp(this, "Blink");
    }

    if (MR::isGreaterStep(this, sCollideEnemyCount)) {
        MR::startBtp(this, "Angry");
        setNerve(GET_NERVE(Unizo, UnizoNrvChase));
    }
}

void Unizo::exeBreak() {
    if (isBreakModelEnd()) {
        kill();
    }
}

void Unizo::exeFireDown() {
    fallBroken();

    if (MR::isStep(this, sFireDownStarPiece)) {
        if (MR::appearStarPiece(this, mBreakModel->mPosition, sStarPieceNum, cNum_exeFireDown_f1, cNum_exeFireDown_f2, true)) {
            MR::startSound(this, "SE_OJ_STAR_PIECE_BURST");
        }
    }

    if (isBreakModelEnd()) {
        kill();
    }
    if (MR::isGreaterEqualStep(this, sDamageFloorFrame)) {
        kill();
    }
}

void Unizo::exeJumpDown() {
    if (MR::isFirstStep(this)) {
        MR::startSound(this, "SE_EM_UNIZO_BREAK");
    }

    fallBroken();

    if (isBreakModelEnd()) {
        kill();
    }
    if (MR::isGreaterEqualStep(this, sDamageFloorFrame)) {
        kill();
    }
}

void Unizo::exePointing() {
    MR::updateActorStateAndNextNerve(this, mBindStarPointerState, GET_NERVE(Unizo, UnizoNrvWait));
}

void Unizo::endPointing() {
    mBindStarPointerState->kill();
}

void Unizo::attackSensor(HitSensor* pSender, HitSensor* pReceiver) {
    if (isBreakNow()) {
        return;
    }

    if (MR::isSensorPlayerOrRide(pReceiver)) {
        if (!isNerve(GET_NERVE(Unizo, UnizoNrvAttack)) && MR::sendMsgEnemyAttackStrong(pReceiver, pSender)) {
            if (!isBreakNow()) {
                doAttack(pReceiver);
            }
        } else {
            MR::sendMsgPush(pReceiver, pSender);
        }
    } else if (MR::isSensorEnemy(pReceiver)) {
        if (MR::sendMsgEnemyAttack(pReceiver, pSender) && !isNerve(GET_NERVE(Unizo, UnizoNrvCollideEnemy))) {
            setNerve(GET_NERVE(Unizo, UnizoNrvCollideEnemy));
        }
    }
}

bool Unizo::receiveMsgPlayerAttack(u32 msg, HitSensor* /*pSender*/, HitSensor* pReceiver) {
    if (MR::isMsgStarPieceReflect(msg)) {
        mAnimScaleController->startHitReaction();
        return true;
    }
    if (isBreakNow()) {
        return false;
    }

    if (MR::isMsgFireBallAttack(msg)) {
        if (mType == TypeSea) {
            doBreak();
        } else {
            Vec3f direction = pReceiver->mPosition - *MR::getPlayerPos();
            MR::normalizeOrZero(&direction);
            doFireDown(direction);
        }
        return true;
    }
    if (MR::isMsgJetTurtleAttack(msg)) {
        doBreak();
        return true;
    }
    return false;
}

// Another Unizo pushes it away by the overlap; any other enemy attack breaks it.
bool Unizo::receiveMsgEnemyAttack(u32 /*msg*/, HitSensor* pSender, HitSensor* pReceiver) {
    if (MR::isSensorType(pSender, ATYPE_UNIZO)) {
        Vec3f direction = pReceiver->mPosition - pSender->mPosition;
        f32 distance = direction.length();
        MR::normalizeOrZero(&direction);
        MR::addVelocityMoveToDirection(this, direction, sReboundEnemy * distance);
        startWallHitSound();
        if (!MR::isBckPlaying(this, "Shock")) {
            MR::startBck(this, "Shock");
        }
    } else {
        doBreak();
    }
    return true;
}

bool Unizo::receiveOtherMsg(u32 msg, HitSensor* /*pSender*/, HitSensor* /*pReceiver*/) {
    if (MR::isMsgSpinStormRange(msg)) {
        doSpin();
        return true;
    }
    return false;
}

void Unizo::updateRotate() {
    MR::rotateQuatRollBall(&mQuat, mVelocity, -mGravity, sBodyRadius * mRadiusScale);
}

// Accelerates toward the player on a heading that swings sChaseDegree / 2 either way, more the faster it rolls.
void Unizo::updateVelocity() {
    Vec3f direction = *MR::getPlayerPos() - mPosition;
    f32 chaseRate = MR::sin(mFrame / sChaseCycle);
    MR::vecKillElement(direction, mGravity, &direction);
    MR::normalizeOrZero(&direction);
    f32 speedRate = mVelocity.length() / sChaseSpeedMax;
    chaseRate *= speedRate >= 1.0f ? 1.0f : speedRate;
    Pos3f rotateMtx;
    f32 angle = (PI / cNum_updateVelocity_f1) * (sChaseDegree * chaseRate / cNum_updateVelocity_f2);
    rotateMtx.makeRotate(mGravity, angle);
    rotateMtx.mult(direction, direction);

    if (direction.dot(mVelocity) > 0.0f) {
        if (mVelocity.length() < sChaseSpeedMax) {
            MR::addVelocityMoveToDirection(this, direction, sAccelerate);
        }
    } else {
        MR::addVelocityMoveToDirection(this, direction, sAccelerate);
    }
}

// Gravity, bounce, landing and rolling sounds, braking; a damage floor breaks it.
void Unizo::updateInfluence() {
    bool rebound;
    if (mJumpCount != 0) {
        MR::addVelocityToGravity(this, mGravityRate);
        rebound = MR::reboundVelocityFromCollision(this, sReboundFloor, cNum_updateInfluence_reboundVelocityFromCollision_1, cNum_updateInfluence_reboundVelocityFromCollision_2);
    } else {
        rebound = MR::reboundVelocityFromCollision(this, cNum_updateInfluence_reboundVelocityFromCollision_3, cNum_updateInfluence_reboundVelocityFromCollision_4, cNum_updateInfluence_reboundVelocityFromCollision_5);
        if (MR::isBindedGround(this)) {
            MR::addVelocityToGravity(this, sGravityRateGround0);
        } else {
            MR::addVelocityToGravity(this, sGravityRateGround1);
        }
    }

    MR::restrictVelocity(this, sTotalSpeedMax);
    if (isBreakGround()) {
        doJumpDown();
        return;
    }

    if (rebound) {
        f32 verticalSpeed = -mGravity.dot(mVelocity);
        if (verticalSpeed >= sLandSoundLandSpeed) {
            if (MR::isInWater(this, Vec3f(0.0f, 0.0f, 0.0f))) {
                MR::startLevelSound(this, "SE_EM_LV_UNIZO_LAND_WATER", cNum_updateInfluence_f1 * verticalSpeed, 30);
            } else {
                MR::startLevelSound(this, "SE_EM_LV_UNIZO_LAND", cNum_updateInfluence_f2 * verticalSpeed, 30);
            }
        }
        if (MR::isBindedWall(this)) {
            startWallHitSound();
        }
    }

    if (MR::isBindedGround(this)) {
        if (isNerve(GET_NERVE(Unizo, UnizoNrvCollidePlayer)) || isNerve(GET_NERVE(Unizo, UnizoNrvCollideEnemy)) ||
            isNerve(GET_NERVE(Unizo, UnizoNrvWait))) {
            MR::attenuateVelocity(this, sBrakeRate);
            if (mVelocity.length() < sStopSpeed) {
                mVelocity.set(Vec3f(0.0f, 0.0f, 0.0f));
            }
        } else {
            MR::attenuateVelocity(this, sBrakeRateChase);
        }
        if (mJumpCount != 0) {
            mJumpCount--;
        }

        mRollSoundTimer = sRollSoundTime;
    }

    if (mRollSoundTimer > 0) {
        f32 speed = mVelocity.length();
        if (speed >= sRollSoundSpeed) {
            if (MR::isInWater(this, Vec3f(0.0f, 0.0f, 0.0f))) {
                MR::startLevelSound(this, "SE_EM_LV_UNIZO_ROLL_WATER", cNum_updateInfluence_f3 * speed);
            } else {
                MR::startLevelSound(this, "SE_EM_LV_UNIZO_ROLL_GROUND", cNum_updateInfluence_f4 * speed);
            }
        }
        mRollSoundTimer--;
    }
}

void Unizo::updateBlink() {
    if (mBlinkFrame == getNerveStep() % sBlinkCycle) {
        MR::startBtp(this, "Blink");
    }
}

void Unizo::updateSurfaceEffect() {
    if (mType != TypeShoal) {
        return;
    }

    WaterInfo waterInfo;
    if (!MR::getWaterAreaObj(&waterInfo, mPosition)) {
        MR::deleteEffect(this, "Ripple");
        return;
    }
    MR::getWaterAreaInfo(&waterInfo, mPosition, mGravity, false);
    if (!waterInfo.isInWater()) {
        MR::deleteEffect(this, "Ripple");
        return;
    }

    f32 depth = waterInfo.mCamWaterDepth;
    bool isRunning = mVelocity.length() > sSurfaceEffectRunSpeed;
    f32 minDepth = isRunning ? sSurfaceEffectMinRunLimit : sSurfaceEffectMinLimit;
    f32 maxDepth = isRunning ? sSurfaceEffectMaxRunLimit : sSurfaceEffectMaxLimit;
    if (depth < minDepth || depth > maxDepth) {
        MR::deleteEffect(this, "Ripple");
        return;
    }

    mRippleMtx.set(mBaseMtx);
    MR::setMtxTrans(mRippleMtx, waterInfo.mSurfacePos.x, waterInfo.mSurfacePos.y, waterInfo.mSurfacePos.z);
    MR::emitEffect(this, "Ripple");
}

void Unizo::deleteEffect() {
    if (mType == TypeLand) {
        MR::deleteEffect(this, "SearchSmoke");
    } else if (mType == TypeShoal) {
        MR::deleteEffect(this, "Ripple");
    }
}

void Unizo::doJump() {
    f32 gravity = mGravityRate * mGravity.length();
    mJumpCount = sJumpNow;
    f32 speed = MR::sqrt(cNum_doJump_f1 * (mRadiusScale * (sJumpHeight * gravity)));
    mVelocity.add(mGravity * speed * -1.0f);
}

void Unizo::doAttack(HitSensor* pSensor) {
    Vec3f direction = mPosition - pSensor->mHost->mPosition;
    Vec3f up = -mGravity;
    MR::normalizeOrZero(&direction);
    MR::normalizeOrZero(&up);
    mJumpCount = sJumpNow;
    mVelocity.set(up * sReboundPlayerV + direction * sReboundPlayerH);
    setNerve(GET_NERVE(Unizo, UnizoNrvAttack));
}

void Unizo::doJumpDown() {
    appearBreakModel();
    if (mType == TypeShoal) {
        MR::startBck(mBreakModel, "Firedown");
        MR::startBrk(mBreakModel, "Break");
    } else if (mType == TypeLand) {
        MR::startBck(mBreakModel, "Firedown");
        MR::startBrk(mBreakModel, "FireDown");
    }
    mVelocity.set(mGravity * -sDamageFloorVertical);
    setNerve(GET_NERVE(Unizo, UnizoNrvJumpDown));
}

void Unizo::doFireDown(const Vec3f& rDirection) {
    appearBreakModel();
    if (mType == TypeShoal) {
        if (MR::appearStarPiece(this, mPosition, sStarPieceNum, cNum_doFireDown_f1, cNum_doFireDown_f2, true)) {
            MR::startSound(this, "SE_OJ_STAR_PIECE_BURST");
        }
        MR::startBck(mBreakModel, "Firedown");
        MR::startBrk(mBreakModel, "Break");
        MR::startSound(this, "SE_EM_UNIZO_BREAK");
        MR::shakeCameraNormal();
        mBreakModel->appear();
        setNerve(GET_NERVE(Unizo, UnizoNrvBreak));
    } else {
        if (mType == TypeLand) {
            MR::startBck(mBreakModel, "Firedown");
            MR::startBrk(mBreakModel, "FireDown");
        }
        MR::zeroVelocity(this);
        MR::setVelocitySeparateHV(this, rDirection, sFireDownHorizontal, sFireDownVertical);
        setNerve(GET_NERVE(Unizo, UnizoNrvFireDown));
    }
}

void Unizo::doBreak() {
    if (MR::appearStarPiece(this, mPosition, sStarPieceNum, cNum_doBreak_f1, cNum_doBreak_f2, mType != TypeSea)) {
        MR::startSound(this, "SE_OJ_STAR_PIECE_BURST");
    }
    MR::startSound(this, "SE_EM_UNIZO_BREAK");
    appearBreakModel();
    if (mType == TypeSea) {
        MR::shakeCameraNormal();
        MR::startBck(mBreakModel, "Break");
        MR::startBrk(mBreakModel, "Break");
        MR::emitEffect(this, "Death");
    } else if (mType == TypeShoal) {
        MR::shakeCameraNormal();
        MR::startBck(mBreakModel, "Break");
        MR::startBrk(mBreakModel, "Break");
    } else if (mType == TypeLand) {
        MR::startBck(mBreakModel, "FireDown");
        MR::startBrk(mBreakModel, "FireDown");
    }
    mBreakModel->appear();
    setNerve(GET_NERVE(Unizo, UnizoNrvBreak));
}

// The spin storm pulls it sideways; above sSpinSpeedMax it keeps its speed.
void Unizo::doSpin() {
    f32 speed = mVelocity.length();
    Vec3f pullVelocity;
    MR::calcPlayerSpinPullVelocity(&pullVelocity, mPosition);
    MR::vecKillElement(pullVelocity, mGravity, &pullVelocity);
    mVelocity.add(pullVelocity * sDriftSpeedRate);
    if (speed > sSpinSpeedMax) {
        mVelocity.setLength(speed);
    }
    if (mSpinAnimTimer == 0 && !MR::isBckPlaying(this, "Shock")) {
        mSpinAnimTimer = sSpinAnimationCount;
        MR::startBck(this, "Shock");
    }
}

// The broken ball falls under its own gravity and carries the break model.
void Unizo::fallBroken() {
    MR::addVelocityToGravity(this, sFireDownGravity);
    if (MR::isBindedGround(this)) {
        MR::attenuateVelocity(this, sFireDownAttenuate);
    }
    updateRotate();
    calcAndSetBaseMtx();
    mBreakModel->mPosition.set(mPosition);
}

bool Unizo::isBreakModelEnd() const {
    return MR::isDead(mBreakModel) || (MR::isBckStopped(mBreakModel) && MR::isBrkStopped(mBreakModel));
}

bool Unizo::isBreakGround() {
    for (u32 i = 0; i < mBinder->mPlaneNum; i++) {
        const Triangle* pPlane = &mBinder->getPlane(i)->mParentTriangle;
        if (MR::isGroundCodeDamage(pPlane) || MR::isGroundCodeDamageFire(pPlane)) {
            return true;
        }
    }
    return false;
}

bool Unizo::isBreakNow() const {
    return isNerve(GET_NERVE(Unizo, UnizoNrvBreak)) || isNerve(GET_NERVE(Unizo, UnizoNrvFireDown)) || isNerve(GET_NERVE(Unizo, UnizoNrvJumpDown));
}

bool Unizo::tryPointBind() {
    if (isEnablePointBind() && mBindStarPointerState->tryStartPointBind()) {
        setNerve(GET_NERVE(Unizo, UnizoNrvPointing));
        return true;
    }
    return false;
}

bool Unizo::isEnablePointBind() const {
    return isNerve(GET_NERVE(Unizo, UnizoNrvWait)) || isNerve(GET_NERVE(Unizo, UnizoNrvJump)) || isNerve(GET_NERVE(Unizo, UnizoNrvChase)) ||
           isNerve(GET_NERVE(Unizo, UnizoNrvAttack)) || isNerve(GET_NERVE(Unizo, UnizoNrvCollidePlayer)) ||
           isNerve(GET_NERVE(Unizo, UnizoNrvCollideEnemy));
}

// The break model faces the camera.
void Unizo::appearBreakModel() {
    Pos3f mtx;
    Vec3f rotation;
    MR::makeMtxFrontUp(&mtx, -MR::getCamZdir(), MR::getCamYdir());
    mtx.getEuler(rotation);
    rotation.set(toDeg(rotation.x), toDeg(rotation.y), toDeg(rotation.z));
    mBreakModel->mPosition.set(mPosition);
    mBreakModel->mScale.set(mScale);
    mBreakModel->mRotation.set(rotation);
    mBreakModel->appear();
    MR::hideModel(this);
    MR::invalidateHitSensors(this);
    MR::invalidateClipping(mBreakModel);
    MR::invalidateClipping(this);
    MR::invalidateShadowAll(this);
    MR::invalidateShadowAll(mBreakModel);
}

void Unizo::startWallHitSound() {
    f32 speed = mVelocity.length();
    if (speed >= sWallHitSoundSpeed) {
        if (MR::isInWater(this, Vec3f(0.0f, 0.0f, 0.0f))) {
            MR::startLevelSound(this, "SE_EM_LV_UNIZO_COLLISION_WATER", cNum_startWallHitSound_f1 * speed, sUnizoCollisionSePlayTime);
        } else {
            MR::startLevelSound(this, "SE_EM_LV_UNIZO_COLLISION", cNum_startWallHitSound_f2 * speed, sUnizoCollisionSePlayTime);
        }
    }
}

MtxPtr Unizo::getBaseMtx() const {
    return const_cast< Pos3f& >(mBaseMtx).toMtxPtr();
}
