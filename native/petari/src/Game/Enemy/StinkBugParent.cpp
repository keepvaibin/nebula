#include "Game/Enemy/StinkBugParent.hpp"
#include "Game/System/NebulaFrameClock.hpp"
#include "Game/LiveActor/Nerve.hpp"
#include "Game/Map/CollisionParts.hpp"
#include "Game/Util/ActorMovementUtil.hpp"
#include "Game/Util/ActorSensorUtil.hpp"
#include "Game/Util/CameraUtil.hpp"
#include "Game/Util/DemoUtil.hpp"
#include "Game/Util/EffectUtil.hpp"
#include "Game/Util/JointUtil.hpp"
#include "Game/Util/LiveActorUtil.hpp"
#include "Game/Util/MathUtil.hpp"
#include "Game/Util/ObjUtil.hpp"
#include "Game/Util/PlayerUtil.hpp"
#include "Game/Util/SoundUtil.hpp"
#include <nebula_geometry.hpp>
#include "Game/System/LumaCore.hpp"

namespace {
    const f32 sTurnRateSearch = 1.0f;
    const f32 sTurnRateDashSign = 3.0f;
    const s32 sStepForWait = 1;
    const s32 sStepForDashSignEnd = 10;
    const s32 sStepForShakeStart = 40;
    const s32 sStepForEnableShake = 25;
    const s32 sStepForRecover = NebulaFrameClock::stepsOfSeconds(1);
    const s32 sStepToJumpBackPlayer = 22;
    const s32 sStepForChildDeathSound = 48;
    const s32 sStepForSurpriseSound = 65;
    const f32 sTerritoryReachMargin = 400.0f;
    const f32 sTerritorySideHalfSize = 200.0f;
    const f32 sTerritoryUpHalfSize = 600.0f;
    const f32 sTerritoryBackOffset = 200.0f;
    const f32 sChildRadius = 150.0f;
    const f32 sHornMargin = 25.0f;
    const char* const cDemoEndPointName = "子連れカメムシデモ後ポイント";

    struct Param {
        f32 mDashSpeed;
        f32 mBackSpeed;  // Home within twice this ends the walk back.
        s32 mPanicSteps;
    };

    const Param sChildParam = {20.0f, 5.0f, 30};
    const Param sAngryParam = {25.0f, 10.0f, 20};

    const Param& getParam(bool isChildAlive) {
        return isChildAlive ? sChildParam : sAngryParam;
    }
};  // namespace

namespace NrvStinkBugParent {
    NEW_NERVE(StinkBugParentNrvWait, StinkBugParent, Wait);
    NEW_NERVE(StinkBugParentNrvSearch, StinkBugParent, Search);
    NEW_NERVE(StinkBugParentNrvDashSign, StinkBugParent, DashSign);
    NEW_NERVE(StinkBugParentNrvDashSignEnd, StinkBugParent, DashSignEnd);
    NEW_NERVE(StinkBugParentNrvDash, StinkBugParent, Dash);
    NEW_NERVE(StinkBugParentNrvDashEnd, StinkBugParent, DashEnd);
    NEW_NERVE(StinkBugParentNrvBack, StinkBugParent, Back);
    NEW_NERVE(StinkBugParentNrvHipDropDown, StinkBugParent, HipDropDown);
    NEW_NERVE(StinkBugParentNrvAttack, StinkBugParent, Attack);
    NEW_NERVE(StinkBugParentNrvSpinReaction, StinkBugParent, SpinReaction);
    NEW_NERVE(StinkBugParentNrvPanic, StinkBugParent, Panic);
    NEW_NERVE(StinkBugParentNrvShakeStart, StinkBugParent, ShakeStart);
    NEW_NERVE(StinkBugParentNrvShake, StinkBugParent, Shake);
    NEW_NERVE(StinkBugParentNrvRecover, StinkBugParent, Recover);
    NEW_NERVE(StinkBugParentNrvDemoPrepare, StinkBugParent, DemoPrepare);
    NEW_NERVE(StinkBugParentNrvDemoChildDown, StinkBugParent, DemoChildDown);
    NEW_NERVE(StinkBugParentNrvDemoAnger, StinkBugParent, DemoAnger);
};  // namespace NrvStinkBugParent

StinkBugParent::StinkBugParent(const char* pName) : StinkBugBase(pName), mParentOnlyParts(nullptr), mIsChildAlive(true) {
    lumacore::setClass(this, lumacore::kClassId< StinkBugParent >);
}

