#include "Game/Boss/SkeletalFishGuard.hpp"
#include "Game/System/NebulaFrameClock.hpp"
#include "Game/Boss/SkeletalFishBoss.hpp"
#include "Game/Enemy/AnimScaleController.hpp"
#include "Game/LiveActor/EffectKeeper.hpp"
#include "Game/LiveActor/Nerve.hpp"
#include "Game/Map/WaterInfo.hpp"
#include "Game/Util/ActorMovementUtil.hpp"
#include "Game/Util/ActorSensorUtil.hpp"
#include "Game/Util/ActorShadowUtil.hpp"
#include "Game/Util/AreaObjUtil.hpp"
#include "Game/Util/CameraUtil.hpp"
#include "Game/Util/EffectUtil.hpp"
#include "Game/Util/GravityUtil.hpp"
#include "Game/Util/JointUtil.hpp"
#include "Game/Util/LiveActorUtil.hpp"
#include "Game/Util/MapUtil.hpp"
#include "Game/Util/MathUtil.hpp"
#include "Game/Util/ObjUtil.hpp"
#include "Game/Util/PlayerUtil.hpp"
#include "Game/Util/RailUtil.hpp"
#include "Game/Util/SoundUtil.hpp"
#include "Game/Util/StarPointerUtil.hpp"
#include "Game/System/LumaCore.hpp"

namespace {
    NEW_NERVE(SkeletalFishGuardNrvWait, SkeletalFishGuard, Wait);
    NEW_NERVE(SkeletalFishGuardNrvAppear, SkeletalFishGuard, Appear);
    NEW_NERVE(SkeletalFishGuardNrvNormal, SkeletalFishGuard, Normal);
    NEW_NERVE(SkeletalFishGuardNrvApart, SkeletalFishGuard, Apart);
    NEW_NERVE(SkeletalFishGuardNrvFollow, SkeletalFishGuard, Follow);
    NEW_NERVE(SkeletalFishGuardNrvStraight, SkeletalFishGuard, Straight);
    NEW_NERVE(SkeletalFishGuardNrvKill, SkeletalFishGuard, Kill);
    NEW_NERVE_ONEND(SkeletalFishGuardNrvNumb, SkeletalFishGuard, Numb, Numb);

    // The appear flight: out to a point 1000 above and 500 aside, then onto the guard's slot by the boss.
    const s32 cAppearSteps = NebulaFrameClock::stepsOfSeconds(5);
    const s32 cAppearRiseSteps = 99;
    const s32 cAppearJoinSteps = 201;
    const s32 cApartSteps = 80;
    const f32 cMaxSpeed = 22.0f;
    const f32 cAccel = 0.8f;
};  // namespace

SkeletalFishGuard::SkeletalFishGuard(SkeletalFishBoss* pFishBoss, const char* pName)
    : LiveActor(pName), mFishBoss(pFishBoss), mGuardOffset(0.0f, 0.0f, 0.0f), mAppearWaitStep(), mApartVelocity(0.0f, 0.0f, 0.0f), mApartFallSpeed(),
      mAttackDelay(), mSpeed(), mNumbReturnNerve(), mFront(0.0f, 0.0f, 0.0f), mAppearRandomDir(0.0f, 0.0f, 0.0f), mTargetRailPos(0.0f, 0.0f, 0.0f),
      mTargetPos(0.0f, 0.0f, 0.0f), mTargetRailDir(0.0f, 0.0f, 1.0f), mAppearStartPos(0.0f, 0.0f, 0.0f) {
    lumacore::setClass(this, lumacore::kClassId< SkeletalFishGuard >);
    mScaleController = new AnimScaleController(nullptr);
}

void SkeletalFishGuard::init(const TableIter& /*rIter*/) {
    initModelManagerWithAnm("SkeletalFishGuard", nullptr, false);
    MR::addToAttributeGroupSearchTurtle(this);
    MR::connectToSceneEnemy(this);
    initNerve(GET_NERVE_ANON(SkeletalFishGuardNrvWait));
    initSensor();
    MR::initLightCtrl(this);
    MR::initShadowVolumeFlatModel(this, "SkeletalFishGuardShadow");
    initEffectKeeper(1, nullptr);
    MR::addEffect(this, "PointerTouchManual");
    MR::setEffectHostMtx(this, "PointerTouchManual", MR::getJointMtx(this, "Head"));
    initSound(6, false);
    initBinder(120.0f, 0.0f, 0);
    MR::invalidateClipping(this);
    MR::setBinderExceptActor(this, mFishBoss->getHeadActor());
    MR::initStarPointerTargetAtJoint(this, "Joint00", 170.0f, Vec3f(0.0f, 0.0f, 0.0f));
    MR::declareStarPiece(this, 128);
}

