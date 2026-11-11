#include "Game/Enemy/StinkBugSmall.hpp"
#include "Game/System/NebulaFrameClock.hpp"
#include "Game/Enemy/AnimScaleController.hpp"
#include "Game/Enemy/WalkerStateBindStarPointer.hpp"
#include "Game/LiveActor/Nerve.hpp"
#include "Game/Util/ActorMovementUtil.hpp"
#include "Game/Util/ActorSensorUtil.hpp"
#include "Game/Util/ActorStateUtil.hpp"
#include "Game/Util/ActorSwitchUtil.hpp"
#include "Game/Util/EffectUtil.hpp"
#include "Game/Util/PlacementUtil.hpp"
#include "Game/Util/LiveActorUtil.hpp"
#include "Game/Util/MapUtil.hpp"
#include "Game/Util/MathUtil.hpp"
#include "Game/Util/MtxUtil.hpp"
#include "Game/Util/ObjUtil.hpp"
#include "Game/Util/PlayerUtil.hpp"
#include "Game/Util/SoundUtil.hpp"
#include "Game/Util/StarPointerUtil.hpp"
#include "Game/Util/Tunable.hpp"
#include "Game/System/LumaCore.hpp"

NEBULA_TUNABLE_NAMED(f32, cNum_init_f1, "init.f1", 110.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_init_f2, "init.f2", 10.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_init_f3, "init.f3", 100.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_init_f4, "init.f4", 30.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_init_f5, "init.f5", 200.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_init_f6, "init.f6", 200.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_init_f7, "init.f7", 130.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_init_f8, "init.f8", 50.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeForceFall_f1, "exeForceFall.f1", 2.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_kill_f1, "kill.f1", 10.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_kill_f2, "kill.f2", 40.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_initAfterPlacement_f1, "initAfterPlacement.f1", 50.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_initAfterPlacement_f2, "initAfterPlacement.f2", 200.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_control_f1, "control.f1", 2.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_calcAndSetBaseMtx_f1, "calcAndSetBaseMtx.f1", 0.3f);
NEBULA_TUNABLE_NAMED(f32, cNum_attackSensor_f1, "attackSensor.f1", 20.0f);

namespace NrvStinkBugSmall {
    NEW_NERVE(StinkBugSmallNrvWait, StinkBugSmall, Wait);
    NEW_NERVE(StinkBugSmallNrvSearch, StinkBugSmall, Search);
    NEW_NERVE(StinkBugSmallNrvDashSign, StinkBugSmall, DashSign);
    NEW_NERVE(StinkBugSmallNrvDashSignEnd, StinkBugSmall, DashSignEnd);
    NEW_NERVE(StinkBugSmallNrvDash, StinkBugSmall, Dash);
    NEW_NERVE(StinkBugSmallNrvDashEnd, StinkBugSmall, DashEnd);
    NEW_NERVE(StinkBugSmallNrvBack, StinkBugSmall, Back);
    NEW_NERVE(StinkBugSmallNrvHipDropDown, StinkBugSmall, HipDropDown);
    NEW_NERVE(StinkBugSmallNrvAttack, StinkBugSmall, Attack);
    NEW_NERVE(StinkBugSmallNrvSpinReaction, StinkBugSmall, SpinReaction);
    NEW_NERVE(StinkBugSmallNrvShakeStart, StinkBugSmall, ShakeStart);
    NEW_NERVE(StinkBugSmallNrvShake, StinkBugSmall, Shake);
    NEW_NERVE(StinkBugSmallNrvPanic, StinkBugSmall, Panic);
    NEW_NERVE(StinkBugSmallNrvRecover, StinkBugSmall, Recover);
    NEW_NERVE_ONEND(StinkBugSmallNrvPointerSwoon, StinkBugSmall, PointerSwoon, PointerSwoon);
    NEW_NERVE(StinkBugSmallNrvForceFall, StinkBugSmall, ForceFall);
};  // namespace NrvStinkBugSmall

