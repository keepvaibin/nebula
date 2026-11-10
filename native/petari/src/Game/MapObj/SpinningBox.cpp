#include "Game/MapObj/SpinningBox.hpp"
#include "Game/System/NebulaFrameClock.hpp"
#include "Game/LiveActor/HitSensor.hpp"
#include "Game/LiveActor/Nerve.hpp"
#include "Game/Util.hpp"
#include "Game/Util/ActorMovementUtil.hpp"
#include "Game/Util/ActorSensorUtil.hpp"
#include "Game/Util/ActorShadowUtil.hpp"
#include "Game/Util/EffectUtil.hpp"
#include "Game/Util/PlacementUtil.hpp"
#include "Game/Util/LiveActorUtil.hpp"
#include "Game/Util/MapUtil.hpp"
#include "Game/Util/MathUtil.hpp"
#include "Game/Util/ObjUtil.hpp"
#include "Game/Util/PlayerUtil.hpp"
#include "Game/Util/SoundUtil.hpp"
#include "Game/Util/StarPointerUtil.hpp"
#include <nebula_geometry.hpp>
#include "Game/Util/Tunable.hpp"
#include "Game/System/LumaCore.hpp"

NEBULA_TUNABLE_NAMED(f32, cNum_init_f1, "init.f1", 160.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_init_f2, "init.f2", 40.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_init_f3, "init.f3", 140.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_init_f4, "init.f4", 80.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_init_f5, "init.f5", 70.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_init_f6, "init.f6", 110.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_init_f7, "init.f7", 80.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_kill_f1, "kill.f1", 50.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_kill_f2, "kill.f2", 10.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_kill_f3, "kill.f3", 40.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeSliding_f1, "exeSliding.f1", 100.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeSliding_f2, "exeSliding.f2", 100.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeSliding_f3, "exeSliding.f3", 100.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeSpinning_f1, "exeSpinning.f1", 100.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeSpinning_f2, "exeSpinning.f2", 100.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeSpinning_f3, "exeSpinning.f3", 100.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeSpinning_f4, "exeSpinning.f4", 100.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeSpinning_f5, "exeSpinning.f5", 100.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeSpinning_f6, "exeSpinning.f6", 100.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_calcPlanarProjectedVec_f1, "calcPlanarProjectedVec.f1", 0.0001f);

// The numbers this file's functions give the game's motion and animation calls, as tunables: "<file>.<function>.<call>.<k>".
NEBULA_TUNABLE_NAMED(f32, cNum_exeFall_reboundVelocityFromCollision_1, "exeFall.reboundVelocityFromCollision.1", 0.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeFall_reboundVelocityFromCollision_2, "exeFall.reboundVelocityFromCollision.2", 0.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeFall_reboundVelocityFromCollision_3, "exeFall.reboundVelocityFromCollision.3", 1.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeWait_reboundVelocityFromCollision_1, "exeWait.reboundVelocityFromCollision.1", 0.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeWait_reboundVelocityFromCollision_2, "exeWait.reboundVelocityFromCollision.2", 0.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeWait_reboundVelocityFromCollision_3, "exeWait.reboundVelocityFromCollision.3", 1.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeWait_applyVelocityDampAndGravity_1, "exeWait.applyVelocityDampAndGravity.1", 0.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeWait_applyVelocityDampAndGravity_2, "exeWait.applyVelocityDampAndGravity.2", 0.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeSliding_reboundVelocityFromCollision_1, "exeSliding.reboundVelocityFromCollision.1", 0.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeSliding_reboundVelocityFromCollision_2, "exeSliding.reboundVelocityFromCollision.2", 0.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeSliding_reboundVelocityFromCollision_3, "exeSliding.reboundVelocityFromCollision.3", 1.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeSpinning_reboundVelocityFromCollision_1, "exeSpinning.reboundVelocityFromCollision.1", 0.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeSpinning_reboundVelocityFromCollision_2, "exeSpinning.reboundVelocityFromCollision.2", 0.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeSpinning_reboundVelocityFromCollision_3, "exeSpinning.reboundVelocityFromCollision.3", 1.0f);