void SkeletalFishGuard::initAfterPlacement() {
    calcTransAndFront();
    makeActorDead();
}

void SkeletalFishGuard::appear() {
    LiveActor::appear();
    MR::validateShadow(this, nullptr);
}

// Touching the map breaks the guard, except while it flies apart.
void SkeletalFishGuard::control() {
    mScaleController->updateNerve();

    if (mAttackDelay > 0) {
        mAttackDelay--;
    }

    if (!isNerve(GET_NERVE_ANON(SkeletalFishGuardNrvKill)) && !isNerve(GET_NERVE_ANON(SkeletalFishGuardNrvApart)) && MR::isBinded(this)) {
        setNerve(GET_NERVE_ANON(SkeletalFishGuardNrvKill));
    }
}

// Two cubic segments: start -> turn point (leaving straight, arriving along appearDir), then turn point -> the
// guard's slot cAppearSteps ahead on the rail (arriving at rail speed).
void SkeletalFishGuard::exeAppear() {
    if (MR::isFirstStep(this)) {
        MR::showModel(this);
        MR::startBck(this, "Swim");
        MR::startBrk(this, "Base");
        MR::startSound(this, "SE_BM_SKL_GUARD_APPEAR");
        MR::validateHitSensors(this);
        MR::clearHitSensors(this);
        MR::offBind(this);
        mFront.set(-mGravity);
        mAppearStartPos.set(mPosition);
        // Drawn z, y, x: the console's right-to-left order of the arguments (as the Windows build's), on every platform.
        const f32 dirZ = MR::getRandom(-1.0f, 1.0f);
        const f32 dirY = MR::getRandom(-1.0f, 1.0f);
        const f32 dirX = MR::getRandom(-1.0f, 1.0f);
        mAppearRandomDir.set< f32 >(dirX, dirY, dirZ);
    }

    Vec3f gravity;
    MR::calcGravityVector(this, mAppearStartPos, &gravity, nullptr, nullptr);
    Vec3f turnPoint = mAppearStartPos - gravity * 1000.0f;
    calcTarget(&mTargetRailPos, &mTargetPos, &mTargetRailDir, cAppearSteps - getNerveStep());
    Vec3f appearDir = mAppearRandomDir;
    appearDir.orthogonalize(gravity);
    MR::normalizeOrZero(&appearDir);
    turnPoint += appearDir * 500.0f;
    appearDir += Vec3f(0.0f, 1.0f, 0.0f);
    MR::normalize(&appearDir);
    Vec3f lastPos = mPosition;

    if (getNerveStep() < cAppearRiseSteps) {
        const f32 steps = cAppearRiseSteps;
        f32 rate = getNerveStep() / steps;
        mPosition.cubic(mAppearStartPos, Vec3f(0.0f, 0.0f, 0.0f), appearDir * 10.0f * steps, turnPoint, rate);
    } else {
        const f32 steps = cAppearJoinSteps;
        f32 step = getNerveStep() - cAppearRiseSteps;
        f32 rate = step / steps;
        f32 railSpeed = MR::getRailCoordSpeed(mFishBoss->getCurrentRail());
        mPosition.cubic(turnPoint, appearDir * 10.0f * steps, mTargetRailDir * railSpeed * steps, mTargetPos, rate);
    }

    Vec3f move = mPosition - lastPos;

    if (!MR::isNearZero(move)) {
        mFront = move;
        MR::normalize(&mFront);
    }

    MR::calcGravity(this);
    MR::startLevelSound(this, "SE_BM_LV_SKL_GUARD_SWIM_NORMAL");
    MR::setNerveAtStep(this, GET_NERVE_ANON(SkeletalFishGuardNrvNormal), cAppearSteps);
}

void SkeletalFishGuard::exeNormal() {
    if (MR::isFirstStep(this)) {
        MR::onBind(this);
    }

    calcTransAndFront();
    MR::calcGravity(this);
    MR::startLevelSound(this, "SE_BM_LV_SKL_GUARD_SWIM_NORMAL");
    tryShiftApart();
}