namespace {
    NEBULA_TUNABLE(f32, sTurnRateSearch, 1.0f);
    NEBULA_TUNABLE(f32, sTurnRateDashSign, 3.0f);
    NEBULA_TUNABLE(s32, sStepForWait, 1);
    NEBULA_TUNABLE(s32, sStepForDashSignEnd, 10);
    NEBULA_TUNABLE(s32, sStepForShakeStart, 40);
    const s32 sStepForRecover = NebulaFrameClock::stepsOfSeconds(1);
    const s32 sStepForPanic = NebulaFrameClock::stepsOfMilliseconds(1500);
    NEBULA_TUNABLE(f32, sDashSpeed, 20.0f);
    NEBULA_TUNABLE(f32, sBackSpeed, 5.0f);
    NEBULA_TUNABLE(f32, sHomeDistance, 10.0f);
    NEBULA_TUNABLE(f32, sTerritoryReachMargin, 400.0f);
    NEBULA_TUNABLE(f32, sTerritorySideHalfSize, 600.0f);
    NEBULA_TUNABLE(f32, sTerritoryUpHalfSize, 200.0f);
    NEBULA_TUNABLE(f32, sTerritoryBackOffset, 200.0f);
};  // namespace

StinkBugSmall::StinkBugSmall(const char* pName) : StinkBugBase(pName), mScaleController(nullptr), mBindStarPointer(nullptr), mIsBodyAttack(false) {
    lumacore::setClass(this, lumacore::kClassId< StinkBugSmall >);
}

// Arg 0 the territory angle, arg 1 the radius, arg 7 a pushing body sensor instead of a collision body.
void StinkBugSmall::init(const TableIter& rIter) {
    StinkBugBase::init(rIter);
    MR::getPlacementArg0NoInit(rIter, &mTerritoryAngle);
    MR::getPlacementArg1NoInit(rIter, &mRadius);
    MR::getPlacementArg7NoInit(rIter, &mIsBodyAttack);
    initHitSensor(3);
    if (mIsBodyAttack) {
        MR::addHitSensorEnemy(this, "body", 8, cNum_init_f1 * mScale.x, Vec3f(0.0f, cNum_init_f2 * mScale.x, 0.0f));
    } else {
        MR::addBodyMessageSensorEnemy(this);
    }

    MR::addHitSensorAtJointEnemyAttack(this, "head", "Face", 8, cNum_init_f3 * mScale.x, Vec3f(0.0f, -cNum_init_f4 * mScale.x, 0.0f));
    if (!mIsBodyAttack) {
        MR::initCollisionParts(this, "Switch", getSensor("body"), nullptr);
    }

    initBinder(cNum_init_f5 * mScale.x, cNum_init_f6 * mScale.x, 0);
    MR::setBinderExceptActor(this, this);
    MR::setBinderIgnoreMovingCollision(this);
    if (!MR::isValidSwitchDead(this)) {
        MR::declareStarPiece(this, 3);
    }

    MR::initStarPointerTarget(this, cNum_init_f7 * mScale.x, Vec3f(0.0f, cNum_init_f8 * mScale.x, 0.0f));
    mScaleController = new AnimScaleController(nullptr);
    mBindStarPointer = new WalkerStateBindStarPointer(this, mScaleController);
    MR::useStageSwitchReadA(this, rIter);
    MR::startBrk(this, "Death");
    MR::setBrkFrameAndStop(this, 0.0f);
    initNerve(GET_NERVE(StinkBugSmall, StinkBugSmallNrvWait));
    makeActorAppeared();
}

bool StinkBugSmall::isPlayerInSmallTerritory() const {
    return isPlayerInTerritory(sTerritoryReachMargin, sTerritorySideHalfSize, sTerritoryUpHalfSize, sTerritoryBackOffset);
}

