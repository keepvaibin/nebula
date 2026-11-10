#include "Game/Player/MarioRecovery.hpp"
#include "Game/Player/Mario.hpp"
#include "Game/Player/MarioActor.hpp"
#include "Game/Util/CameraUtil.hpp"
#include "Game/Util/MathUtil.hpp"
#include <nebula_math.h>
#include "Game/Util/Tunable.hpp"

NEBULA_TUNABLE_NAMED(f32, cNum_doRecovery_f1, "doRecovery.f1", 160.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_calcFirstVector_f1, "calcFirstVector.f1", 10.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_calcFirstVector_f2, "calcFirstVector.f2", 3.1415927f);
NEBULA_TUNABLE_NAMED(f32, cNum_calcFirstVector_f3, "calcFirstVector.f3", 6.2831855f);
NEBULA_TUNABLE_NAMED(f32, cNum_calcFirstVector_f4, "calcFirstVector.f4", 500.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_calcFirstVector_f5, "calcFirstVector.f5", 20.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_updateJump_f1, "updateJump.f1", 4.712389f);
NEBULA_TUNABLE_NAMED(f32, cNum_updateJump_f2, "updateJump.f2", 0.5f);
NEBULA_TUNABLE_NAMED(f32, cNum_updateJump_f3, "updateJump.f3", 300.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_updateJump_f4, "updateJump.f4", 300.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_updateJump_f5, "updateJump.f5", 25.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_updateJump_f6, "updateJump.f6", 25.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_updateJump_f7, "updateJump.f7", 60.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_updateJump_f8, "updateJump.f8", 0.8f);
NEBULA_TUNABLE_NAMED(f32, cNum_updateJump_f9, "updateJump.f9", 30.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_updateJump_f10, "updateJump.f10", 30.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_start_f1, "start.f1", 1.5707964f);
NEBULA_TUNABLE_NAMED(f32, cNum_start_f2, "start.f2", 600.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_start_f3, "start.f3", 600.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_update_f1, "update.f1", 0.1f);
NEBULA_TUNABLE_NAMED(f32, cNum_update_f2, "update.f2", 20.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_update_f3, "update.f3", 50.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_update_f4, "update.f4", 300.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_update_f5, "update.f5", 10.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_update_f6, "update.f6", 10.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_update_f7, "update.f7", 10.0f);

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
        position += gravity * cNum_doRecovery_f1;
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
    offset.setLength(cNum_calcFirstVector_f1);
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
        angle += cNum_calcFirstVector_f2;
        mArcAngle = angle;
    } else {
        mArcAngle = cNum_calcFirstVector_f3 - MR::diffAngleAbs(horizontal, mArcDir);
    }
    mJumpLength = 200;
    f32 extra = (distance - cNum_calcFirstVector_f4) / cNum_calcFirstVector_f5;
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
        if (mArcAngle < cNum_updateJump_f1) {
            f32 frames = mJumpTimer - 60;
            mArcRadius += -mArcRadius / frames;
        }
        mArcAngle += cNum_updateJump_f2 * mArcStep;
    } else {
        mArcAngle += mArcStep;
    }
    if (mJumpTimer) {
        mJumpTimer--;
    }
    f32 radius = mArcRadius;
    if (radius < cNum_updateJump_f3) {
        radius = cNum_updateJump_f4;
    }
    rotated.setLength(radius);
    Vec3f target(mTargetPos + rotated);
    Vec3f velocity(target - getTrans());
    velocity = velocity * (cNum_updateJump_f5 / (cNum_updateJump_f6 + mJumpTimer));
    u32 elapsed = mJumpLength - mJumpTimer;
    if (elapsed < 60) {
        f32 factor = 1.0f - elapsed / cNum_updateJump_f7;
        factor *= cNum_updateJump_f8;
        velocity += mJumpFront * factor;
        velocity += mJumpVertical * factor;
    }
    f32 vertical = MR::vecKillElement(velocity, getGravityVec(), &velocity);
    if (velocity.length() > cNum_updateJump_f9) {
        velocity.setLength(cNum_updateJump_f10);
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
        if (MR::diffAngleAbs(mTargetPos - mPullBackCenter, mPullBackUp) >= cNum_start_f1) {
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
            mTargetPos = mPullBackCenter + mPullBackUp * cNum_start_f2 + direction * cNum_start_f3;
        }
    } else if (!calcFirstVector()) {
        return false;
    }
    MR::startGlobalEventCameraNoTarget("引き戻し");
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
        getPlayer()->setFrontVecKeepUp(mJumpFront, cNum_update_f1);
        if (mTurnTimer) {
            mTurnTimer--;
            if (!mTurnTimer) {
                mActor->mBindOn = true;
            }
        }
        if (mTimer) {
            mTimer--;
        }
        if (!mTimer || (mTargetPos - getTrans()).length() < cNum_update_f2) {
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
        if (shadowOffset.length() < cNum_update_f3) {
            distant = false;
        }
        MR::normalizeOrZero(&shadowOffset);
        MR::normalizeOrZero(&initialOffset);
        if (shadowOffset.dot(initialOffset) < 0.0f) {
            passed = true;
        }
        if (passed) {
            if (distant && (shadow - getTrans()).length() > cNum_update_f4) {
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
            if (MR::abs(distance) < cNum_update_f5) {
                addTrans(mPullBackUp * distance);
                mStep = 0;
                calcFirstVector();
            } else {
                addTrans(mPullBackUp * MR::clamp(distance, -cNum_update_f6, cNum_update_f7));
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