// Leaves the boss with its last step's velocity: the sideways part brakes to a stop, the part along gravity grows
// with time; the turn toward the player speeds up quadratically.
void SkeletalFishGuard::exeApart() {
    if (MR::isFirstStep(this)) {
        Vec3f side = mApartVelocity.killElement(mGravity);
        mApartFallSpeed = mGravity.dot(mApartVelocity - side);
        mApartVelocity.set(side);
        MR::startBck(this, "Turn");
        MR::startBrk(this, "Attack");
        mAttackDelay = 0;
    }

    f32 rate = (f32)getNerveStep() / (f32)cApartSteps;
    Vec3f brake = mApartVelocity;

    if (!MR::isNearZero(brake)) {
        MR::normalize(&brake);
        brake.setLength(0.3f);

        if (brake.squared() >= mApartVelocity.squared()) {
            mApartVelocity.zero();
        } else {
            mApartVelocity.sub(brake * 0.3f);
        }
    }

    f32 fall = mApartFallSpeed * rate;
    Vec3f lastGravity(mGravity);
    MR::calcGravity(this);
    Quat4f gravityTurn;
    gravityTurn.setRotate(lastGravity, mGravity);
    gravityTurn.transform(mFront);
    gravityTurn.transform(mApartVelocity);
    f32 turnRadian = (rate * (rate * (6 * MR::pi()))) / 180.0f;
    lookToPlayer(turnRadian, turnRadian);
    mPosition += mGravity * fall + mApartVelocity;
    MR::calcGravity(this);
    MR::startLevelSound(this, "SE_BM_LV_SKL_GUARD_SWIM_NORMAL");
    MR::startLevelSound(this, "SE_BM_LV_SKL_GUARD_ALARM", MR::calcDistanceToPlayer(this));
    MR::setNerveAtStep(this, GET_NERVE_ANON(SkeletalFishGuardNrvFollow), cApartSteps);
}

// Homes in: full turn rate beyond 1500 from the player, easing to none at 1000.
void SkeletalFishGuard::exeFollow() {
    if (MR::isFirstStep(this)) {
        mSpeed = 0.0f;
        MR::startBck(this, "Attack");
        MR::startBrk(this, "Attack");
        MR::startSound(this, "SE_BM_SKL_GUARD_ATTACK_START");
    }

    MR::startLevelSound(this, "SE_BM_LV_SKL_GUARD_SWIM_ATTACK");
    MR::startLevelSound(this, "SE_BM_LV_SKL_GUARD_SWIM_NORMAL");
    MR::startLevelSound(this, "SE_BM_LV_SKL_GUARD_ALARM", MR::calcDistanceToPlayer(this));
    mPosition += mFront * mSpeed;
    mSpeed += cAccel;
    if (mSpeed >= cMaxSpeed) {
        mSpeed = cMaxSpeed;
    }

    f32 distance = MR::getPlayerCenterPos()->distance(mPosition);
    f32 rate = MR::clamp((distance - 1000.0f) / 500.0f, 0.0f, 1.0f);
    f32 rest = 1.0f - rate;
    rest *= rest;
    f32 ease = 1.0f - rest;
    f32 yawDegree = 1.5f * ease;
    f32 pitchDegree = ease;
    lookToPlayer(yawDegree * MR::pi() / 180.0f, pitchDegree * MR::pi() / 180.0f);
    MR::calcGravity(this);

    if (!tryShiftNumb(GET_NERVE_ANON(SkeletalFishGuardNrvFollow))) {
        tryShiftStraight();
    }
}

void SkeletalFishGuard::exeStraight() {
    if (MR::isFirstStep(this)) {
        MR::startBrk(this, "AttackNear");
    }

    MR::startLevelSound(this, "SE_BM_LV_SKL_GUARD_SWIM_ATTACK");
    MR::startLevelSound(this, "SE_BM_LV_SKL_GUARD_SWIM_NORMAL");
    MR::startLevelSound(this, "SE_BM_LV_SKL_GUARD_ALARM", MR::calcDistanceToPlayer(this));
    mPosition += mFront * mSpeed;

    mSpeed += cAccel;
    if (mSpeed >= cMaxSpeed) {
        mSpeed = cMaxSpeed;
    }

    MR::calcGravity(this);
    if (!tryShiftNumb(GET_NERVE_ANON(SkeletalFishGuardNrvStraight))) {
        tryShiftKill();
    }
}

void SkeletalFishGuard::exeDefence() {
}