// Angle 0 watches straight ahead and never searches.
void StinkBugSmall::exeWait() {
    if (MR::isFirstStep(this) && mTerritoryAngle == 0.0f) {
        MR::tryStartBck(this, "Search");
    }

    fixInitPos();
    if (isPlayerInSmallTerritory()) {
        setNerve(GET_NERVE(StinkBugSmall, StinkBugSmallNrvDashSign));
    } else if (mTerritoryAngle != 0.0f && MR::isGreaterStep(this, sStepForWait)) {
        setNerve(GET_NERVE(StinkBugSmall, StinkBugSmallNrvSearch));
    }
}

void StinkBugSmall::exeSearch() {
    if (MR::isFirstStep(this)) {
        MR::tryStartBck(this, "Search");
    }

    fixInitPos();
    if (tryTurnSearch(sTurnRateSearch)) {
        setNerve(GET_NERVE(StinkBugSmall, StinkBugSmallNrvWait));
    } else if (isPlayerInSmallTerritory()) {
        setNerve(GET_NERVE(StinkBugSmall, StinkBugSmallNrvDashSign));
    }
}

void StinkBugSmall::exeDashSign() {
    if (MR::isFirstStep(this)) {
        MR::startBck(this, "RushStart");
        MR::startSound(this, "SE_EV_STINKBUG_S_FIND");
    }

    MR::startLevelSound(this, "SE_EM_LV_STINKBUG_S_DASH_SIGN");
    fixInitPos();
    turnDashSign(sTurnRateDashSign);
    if (MR::isBckStopped(this)) {
        setNerve(GET_NERVE(StinkBugSmall, StinkBugSmallNrvDashSignEnd));
    }
}

void StinkBugSmall::exeDashSignEnd() {
    if (MR::isStep(this, sStepForDashSignEnd)) {
        setNerve(GET_NERVE(StinkBugSmall, StinkBugSmallNrvDash));
    }
}

void StinkBugSmall::exeDash() {
    if (MR::isFirstStep(this)) {
        MR::startBck(this, "Rush");
        MR::validateHitSensors(this);
    }

    MR::startLevelSound(this, "SE_EM_LV_STINKBUG_S_DASH");
    if (!MR::isNear(this, mInitPos, mRadius) || MR::isBindedWall(this)) {
        mVelocity.zero();
        setNerve(GET_NERVE(StinkBugSmall, StinkBugSmallNrvDashEnd));
        MR::startSound(this, "SE_EM_STINKBUG_S_DASH_END");
        return;
    }

    setDashVelocity(sDashSpeed);
}

void StinkBugSmall::exeDashEnd() {
    if (MR::isFirstStep(this)) {
        MR::startBck(this, "RushStop");
    }

    mVelocity.zero();
    if (MR::isBckStopped(this)) {
        setNerve(GET_NERVE(StinkBugSmall, StinkBugSmallNrvBack));
    }
}

void StinkBugSmall::exeBack() {
    if (MR::isFirstStep(this)) {
        MR::startBck(this, "Back");
        MR::invalidateHitSensor(this, "head");
    }

    MR::startLevelSound(this, "SE_EM_LV_STINKBUG_S_BACK");
    if (MR::isNear(this, mInitPos, sHomeDistance)) {
        setNerve(GET_NERVE(StinkBugSmall, StinkBugSmallNrvWait));
        return;
    }

    Vec3f toHome;
    toHome.sub(mInitPos, mPosition);
    MR::normalize(&toHome);
    MR::turnVecToPlane(&toHome, toHome, mGravity);
    mVelocity.scale(sBackSpeed, toHome);
}

void StinkBugSmall::exeHipDropDown() {
    if (MR::isFirstStep(this)) {
        MR::startBck(this, "Death");
        MR::startBrk(this, "Death");
        if (!mIsBodyAttack) {
            MR::invalidateCollisionParts(this);
        }

        MR::invalidateHitSensors(this);
        MR::invalidateClipping(this);
        MR::startSound(this, "SE_EM_STOMPED_S");
        MR::startSound(this, "SE_EV_STINKBUG_S_STOMPED");
    }

    mVelocity.zero();
    if (MR::isBckStopped(this)) {
        kill();
    }
}

