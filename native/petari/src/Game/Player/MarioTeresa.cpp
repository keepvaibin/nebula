#include "Game/Player/MarioTeresa.hpp"
#include "Game/Animation/XanimePlayer.hpp"
#include "Game/Animation/XanimeResource.hpp"
#include "Game/LiveActor/ModelManager.hpp"
#include "Game/Map/HitInfo.hpp"
#include "Game/MapObj/BigFanHolder.hpp"
#include "Game/Player/Mario.hpp"
#include "Game/Player/MarioActor.hpp"
#include "Game/Player/MarioAnimator.hpp"
#include "Game/Player/MarioConst.hpp"
#include "Game/Player/MarioParts.hpp"
#include "Game/Util/EffectUtil.hpp"
#include "Game/Util/LiveActorUtil.hpp"
#include "Game/Util/MapUtil.hpp"
#include "Game/Util/MathUtil.hpp"
#include "Game/Util/ModelUtil.hpp"
#include "Game/Util/SoundUtil.hpp"
#include "Game/Util/StringUtil.hpp"

void Mario::startTeresaMode() {
    mTeresaWallThroughTimer = 0;
    mAirControlInhibitTimer = 0;
    getPlayer()->mMovementStates.jumping = true;
    getPlayer()->mMovementStates.onGround = false;
    getPlayer()->mMovementStates.inputLocked = false;
    mJumpVec.zero();
    changeStatus(mTeresa);
}

void MarioTeresa::updateDropFlag() {
    mDropping = 0;
    if (mVelocity.dot(getAirGravityVec()) > 1.0f) {
        Vec3f horizontal;
        f32 vertical = MR::vecKillElement(mVelocity, getAirGravityVec(), &horizontal);
        if (MR::isNearZero(getStickP()) && horizontal.length() < vertical) {
            mDropping = 1;
        }
    }
}

bool Mario::getHitWallNorm(Vec3f* pNormal) {
    if (getPlayer()->mMovementStates.frontWall) {
        if (!isThroughWall(mFrontWallTriangle)) {
            *pNormal = *mFrontWallTriangle->getNormal(0);
            return true;
        }
    } else if (getPlayer()->mMovementStates.backWall) {
        if (!isThroughWall(mBackWallTriangle)) {
            *pNormal = *mBackWallTriangle->getNormal(0);
            return true;
        }
    } else if (getPlayer()->mMovementStates.sideWall && !isThroughWall(mSideWallTriangle)) {
        *pNormal = *mSideWallTriangle->getNormal(0);
        return true;
    }

    return false;
}

void Mario::resetTeresaMode() {
    mActor->mBindOn = true;
    mTeresaWallThroughTimer = 0;
    mTeresa->resetTeresaMode();
}

void MarioTeresa::resetTeresaMode() {
    mReflectTimer = 0;
    mAccelTimer = 0;
    mWallReflectTimer = 0;
    mPushTimer = 0;
    mVelocity.zero();
    mWind.zero();
}

bool MarioTeresa::isTeresaAccel() const {
    return mAccelTimer != 0;
}

MarioTeresa::MarioTeresa(MarioActor* pActor) : MarioState(pActor, MarioStatus_Teresa) {
    mPrevJumpVec.zero();
    mHeight = 0.0f;
    mWaitHeight = 0.0f;
    mDropping = 0;
    resetTeresaMode();
}

bool MarioTeresa::close() {
    return true;
}

bool MarioTeresa::update() {
    if (getPlayerMode() != PlayerMode_Teresa) {
        return false;
    }

    if (getPlayer()->isStatusActive(MarioStatus_Recovery)) {
        return true;
    }

    if (mActor->mInDemo) {
        return true;
    }

    mPrevJumpVec = getJumpVec();
    if (!getPlayer()->mTeresaWallThroughTimer) {
        mDropping = 0;
    }

    getPlayer()->mMovementStates.hipDrop = false;
    mHeight = checkHeight();
    mWaitHeight = mActor->getConst().getTable()->mTeresaWaitHeight;
    checkAccel();
    checkWind();
    checkGroundReflect();
    checkWallCeilReflect();
    addVelocity(mVelocity);
    procNoControl();
    procNearGroundControl();
    procAirControl();
    procDrop();
    Mario* player = getPlayer();
    player->mAirTimer = 1;
    procControl();
    f32 speed = mPrevJumpVec.length() / 10.0f;
    if (speed < 0.01f) {
        speed = 0.0f;
    }

    getPlayer()->mWalkSpeed = MR::clamp(speed, 0.0f, 1.0f);
    setJumpVec(mPrevJumpVec);
    if (!mReflectTimer) {
        getPlayer()->playSoundTeresaFlying();
    }

    return true;
}

