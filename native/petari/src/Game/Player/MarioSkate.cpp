#include "Game/Player/MarioSkate.hpp"
#include "Game/Map/CollisionCode.hpp"
#include "Game/Player/Mario.hpp"
#include "Game/Player/MarioActor.hpp"
#include "Game/Player/MarioAnimator.hpp"
#include "Game/Util/MathUtil.hpp"
#include "Game/Util/MtxUtil.hpp"
#include "Game/Util/Tunable.hpp"

NEBULA_TUNABLE_NAMED(f32, cNum_postureCtrl_f1, "postureCtrl.f1", 3.1415927f);
NEBULA_TUNABLE_NAMED(f32, cNum_update_f1, "update.f1", 20.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_update_f2, "update.f2", 50.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_update_f3, "update.f3", 1.25f);
NEBULA_TUNABLE_NAMED(f32, cNum_update_f4, "update.f4", 1.5f);
NEBULA_TUNABLE_NAMED(f32, cNum_update_f5, "update.f5", 20.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_update_f6, "update.f6", 1.2f);
NEBULA_TUNABLE_NAMED(f32, cNum_update_f7, "update.f7", 1.2f);
NEBULA_TUNABLE_NAMED(f32, cNum_update_f8, "update.f8", 0.04f);
NEBULA_TUNABLE_NAMED(f32, cNum_update_f9, "update.f9", 0.04f);
NEBULA_TUNABLE_NAMED(f32, cNum_update_f10, "update.f10", 0.1f);
NEBULA_TUNABLE_NAMED(f32, cNum_update_f11, "update.f11", 0.1f);
NEBULA_TUNABLE_NAMED(f32, cNum_update_f12, "update.f12", 0.5f);
NEBULA_TUNABLE_NAMED(f32, cNum_update_f13, "update.f13", 0.3926991f);
NEBULA_TUNABLE_NAMED(f32, cNum_update_f14, "update.f14", 0.3926991f);
NEBULA_TUNABLE_NAMED(f32, cNum_update_f15, "update.f15", 0.5235988f);
NEBULA_TUNABLE_NAMED(f32, cNum_update_f16, "update.f16", 0.5235988f);
NEBULA_TUNABLE_NAMED(f32, cNum_update_f17, "update.f17", 0.8f);
NEBULA_TUNABLE_NAMED(f32, cNum_update_f18, "update.f18", 0.9f);
NEBULA_TUNABLE_NAMED(f32, cNum_update_f19, "update.f19", 0.1f);
NEBULA_TUNABLE_NAMED(f32, cNum_update_f20, "update.f20", 0.95f);
NEBULA_TUNABLE_NAMED(f32, cNum_update_f21, "update.f21", 0.05f);
NEBULA_TUNABLE_NAMED(f32, cNum_close_f1, "close.f1", 1.5f);

bool Mario::isSkatableFloor() const {
    if (mFloorCode == CollisionFloorCode_Ice) {
        return true;
    }

    return mFloorCode == CollisionFloorCode_GlassIce;
}

bool Mario::doSkate() {
    changeStatus(mSkate);
    return true;
}

MarioSkate::MarioSkate(MarioActor* pActor) : MarioState(pActor, MarioStatus_Skate) {
    mStrideTimer = 0;
    mTwist = 0.0f;
    mExitJump = 0;
    mBackward = 0;
    mStandStart = 0;
    mStrideSide = 0;
    mStrideReady = 0;
    mStrideCount = 0;
    mTilt = 0.0f;
}

bool MarioSkate::postureCtrl(MtxPtr pMtx) {
    getPlayer()->postureCtrl(pMtx);
    f32 rotation = mTwist;
    rotation *= cNum_postureCtrl_f1;
    NebulaMtxConcat(pMtx, MR::tmpMtxRotYRad(rotation), pMtx);
    NebulaMtxConcat(pMtx, MR::tmpMtxRotZRad(mTilt), pMtx);
    Mtx direction;
    getPlayer()->createDirectionMtx(direction);
    NebulaMtxConcat(direction, pMtx, pMtx);
    return true;
}