namespace {
    NEBULA_TUNABLE(f32, hForceBindOffset, 65.0f);
    NEBULA_TUNABLE(f32, hForceBindMul, 0.5f);
    NEBULA_TUNABLE(f32, sShadowSize, 160.0f);
    NEBULA_TUNABLE(f32, hGravityVel, 3.0f);
    NEBULA_TUNABLE(f32, hVelHMin, 10.0f);
    NEBULA_TUNABLE(f32, hReflectRate, 0.5f);
    NEBULA_TUNABLE(f32, hSpinningAttackStrongVel, 10.0f);
    NEBULA_TUNABLE(f32, hStopCheckVel, 0.1f);
    NEBULA_TUNABLE(f32, hGroundCosine, 0.4f);
    static const s32 hWaitTime = NebulaFrameClock::stepsOfMilliseconds(500);
    NEBULA_TUNABLE(f32, hPlayerIsSideDotCheck, 0.25f);
    NEBULA_TUNABLE(f32, hSpinningAngle, 2.0f);
    NEBULA_TUNABLE(f32, hDampVelHWait, 0.6f);
    NEBULA_TUNABLE(f32, hDampVelVWait, 0.6f);
    NEBULA_TUNABLE(f32, hTurnYDirRatio, 0.125f);
    NEBULA_TUNABLE(f32, hPointedRotDamp, 0.8f);
    NEBULA_TUNABLE(f32, hSlidingVelocitySub, 0.08f);
    NEBULA_TUNABLE(f32, hSlidingVelMax, 17.0f);
    NEBULA_TUNABLE(f32, hSlidingRotateSub, 0.01f);
    NEBULA_TUNABLE(f32, hSlidingRotVelMin, 0.02f);
    NEBULA_TUNABLE(f32, hSlidingRotVelMax, 0.02f);
    static const s32 hKickableTime = NebulaFrameClock::stepsOfMilliseconds(500);
    NEBULA_TUNABLE(f32, hSpinningVelocitySub, 0.01f);
    NEBULA_TUNABLE(f32, hSpinningVelMin, 0.0f);
    NEBULA_TUNABLE(f32, hSpinningVelMax, 25.0f);
    NEBULA_TUNABLE(f32, hSpinningRotateSub, 0.001f);
    NEBULA_TUNABLE(f32, hSpinningRotVelMin, 0.1f);
    NEBULA_TUNABLE(f32, hSpinningRotVelMax, 0.5f);
    NEBULA_TUNABLE(f32, hPlayerVelocityCheck, 10.0f);
    NEBULA_TUNABLE(f32, hIceInitBlowVelV, 100.0f);
    NEBULA_TUNABLE(f32, hKickCheckOffsetUp, 0.0f);
    NEBULA_TUNABLE(f32, hIceInitBlowVelH, 0.0f);
};  // namespace

namespace NrvSpinningBox {
    NEW_NERVE(HostTypeNrvFall, SpinningBox, Fall);
    NEW_NERVE_ONEND(HostTypeNrvWait, SpinningBox, Wait, Wait);
    NEW_NERVE_ONEND(HostTypeNrvSliding, SpinningBox, Sliding, Sliding);
    NEW_NERVE_ONEND(HostTypeNrvSpinning, SpinningBox, Spinning, Spinning);
    NEW_NERVE_ONEND(HostTypeNrvPointed, SpinningBox, Pointed, Pointed);
};  // namespace NrvSpinningBox

SpinningBox::SpinningBox(const char* pName)
    : LiveActor(pName), mRotate(0.0f, 0.0f, 0.0f, 1.0f), mRotateSpeed(0.0f), mSlideSpeed(0.0f), mSlideDir(1.0f, 0.0f, 0.0f), mIsOnGround(false),
      mIsIceBox(false), mSpinAngle(0.0f) {
    lumacore::setClass(this, lumacore::kClassId< SpinningBox >);
}

