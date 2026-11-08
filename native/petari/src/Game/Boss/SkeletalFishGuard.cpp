#include "Game/Boss/SkeletalFishGuard.hpp"
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

namespace {
    NEW_NERVE(SkeletalFishGuardNrvWait, SkeletalFishGuard, Wait);
    NEW_NERVE(SkeletalFishGuardNrvAppear, SkeletalFishGuard, Appear);
    NEW_NERVE(SkeletalFishGuardNrvNormal, SkeletalFishGuard, Normal);
    NEW_NERVE(SkeletalFishGuardNrvApart, SkeletalFishGuard, Apart);
    NEW_NERVE(SkeletalFishGuardNrvFollow, SkeletalFishGuard, Follow);
    NEW_NERVE(SkeletalFishGuardNrvStraight, SkeletalFishGuard, Straight);
    NEW_NERVE(SkeletalFishGuardNrvKill, SkeletalFishGuard, Kill);
    NEW_NERVE_ONEND(SkeletalFishGuardNrvNumb, SkeletalFishGuard, Numb, Numb);
};  // namespace

SkeletalFishGuard::SkeletalFishGuard(SkeletalFishBoss* pFishBoss, const char* pName)
    : LiveActor(pName), mFishBoss(pFishBoss), mGuardOffset(0.0f, 0.0f, 0.0f), mAppearWaitStep(), mApartVelocity(0.0f, 0.0f, 0.0f), mApartFallSpeed(), mAttackDelay(),
      mSpeed(), mNumbReturnNerve(), mFront(0.0f, 0.0f, 0.0f), mAppearRandomDir(0.0f, 0.0f, 0.0f), mTargetRailPos(0.0f, 0.0f, 0.0f), mTargetPos(0.0f, 0.0f, 0.0f), mTargetRailDir(0.0f, 0.0f, 1.0f),
      mAppearStartPos(0.0f, 0.0f, 0.0f) {
    mScaleController = new AnimScaleController(nullptr);
}

void SkeletalFishGuard::init(const TableIter& rIter) {
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

void SkeletalFishGuard::control() {
    mScaleController->updateNerve();

    if (mAttackDelay > 0) {
        mAttackDelay--;
    }

    if (!isNerve(GET_NERVE_ANON(SkeletalFishGuardNrvKill)) && !isNerve(GET_NERVE_ANON(SkeletalFishGuardNrvApart))) {
        if (MR::isBinded(this)) {
            if (MR::isBindedGround(this)) {
            }

            if (MR::isBindedWall(this)) {
            }

            if (MR::isBindedRoof(this)) {
            }

            setNerve(GET_NERVE_ANON(SkeletalFishGuardNrvKill));
        }
    }
}

/* the stack on this function is not correct
    but the layers of inlines are extremely complex
*/
void SkeletalFishGuard::exeAppear() {
    Vec3f v32;
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
        mAppearRandomDir.set< f32 >(MR::getRandom(-1.0f, 1.0f), MR::getRandom(-1.0f, 1.0f), MR::getRandom(-1.0f, 1.0f));
    }

    MR::calcGravityVector(this, mAppearStartPos, &v32, nullptr, false);
    Vec3f v31 = mAppearStartPos - v32 * 1000.0f;
    calcTarget(&mTargetRailPos, &mTargetPos, &mTargetRailDir, 300 - getNerveStep());
    Vec3f v30 = mAppearRandomDir;
    v30.orthogonalize(v32);
    MR::normalizeOrZero(&v30);
    v31 += v30 * 500.0f;
    v30 += Vec3f(0.0f, 1.0f, 0.0f);
    MR::normalize(&v30);
    Vec3f v29 = mPosition;

    if (getNerveStep() < 99) {
        s32 v7 = getNerveStep();
        s32 max = 0x63;
        f32 scaled = v7 / (f32)max;
        mPosition.cubic(mAppearStartPos, Vec3f(0.0f, 1.0f, 0.0f) * 0.0f * (f32)max, v30 * 10.0f * (f32)max, v31, scaled);
    } else {
        f32 v9 = (getNerveStep() - 0x63);
        s32 max = 0xC9;
        f32 scaled = v9 / (f32)max;
        f32 v11 = MR::getRailCoordSpeed(mFishBoss->getCurrentRail());
        mPosition.cubic(v31, v30 * 10.0f * (f32)max, mTargetRailDir * v11 * (f32)max, mTargetPos, scaled);
    }

    Vec3f v28 = mPosition - v29;

    if (!MR::isNearZero(v28)) {
        mFront = v28;
        MR::normalize(&mFront);
    }

    MR::calcGravity(this);
    MR::startLevelSound(this, "SE_BM_LV_SKL_GUARD_SWIM_NORMAL");
    MR::setNerveAtStep(this, GET_NERVE_ANON(SkeletalFishGuardNrvNormal), 300);
}