f32 MarioTeresa::checkHeight() {
    f32 offset = 0.0f;
    f32 height = getPlayer()->mVerticalSpeed;
    MarioActor* actor;
    while (offset < 2.0f * (actor = mActor)->getConst().getTable()->mTeresaWaitHeight) {
        const Vec3f& position = actor->mCenterPos;
        bool found = false;
        f32 radius = 120.0f;
        s32 count = Collision::checkStrikeBallToMapWithThickness(position + getGravityVec() * offset, radius, radius, nullptr, nullptr);
        for (u32 i = 0; i < count; i++) {
            const HitInfo* hit = Collision::getStrikeInfoMap(i);
            Vec3f normal(*hit->mParentTriangle.getNormal(0));
            Vec3f direction(hit->mHitPos);
            direction.sub(direction, position);
            f32 distance = direction.length() - 110.0f;
            if (distance < 0.0f) {
                distance = 0.0f;
            }

            MR::normalizeOrZero(&direction);
            if (normal.dot(getGravityVec()) < -0.1f && direction.dot(getGravityVec()) > 0.1f) {
                if (height > distance) {
                    height = distance;
                }

                found = true;
            }
        }

        if (found) {
            break;
        }

        offset += radius;
    }

    return height;
}

void MarioTeresa::checkAccel() {
    if (checkTrgA()) {
        if (!mAccelTimer) {
            getPlayer()->mHitStates.teresaAccel = true;
        }

        mAccelTimer = mActor->getConst().getTable()->mTeresaAccelTime;
    }

    if (mAccelTimer) {
        mAccelTimer--;
    }
}

void MarioTeresa::checkGroundReflect() {
    if (getPlayer()->mMovementStates.onGround) {
        getPlayer()->mMovementStates.jumping = false;
    }

    if (!getPlayer()->mMovementStates.jumping && getPlayer()->mLandTimer) {
        getPlayer()->mDrawStates.onSlipFloor = false;
        getPlayer()->tryJump();
        cutGravityElementFromJumpVec(true);
        f32 vertical = MR::vecKillElement(mVelocity, getAirGravityVec(), &mVelocity);
        if (vertical < 0.0f) {
            mVelocity += getAirGravityVec() * vertical;
        }
    }
}

void MarioTeresa::procNoControl() {
    if (mWallReflectTimer) {
        mWallReflectTimer--;
        mPushTimer = 0;
        mVelocity.scale(mActor->getConst().getTable()->mTeresaWallReflectReduction);
    }

    if (MR::isNearZero(getStickP())) {
        mVelocity.x *= 0.99f;
        mVelocity.y *= 0.99f;
        mVelocity.z *= 0.99f;
    }
}

void MarioTeresa::procNearGroundControl() {
    if (MR::isNearZero(getVelocity())) {
        return;
    }

    if (calcAngleD(getPlayer()->getShadowNorm()) < 60.0f) {
        if (!(mHeight < 50.0f + mWaitHeight)) {
            return;
        }

        if (mWallReflectTimer) {
            return;
        }

        if (mVelocity.dot(getPlayer()->getShadowNorm()) >= 0.0f) {
            return;
        }

        f32 speed = mVelocity.length();
        Vec3f direction(mVelocity);
        if (MR::normalizeOrZero(&direction)) {
            return;
        }

        Vec3f side;
        side.cross(direction, getPlayer()->getShadowNorm());
        direction.cross(getPlayer()->getShadowNorm(), side);
        direction.setLength(speed);
        f32 blend = MR::clamp((50.0f + mWaitHeight - mHeight) / 50.0f, 0.0f, 1.0f);
        blend *= 0.5f;
        f32 reduction = MR::diffAngleAbs(mVelocity, direction) / 3.1415927f;
        MR::vecBlendSphere(mVelocity, direction, &mVelocity, blend);
        mVelocity.scale(1.0f - reduction);
    }
}

