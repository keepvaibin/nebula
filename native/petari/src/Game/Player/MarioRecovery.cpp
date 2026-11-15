#include "Game/Player/MarioRecovery.hpp"
#include "Game/Player/Mario.hpp"
#include "Game/Player/MarioActor.hpp"
#include "Game/Util/CameraUtil.hpp"
#include "Game/Util/MathUtil.hpp"
#include <nebula_math.h>

bool Mario::doRecovery() {
    if (isStatusActive(MarioStatus_Foo)) {
        return false;
    }
    if (isStatusActive(MarioStatus_Recovery)) {
        return false;
    }
    if (isStatusActive(MarioStatus_Warp)) {
        return false;
    }
    if (mActor->mInRush) {
        return false;
    }
    if (mActor->mInDemo) {
        return false;
    }

    cancelSquatMode();
    stopWalk();
    if (mRecovery->mPullBack) {
        changeStatus(mRecovery);
        stopJump();
    } else {
        Vec3f gravity;
        Vec3f position(*getLastSafetyTrans(&gravity));
        position += gravity * 160.0f;
        Vec3f direction(mPosition - position);
        MR::normalizeOrZero(&direction);
        doPointWarpRecovery(position, direction);
    }
    return true;
}

MarioRecovery::MarioRecovery(MarioActor* actor) : MarioState(actor, MarioStatus_Recovery) {
    mTimer = 0;
    mStep = 0xFFFF;
    mPullBackUp.zero();
    mPullBack = 0;
    mTurnTimer = 0;
    mJumpLength = 0;
    mJumpFront.zero();
    mJumpVertical.zero();
    mTargetPos.zero();
    mStartPos.zero();
    mPullBackCenter.zero();
    mArcAxis.zero();
    mArcDir.zero();
    mJumpTimer = 0;
    mArcAngle = 0.0f;
    mArcStep = 0.0f;
    mArcRadius = 0.0f;
}

bool MarioRecovery::calcFirstVector() {
    mStartPos = getTrans();
    MarioActor* actor = mActor;
    f32 vertical = MR::vecKillElement(actor->getLastMove(), getGravityVec(), &mJumpFront);
    mJumpVertical = getGravityVec() * vertical;
    Vec3f offset(getTrans() - mTargetPos);
    offset.setLength(10.0f);
    mJumpFront += offset;
    mTurnTimer = 60;
    mArcDir = getTrans() - mTargetPos;
    f32 distance = mArcDir.length();
    mArcRadius = mArcDir.length();
    if (MR::normalizeOrZero(&mArcDir)) {
        return false;
    }

    Vec3f horizontal;
    MR::vecKillElement(mArcDir, getGravityVec(), &horizontal);
    if (mPullBack) {
        Vec3f alternate;
        MR::vecKillElement(mPullBackCenter - mTargetPos, getGravityVec(), &alternate);
        horizontal = alternate;
    }
    if (MR::normalizeOrZero(&horizontal)) {
        return false;
    }
    mArcAxis.cross(getGravityVec(), horizontal);
    if (MR::normalizeOrZero(&mArcAxis)) {
        return false;
    }

    if (mPullBack) {
        f32 angle = MR::diffAngleAbs(horizontal, -mArcDir);
        angle += 3.1415927f;
        mArcAngle = angle;
    } else {
        mArcAngle = 6.2831855f - MR::diffAngleAbs(horizontal, mArcDir);
    }
    mJumpLength = 200;
    f32 extra = (distance - 500.0f) / 20.0f;
    if (extra > 0.0f) {
        mJumpLength = static_cast< s32 >(extra) + 200;
    }
    mJumpTimer = mJumpLength;
    mArcStep = mArcAngle / mJumpLength;
    mArcAngle = 0.0f;
    mActor->mBindOn = false;
    const Vec3f& position = mActor->mPosition;
    addTrans(position - getTrans());
    update();
    mPullBack = 0;
    return true;
}

void MarioRecovery::updateJump() {
    Mtx rotation;
    NebulaMtxRotAxisRad(rotation, &mArcAxis, mArcAngle);
    Vec3f rotated;
    NebulaMtxMultVec(rotation, &mArcDir, &rotated);
    if (mJumpTimer > 60 && mJumpTimer < 120) {
        if (mArcAngle < 4.712389f) {
            f32 frames = mJumpTimer - 60;
            mArcRadius += -mArcRadius / frames;
        }
        mArcAngle += 0.5f * mArcStep;
    } else {
        mArcAngle += mArcStep;
    }
    if (mJumpTimer) {
        mJumpTimer--;
    }
    f32 radius = mArcRadius;
    if (radius < 300.0f) {
        radius = 300.0f;
    }
    rotated.setLength(radius);
    Vec3f target(mTargetPos + rotated);
    Vec3f velocity(target - getTrans());
    velocity = velocity * (25.0f / (25.0f + mJumpTimer));
    u32 elapsed = mJumpLength - mJumpTimer;
    if (elapsed < 60) {
        f32 factor = 1.0f - elapsed / 60.0f;
        factor *= 0.8f;
        velocity += mJumpFront * factor;
        velocity += mJumpVertical * factor;
    }
    f32 vertical = MR::vecKillElement(velocity, getGravityVec(), &velocity);
    if (velocity.length() > 30.0f) {
        velocity.setLength(30.0f);
    }
    velocity += getGravityVec() * vertical;
    addTrans(velocity);
}