void SkeletalFishGuard::exeKill() {
    if (MR::isFirstStep(this)) {
        MR::invalidateHitSensors(this);
        MR::invalidateShadow(this, nullptr);
        MR::startBck(this, "Break");
        MR::startSound(this, "SE_BM_SKL_GUARD_EXPLODE");
    }

    if (MR::isBckOneTimeAndStopped(this)) {
        MR::hideModel(this);
        kill();
    }
}

// Held by the 2P pointer: frozen until let go, then back to mNumbReturnNerve.
void SkeletalFishGuard::exeNumb() {
    if (MR::isFirstStep(this)) {
        MR::startPointerHitSound();
        MR::stopBck(this);
        mEffectKeeper->forceDeleteEmitterAll();
        MR::emitEffectWithScale(this, "PointerTouchManual", 1.5f, -1);
        mScaleController->startPointerHitVibration();
    }

    MR::startPointerFreezeLevelSound(this);

    if (!MR::isStarPointerPointing2POnPressButton(this, "弱", true, false)) {
        setNerve(mNumbReturnNerve);
    }
}

void SkeletalFishGuard::endNumb() {
    MR::deleteEffect(this, "PointerTouchManual");
    mScaleController->startAnim();
}

void SkeletalFishGuard::appearNaturally() {
    appear();
    setNerve(GET_NERVE_ANON(SkeletalFishGuardNrvWait));
    MR::hideModel(this);
}

void SkeletalFishGuard::appearForce() {
    makeActorAppeared();
    MR::startSound(this, "SE_BM_SKL_GUARD_APPEAR");
    setNerve(GET_NERVE_ANON(SkeletalFishGuardNrvNormal));
    MR::showModel(this);
}

void SkeletalFishGuard::killNaturally() {
    if (!MR::isDead(this)) {
        if (isNerve(GET_NERVE_ANON(SkeletalFishGuardNrvWait))) {
            makeActorDead();
        } else if (!isNerve(GET_NERVE_ANON(SkeletalFishGuardNrvKill))) {
            setNerve(GET_NERVE_ANON(SkeletalFishGuardNrvKill));
        }
    }
}

void SkeletalFishGuard::waitAttack(s32 steps) {
    mAttackDelay = steps;
}

// Front is mFront. While appearing, the rail frame at the target is turned onto mFront; otherwise up is against
// gravity, or (mFront along gravity) last frame's axes turned onto mFront.
void SkeletalFishGuard::calcAndSetBaseMtx() {
    Vec3f scale = mScaleController->mScale * mScale;
    MR::setBaseScale(this, scale);

    if (isNerve(GET_NERVE_ANON(SkeletalFishGuardNrvAppear))) {
        Vec3f up;
        MR::calcGravityVector(this, mTargetPos, &up, nullptr, 0);
        up.negate();
        Vec3f side;
        MR::crossOrPerpendicular(&side, up, mTargetRailDir);
        MR::normalize(&side);
        up.cross(mTargetRailDir, side);
        Quat4f toFront;
        toFront.setRotate(mTargetRailDir, mFront);
        toFront.transform(up);
        toFront.transform(side);
        Pos3f baseMtx;
        baseMtx.setXDir(side);
        baseMtx.setYDir(up);
        baseMtx.setZDir(mFront);
        baseMtx.setTrans(mPosition);
        MR::setBaseTRMtx(this, baseMtx);
    } else {
        Vec3f up = -mGravity;
        Vec3f side = up.cross(mFront);

        if (MR::isNearZero(side)) {
            Pos3f lastMtx;
            lastMtx.setInline(getBaseMtx());
            lastMtx.getXDir(side);
            lastMtx.getYDir(up);
            Vec3f lastFront;
            lastMtx.getZDir(lastFront);
            Quat4f toFront;
            toFront.setRotate(lastFront, mFront);
            toFront.transform(side);
            toFront.transform(up);
        } else {
            MR::normalize(&side);
            up.cross(mFront, side);
        }

        Pos3f baseMtx;
        baseMtx.setXDir(side);
        baseMtx.setYDir(up);
        baseMtx.setZDir(mFront);
        baseMtx.setTrans(mPosition);
        MR::setBaseTRMtx(this, baseMtx);
    }
}

void SkeletalFishGuard::exeWait() {
    MR::setNerveAtStep(this, GET_NERVE_ANON(SkeletalFishGuardNrvAppear), mAppearWaitStep);
}