void StinkBugParent::init(const TableIter& rIter) {
    StinkBugBase::init(rIter);
    initHitSensor(2);
    MR::addHitSensorEye(this, "body", 4, 250.0f, Vec3f(0.0f, 150.0f, 0.0f));
    MR::addHitSensorAtJointEnemyAttack(this, "head", "Face", 8, 160.0f, Vec3f(0.0f, -50.0f, 80.0f));
    // With the child on its back first; its own collision takes over when the child is gone.
    MR::initCollisionParts(this, "SwitchChildJ", getSensor("body"), nullptr);
    mParentOnlyParts = MR::createCollisionPartsFromLiveActor(this, "Switch", getSensor("body"), getBaseMtx(), MR::CollisionScaleType_Default);
    initBinder(300.0f, 300.0f, 0);
    MR::setBinderExceptActor(this, this);
    MR::setBinderIgnoreMovingCollision(this);
    MR::declarePowerStar(this);
    MR::declareStarPiece(this, 8);

    if (MR::tryRegisterDemoCast(this, rIter)) {
        MR::registerDemoActionNerve(this, GET_NERVE(StinkBugParent, StinkBugParentNrvDemoAnger), "ダメージ中");
    }

    mTerritoryAngle = 50.0f;
    mRadius = 1200.0f;
    MR::startBrk(this, "Anger");
    MR::setBrkFrameAndStop(this, 0.0f);
    initNerve(GET_NERVE(StinkBugParent, StinkBugParentNrvWait));
    makeActorAppeared();
}

bool StinkBugParent::isPlayerInParentTerritory() const {
    return isPlayerInTerritory(sTerritoryReachMargin, sTerritorySideHalfSize, sTerritoryUpHalfSize, sTerritoryBackOffset);
}

void StinkBugParent::exeWait() {
    if (MR::isFirstStep(this)) {
        MR::startBck(this, "Wait");
        MR::resetPosition(this, mInitPos);
    }

    fixInitPos();

    if (isPlayerInParentTerritory()) {
        setNerve(GET_NERVE(StinkBugParent, StinkBugParentNrvDashSign));
    } else if (MR::isGreaterStep(this, sStepForWait)) {
        setNerve(GET_NERVE(StinkBugParent, StinkBugParentNrvSearch));
    }
}

void StinkBugParent::exeSearch() {
    if (MR::isFirstStep(this)) {
        MR::startBck(this, mIsChildAlive ? "Search" : "AngerSearch");
    }

    fixInitPos();

    if (tryTurnSearch(sTurnRateSearch)) {
        setNerve(GET_NERVE(StinkBugParent, StinkBugParentNrvWait));
    } else if (isPlayerInParentTerritory()) {
        setNerve(GET_NERVE(StinkBugParent, StinkBugParentNrvDashSign));
    }
}

void StinkBugParent::exeDashSign() {
    if (MR::isFirstStep(this)) {
        MR::startBck(this, "RushStart");
        MR::startSound(this, "SE_EV_STINKBUG_L_FIND");
    }

    MR::startLevelSound(this, "SE_EM_LV_STINKBUG_L_DASH_SIGN");
    fixInitPos();
    turnDashSign(sTurnRateDashSign);

    if (MR::isBckStopped(this)) {
        setNerve(GET_NERVE(StinkBugParent, StinkBugParentNrvDashSignEnd));
    }
}

void StinkBugParent::exeDashSignEnd() {
    if (MR::isStep(this, sStepForDashSignEnd)) {
        setNerve(GET_NERVE(StinkBugParent, StinkBugParentNrvDash));
    }
}

void StinkBugParent::exeDash() {
    if (MR::isFirstStep(this)) {
        MR::startBck(this, "Rush");
        MR::validateHitSensors(this);
    }

    MR::startLevelSound(this, "SE_EM_LV_STINKBUG_L_DASH");

    if (!MR::isNear(this, mInitPos, mRadius) || MR::isBindedWall(this)) {
        mVelocity.zero();
        setNerve(GET_NERVE(StinkBugParent, StinkBugParentNrvDashEnd));
        MR::startSound(this, "SE_EM_STINKBUG_L_DASH_END");
    } else {
        setDashVelocity(getParam(mIsChildAlive).mDashSpeed);
    }
}

void StinkBugParent::exeDashEnd() {
    if (MR::isFirstStep(this)) {
        MR::startBck(this, "RushStop");
    }

    mVelocity.zero();

    if (MR::isBckStopped(this)) {
        setNerve(GET_NERVE(StinkBugParent, StinkBugParentNrvBack));
    }
}