void MarioSkate::exitJump() {
    mExitJump = 1;
    getPlayer()->tryJump();
    playSound("スケートジャンプ");
}

bool MarioSkate::start() {
    mStrideTimer = 0;
    mStrideSide = 0;
    mStrideReady = 0;
    mExitJump = 0;
    mStrideCount = 1;
    mBackward = 0;
    mTilt = 0.0f;
    mStandStart = 0;

    if (isAnimationRun("スケートアクセルジャンプ") || isAnimationRun("スケートジャンプ2") || isAnimationRun("スケートジャンプ3")) {
        mStrideCount = 1;
        changeAnimation(nullptr, "基本");
        changeAnimationNonStop("スケート着地");
        playEffect("スケート右");
        playEffect("スケート左");
        playSound("スケート着地");
        mBackward = 0;
        mTwist = 0.0f;
    } else {
        mTwist = 0.0f;
        if (getPlayer()->mTargetWalkSpeedIndex < 3) {
            mStandStart = 1;
            changeAnimationNonStop("アイスひねり静止");
        } else {
            changeAnimationNonStop("アイスひねり");
            playEffect("スケート左");
        }

        playSound("スケートスピン");
        playSound("スピンジャンプ");
        playSound("声スピン");
    }

    return true;
}

bool MarioSkate::update() {
    f32 speed;
    if (!getPlayer()->mMovementStates.onGround || getPlayer()->mMovementStates.jumping) {
        return false;
    }

    if (checkTrgA() || getPlayer()->mMovementStates.jumpBuffered) {
        exitJump();
        return false;
    }

    if (!getPlayer()->isSkatableFloor()) {
        return false;
    }

    if (!mStandStart) {
        getPlayer()->mainMove();
    } else {
        return !isAnimationTerminate(nullptr);
    }

    if (getPlayer()->mMovementStates.turnStickNeutral) {
        getPlayer()->mMovementStates.turnStickNeutral = false;
        getPlayer()->mTurnReadyTimer = 0;
    }

    if (checkTrgZ()) {
        bool reverse = !mBackward;
        mStrideTimer = 20;
        mBackward = reverse;
        mStrideReady = 1;
        mStrideSide = 1 - mStrideSide;
        playSound("声壁押し");
    }

    getPlayer()->updateWalkSpeed();
    if (isAnimationRun("スケート着地")) {
        if (isAnimationTerminate(nullptr)) {
            mStrideReady = 1;
        }
    } else if (mStrideCount == 1) {
        f32 stick = getStickP();
        f32 frame = cNum_update_f1 + cNum_update_f2 * (1.0f - stick);
        if (getAnimator()->getFrame() > frame && getStickP()) {
            mStrideReady = 1;
        }
    }

    if (mActor->isRequestSpin()) {
        if (mStrideCount && (!isAnimationRun("アイスひねり移動") || isAnimationTerminate(nullptr))) {
            stopAnimation(nullptr);
            changeAnimationNonStop("アイスひねり移動");
            playSound("スケートスピン");
            playSound("スピンジャンプ");
            playSound("声パンチ");
            if (getPlayer()->mWalkSpeed < cNum_update_f3) {
                getPlayer()->mWalkSpeed = cNum_update_f4 * getPlayer()->mWalkSpeed;
            }
        }

        if (getAnimator()->getFrame() > cNum_update_f5) {
            mStrideReady = 1;
        }
    }

    speed = getPlayer()->mWalkSpeed;
    if (speed < cNum_update_f6 * getStickP()) {
        getPlayer()->mWalkSpeed = cNum_update_f7 * getStickP();
    }

    if (speed > 0.0f && !isAnimationRun("基本")) {
        playSound("スケート滑り");
    }

    if (mBackward) {
        mTwist += cNum_update_f8;
        mTwist = MR::clamp(mTwist, -1.0f, 1.0f);
    } else {
        if (mTwist == 1.0f) {
            mTwist = -1.0f;
        }

        bool negative = false;
        if (mTwist <= 0.0f) {
            negative = true;
        }

        mTwist += cNum_update_f9;
        if (negative) {
            mTwist = -MR::clamp(-mTwist, 0.0f, 1.0f);
        } else {
            mTwist = MR::clamp(mTwist, -1.0f, 1.0f);
        }
    }

    f32 angle = MR::diffAngleAbsHorizontal(getWorldPadDir(), getFrontVec(), getGravityVec());
    Vec3f cross;
    const Vec3f& rPad = getWorldPadDir();
    cross.cross(getFrontVec(), rPad);
    if (cross.dot(getGravityVec()) < 0.0f) {
        angle = -angle;
    }

    if (mActor->mPunchSensorOn) {
        mStrideTimer = 15;
    } else {
        mStrideTimer++;
    }

    u32 interval = 30;
    if (mBackward) {
        interval = 60;
    }

    if (mStrideTimer >= interval) {
        if (mStrideReady) {
            if (!(angle >= cNum_update_f10 && mStrideSide == 1) && !(angle <= -cNum_update_f11 && mStrideSide == 0)) {
                mStrideReady = 0;
                mStrideTimer = 0;
                mStrideSide = 1 - mStrideSide;
                if (mStrideCount < 1) {
                    mStrideCount++;
                } else {
                    switch (mStrideSide) {
                    case 0:
                        if (mBackward) {
                            changeAnimationNonStop("氷上後行右");
                        } else {
                            changeAnimationNonStop("氷上力行左");
                        }

                        playEffect("スケート左");
                        stopEffect("スケート右");
                        playSound("スケート足");
                        break;
                    case 1:
                        if (mBackward) {
                            changeAnimationNonStop("氷上後行左");
                        } else {
                            changeAnimationNonStop("氷上力行右");
                        }

                        playEffect("スケート右");
                        stopEffect("スケート左");
                        playSound("スケート足");
                        break;
                    }
                }
            }
        } else if (mStrideTimer > 90) {
            return false;
        }
    }

    f32 animationSpeed = 1.0f;
    f32 animationScale = 1.0f - cNum_update_f12 * (1.0f - getStickP());
    if (!isAnimationRun("スケート着地")) {
        getAnimator()->setSpeed(animationSpeed * animationScale);
    }

    f32 tilt;
    if (mBackward) {
        tilt = MR::clamp(angle, -cNum_update_f13, cNum_update_f14);
    } else {
        tilt = MR::clamp(angle, -cNum_update_f15, cNum_update_f16);
    }

    f32 targetTilt = tilt * (1.0f - cNum_update_f17 * (1.0f - getStickP()));
    if (MR::abs(targetTilt) > MR::abs(mTilt)) {
        mTilt = cNum_update_f18 * mTilt + cNum_update_f19 * targetTilt;
    } else {
        mTilt = cNum_update_f20 * mTilt + cNum_update_f21 * targetTilt;
    }

    return true;
}

bool MarioSkate::close() {
    if (mExitJump) {
        if (!mStandStart) {
            switch (getPlayer()->mJumpKind) {
            case 1:
                changeAnimationNonStop("スケートジャンプ2");
                break;
            case 2:
                changeAnimationNonStop("スケートジャンプ3");
                break;
            default:
                changeAnimationNonStop("スケートアクセルジャンプ");
                break;
            }

            Vec3f velocity;
            Mario* pPlayer = getPlayer();
            f32 vertical = MR::vecKillElement(pPlayer->mJumpVec, getGravityVec(), &velocity);
            velocity *= cNum_close_f1;
            velocity += getGravityVec() * vertical;
            getPlayer()->setJumpVec(velocity);
        }
    } else if (getPlayer()->mMovementStates.onGround) {
        stopAnimation(nullptr, "基本");
    } else {
        stopAnimation(nullptr, "落下");
    }

    stopEffect("スケート左");
    stopEffect("スケート右");
    return true;
}