void SpinningBox::init(const TableIter& rIter) {
    if (MR::isValidInfo(rIter)) {
        MR::initDefaultPos(this, rIter);
    }

    if (mIsIceBox) {
        initModelManagerWithAnm("IceBox", nullptr, false);
    } else {
        initModelManagerWithAnm("SpinningBox", nullptr, false);
    }

    MR::connectToSceneMapObjStrongLight(this);
    initNerve(GET_NERVE(SpinningBox, HostTypeNrvFall));

    f32 scale = getScale();
    initHitSensor(2);
    MR::addHitSensorMapObj(this, "body", 32, scale * cNum_init_f1, Vec3f(0.0f, -cNum_init_f2, 0.0f));

    if (mIsIceBox) {
        MR::initCollisionParts(this, "IceBox", getSensor("body"), nullptr);
        setName("アイスボックス");
    } else {
        MR::initCollisionPartsAutoEqualScaleOne(this, "SpinningBox", getSensor("body"), nullptr);
    }

    initBinder(cNum_init_f3, cNum_init_f4, 0);
    MR::setBinderExceptActor(this, this);
    initEffectKeeper(0, "SpinningBox");
    initSound(6, false);

    MR::onCalcGravity(this);
    MR::initShadowVolumeBox(this, Vec3f(::sShadowSize, -cNum_init_f5, ::sShadowSize) * scale, getBaseMtx());
    MR::excludeCalcShadowToMyCollision(this, nullptr);

    if (MR::isValidInfo(rIter)) {
        MR::setGroupClipping(this, rIter, 32);
    }

    MR::addToAttributeGroupReflectSpinningBox(this);
    MR::initStarPointerTarget(this, cNum_init_f6, Vec3f(0.0f, cNum_init_f7, 0.0f));
    MR::declareStarPiece(this, 3);
    if (mIsIceBox) {
        makeActorDead();
    } else {
        MR::startBrk(this, "SpinningBox");
        makeActorAppeared();
    }
}

void SpinningBox::kill() {
    MR::startSound(this, "SE_OJ_SPIN_BOX_BREAK");
    MR::startSound(this, "SE_OJ_STAR_PIECE_BURST");
    LiveActor::kill();
    MR::appearStarPiece(this, mPosition - mGravity * cNum_kill_f1, MR::getDeclareRemnantStarPieceCount(this), cNum_kill_f2, cNum_kill_f3, false);
    MR::emitEffect(this, "Break");
}

void SpinningBox::control() {
    if (MR::isInDeath(this, Vec3f(0.0f, 0.0f, 0.0f)) || MR::isInWater(mPosition)) {
        kill();
        return;
    }

    if (MR::isBindedDamageElectric(this)) {
        kill();
        return;
    }

    Vec3f slideDir = mSlideDir;
    Vec3f norm;
    if (MR::isBindedGround(this)) {
        norm.set(*MR::getGroundNormal(this));
        calcPlanarProjectedVec(&slideDir, norm, mGravity);
    } else {
        norm.set(-mGravity);
        MR::vecKillElement(slideDir, norm, &slideDir);
    }

    MR::normalizeOrZero(&slideDir);
    if (!MR::isNearZero(slideDir)) {
        mSlideDir.set(slideDir);
    }

    Vec3f up;
    mRotate.getYDir(up);
    Quat4f rotY;
    rotY.setRotate(up, norm, ::hTurnYDirRatio);
    mRotate.mult(rotY);
    mRotate.getYDir(up);
    rotY.setRotate(up, -mRotateSpeed);
    mRotate.mult(rotY);
    mRotate.normalize();

    if (MR::isOnPlayer(getSensor("body")) && !isNerve(GET_NERVE(SpinningBox, HostTypeNrvWait))) {
        setNerve(GET_NERVE(SpinningBox, HostTypeNrvWait));
    }
}