void StinkBugParent::exeBack() {
    if (MR::isFirstStep(this)) {
        MR::startBck(this, "Back");
        MR::invalidateHitSensor(this, "head");
    }

    MR::startLevelSound(this, "SE_EM_LV_STINKBUG_L_BACK");

    const Param& rParam = getParam(mIsChildAlive);
    if (MR::isNear(this, mInitPos, (2.0f * rParam.mBackSpeed))) {
        setNerve(GET_NERVE(StinkBugParent, StinkBugParentNrvWait));
        return;
    }

    Vec3f toHome;
    toHome.sub(mInitPos, mPosition);
    MR::normalize(&toHome);
    MR::turnVecToPlane(&toHome, toHome, mGravity);
    mVelocity.scale(rParam.mBackSpeed, toHome);
}

void StinkBugParent::exeHipDropDown() {
    if (MR::isFirstStep(this)) {
        MR::startBck(this, "Death");
        MR::startBrk(this, "Death");
        MR::invalidateCollisionParts(mParentOnlyParts);
        MR::invalidateHitSensors(this);
        MR::startSound(this, "SE_EM_STINKBUG_STOMPED_S");
        MR::startSound(this, "SE_EV_STINKBUG_L_STOMPED");
    }

    mVelocity.zero();

    if (MR::isBckStopped(this)) {
        kill();
    }
}

void StinkBugParent::exeAttack() {
    if (MR::isFirstStep(this)) {
        MR::startBck(this, "Attack");
    }

    mVelocity.zero();

    if (MR::isBckStopped(this)) {
        setNerve(GET_NERVE(StinkBugParent, StinkBugParentNrvBack));
    }
}

void StinkBugParent::exeSpinReaction() {
    if (MR::isFirstStep(this)) {
        MR::startBck(this, "SpinAction");
        MR::startSound(this, "SE_EM_GUARD_S");
    }

    mVelocity.zero();

    if (MR::isBckStopped(this)) {
        setNerve(GET_NERVE(StinkBugParent, StinkBugParentNrvBack));
    }
}

void StinkBugParent::exePanic() {
    if (MR::isFirstStep(this)) {
        MR::startBck(this, "Loss");
        MR::invalidateHitSensor(this, "head");
        MR::startSound(this, "SE_EV_STINKBUG_L_PANIC");
    }

    MR::startLevelSound(this, "SE_EM_LV_STINKBUG_L_PANIC");
    mVelocity.zero();

    if (!MR::isOnPlayer(getSensor("body"))) {
        setNerve(GET_NERVE(StinkBugParent, StinkBugParentNrvRecover));
    } else if (MR::isStep(this, getParam(mIsChildAlive).mPanicSteps)) {
        setNerve(GET_NERVE(StinkBugParent, StinkBugParentNrvShakeStart));
    }
}

void StinkBugParent::exeRecover() {
    mVelocity.zero();
    MR::startLevelSound(this, "SE_EM_LV_STINKBUG_L_PANIC");

    if (MR::isStep(this, sStepForRecover)) {
        setNerve(GET_NERVE(StinkBugParent, StinkBugParentNrvBack));
    }
}

void StinkBugParent::exeShakeStart() {
    if (MR::isFirstStep(this)) {
        MR::startBck(this, "Repel");
    }

    if (MR::isStep(this, sStepForShakeStart)) {
        setNerve(GET_NERVE(StinkBugParent, StinkBugParentNrvShake));
    }
}

void StinkBugParent::exeShake() {
    if (MR::isBckStopped(this)) {
        setNerve(GET_NERVE(StinkBugParent, StinkBugParentNrvBack));
    }
}

void StinkBugParent::exeDemoPrepare() {
    if (MR::tryStartDemoRegisteredMarioPuppetable(this, nullptr)) {
        setNerve(GET_NERVE(StinkBugParent, StinkBugParentNrvDemoChildDown));
    }
}

void StinkBugParent::exeDemoChildDown() {
    if (MR::isFirstStep(this)) {
        MR::startBck(this, "ChildDeath");
        MR::startBva(this, "ChildDeath");
        MR::startBrk(this, "ChildDeath");
        MR::startSound(this, "SE_EV_STINKBUG_S_STOMPED");
        MR::startSound(this, "SE_EM_STINKBUG_STOMPED_S");
        mIsChildAlive = false;
        MR::invalidateCollisionParts(this);
        MR::validateCollisionParts(mParentOnlyParts);
    }

    if (MR::isStep(this, sStepToJumpBackPlayer)) {
        jumpBackPlayerFromChild();
    }

    if (MR::isStep(this, sStepForChildDeathSound)) {
        MR::startSound(this, "SE_EM_EXPLODE_S");
    }

    if (MR::isStep(this, sStepForSurpriseSound)) {
        MR::startSound(this, "SE_EV_STINKBUG_L_SURPRISE");
    }

    mVelocity.zero();
}