bool MarioRecovery::start() {
    mTimer = 0;
    mStep = 0;
    mTargetPos = *getPlayer()->getLastSafetyTrans(nullptr);
    changeAnimationNonStop("引き戻し");
    playSound("声慌て");
    playEffect("引き戻し泡");
    if (mPullBack) {
        mStep = 3;
        mTimer = 180;
        mActor->mBindOn = false;
        if (MR::diffAngleAbs(mTargetPos - mPullBackCenter, mPullBackUp) >= 1.5707964f) {
            Vec3f direction(mTargetPos - mPullBackCenter);
            if (MR::normalizeOrZero(&direction)) {
                direction.set< f32 >(1.0f, 0.0f, 0.0f);
            }
            Vec3f cross = mPullBackUp.cross(direction);
            if (MR::normalizeOrZero(&cross)) {
                cross.set< f32 >(0.0f, 0.0f, 1.0f);
            }
            direction.cross(cross, mPullBackUp);
            MR::normalizeOrZero(&direction);
            mTargetPos = mPullBackCenter + mPullBackUp * 600.0f + direction * 600.0f;
        }
    } else if (!calcFirstVector()) {
        return false;
    }
    if (!mActor->isExtraPlayer()) {
        MR::startGlobalEventCameraNoTarget("引き戻し");
    }
    return true;
}

bool MarioRecovery::update() {
    clearVelocity();
    switch (mStep) {
    case 0:
        mStep++;
        mTimer = mJumpLength + 60;
    case 1: {
        s32 volume;
        if (!mTimer) {
            volume = 0;
        } else {
            u16 duration = mJumpLength;
            volume = mTimer / 2;
            if (mTimer > duration) {
                volume = 100 - (mTimer - duration);
            }
        }
        if (volume < 0) {
            volume = 0;
        }
        if (volume > 100) {
            volume = 100;
        }
        playSound("引き戻し基本");
        playSound("引き戻し浮遊", volume);
        getPlayer()->setFrontVecKeepUp(mJumpFront, 0.1f);
        if (mTurnTimer) {
            mTurnTimer--;
            if (!mTurnTimer) {
                mActor->mBindOn = true;
            }
        }
        if (mTimer) {
            mTimer--;
        }
        if (!mTimer || (mTargetPos - getTrans()).length() < 20.0f) {
            addTrans(mTargetPos - getTrans());
            return false;
        }
        updateJump();
        bool passed = false;
        bool distant = true;
        Vec3f shadow(getShadowPos());
        Vec3f shadowOffset(shadow - mTargetPos);
        Vec3f initialOffset(mStartPos - mTargetPos);
        if (MR::isNearZero(shadowOffset)) {
            passed = true;
        }
        if (shadowOffset.length() < 50.0f) {
            distant = false;
        }
        MR::normalizeOrZero(&shadowOffset);
        MR::normalizeOrZero(&initialOffset);
        if (shadowOffset.dot(initialOffset) < 0.0f) {
            passed = true;
        }
        if (passed) {
            if (distant && (shadow - getTrans()).length() > 300.0f) {
                break;
            }
            stopEffectForce("引き戻し泡");
            playEffect("引き戻し泡破裂");
            playSound("引き戻し泡破裂");
            mStep++;
        }
        break;
    }
    case 2:
        getPlayer()->mActionStates.recoveryLand = true;
        return false;
    case 3:
        if (mTimer) {
            mTimer--;
        }
        if (!mTimer) {
            mStep = 0;
            calcFirstVector();
        } else {
            Vec3f horizontal;
            f32 distance = MR::vecKillElement(mTargetPos - getTrans(), mPullBackUp, &horizontal);
            if (MR::abs(distance) < 10.0f) {
                addTrans(mPullBackUp * distance);
                mStep = 0;
                calcFirstVector();
            } else {
                addTrans(mPullBackUp * MR::clamp(distance, -10.0f, 10.0f));
            }
            playSound("引き戻し基本");
        }
        break;
    }
    return true;
}

bool MarioRecovery::close() {
    MR::endGlobalEventCamera("引き戻し", -1, true);
    getPlayer()->mMovementStates.onGround = true;
    getPlayer()->mMovementStates.jumping = false;
    Mario* player = getPlayer();
    player->mMoveLockTimer = 16;
    stopEffectForce("引き戻し泡");
    playEffect("引き戻し泡破裂");
    stopAnimation(nullptr);
    changeAnimation(nullptr, "基本");
    mActor->mBindOn = true;
    return true;
}