void SkeletalFishGuard::exeNormal() {
    if (MR::isFirstStep(this)) {
        MR::onBind(this);
    }

    calcTransAndFront();
    MR::calcGravity(this);
    MR::startLevelSound(this, "SE_BM_LV_SKL_GUARD_SWIM_NORMAL");
    if (tryShiftApart()) {
        return;
    }
}

void SkeletalFishGuard::exeApart() {
    if (MR::isFirstStep(this)) {
        Vec3f v15 = mApartVelocity.killElement(mGravity);
        mApartFallSpeed = mGravity.dot(mApartVelocity - v15);
        mApartVelocity.set(v15);
        MR::startBck(this, "Turn");
        MR::startBrk(this, "Attack");
        mAttackDelay = 0;
    }

    f32 v3 = (f32)getNerveStep() / 80.0f;
    Vec3f v14 = mApartVelocity;

    if (!MR::isNearZero(v14)) {
        MR::normalize(&v14);
        v14.setLength(0.3f);

        f32 v6 = mApartVelocity.squared();
        if (v14.squared() >= v6) {
            mApartVelocity.zero();
        } else {
            mApartVelocity.sub(v14 * 0.3f);
        }
    }

    f32 v7 = mApartFallSpeed * v3;
    Vec3f grav(mGravity);
    MR::calcGravity(this);
    Quat4f quat;
    quat.setRotate(grav, mGravity);
    quat.transform(mFront);
    quat.transform(mApartVelocity);
    lookToPlayer((v3 * (v3 * (6 * MR::pi()))) / 180.0f, ((v3 * (v3 * (6 * MR::pi()))) / 180.0f));
    mPosition += mGravity * v7 + mApartVelocity;
    MR::calcGravity(this);
    MR::startLevelSound(this, "SE_BM_LV_SKL_GUARD_SWIM_NORMAL");
    MR::startLevelSound(this, "SE_BM_LV_SKL_GUARD_ALARM", MR::calcDistanceToPlayer(this));
    MR::setNerveAtStep(this, GET_NERVE_ANON(SkeletalFishGuardNrvFollow), 80);
}

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
    mSpeed += 0.8f;
    if (mSpeed >= 22.0f) {
        mSpeed = 22.0f;
    }

    f32 dist = MR::getPlayerCenterPos()->distance(mPosition);
    f32 rate = MR::clamp((dist - 1000.0f) / 500.0f, 0.0f, 1.0f);
    f32 oneMinusRate = 1.0f - rate;
    oneMinusRate *= oneMinusRate;
    f32 ease = 1.0f - oneMinusRate;
    f32 yawRate = 1.5f * ease;
    f32 pitchRate = ease;
    lookToPlayer(yawRate * MR::pi() / 180.0f, pitchRate * MR::pi() / 180.0f);
    MR::calcGravity(this);

    if (!tryShiftNumb(GET_NERVE_ANON(SkeletalFishGuardNrvFollow))) {
        if (tryShiftStraight()) {
            return;
        }
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

    mSpeed += 0.8f;
    if (mSpeed >= 22.0f) {
        mSpeed = 22.0f;
    }

    MR::calcGravity(this);
    if (!tryShiftNumb(GET_NERVE_ANON(SkeletalFishGuardNrvStraight)) && tryShiftKill()) {
        return;
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

void SkeletalFishGuard::waitAttack(s32 time) {
    mAttackDelay = time;
}

void SkeletalFishGuard::calcAndSetBaseMtx() {
    Vec3f scale = mScaleController->mScale * mScale;
    MR::setBaseScale(this, scale);

    if (isNerve(GET_NERVE_ANON(SkeletalFishGuardNrvAppear))) {
        Vec3f gravityVec;
        MR::calcGravityVector(this, mTargetPos, &gravityVec, nullptr, 0);
        gravityVec.negate();
        Vec3f stack_4C = gravityVec.cross(mTargetRailDir);
        MR::normalize(&stack_4C);
        gravityVec.cross(mTargetRailDir, stack_4C);
        Quat4f quat;
        quat.setRotate(mTargetRailDir, mFront);
        quat.transform(gravityVec);
        quat.transform(stack_4C);
        Pos3f stack_D0;
        stack_D0.setXDir(stack_4C);
        stack_D0.setYDir(gravityVec);
        stack_D0.setZDir(mFront);
        stack_D0.setTrans(mPosition);
        MR::setBaseTRMtx(this, stack_D0);
    } else {
        Vec3f stack_30 = -mGravity;
        Vec3f stack_24 = stack_30.cross(mFront);

        if (MR::isNearZero(stack_24)) {
            MtxPtr mtx = getBaseMtx();
            Pos3f stack_A0;
            stack_A0.setInline(mtx);
            stack_A0.getXDir(stack_24);
            stack_A0.getYDir(stack_30);
            Vec3f stack_18;
            stack_A0.getZDir(stack_18);
            Quat4f stack_8;
            stack_8.setRotate(stack_18, mFront);
            stack_8.transform(stack_24);
            stack_8.transform(stack_30);
        } else {
            MR::normalize(&stack_24);
            stack_30.cross(mFront, stack_24);
        }

        Pos3f stack_70;
        stack_70.setXDir(stack_24);
        stack_70.setYDir(stack_30);
        stack_70.setZDir(mFront);
        stack_70.setTrans(mPosition);
        MR::setBaseTRMtx(this, stack_70);
    }
}

void SkeletalFishGuard::exeWait() {
    MR::setNerveAtStep(this, GET_NERVE_ANON(SkeletalFishGuardNrvAppear), mAppearWaitStep);
}

void SkeletalFishGuard::attackSensor(HitSensor* pSender, HitSensor* pReceiver) {
    if (!isNerve(GET_NERVE_ANON(SkeletalFishGuardNrvKill)) && (!isNerve(GET_NERVE_ANON(SkeletalFishGuardNrvNormal)) || getNerveStep() >= 2)) {
        if (MR::isSensorPlayer(pReceiver) && MR::sendMsgEnemyAttackStrong(pReceiver, pSender)) {
            setNerve(GET_NERVE_ANON(SkeletalFishGuardNrvKill));
        } else {
            MR::sendMsgPush(pReceiver, pSender);
        }
    }
}

bool SkeletalFishGuard::receiveMsgPlayerAttack(u32 msg, HitSensor* pSender, HitSensor* pReceiver) {
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

void SkeletalFishGuard::calcTransAndFront() {
    calcTarget(&mTargetRailPos, &mTargetPos, &mTargetRailDir, 0);
    mFront.set(mTargetRailDir);
    mApartVelocity.sub(mTargetPos, mPosition);
    mPosition.set(mTargetPos);
}

void SkeletalFishGuard::rotateHorizontal(const Vec3f& rA2, f32 scalar) {
    Vec3f v10 = rA2.killElement(mGravity);
    if (!MR::isNearZero(v10)) {
        MR::normalize(&v10);
        Vec3f* v11 = &mFront;
        Vec3f* g = &mGravity;
        Vec3f v9 = mFront.killElement(*g);
        if (!MR::isNearZero(v9)) {
            MR::normalize(&v9);
            turn(v11, v9, v10, scalar);
        }
    }
}

void SkeletalFishGuard::rotateVertical(const Vec3f& rA2, f32 a3) {
    Vec3f v12 = mGravity.cross(mFront);

    if (!MR::isNearZero(v12)) {
        MR::normalize(&v12);
        f32 angle = mGravity.angle(rA2);
        f32 v7 = (angle - mGravity.angle(mFront));

        if (v7 > 0.0f) {
            if (v7 > a3) {
                v7 = a3;
            }
        } else {
            if (v7 < -a3) {
                v7 = -a3;
            }
        }

        Quat4f v10;
        v10.setRotate(v12, v7);
        v10.transform(mFront);
    }
}

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

bool SkeletalFishGuard::tryShiftStraight() {
    if (mFront.dot(*MR::getPlayerCenterPos() - mPosition) < 0.0f) {
        setNerve(GET_NERVE_ANON(SkeletalFishGuardNrvStraight));
        return true;
    }

    return false;
}

bool SkeletalFishGuard::tryShiftKill() {
    WaterInfo info;
    if (MR::getWaterAreaObj(&info, mPosition)) {
        return false;
    }

    setNerve(GET_NERVE_ANON(SkeletalFishGuardNrvKill));
    return true;
}

void SkeletalFishGuard::turn(Vec3f* pA1, const Vec3f& rA2, const Vec3f& rA3, f32 a4) {
    Quat4f quat;

    f32 angle = rA2.angle(rA3);
    f32 v10 = 1.0f;

    if (angle > a4) {
        v10 = (a4 / angle);
    }

    quat.setRotate(rA2, rA3, v10);
    quat.transform(*pA1);
}

void SkeletalFishGuard::lookToPlayer(f32 a2, f32 a3) {
    Vec3f pos(*MR::getPlayerCenterPos());
    pos -= mPosition;

    if (!MR::isNearZero(pos)) {
        MR::normalize(&pos);
        rotateHorizontal(pos, a2);
        rotateVertical(pos, a3);
    }
}

// functionally equiv. just needs stack work
void SkeletalFishGuard::calcTarget(Vec3f* pA1, Vec3f* pA2, Vec3f* pA3, s32 a4) {
    f32 v11 = MR::getRailCoord(mFishBoss->getCurrentRail());
    f32 speed = MR::getRailCoordSpeed(mFishBoss->getCurrentRail());
    f32 v13 = (mGuardOffset.z + (v11 + (speed * a4)));
    MR::calcRailPosAtCoord(pA1, mFishBoss->getCurrentRail(), v13);
    MR::calcRailDirectionAtCoord(pA3, mFishBoss->getCurrentRail(), v13);
    Vec3f v22;
    MR::calcGravityVector(this, *pA1, &v22, nullptr, 0);
    Vec3f v21 = pA3->cross(v22);
    MR::normalizeOrZero(&v21);
    pA2->add(*pA1, v21 * mGuardOffset.x + v22 * -mGuardOffset.y);
}

bool SkeletalFishGuard::isInScreen() const {
    Vec2f screen;
    return MR::calcScreenPosition(&screen, mPosition);
}

// this is the only way I could match this
// TODO fix
bool SkeletalFishGuard::isPlayerInAttackRange() const {
    const Vec3f& rPosition = mPosition;
    Vec3f v7 = *MR::getPlayerCenterPos() - rPosition;

    if (v7.length() > 5000.0f) {
        return false;
    }

    if (MR::isNearZero(v7)) {
        return false;
    }

    MR::normalize(&v7);
    f32 minCos = MR::cosDegree(180.0f);
    const Vec3f& rFront = mFront;
    if (v7.dot(rFront) < minCos) {
        return false;
    }

    return true;
}

bool SkeletalFishGuard::isLineOfSightClear() const {
    Vec3f v12 = *MR::getPlayerCenterPos() - mPosition;

    if (Collision::checkStrikeLineToMap(mPosition, v12, 0, nullptr, nullptr)) {
        return false;
    }

    if (Collision::checkStrikeLineToMap(mPosition, MR::getCamPos() - mPosition, 0, nullptr, nullptr)) {
        return false;
    }

    Vec3f v11 = mTargetRailPos - mPosition;
    v11.orthogonalize2(mTargetRailDir);
    v12.orthogonalize2(mTargetRailDir);


    if (!MR::isNearZero(v11) && !MR::isNearZero(v12)) {
        f64 angle = MR::asin(mFishBoss->getBodyThickness() / v11.length());
        if (v11.angle(v12) < angle) {
            return false;
        }
    }

    return true;
}

bool SkeletalFishGuard::tryShiftNumb(const Nerve* pNerve) {
    if (MR::isStarPointerPointing2POnPressButton(this, "弱", true, false)) {
        mNumbReturnNerve = pNerve;
        setNerve(GET_NERVE_ANON(SkeletalFishGuardNrvNumb));
        return true;
    }

    return false;
}