void StinkBugSmall::exeAttack() {
    if (MR::isFirstStep(this)) {
        MR::startBck(this, "Attack");
    }

    mVelocity.zero();
    if (MR::isBckStopped(this)) {
        setNerve(GET_NERVE(StinkBugSmall, StinkBugSmallNrvBack));
    }
}

void StinkBugSmall::exeSpinReaction() {
    if (MR::isFirstStep(this)) {
        MR::startBck(this, "SpinAction");
        MR::startSound(this, "SE_EM_GUARD_S");
    }

    mVelocity.zero();
    if (MR::isBckStopped(this)) {
        setNerve(GET_NERVE(StinkBugSmall, StinkBugSmallNrvBack));
    }
}

void StinkBugSmall::exeShakeStart() {
    if (MR::isFirstStep(this)) {
        MR::startBck(this, "Repel");
    }

    if (MR::isStep(this, sStepForShakeStart)) {
        setNerve(GET_NERVE(StinkBugSmall, StinkBugSmallNrvShake));
    }
}

void StinkBugSmall::exeShake() {
    if (MR::isBckStopped(this)) {
        setNerve(GET_NERVE(StinkBugSmall, StinkBugSmallNrvBack));
    }
}

void StinkBugSmall::exePanic() {
    if (MR::isFirstStep(this)) {
        MR::startBck(this, "Loss");
        MR::invalidateHitSensor(this, "head");
    }

    MR::startLevelSound(this, "SE_EV_LV_STINKBUG_S_PANIC", -1, -1, 15);
    MR::startLevelSound(this, "SE_EM_LV_STINKBUG_S_PANIC");
    mVelocity.zero();
    if (!MR::isOnPlayer(getSensor("body"))) {
        setNerve(GET_NERVE(StinkBugSmall, StinkBugSmallNrvRecover));
    } else if (MR::isStep(this, sStepForPanic)) {
        setNerve(GET_NERVE(StinkBugSmall, StinkBugSmallNrvShakeStart));
    }
}

void StinkBugSmall::exeRecover() {
    mVelocity.zero();
    MR::startLevelSound(this, "SE_EM_LV_STINKBUG_S_PANIC");
    if (MR::isStep(this, sStepForRecover)) {
        setNerve(GET_NERVE(StinkBugSmall, StinkBugSmallNrvBack));
    }
}

void StinkBugSmall::exePointerSwoon() {
    if (MR::isFirstStep(this)) {
        MR::deleteEffectAll(this);
    }

    if (MR::isEffectValid(this, "RushSmoke")) {
        MR::deleteEffect(this, "RushSmoke");
    }

    MR::updateActorStateAndNextNerve(this, mBindStarPointer, GET_NERVE(StinkBugSmall, StinkBugSmallNrvBack));
}

void StinkBugSmall::endPointerSwoon() {
    mBindStarPointer->kill();
}

void StinkBugSmall::exeForceFall() {
    if (MR::isFirstStep(this)) {
        mVelocity.zero();
        MR::calcGravity(this);
        MR::onBind(this);
    }

    mVelocity.scaleAdd(cNum_exeForceFall_f1, mGravity, mVelocity);
}

// With a dead switch the switch is the reward; without, three star pieces.
void StinkBugSmall::kill() {
    if (MR::isValidSwitchDead(this)) {
        MR::onSwitchDead(this);
    } else {
        MR::appearStarPiece(this, mPosition, 3, cNum_kill_f1, cNum_kill_f2, false);
        MR::startSound(this, "SE_OJ_STAR_PIECE_BURST");
    }

    MR::emitEffect(this, "Death");
    MR::startSound(this, "SE_EM_EXPLODE_S");
    LiveActor::kill();
}

// Home is the ground under the placement.
void StinkBugSmall::initAfterPlacement() {
    Vec3f up;
    MR::calcUpVec(&up, this);
    Vec3f rayStart;
    rayStart.scaleAdd(cNum_initAfterPlacement_f1, up, mPosition);
    Vec3f ray(up);
    ray.scale(-cNum_initAfterPlacement_f2);
    MR::getFirstPolyOnLineToMapExceptSensor(&mInitPos, nullptr, rayStart, ray, getSensor("body"));
    MR::resetPosition(this, mInitPos);
}