void MarioTeresa::procDrop() {
    MarioActor* actor = mActor;
    bool descending = actor->getLastMove().dot(getGravityVec()) >= 0.0f;
    f32 ratio = 1.0f;
    bool nearGround = mHeight < mWaitHeight + mActor->getConst().getTable()->mTeresaDropDownHeight;
    if (descending) {
        if (nearGround) {
            ratio = (mHeight - mWaitHeight) / mActor->getConst().getTable()->mTeresaDropDownHeight;
            if (ratio < 0.0f) {
                ratio = 0.0f;
            }
        }
    } else {
        if (nearGround) {
            ratio = 0.0f;
        }

        if (nearGround) {
            ratio = 0.0f;
        }
    }

    if (mHeight > mWaitHeight) {
        f32 distance = mHeight - mWaitHeight;
        f32 ratio = 1.0f;
        if (distance < 100.0f) {
            ratio = distance / 100.0f;
        }

        if (mDropping) {
            addTeresaVerticalVelocity(0.25f * ratio);
        } else {
            addTeresaVerticalVelocity(0.1f * ratio);
        }

        f32 vertical = MR::vecKillElement(mVelocity, getAirGravityVec(), &mVelocity);
        if (1.5f * vertical > mHeight - mWaitHeight) {
            vertical *= 0.75f;
        }

        mVelocity += getAirGravityVec() * vertical;
    } else {
        f32 vertical = MR::vecKillElement(mVelocity, getAirGravityVec(), &mVelocity);
        if (vertical < 0.0f) {
            mVelocity += getAirGravityVec() * vertical;
        }
    }

    if (mHeight > 10.0f + mWaitHeight) {
        Vec3f horizontal;
        MarioActor* actor = mActor;
        f32 vertical = MR::vecKillElement(actor->getLastMove(), getGravityVec(), &horizontal);
        f32 brake;
        if (vertical > -0.5f) {
            f32 dropSpeed = vertical;
            dropSpeed += 0.5f;
            brake = dropSpeed / mActor->getConst().getTable()->mTeresaDropBase;
            brake += 0.04f;
        } else {
            brake = mActor->getConst().getTable()->mTeresaRisingBrake;
        }

        MR::clamp(brake, 0.0f, 1.0f);
        getPlayer()->mDrawStates.teresaDrop = true;
    }
}

void MarioTeresa::addTeresaVerticalVelocity(f32 amount) {
    mVelocity += getAirGravityVec() * amount;
    Vec3f horizontal;
    f32 vertical = MR::vecKillElement(mVelocity, getAirGravityVec(), &horizontal);
    f32 minimum = -mActor->getConst().getTable()->mTeresaRiseSpeedMax;
    f32 maximum = mActor->getConst().getTable()->mTeresaDropSpeedMax;
    minimum = 10.0f * minimum;
    maximum = 20.0f * maximum;
    if (mDropping) {
        f32 angle = 3.1415927f * (static_cast< f32 >(getPlayer()->mTeresaWallThroughTimer) / mActor->getConst().getTable()->mTeresaWallThroughTime);
        maximum *= 1.0f + 0.5f * Angle::sinRadian(angle);
    }

    if (getPlayer()->mDrawStates.teresaInWind && mWind.dot(getAirGravityVec()) > 0.707f) {
        maximum *= 2.0f;
    }

    vertical = MR::clamp(vertical, minimum, maximum);
    mVelocity = horizontal + getAirGravityVec() * vertical;
}

void MarioTeresa::addTeresaHorizontalVelocity(const Vec3f& rVelocity) {
    Vec3f addition;
    MR::vecKillElement(rVelocity, getAirGravityVec(), &addition);
    mVelocity += addition;
    Vec3f horizontal;
    f32 vertical = MR::vecKillElement(mVelocity, getAirGravityVec(), &horizontal);
    f32 maximum = mActor->getConst().getTable()->mTeresaHorizontalSpeedMax;
    if (getPlayer()->mDrawStates.teresaInWind && mWind.dot(horizontal) > 0.707f) {
        maximum *= 2.0f;
    }

    if (horizontal.length() >= maximum) {
        horizontal.setLength(maximum);
    }

    mVelocity = horizontal + getAirGravityVec() * vertical;
}

void Mario::doTeresaReflection(const Vec3f& rNormal, bool effect) {
    mTeresa->doTeresaReflection(rNormal, effect);
}