void StinkBugParent::exeDemoAnger() {
    if (MR::isFirstStep(this)) {
        MR::overlayWithPreviousScreen(2);
        MR::startBck(this, "Anger");
        MR::startBrk(this, "Anger");
        MR::resetPosition(this, mInitPos);
        MR::setPlayerPosAndWait(cDemoEndPointName);
        // Without the point the bug turns to its home.
        Vec3f demoEndPos(mInitPos);
        MR::findNamePos(cDemoEndPointName, &demoEndPos, nullptr);
        MR::turnDirectionToTargetUseGroundNormalDegree(this, &mFront, demoEndPos, 180.0f);
    }

    MR::startLevelSound(this, "SE_EV_LV_STINKBUG_L_ANGRY");
    MR::startLevelSound(this, "SE_EM_LV_STINKBUG_L_ANGRY");

    if (MR::isBckStopped(this)) {
        // Star pieces burst ahead and up.
        Vec3f piecePos;
        piecePos.scaleAdd(200.0f, mFront, mPosition);
        Vec3f pieceDir;
        pieceDir.scaleAdd(-1.0f, mGravity, mFront);
        MR::normalize(&pieceDir);
        MR::appearStarPieceToDirection(this, piecePos, pieceDir, 8, 25.0f, 40.0f, false);
        MR::startSound(this, "SE_OJ_STAR_PIECE_BURST_F");
        MR::validateHitSensors(this);
        setNerve(GET_NERVE(StinkBugParent, StinkBugParentNrvWait));
        MR::startSound(this, "SE_EM_STINKBUG_L_ANGRY_END");
    }
}

void StinkBugParent::kill() {
    MR::startSound(this, "SE_EM_EXPLODE_S");
    MR::requestAppearPowerStar(this, mPosition);
    LiveActor::kill();
}

void StinkBugParent::control() {
    if (!mIsChildAlive) {
        mParentOnlyParts->setMtx();
    }

    if (!isNrvMoving()) {
        MR::offBind(this);
        return;
    }

    // Moves along the ground and falls.
    MR::onBind(this);
    mVelocity.scaleAdd(-mGravity.dot(mVelocity), mGravity, mVelocity);
    mVelocity.scaleAdd(2.0f, mGravity, mVelocity);
}

void StinkBugParent::calcAndSetBaseMtx() {
    Pos3f mtx;
    MR::calcMtxFromGravityAndZAxis(&mtx, this, mGravity, mFront);
    MR::setBaseTRMtx(this, mtx);
}

void StinkBugParent::attackSensor(HitSensor* pSender, HitSensor* pReceiver) {
    if (isNerve(GET_NERVE(StinkBugParent, StinkBugParentNrvHipDropDown)) || isNrvDemo() || isNerve(GET_NERVE(StinkBugParent, StinkBugParentNrvAttack)) ||
        !MR::isSensorPlayer(pReceiver)) {
        return;
    }

    if (MR::isSensorEnemyAttack(pSender)) {
        if (!isHitHorn(pSender, pReceiver, sHornMargin)) {
            return;
        }

        if (MR::sendMsgEnemyAttackStrong(pReceiver, pSender)) {
            MR::emitEffectHitBetweenSensors(this, pSender, pReceiver, 0.0f, nullptr);
            if (!isNerve(GET_NERVE(StinkBugParent, StinkBugParentNrvDash))) {
                setNerve(GET_NERVE(StinkBugParent, StinkBugParentNrvAttack));
                return;
            }
        }
    }

    // Shaking flips off a player standing on its back.
    if (MR::isSensorEye(pSender) && isShakeChance() && !MR::isPlayerHipDropFalling()) {
        MR::sendMsgEnemyAttackFlipToDir(pReceiver, pSender, mFront);
    }
}