void StinkBugSmall::control() {
    if (MR::isInDeath(this, Vec3f(0.0f, 0.0f, 0.0f))) {
        kill();
        return;
    }

    mScaleController->updateNerve();
    if (tryPointerSwoon() || tryForceFall() || isNerve(GET_NERVE(StinkBugSmall, StinkBugSmallNrvForceFall))) {
        return;
    }

    if (!isNerve(GET_NERVE(StinkBugSmall, StinkBugSmallNrvDash)) && !isNerve(GET_NERVE(StinkBugSmall, StinkBugSmallNrvBack))) {
        MR::offBind(this);
        return;
    }

    // Moving: gravity follows the ground; moves along it and falls.
    MR::onBind(this);
    if (MR::isBindedGround(this)) {
        mGravity.set(-*MR::getGroundNormal(this));
    }

    mVelocity.killElement(mVelocity, mGravity);
    mVelocity.scaleAdd(cNum_control_f1, mGravity, mVelocity);
    Vec3f front(mFront);
    MR::turnVecToPlane(&mFront, front, mGravity);
}

void StinkBugSmall::calcAndSetBaseMtx() {
    Pos3f mtx;
    MR::calcMtxFromGravityAndZAxis(&mtx, this, mGravity, mFront);
    MR::blendMtx(getBaseMtx(), mtx, cNum_calcAndSetBaseMtx_f1, mtx);
    MR::setBaseTRMtx(this, mtx);
    Vec3f scale = mScaleController->mScale * mScale;
    MR::setBaseScale(this, scale);
}

void StinkBugSmall::attackSensor(HitSensor* pSender, HitSensor* pReceiver) {
    if (isNerve(GET_NERVE(StinkBugSmall, StinkBugSmallNrvHipDropDown))) {
        return;
    }

    if (mIsBodyAttack && pSender == getSensor("body")) {
        MR::sendMsgPush(pReceiver, pSender);
        return;
    }

    if (isNerve(GET_NERVE(StinkBugSmall, StinkBugSmallNrvAttack)) || isNerve(GET_NERVE(StinkBugSmall, StinkBugSmallNrvPointerSwoon)) ||
        !MR::isSensorPlayer(pReceiver) || !MR::isSensorEnemyAttack(pSender)) {
        return;
    }

    if (MR::isPlayerDamaging()) {
        MR::sendMsgPush(pReceiver, pSender);
        return;
    }

    if (isHitHorn(pSender, pReceiver, mScale.x * cNum_attackSensor_f1) && MR::sendMsgEnemyAttackStrong(pReceiver, pSender)) {
        MR::emitEffectHitBetweenSensors(this, pSender, pReceiver, 0.0f, nullptr);
        if (!isNerve(GET_NERVE(StinkBugSmall, StinkBugSmallNrvDash))) {
            setNerve(GET_NERVE(StinkBugSmall, StinkBugSmallNrvAttack));
        }
    }
}

bool StinkBugSmall::receiveMsgPlayerAttack(u32 msg, HitSensor* /*pSender*/, HitSensor* pReceiver) {
    if (isNerve(GET_NERVE(StinkBugSmall, StinkBugSmallNrvHipDropDown)) || pReceiver != getSensor("body")) {
        return false;
    }

    if (MR::isMsgInvincibleAttack(msg) || MR::isMsgPlayerHipDropFloor(msg) || MR::isMsgPlayerHipDrop(msg)) {
        setNerve(GET_NERVE(StinkBugSmall, StinkBugSmallNrvHipDropDown));
        return true;
    }

    if (isNerve(GET_NERVE(StinkBugSmall, StinkBugSmallNrvPointerSwoon))) {
        return false;
    }

    if (MR::isMsgLockOnStarPieceShoot(msg)) {
        return true;
    }

    if (MR::isMsgStarPieceReflect(msg)) {
        if (isNrvEnableStarPieceAttack()) {
            setNerve(GET_NERVE(StinkBugSmall, StinkBugSmallNrvSpinReaction));
        }

        return true;
    }

    if (!isNerve(GET_NERVE(StinkBugSmall, StinkBugSmallNrvSpinReaction)) && !isNerve(GET_NERVE(StinkBugSmall, StinkBugSmallNrvDash)) &&
        MR::isMsgPlayerSpinAttack(msg)) {
        setNerve(GET_NERVE(StinkBugSmall, StinkBugSmallNrvSpinReaction));
        return true;
    }

    return false;
}