void Mario::startTeresaDisappear() {
    mTeresaWallThroughTimer = mActor->getConst().getTable()->mTeresaWallThroughTime;
    playSound("テレサ消える");
    playSound("声トルネード");
    MR::startCSSound("CS_TERESA", nullptr, 0);
    mTeresa->updateDropFlag();
    resetSleepTimer();
    if (mPolygonFloorCode == 5 && mVerticalSpeed < 100.0f) {
        mTeresa->mVelocity += getAirGravityVec() * 10.0f;
    }
}

bool MarioTeresa::start() {
    if (getPlayer()->mJumpKind == 4) {
        Vec3f direction(-getPlayer()->mTurnFrontVec);
        getPlayer()->setFrontVecKeepUp(direction);
    }

    Mario* player = getPlayer();
    player->mInvincibleJump = 0;
    player->mJumpKind = 0;
    getPlayer()->cancelSquatMode();
    changeAnimation("落下", "落下");
    return true;
}

void MarioTeresa::checkWind() {
    Vec3f wind;
    f32 strength;
    BigFanFunction::calcWindInfo(&wind, mActor->mCenterPos, &strength);
    strength = MR::clamp(strength, 0.0f, 10.0f);
    if (strength > 0.0f) {
        playSound("テレサ風に乗る", 50.0f * strength);
    }

    if (!MR::isNearZero(strength)) {
        getPlayer()->mDrawStates.teresaInWind = true;
    }

    wind.scale(strength);
    mWind.x *= 0.94f;
    mWind.y *= 0.94f;
    mWind.z *= 0.94f;
    mWind += wind * 0.1f * 1.5f;
    mVelocity += wind * 0.2f;
    if (mWind.length() > 0.2f) {
        getPlayer()->mDrawStates.teresaInWind = true;
    }
}

void MarioTeresa::checkWallCeilReflect() {
    MarioParts* actor;
    Vec3f normal;
    Vec3f wallNormal;
    bool hit = false;
    if (getPlayer()->calcDistToCeil(false) < 200.0f) {
        normal = getPlayer()->mCeilDistNorm;
        hit = true;
    }

    if (!hit) {
        hit = getPlayer()->getHitWallNorm(&wallNormal);
        normal = wallNormal;
    }

    if (hit) {
        f32 speed = MR::vecKillElement(mVelocity, normal, &mVelocity);
        if (speed < 0.0f) {
            mVelocity += normal * -speed;
        } else {
            mVelocity += normal * speed * 1.5f;
        }

        if (!mReflectTimer) {
            playSound("テレサ壁反射");
            playSound("声壁反射");
            mActor->changeTeresaAnimation("hit", -1);
            actor = mActor->mTeresaModel;
            MR::emitEffectHit(actor, getPlayer()->getWallPos(), getPlayer()->getWallNorm(), "WallHit");
            mActor->mSpinIntervalTimer = 10;
            mWallReflectTimer = mActor->getConst().getTable()->mTeresaWallReflectTime;
            mPrevJumpVec.zero();
            Mario* player = getPlayer();
            player->mTeresaWallThroughTimer = 0;
        }

        mReflectTimer++;
        return;
    }

    mReflectTimer = 0;
}

void MarioTeresa::procAirControl() {
    if (getPlayer()->mTeresaWallThroughTimer < mActor->getConst().getTable()->mTeresaWallThroughTime - 30 && ((isTeresaAccel() && !mWallReflectTimer) || mPushTimer)) {
        if (getPlayer()->mHitStates.teresaAccel) {
            if (mVelocity.dot(getAirGravityVec()) > 0.0f) {
                addTeresaVerticalVelocity(-2.0f);
            } else {
                addTeresaVerticalVelocity(-0.4f);
            }
        } else {
            addTeresaVerticalVelocity(-0.25f);
        }

        if (isTeresaAccel()) {
            playSound("テレサ踏ん張り");
            if (getPlayer()->mHitStates.teresaAccel) {
                mPushTimer = mActor->getConst().getTable()->mTeresaTrgOnPushTime1;
            } else if (mPushTimer < 15) {
                mPushTimer = mActor->getConst().getTable()->mTeresaTrgOnPushTime2;
            }
        }

        if (mPushTimer) {
            mPushTimer--;
        }
    }
}