void SpinningBox::exeFall() {
    if (MR::isFirstStep(this)) {
        MR::invalidateClipping(this);
    }

    MR::reboundVelocityFromCollision(this, cNum_exeFall_reboundVelocityFromCollision_1, cNum_exeFall_reboundVelocityFromCollision_2, cNum_exeFall_reboundVelocityFromCollision_3);
    MR::applyVelocityDampAndGravity(this, ::hGravityVel, ::hDampVelHWait, ::hDampVelHWait, ::hDampVelVWait, ::hVelHMin);

    if (MR::isOnGround(this) && MR::isGreaterStep(this, ::hWaitTime)) {
        setNerve(GET_NERVE(SpinningBox, HostTypeNrvWait));
    }
}

void SpinningBox::exeWait() {
    if (MR::isFirstStep(this)) {
        mIsOnGround = false;
        mRotateSpeed = 0.0f;
        MR::validateClipping(this);
    }

    if (MR::isStarPointerPointing2POnPressButton(this, "弱", true, false)) {
        setNerve(GET_NERVE(SpinningBox, HostTypeNrvPointed));
        return;
    }

    Vec3f originalGravity = mGravity;
    if (MR::isOnGround(this)) {
        mIsOnGround = true;
        mGravity.set(-*MR::getGroundNormal(this));
    }

    MR::reboundVelocityFromCollision(this, cNum_exeWait_reboundVelocityFromCollision_1, cNum_exeWait_reboundVelocityFromCollision_2, cNum_exeWait_reboundVelocityFromCollision_3);

    if (mIsOnGround) {
        MR::applyVelocityDampAndGravity(this, ::hGravityVel, cNum_exeWait_applyVelocityDampAndGravity_1, cNum_exeWait_applyVelocityDampAndGravity_2, ::hDampVelVWait, ::hVelHMin);
    } else {
        MR::applyVelocityDampAndGravity(this, ::hGravityVel, ::hDampVelHWait, ::hDampVelHWait, ::hDampVelVWait, ::hVelHMin);
    }

    mGravity.set(originalGravity);  // "gravity" was only used to update velocity, revert to initial
}

void SpinningBox::endWait() {
}

void SpinningBox::exeSliding() {
    if (MR::isFirstStep(this)) {
        mRotateSpeed = ::hSlidingRotVelMax;
        MR::invalidateClipping(this);
    }

    if (MR::isStarPointerPointing2POnPressButton(this, "弱", true, false)) {
        setNerve(GET_NERVE(SpinningBox, HostTypeNrvPointed));
        return;
    }

    mRotateSpeed = MR::clamp(mRotateSpeed - ::hSlidingRotateSub, ::hSlidingRotVelMin, ::hSlidingRotVelMax);
    mSlideSpeed = MR::clamp(mSlideSpeed - ::hSlidingVelocitySub, 0.0f, ::hSlidingVelMax);
    f32 slideSoundLvl = (mSlideSpeed / ::hSlidingVelMax) * cNum_exeSliding_f1;
    if (slideSoundLvl > cNum_exeSliding_f2) {
        slideSoundLvl = cNum_exeSliding_f3;
    }
    MR::startLevelSound(this, "SE_OJ_LV_SPIN_BOX_SLIDE", slideSoundLvl);

    if (mSlideSpeed <= ::hStopCheckVel) {
        setNerve(GET_NERVE(SpinningBox, HostTypeNrvWait));
        return;
    }

    MR::reboundVelocityFromCollision(this, cNum_exeSliding_reboundVelocityFromCollision_1, cNum_exeSliding_reboundVelocityFromCollision_2, cNum_exeSliding_reboundVelocityFromCollision_3);

    MR::vecKillElement(mVelocity, mSlideDir, &mVelocity);
    mVelocity = mSlideDir * mSlideSpeed;

    if (!MR::isOnGround(this)) {
        MR::forceBindOnGround(this, ::hForceBindOffset, ::hForceBindMul);
    }

    Vec3f norm;
    if (MR::isBindedWallOrSlopeGround(this, ::hGroundCosine, &norm)) {
        MR::startSound(this, "SE_OJ_SPIN_BOX_BOUND");
        MR::calcReflectionVector(&mSlideDir, norm, 1.0f, 0.0f);
        mSlideSpeed *= ::hReflectRate;
        MR::normalizeOrZero(&mSlideDir);
    }
}