bool StinkBugSmall::receiveOtherMsg(u32 msg, HitSensor* pSender, HitSensor* pReceiver) {
    if (isNerve(GET_NERVE(StinkBugSmall, StinkBugSmallNrvHipDropDown))) {
        return false;
    }

    // An invincible player beats it by touch.
    if (MR::isPlayerElementModeInvincible() && (MR::isMsgFloorTouch(msg) || MR::isMsgWallTouch(msg) || MR::isMsgCeilTouch(msg))) {
        setNerve(GET_NERVE(StinkBugSmall, StinkBugSmallNrvHipDropDown));
        return true;
    }

    if (isNerve(GET_NERVE(StinkBugSmall, StinkBugSmallNrvPointerSwoon)) || isNerve(GET_NERVE(StinkBugSmall, StinkBugSmallNrvPanic)) ||
        isNerve(GET_NERVE(StinkBugSmall, StinkBugSmallNrvShakeStart))) {
        return false;
    }

    if (MR::isMsgFloorTouch(msg) && MR::isOnPlayer(getSensor("body"))) {
        if (isShakeChance()) {
            MR::sendMsgEnemyAttackFlip(pSender, pReceiver);
        } else {
            setNerve(GET_NERVE(StinkBugSmall, StinkBugSmallNrvPanic));
        }

        return true;
    }

    return false;
}

bool StinkBugSmall::tryPointerSwoon() {
    if (isNerve(GET_NERVE(StinkBugSmall, StinkBugSmallNrvHipDropDown)) || isNerve(GET_NERVE(StinkBugSmall, StinkBugSmallNrvPointerSwoon))) {
        return false;
    }

    if (!mBindStarPointer->tryStartPointBind()) {
        return false;
    }

    setNerve(GET_NERVE(StinkBugSmall, StinkBugSmallNrvPointerSwoon));
    return true;
}

bool StinkBugSmall::tryForceFall() {
    if (!MR::isValidSwitchA(this) || !MR::isOnSwitchA(this)) {
        return false;
    }

    if (isNerve(GET_NERVE(StinkBugSmall, StinkBugSmallNrvPointerSwoon)) || isNerve(GET_NERVE(StinkBugSmall, StinkBugSmallNrvForceFall))) {
        return false;
    }

    setNerve(GET_NERVE(StinkBugSmall, StinkBugSmallNrvForceFall));
    return true;
}

bool StinkBugSmall::isShakeChance() const {
    if (isNerve(GET_NERVE(StinkBugSmall, StinkBugSmallNrvHipDropDown)) || !isNerve(GET_NERVE(StinkBugSmall, StinkBugSmallNrvShake))) {
        return false;
    }

    return !MR::isPlayerHipDropFalling() && !MR::isPlayerHipDropLand();
}

bool StinkBugSmall::isNrvEnableStarPieceAttack() const {
    return isNerve(GET_NERVE(StinkBugSmall, StinkBugSmallNrvWait)) || isNerve(GET_NERVE(StinkBugSmall, StinkBugSmallNrvSearch)) ||
           isNerve(GET_NERVE(StinkBugSmall, StinkBugSmallNrvDashSign)) || isNerve(GET_NERVE(StinkBugSmall, StinkBugSmallNrvDashSignEnd)) ||
           isNerve(GET_NERVE(StinkBugSmall, StinkBugSmallNrvBack));
}