void MarioTeresa::procControl() {
    if (!mWallReflectTimer) {
        if (!MR::isNearZero(getStickP())) {
            getPlayer()->setFrontVecKeepUp(getWorldPadDir(), mActor->getConst().getTable()->mTeresaAirWalkTurnSpd);
        }

        if (getPlayer()->mAirWalkTimer < (mActor->getConst().getTable()->mAirWalkTime / 2)) {
            getAnimator()->setSpeed(1.5f);
        }

        Vec3f velocity(mPrevJumpVec);
        f32 alignment = 1.0f;
        if (!MR::isNearZero(getStickP())) {
            velocity.dot(getWorldPadDir());
        }

        if (alignment < 0.0f) {
            alignment *= 0.3f;
        }

        f32 acceleration = 0.3f;
        if (getPlayer()->mTeresaWallThroughTimer > (mActor->getConst().getTable()->mTeresaWallThroughTime / 2)) {
            acceleration = 2.0f;
        }

        addTeresaHorizontalVelocity(getWorldPadDir() * alignment * acceleration);
    }

    if (checkTrgZ()) {
        if (mVelocity.dot(getAirGravityVec()) < 0.0f) {
            MR::vecKillElement(mVelocity, getAirGravityVec(), &mVelocity);
        }

        mActor->changeTeresaAnimation("fallquicklystart", -1);
        playSound("声壁押し");
    }
}

void MarioActor::runTeresaBaseAnimation() {
    if (!mMario->isStatusActive(MarioStatus_Wait) && !mTeresaAnimPlayer->isRun("基本")) {
        mTeresaAnimPlayer->changeAnimation("基本");
        mTeresaBlinkTimer = MR::getRandom(60, 180);
        MR::startBtp(mTeresaModel, "blink");
    }
}

void MarioActor::changeTeresaAnimation(const char* pAnimation, s32 interpolation) {
    if (MR::isBckPlaying(mTeresaModel, "sleep")) {
        MR::deleteEffect(mTeresaModel, "Sleep");
    }

    if (interpolation == -1) {
        MR::startBck(mTeresaModel, pAnimation);
    } else {
        MR::startBckWithInterpole(mTeresaModel, pAnimation, interpolation);
    }

    if (MR::isEqualString(pAnimation, "wait") || MR::isEqualString(pAnimation, "run")) {
        mTeresaBlinkTimer = MR::getRandom(60, 180);
        MR::stopBtp(mTeresaModel);
        return;
    }

    mTeresaBlinkTimer = 0;
    if (MR::isExistBtp(mTeresaModel, pAnimation)) {
        MR::startBtp(mTeresaModel, pAnimation);
        return;
    }

    MR::startBtp(mTeresaModel, "blink");
}

void MarioActor::updateTeresaAnimation() {
    bool canChange = true;
    if (MR::isBckPlaying(mTeresaModel, "sleep")) {
        bool moving = false;
        if (mMario->mTeresa->isTeresaAccel()) {
            moving = true;
        }

        if (mMario->getStickP() != 0.0f) {
            moving = true;
        }

        if (mMario->mJumpVec.length() > 1.0f) {
            moving = true;
        }

        if (moving) {
            runTeresaBaseAnimation();
            MR::deleteEffect(mTeresaModel, "Sleep");
        }
    }

    if (MR::isBckOneTimeAndStopped(mTeresaModel)) {
        runTeresaBaseAnimation();
    }

    if (MR::isBckPlaying(mTeresaModel, "hit")) {
        canChange = false;
    }

    if (MR::isBckPlaying(mTeresaModel, "spin") && !mMario->mTeresa->isTeresaAccel()) {
        canChange = false;
    }

    if (canChange) {
        if (mMario->mTeresa->isTeresaAccel()) {
            if (!MR::isBckPlaying(mTeresaModel, "fly") && !MR::isBckPlaying(mTeresaModel, "spin")) {
                changeTeresaAnimation("fly", 16);
            }
        } else if (MR::isBckPlaying(mTeresaModel, "fly") || mTeresaAnimPlayer->isRun("基本")) {
            if (getLastMove().dot(getGravityVec()) >= 1.0f) {
                changeTeresaAnimation("fall", 16);
            } else if (!mMario->mDrawStates.teresaDrop) {
                if (MR::abs(getLastMove().dot(getGravityVec())) < 1.0f) {
                    runTeresaBaseAnimation();
                }
            }
        } else if (MR::isBckPlaying(mTeresaModel, "fall")) {
            if (getLastMove().dot(getGravityVec()) < 1.0f) {
                runTeresaBaseAnimation();
            }
        }

        if (mTeresaAnimPlayer->isRun("基本")) {
            mTeresaFlyWeight = mMario->mJumpVec.length() / 10.0f;
            mTeresaFlyWeight = MR::clamp(mTeresaFlyWeight, 0.0f, 1.0f);
            mTeresaAnimPlayer->changeTrackWeight(0, 1.0f - mTeresaFlyWeight);
            mTeresaAnimPlayer->changeTrackWeight(1, mTeresaFlyWeight);
        }
    }

    if (mMario->mTeresaWallThroughTimer) {
        if (!MR::isBckPlaying(mTeresaModel, "spin")) {
            if (mMario->mTeresaWallThroughTimer > getConst().getTable()->mTeresaWallThroughTime - 3) {
                changeTeresaAnimation("spin", -1);
            }
        }

        if (mTeresaAlpha < getConst().getTable()->mTeresaAlphaLevelMax) {
            mTeresaAlpha += getConst().getTable()->mTeresaAlphaLevelInc;
        }

        mMario->mTeresaWallThroughTimer--;
        if (!mMario->mTeresaWallThroughTimer) {
            playSound("テレサ現れる", -1);
        }
    } else {
        if (mTeresaAlpha > 0.0f) {
            mTeresaAlpha -= getConst().getTable()->mTeresaAlphaLevelDec;
        }

        if (mTeresaAlpha > 0.0f && mMario->mTeresa->mWallReflectTimer) {
            mTeresaAlpha -= getConst().getTable()->mTeresaAlphaLevelDec;
        }
    }

    MR::setBrkFrameAndStop(mTeresaModel, mTeresaAlpha);
    if (mTeresaBlinkTimer) {
        mTeresaBlinkTimer--;
        if (!mTeresaBlinkTimer) {
            mTeresaBlinkTimer = MR::getRandom(90, 240);
            MR::startBtp(mTeresaModel, "blink");
        }
    }
}