// Harmless for the first two steps after joining the boss.
void SkeletalFishGuard::attackSensor(HitSensor* pSender, HitSensor* pReceiver) {
    if (!isNerve(GET_NERVE_ANON(SkeletalFishGuardNrvKill)) && (!isNerve(GET_NERVE_ANON(SkeletalFishGuardNrvNormal)) || getNerveStep() >= 2)) {
        if (MR::isSensorPlayer(pReceiver) && MR::sendMsgEnemyAttackStrong(pReceiver, pSender)) {
            setNerve(GET_NERVE_ANON(SkeletalFishGuardNrvKill));
        } else {
            MR::sendMsgPush(pReceiver, pSender);
        }
    }
}

bool SkeletalFishGuard::receiveMsgPlayerAttack(u32 msg, HitSensor* /*pSender*/, HitSensor* /*pReceiver*/) {
    if (isNerve(GET_NERVE_ANON(SkeletalFishGuardNrvKill))) {
        return false;
    }

    if (MR::isMsgStarPieceReflect(msg)) {
        return true;
    }

    if (isNerve(GET_NERVE_ANON(SkeletalFishGuardNrvNormal)) && getNerveStep() < 2) {
        return false;
    }

    if (MR::isMsgJetTurtleAttack(msg)) {
        MR::appearStarPiece(this, mPosition, 3, 10.0f, 40.0f, false);
        MR::startSound(this, "SE_OJ_STAR_PIECE_BURST_W_F");
        setNerve(GET_NERVE_ANON(SkeletalFishGuardNrvKill));
        return true;
    }

    return false;
}

void SkeletalFishGuard::initSensor() {
    initHitSensor(2);
    MR::addHitSensorAtJointEnemy(this, "head", "Joint00", 16, 120.0f, Vec3f(0.0f, 0.0f, 0.0f));
    MR::addHitSensorAtJointEnemy(this, "body", "Joint02", 16, 120.0f, Vec3f(0.0f, 0.0f, 0.0f));
}

// Rides its slot by the boss; mApartVelocity keeps the last step's move for exeApart.
void SkeletalFishGuard::calcTransAndFront() {
    calcTarget(&mTargetRailPos, &mTargetPos, &mTargetRailDir, 0);
    mFront.set(mTargetRailDir);
    mApartVelocity.sub(mTargetPos, mPosition);
    mPosition.set(mTargetPos);
}

// Turns mFront toward rDir off the gravity axis, at most maxRadian.
void SkeletalFishGuard::rotateHorizontal(const Vec3f& rDir, f32 maxRadian) {
    Vec3f dirH = rDir.killElement(mGravity);
    if (!MR::isNearZero(dirH)) {
        MR::normalize(&dirH);
        Vec3f frontH = mFront.killElement(mGravity);
        if (!MR::isNearZero(frontH)) {
            MR::normalize(&frontH);
            turn(&mFront, frontH, dirH, maxRadian);
        }
    }
}

// Pitches mFront toward rDir's angle from gravity, at most maxRadian.
void SkeletalFishGuard::rotateVertical(const Vec3f& rDir, f32 maxRadian) {
    Vec3f axis = mGravity.cross(mFront);

    if (!MR::isNearZero(axis)) {
        MR::normalize(&axis);
        f32 pitch = mGravity.angle(rDir) - mGravity.angle(mFront);

        if (pitch > 0.0f) {
            if (pitch > maxRadian) {
                pitch = maxRadian;
            }
        } else if (pitch < -maxRadian) {
            pitch = -maxRadian;
        }

        Quat4f rotate;
        rotate.setRotate(axis, pitch);
        rotate.transform(mFront);
    }
}

// Attacks when not held back, on screen, in range, and with a clear line to the player and the camera.
bool SkeletalFishGuard::tryShiftApart() {
    if (mAttackDelay > 0) {
        return false;
    }

    if (!isInScreen()) {
        return false;
    }

    if (!isPlayerInAttackRange()) {
        return false;
    }

    if (!isLineOfSightClear()) {
        return false;
    }

    mFishBoss->notifyAttack(this);
    setNerve(GET_NERVE_ANON(SkeletalFishGuardNrvApart));
    return true;
}

// Past the player: stops homing.
bool SkeletalFishGuard::tryShiftStraight() {
    if (mFront.dot(*MR::getPlayerCenterPos() - mPosition) < 0.0f) {
        setNerve(GET_NERVE_ANON(SkeletalFishGuardNrvStraight));
        return true;
    }

    return false;
}