void SpinningBox::endSliding() {
    MR::deleteEffect(this, "Blur");
}

void SpinningBox::exeSpinning() {
    if (MR::isFirstStep(this)) {
        MR::emitEffect(this, "Blur");
        mSpinAngle = 0.0f;
        MR::invalidateClipping(this);
    }

    if (MR::isStarPointerPointing2POnPressButton(this, "弱", true, false)) {
        setNerve(GET_NERVE(SpinningBox, HostTypeNrvPointed));
        return;
    }

    mRotateSpeed = MR::clamp(mRotateSpeed - ::hSpinningRotateSub, ::hSpinningRotVelMin, ::hSpinningRotVelMax);
    mSlideSpeed = MR::clamp(mSlideSpeed - ::hSpinningVelocitySub, ::hSpinningVelMin, ::hSpinningVelMax);
    mSpinAngle += mRotateSpeed;

    if (mSpinAngle > ::hSpinningAngle) {
        while (mSpinAngle > ::hSpinningAngle) {
            mSpinAngle -= ::hSpinningAngle;
        }

        f32 spinSoundLvl = (mRotateSpeed / ::hSpinningRotVelMax) * cNum_exeSpinning_f1;
        if (spinSoundLvl > cNum_exeSpinning_f2) {
            spinSoundLvl = cNum_exeSpinning_f3;
        }
        MR::startSound(this, "SE_OJ_SPIN_BOX_SPIN", spinSoundLvl);
    }

    f32 slideSoundLvl = (mSlideSpeed / ::hSlidingVelMax) * cNum_exeSpinning_f4;
    if (slideSoundLvl > cNum_exeSpinning_f5) {
        slideSoundLvl = cNum_exeSpinning_f6;
    }
    MR::startLevelSound(this, "SE_OJ_LV_SPIN_BOX_SLIDE", slideSoundLvl);

    if (mSlideSpeed <= ::hStopCheckVel) {
        setNerve(GET_NERVE(SpinningBox, HostTypeNrvWait));
        return;
    }

    MR::reboundVelocityFromCollision(this, cNum_exeSpinning_reboundVelocityFromCollision_1, cNum_exeSpinning_reboundVelocityFromCollision_2, cNum_exeSpinning_reboundVelocityFromCollision_3);

    MR::vecKillElement(mVelocity, mSlideDir, &mVelocity);
    mVelocity = mSlideDir * mSlideSpeed;

    if (!MR::isOnGround(this)) {
        MR::forceBindOnGround(this, ::hForceBindOffset, ::hForceBindMul);
    }

    if (MR::isBindedWallOrSlopeGround(this, ::hGroundCosine, nullptr)) {
        kill();
    }
}

void SpinningBox::endSpinning() {
    MR::deleteEffect(this, "Blur");
}

void SpinningBox::exePointed() {
    if (MR::isFirstStep(this)) {
        mVelocity.zero();
        MR::startPointerHitSound();
        MR::emitEffect(this, "Touch");
        MR::invalidateClipping(this);
    }

    mRotateSpeed *= ::hPointedRotDamp;
    MR::startPointerFreezeLevelSound(this);

    if (!MR::isStarPointerPointing2POnPressButton(this, "弱", true, false)) {
        setNerve(GET_NERVE(SpinningBox, HostTypeNrvWait));
    }
}

void SpinningBox::endPointed() {
    MR::deleteEffect(this, "Touch");
}