void MarioTeresa::doTeresaReflection(const Vec3f& rNormal, bool effect) {
    Vec3f direction(rNormal);
    MR::normalizeOrZero(&direction);
    Vec3f tangent;
    f32 speed = MR::vecKillElement(mVelocity, direction, &tangent);
    if (speed < 0.0f) {
        if (speed > -10.0f) {
            speed = -10.0f;
        }

        Vec3f horizontal;
        f32 vertical = MR::vecKillElement(direction * -speed, getAirGravityVec(), &horizontal);
        addTeresaHorizontalVelocity(horizontal * 2.0f);
        addTeresaVerticalVelocity(2.0f * vertical);
    } else {
        Vec3f horizontal;
        f32 vertical = MR::vecKillElement(direction, getAirGravityVec(), &horizontal);
        addTeresaHorizontalVelocity(horizontal);
        addTeresaVerticalVelocity(vertical);
    }

    mWallReflectTimer = mActor->getConst().getTable()->mTeresaWallReflectTime;
    mPrevJumpVec.zero();
    if (!mReflectTimer) {
        mActor->changeTeresaAnimation("hit", -1);
        playSound("声壁反射");
        if (effect) {
            playSound("テレサ壁反射");
            Mario* player = getPlayer();
            MR::emitEffectHit(mActor->mTeresaModel, getPlayer()->mBinderHitPos, player->mBinderHitNorm, "WallHit");
        }
    }

    Mario* player = getPlayer();
    player->mTeresaWallThroughTimer = 0;
    mActor->mSpinIntervalTimer = 5;
    mReflectTimer++;
}

XanimeGroupInfo teresaAnimeTable[] = {
    {{"基本"}, 1.0f, 16},
    {{""}},
};

XanimeBckTable2 teresaAnime2[] = {
    {{"基本"}, {{"wait", 1.0f}, {"run", 0.0f}}},
    {{""}, {{"", 0.0f}, {nullptr, 0.0f}}},
};

void MarioActor::initTeresaMarioAnimation() {
    mTeresaFlyWeight = 0.0f;
    mTeresaAnimTable = new XanimeResourceTable(MR::getResourceHolder(mTeresaModel), teresaAnimeTable, nullptr, nullptr, nullptr, teresaAnime2, nullptr, nullptr, nullptr);
    mTeresaAnimPlayer = new XanimePlayer(MR::getModel(mTeresaModel), mTeresaAnimTable);
    mTeresaAnimPlayer->setDefaultAnimation("基本");
    mTeresaAnimPlayer->changeAnimation("基本");
    mTeresaModel->mModelManager->mXanimePlayer = mTeresaAnimPlayer;
}