// Out of the water: breaks.
bool SkeletalFishGuard::tryShiftKill() {
    WaterInfo info;
    if (MR::getWaterAreaObj(&info, mPosition)) {
        return false;
    }

    setNerve(GET_NERVE_ANON(SkeletalFishGuardNrvKill));
    return true;
}

// Rotates *pVec by the turn from rFrom to rTo, capped at maxRadian.
void SkeletalFishGuard::turn(Vec3f* pVec, const Vec3f& rFrom, const Vec3f& rTo, f32 maxRadian) {
    Quat4f rotate;

    f32 angle = rFrom.angle(rTo);
    f32 rate = 1.0f;

    if (angle > maxRadian) {
        rate = maxRadian / angle;
    }

    rotate.setRotate(rFrom, rTo, rate);
    rotate.transform(*pVec);
}

void SkeletalFishGuard::lookToPlayer(f32 yawRadian, f32 pitchRadian) {
    Vec3f toPlayer(*MR::getPlayerCenterPos());
    toPlayer -= mPosition;

    if (!MR::isNearZero(toPlayer)) {
        MR::normalize(&toPlayer);
        rotateHorizontal(toPlayer, yawRadian);
        rotateVertical(toPlayer, pitchRadian);
    }
}

// The slot stepsAhead steps along the boss's rail: mGuardOffset.z along it, .x to the side, .y up.
void SkeletalFishGuard::calcTarget(Vec3f* pRailPos, Vec3f* pPos, Vec3f* pRailDir, s32 stepsAhead) {
    f32 coord = MR::getRailCoord(mFishBoss->getCurrentRail());
    f32 speed = MR::getRailCoordSpeed(mFishBoss->getCurrentRail());
    f32 targetCoord = mGuardOffset.z + (coord + (speed * stepsAhead));
    MR::calcRailPosAtCoord(pRailPos, mFishBoss->getCurrentRail(), targetCoord);
    MR::calcRailDirectionAtCoord(pRailDir, mFishBoss->getCurrentRail(), targetCoord);
    Vec3f gravity;
    MR::calcGravityVector(this, *pRailPos, &gravity, nullptr, 0);
    Vec3f side = pRailDir->cross(gravity);
    MR::normalizeOrZero(&side);
    pPos->add(*pRailPos, side * mGuardOffset.x + gravity * -mGuardOffset.y);
}

bool SkeletalFishGuard::isInScreen() const {
    Vec2f screen;
    return MR::calcScreenPosition(&screen, mPosition);
}

// Within 5000, in any direction (the cone is 180 degrees).
bool SkeletalFishGuard::isPlayerInAttackRange() const {
    Vec3f toPlayer = *MR::getPlayerCenterPos() - mPosition;

    if (toPlayer.length() > 5000.0f) {
        return false;
    }

    if (MR::isNearZero(toPlayer)) {
        return false;
    }

    MR::normalize(&toPlayer);
    return !(toPlayer.dot(mFront) < MR::cosDegree(180.0f));
}

// No map between the guard and the player or the camera, and the boss's body (seen across the rail) not between
// the guard and the player.
bool SkeletalFishGuard::isLineOfSightClear() const {
    Vec3f toPlayer = *MR::getPlayerCenterPos() - mPosition;

    if (Collision::checkStrikeLineToMap(mPosition, toPlayer, 0, nullptr, nullptr)) {
        return false;
    }

    if (Collision::checkStrikeLineToMap(mPosition, MR::getCamPos() - mPosition, 0, nullptr, nullptr)) {
        return false;
    }

    Vec3f toRail = mTargetRailPos - mPosition;
    toRail.orthogonalize2(mTargetRailDir);
    toPlayer.orthogonalize2(mTargetRailDir);

    if (!MR::isNearZero(toRail) && !MR::isNearZero(toPlayer)) {
        f32 bodyAngle = MR::asin(mFishBoss->getBodyThickness() / toRail.length());
        if (toRail.angle(toPlayer) < bodyAngle) {
            return false;
        }
    }

    return true;
}

bool SkeletalFishGuard::tryShiftNumb(const Nerve* pReturnNerve) {
    if (MR::isStarPointerPointing2POnPressButton(this, "弱", true, false)) {
        mNumbReturnNerve = pReturnNerve;
        setNerve(GET_NERVE_ANON(SkeletalFishGuardNrvNumb));
        return true;
    }

    return false;
}