void SpinningBox::calcPlanarProjectedVec(Vec3f* pProj, const Vec3f& rNorm, const Vec3f& rGrav) {
    // No gravity (or none along the ground) leaves the direction as it is.
    const f32 along = rNorm.dot(rGrav);

    if (MR::isNearZero(along, cNum_calcPlanarProjectedVec_f1)) {
        return;
    }

    pProj->add(rGrav * (-rNorm.dot(*pProj) / along));
}

void SpinningBox::calcHitDirection(const Vec3f& rPos) {
    Vec3f hitDir = mPosition - rPos;
    MR::vecKillElement(hitDir, mGravity, &hitDir);
    MR::normalizeOrZero(&hitDir);

    if (MR::isNearZero(hitDir)) {
        return;
    }

    Vec3f velDir = mVelocity;
    MR::normalizeOrZero(&velDir);
    velDir.add(hitDir);
    MR::normalizeOrZero(&velDir);
    if (MR::isNearZero(velDir)) {
        mSlideDir = hitDir;
    } else {
        mSlideDir = velDir;
    }
}

void SpinningBox::generateIceBox(HitSensor* pSender, HitSensor* pReceiver) {
    appear();
    mPosition.set(pReceiver->mHost->mPosition);
    Pos3f mtx = pReceiver->mHost->getBaseMtx();
    Vec3f up, front;
    mtx.getYDir(up);
    mtx.getZDir(front);

    MR::makeQuatFromVec(&mRotate, front, up);

    Vec3f grav = pReceiver->mHost->mGravity;
    Vec3f dir = pReceiver->mPosition - pSender->mPosition;
    MR::vecKillElement(dir, grav, &dir);
    dir.setLength(::hIceInitBlowVelH);
    dir.add((-grav) * (::hIceInitBlowVelV));
    mVelocity.set(dir);

    if (::hPlayerVelocityCheck < MR::getPlayerVelocity()->length()) {
        calcHitDirection(pSender->mPosition);
        setNerve(GET_NERVE(SpinningBox, HostTypeNrvSpinning));
    } else {
        setNerve(GET_NERVE(SpinningBox, HostTypeNrvFall));
    }
}

void SpinningBox::attackSensor(HitSensor* pSender, HitSensor* pReceiver) {
    if (pReceiver->isType(ATYPE_BEGOMAN)) {
        MR::sendMsgEnemyAttack(pReceiver, pSender);
        return;
    }

    if (MR::isSensorPlayer(pReceiver)) {
        if (MR::isPlayerExistSide(this, ::hKickCheckOffsetUp, ::hPlayerIsSideDotCheck) && !MR::isOnPlayer(pSender)) {
            if (isNerve(GET_NERVE(SpinningBox, HostTypeNrvSpinning))) {
                if (MR::isPlayerSwingAction() || !MR::isOnGroundPlayer()) {
                    return;
                }

                bool hit;
                if (::hSpinningAttackStrongVel < mSlideSpeed) {
                    hit = MR::sendMsgEnemyAttackStrong(pReceiver, pSender);
                } else {
                    hit = MR::sendMsgEnemyAttackFlipWeak(pReceiver, pSender);
                }

                if (hit) {
                    hitReflection(pReceiver, pSender);
                } else {
                    MR::sendMsgPush(pReceiver, pSender);
                }

            } else if (isNerve(GET_NERVE(SpinningBox, HostTypeNrvSliding))) {
                MR::sendMsgPush(pReceiver, pSender);
            }
        }
        return;
    }

    if (MR::isSensorMapObj(pReceiver) || MR::isSensorEnemy(pReceiver)) {
        if (isNerve(GET_NERVE(SpinningBox, HostTypeNrvSpinning)) || isNerve(GET_NERVE(SpinningBox, HostTypeNrvSliding))) {
            if (MR::sendMsgToEnemyAttackBlow(pReceiver, pSender)) {
                if (!MR::isExistInAttributeGroupReflectSpinningBox(pReceiver->mHost)) {
                    kill();
                }
            }
        }
    }
}