bool StinkBugParent::receiveMsgPlayerAttack(u32 msg, HitSensor* /*pSender*/, HitSensor* pReceiver) {
    if (isNerve(GET_NERVE(StinkBugParent, StinkBugParentNrvHipDropDown)) || isNrvDemo()) {
        return false;
    }

    if (MR::isMsgLockOnStarPieceShoot(msg)) {
        return true;
    }

    if (MR::isMsgStarPieceReflect(msg) && pReceiver == getSensor("body")) {
        if (isNrvEnableStarPieceAttack()) {
            setNerve(GET_NERVE(StinkBugParent, StinkBugParentNrvSpinReaction));
        }
        return true;
    }

    // A hip drop on the child starts the child's demo; once it is gone, one on the parent beats it.
    if (MR::isMsgPlayerHipDropFloor(msg)) {
        if (!mIsChildAlive) {
            setNerve(GET_NERVE(StinkBugParent, StinkBugParentNrvHipDropDown));
        } else if (isHitChild()) {
            setNerve(GET_NERVE(StinkBugParent, StinkBugParentNrvDemoPrepare));
        } else {
            return false;
        }
        return true;
    }

    if (!isNerve(GET_NERVE(StinkBugParent, StinkBugParentNrvSpinReaction)) && !isNerve(GET_NERVE(StinkBugParent, StinkBugParentNrvDash)) &&
        MR::isMsgPlayerSpinAttack(msg) && pReceiver == getSensor("body")) {
        setNerve(GET_NERVE(StinkBugParent, StinkBugParentNrvSpinReaction));
        return true;
    }

    return false;
}

bool StinkBugParent::receiveOtherMsg(u32 msg, HitSensor* /*pSender*/, HitSensor* /*pReceiver*/) {
    if (isNerve(GET_NERVE(StinkBugParent, StinkBugParentNrvHipDropDown)) || isNrvDemo() || isNerve(GET_NERVE(StinkBugParent, StinkBugParentNrvPanic)) ||
        isNerve(GET_NERVE(StinkBugParent, StinkBugParentNrvShakeStart)) || isNerve(GET_NERVE(StinkBugParent, StinkBugParentNrvShake))) {
        return false;
    }

    if (MR::isMsgFloorTouch(msg) && MR::isOnPlayer(getSensor("body"))) {
        setNerve(GET_NERVE(StinkBugParent, StinkBugParentNrvPanic));
        return true;
    }

    return false;
}

// Throws the player toward the demo's end point, away from the child.
void StinkBugParent::jumpBackPlayerFromChild() const {
    // Without the point the player is thrown from where the bug stands.
    Vec3f demoEndPos(mPosition);
    MR::findNamePos(cDemoEndPointName, &demoEndPos, nullptr);
    Vec3f toEnd;
    toEnd.sub(demoEndPos, *MR::getPlayerPos());
    Vec3f toEndDir;
    MR::normalize(toEnd, &toEndDir);
    Vec3f flyDir = -toEndDir;
    MR::offBind(MR::getPlayerDemoActor());
    MR::unlockPlayerAnimation();
    MR::forceFlyPlayer(demoEndPos, flyDir, 0);
}

bool StinkBugParent::isShakeChance() const {
    if (isNerve(GET_NERVE(StinkBugParent, StinkBugParentNrvHipDropDown)) || isNrvDemo() || !isNerve(GET_NERVE(StinkBugParent, StinkBugParentNrvShake))) {
        return false;
    }

    if (MR::isGreaterStep(this, sStepForEnableShake)) {
        return false;
    }

    return MR::isOnPlayer(getSensor("body"));
}

// The player is over the child: within its radius of the child's centre, across the up axis.
bool StinkBugParent::isHitChild() const {
    if (!mIsChildAlive) {
        return false;
    }

    Vec3f childPos;
    MR::copyJointPos(this, "ChildCenter", &childPos);
    Vec3f toPlayer;
    toPlayer.sub(*MR::getPlayerPos(), childPos);
    Vec3f up;
    MR::calcUpVec(&up, this);
    f32 height = up.dot(toPlayer);
    Vec3f across;
    across.scaleAdd(-height, up, toPlayer);
    return across.length() <= sChildRadius;
}

bool StinkBugParent::isNrvEnableStarPieceAttack() const {
    return isNerve(GET_NERVE(StinkBugParent, StinkBugParentNrvWait)) || isNerve(GET_NERVE(StinkBugParent, StinkBugParentNrvSearch)) ||
           isNerve(GET_NERVE(StinkBugParent, StinkBugParentNrvDashSign)) || isNerve(GET_NERVE(StinkBugParent, StinkBugParentNrvDashSignEnd)) ||
           isNerve(GET_NERVE(StinkBugParent, StinkBugParentNrvBack));
}

bool StinkBugParent::isNrvMoving() const {
    return isNerve(GET_NERVE(StinkBugParent, StinkBugParentNrvDash)) || isNerve(GET_NERVE(StinkBugParent, StinkBugParentNrvBack));
}

bool StinkBugParent::isNrvDemo() const {
    return isNerve(GET_NERVE(StinkBugParent, StinkBugParentNrvDemoPrepare)) || isNerve(GET_NERVE(StinkBugParent, StinkBugParentNrvDemoChildDown)) ||
           isNerve(GET_NERVE(StinkBugParent, StinkBugParentNrvDemoAnger));
}