bool SpinningBox::receiveOtherMsg(u32 msg, HitSensor* /*pSender*/, HitSensor* pReceiver) {
    if (MR::isMsgInhaleBlackHole(msg)) {
        kill();
        return true;
    }

    if (MR::isMsgPlayerKick(msg) && MR::isSensorMapObj(pReceiver)) {
        if (isNerve(GET_NERVE(SpinningBox, HostTypeNrvSpinning))) {
            return false;
        }

        if (isNerve(GET_NERVE(SpinningBox, HostTypeNrvSliding)) && MR::isLessStep(this, ::hKickableTime)) {
            return false;
        }

        if (MR::isOnPlayer(getSensor("body"))) {
            return false;
        }

        if (MR::isPlayerExistSide(this, ::hKickCheckOffsetUp, ::hPlayerIsSideDotCheck)) {
            if (isNerve(GET_NERVE(SpinningBox, HostTypeNrvPointed))) {
                return false;
            }

            MR::getPlayerFrontVec(&mSlideDir);
            mSlideSpeed = ::hSlidingVelMax;
            setNerve(GET_NERVE(SpinningBox, HostTypeNrvSliding));
            return true;
        }
    }

    return false;
}

bool SpinningBox::receiveMsgPlayerAttack(u32 msg, HitSensor* /*pSender*/, HitSensor* pReceiver) {
    if (MR::isSensorEnemyAttack(pReceiver)) {
        return false;
    }

    if (MR::isMsgPlayerHipDrop(msg)) {
        kill();
        return true;
    }

    if (MR::isMsgPlayerHitAll(msg)) {
        if (isNerve(GET_NERVE(SpinningBox, HostTypeNrvFall)) || MR::isOnPlayer(getSensor("body"))) {
            return false;
        }

        MR::getPlayerFrontVec(&mSlideDir);
        MR::vecKillElement(mSlideDir, mGravity, &mSlideDir);
        MR::normalizeOrZero(&mSlideDir);
        mRotateSpeed = ::hSpinningRotVelMax;
        mSlideSpeed = ::hSpinningVelMax;
        MR::startSpinHitSound(this);
        MR::startBlowHitSound(this);
        setNerve(GET_NERVE(SpinningBox, HostTypeNrvSpinning));
        return true;
    }

    return false;
}

bool SpinningBox::receiveMsgEnemyAttack(u32 /*msg*/, HitSensor* pSender, HitSensor* pReceiver) {
    if (MR::isExistInAttributeGroupReflectSpinningBox(pSender->mHost)) {
        if (!isNerve(GET_NERVE(SpinningBox, HostTypeNrvSpinning)) && !isNerve(GET_NERVE(SpinningBox, HostTypeNrvSliding))) {
            Vec3f dir = mPosition - pSender->mPosition;
            MR::normalizeOrZero(&dir);
            MR::vecKillElement(pSender->mHost->mVelocity, dir, &dir);
            MR::separateScalarAndDirection(&mSlideSpeed, &mSlideDir, dir);
            mSlideSpeed *= ::hReflectRate;
            setNerve(GET_NERVE(SpinningBox, HostTypeNrvSliding));
        } else {
            hitReflection(pSender, pReceiver);
        }
        return true;
    }

    return false;
}

void SpinningBox::hitReflection(HitSensor* pSender, HitSensor* pReceiver) {
    Vec3f dir = pReceiver->mPosition - pSender->mPosition;
    MR::normalizeOrZero(&dir);
    if (MR::isNearZero(dir)) {
        return;
    }

    MR::calcReflectionVector(&mSlideDir, dir, 1.0f, 0.0f);
    mSlideSpeed = ::hReflectRate;  // BUG, this should be *=, not =
    MR::normalizeOrZero(&mSlideDir);
}

void SpinningBox::calcAndSetBaseMtx() {
    Pos3f mtx;
    mtx.makeQuat(mRotate);
    mtx.setTrans(mPosition);
    MR::setBaseTRMtx(this, mtx);
}

