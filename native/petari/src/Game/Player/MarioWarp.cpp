#include "Game/Player/MarioWarp.hpp"
#include "Game/AreaObj/WarpCube.hpp"
#include "Game/MapObj/WarpPod.hpp"
#include "Game/Player/MarioActor.hpp"
#include "Game/Player/MarioConst.hpp"
#include "Game/Util/ActorMovementUtil.hpp"
#include "Game/Util/AreaObjUtil.hpp"
#include "Game/Util/CameraUtil.hpp"
#include "Game/Util/MathUtil.hpp"

bool Mario::doObjWarp(LiveActor* pActor) {
    if (isStatusActive(MarioStatus_Warp)) {
        return false;
    }

    if (isStatusActive(MarioStatus_Warp)) {
        return false;
    }

    WarpPod* pPod = MR::getWarpPodManager()->getPairPod(pActor);

    if (pPod != nullptr) {
        mWarp->mPod = pActor;

        mVelocity.zero();

        stopWalk();

        mActor->setBlendMtxTimer(16);

        Vec3f axisY;
        Vec3f pairAxisY;

        MR::calcActorAxisY(&axisY, pActor);
        MR::calcActorAxisY(&pairAxisY, pPod);

        mWarp->mTargetPos = pPod->mPosition + pairAxisY * 160.0f;
        mWarp->mStartPos = pActor->mPosition + axisY * 160.0f;

        MR::getRotatedAxisZ(&mWarp->mEndFront, pPod->mRotation);
        MR::getRotatedAxisY(&mWarp->mEndUp, pPod->mRotation);

        mWarp->mKind = pPod->mVisibilityState;
        mWarp->mInputFree = pPod->mArg2;

        stopWalk();

        changeStatus(mWarp);

        stopJump();

        return true;
    }

    return false;
}

bool Mario::doPointWarp(const Vec3f& rTarget, const Vec3f& rEndFront, s32 frames) {
    if (isStatusActive(MarioStatus_Warp)) {
        return false;
    }

    mVelocity.zero();

    stopWalk();

    mActor->setBlendMtxTimer(16);

    mWarp->mTargetPos = rTarget;
    mWarp->mStartPos = mPosition;
    mWarp->mEndFront = rEndFront;
    mWarp->mKind = 2;
    mWarp->mInputFree = false;

    if (frames != 0) {
        mWarp->mPointFrames = frames;
    } else {
        mWarp->mPointFrames = 90;
    }

    changeStatus(mWarp);

    stopJump();

    return true;
}

bool Mario::isVisibleRecoveryWarpBubble() const {
    if (isStatusActive(MarioStatus_Warp) && mWarp->mKind == 3) {
        return true;
    }

    return false;
}

MarioWarp::MarioWarp(MarioActor* pActor) : MarioState(pActor, MarioStatus_Warp), mEndAnimStarted(-1) {
    mPod = nullptr;
    mPointFrames = 0;

    mTargetPos.zero();
    mStartPos.zero();
    mEndUp.zero();
    mEndFront.zero();

    mInputFree = false;
    mKind = 0;

    mEnterTimer = 0;
    mEnterLength = 0;

    mArcAxis.zero();
    mArcCenter.zero();
    mArcStart.zero();

    mTimer = 0;
    mArcStartAngle = 0.0f;
    mArcEndAngle = 0.0f;
    mLength = 0;
}

void MarioWarp::calcAxis() {
    Vec3f axis;
    Vec3f normal;
    Vec3f displacement = mTargetPos - getTrans();
    f32 distance = displacement.length();
    axis.cross(displacement, getGravityVec());
    MR::normalizeOrZero(&axis);
    Vec3f midpoint = getTrans() + displacement * 0.5f;
    normal.cross(axis, displacement);
    MR::normalizeOrZero(&normal);
    f32 angle = PI / 4.0f;
    if (mKind == 3) {
        angle = PI * 0.4f;
    }

    f32 radius = (0.5f * distance) / MR::sin(angle);
    f32 height = MR::sqrt(radius * radius - 0.5f * (0.5f * distance * distance));
    mArcAxis = axis;
    mArcCenter = midpoint + normal * height;
    mArcStart = -normal * radius;
    mArcEndAngle = angle;
    mArcStartAngle = -angle;
    mTimer = radius / 25.0f;
    if (mTimer == 0) {
        mTimer = 1;
    }

    if (mTimer < 120) {
        mTimer = 120;
    }

    if (mKind == 2) {
        mTimer = mPointFrames;
    }

    mLength = mTimer;
}

void Mario::doCubeWarp() {
    if (isStatusActive(MarioStatus_Warp)) {
        return;
    }

    if (isStatusActive(MarioStatus_Warp)) {
        return;
    }

    WarpCube* pWarpCube = static_cast< WarpCube* >(MR::getAreaObj("WarpCube", mActor->mCenterPos));
    WarpCubeMgr* pWarpCubeMgr = static_cast< WarpCubeMgr* >(MR::getAreaObjManager("WarpCube"));

    if (pWarpCube != nullptr) {
        if (mArrivedWarpCube == pWarpCube) {
            return;
        }

        WarpCube* pPairCube = pWarpCubeMgr->getPairCube(pWarpCube);
        if (pPairCube == nullptr) {
            return;
        }

        pWarpCubeMgr->startEventCamera(pWarpCube);

        Vec3f cubePos;
        MR::calcCubePos(pPairCube, &cubePos);

        mVelocity.zero();
        stopWalk();
        mActor->mStunTimer = 10;

        mArrivedWarpCube = pPairCube;
        mWarp->mTargetPos = cubePos;

        MR::calcCubePos(pWarpCube, &cubePos);
        mWarp->mStartPos = cubePos;
        mWarp->mEndFront.zero();
        mWarp->mEndUp.zero();
        mWarp->mKind = 0;
        mWarp->mInputFree = 0;

        stopWalk();

        changeStatus(mWarp);

        stopJump();

        return;
    }

    if (mArrivedWarpCube != nullptr) {
        pWarpCubeMgr->setInvalidateTimer(mArrivedWarpCube, 300);
    }

    mArrivedWarpCube = nullptr;
}

bool Mario::doPointWarpRecovery(const Vec3f& rTarget, const Vec3f& rEndFront) {
    if (isStatusActive(MarioStatus_Warp)) {
        return false;
    }

    mVelocity.zero();

    stopWalk();

    mActor->setBlendMtxTimer(16);

    mWarp->mTargetPos = rTarget;
    mWarp->mStartPos = mPosition;
    mWarp->mEndFront = rEndFront;
    mWarp->mEndUp.zero();
    mWarp->mKind = 3;
    mWarp->mInputFree = false;

    changeStatus(mWarp);

    stopJump();

    MR::startGlobalEventCameraNoTarget("引き戻し");

    return true;
}

void MarioWarp::updateJump() {
    if (mTimer == 0) {
        return;
    }

    f32 angle = (mLength - mTimer - 0.5f * mLength) / mLength;
    angle *= PI;
    f32 progress = (1.0f + MR::sin(angle)) / 2.0f;
    if (mKind == 2) {
        progress = 1.0f - static_cast< f32 >(mTimer - 1) / mLength;
    }

    Mtx rotation;
    Vec3f offset;
    NebulaMtxRotAxisRad(rotation, &mArcAxis, mArcStartAngle * (1.0f - progress) + mArcEndAngle * progress);
    NebulaMtxMultVecSR(rotation, &mArcStart, &offset);
    Vec3f position = mArcCenter + offset;
    f32 followRate = 0.5f;
    if (mKind == 2) {
        followRate = 1.0f;
    }

    if (mKind == 3) {
        f32 remaining = static_cast< f32 >(mTimer) / mLength;
        followRate = (1.0f - remaining) + followRate * remaining;
        position = position * remaining + mTargetPos * (1.0f - remaining);
    }

    addTrans((position - getTrans()) * followRate);
    if (mKind == 2) {
        getPlayer()->setFrontVecKeepUp(mEndFront, 0.2f);
    }
}

bool MarioWarp::start() {
    mEndAnimStarted = 0;
    switch (mKind) {
    case 0:
    case 2:
        mEnterTimer = 1;
        break;
    case 1:
        mEnterTimer = 60;
        break;
    case 3:
        mEnterTimer = 40;
        break;
    }

    mEnterLength = mEnterTimer;
    if (mKind == 3) {
        playEffect("引き戻し泡");
    }

    if (mKind == 1) {
        playEffect("ワープポッドブラー");
        playSound("ワープポッド入り");
    }

    if (mKind == 2) {
        playSound("声小ジャンプ");
    } else {
        playSound("声慌て");
    }

    switch (mKind) {
    case 0:
        break;
    case 3:
        changeAnimationNonStop("引き戻し");
        break;
    case 1:
        changeAnimationNonStop("ポッドワープ開始");
        break;
    case 2:
        changeAnimationNonStop("しゃがみジャンプ");
        break;
    }

    mActor->mBindOn = 0;
    const Vec3f& position = mActor->mPosition;
    addTrans(position - getTrans());
    update();
    if (mKind == 3 && getPlayerMode() != 1) {
        mActor->mBlurState = 2;
    }

    if (!mInputFree) {
        getPlayer()->mMovementStates.spinBlocked = true;
    }

    return true;
}

bool MarioWarp::update() {
    if (!mInputFree) {
        getPlayer()->mMovementStates.spinBlocked = true;
        getPlayer()->mMovementStates.inputLocked = true;
    }

    clearVelocity();
    if (mEnterTimer) {
        if (mKind == 1 || mKind == 3) {
            playSound("引き戻し基本");
        }

        if (mKind == 1) {
            playSound("ワープポッド移動");
        }

        mEnterTimer--;
        Vec3f displacement = mStartPos - getTrans();
        addTrans(displacement * (1.0f - static_cast< f32 >(mEnterTimer) / mEnterLength));
        if (mEnterTimer == 0) {
            calcAxis();
            if (mKind == 1 && getPlayerMode() != 6) {
                MarioActor* actor = mActor;
                actor->mHiddenByDemo = true;
                actor->updateHand();
                actor->updateFace();
            }
        }

        if (mPod != nullptr) {
            MR::getWarpPodManager()->startEventCamera(mPod);
        }

        return true;
    }

    updateJump();
    if (mKind == 0) {
        Vec3f offset = getTrans() - mStartPos;
        Vec3f direction = mTargetPos - mStartPos;
        MR::normalize(&direction);
        f32 distance = MR::vecKillElement(offset, direction, &offset);
        addTrans((mStartPos + direction * distance) - getTrans());
    }

    switch (mKind) {
    case 2:
        break;
    case 0:
        playSound("惑星貫通中");
        break;
    case 1:
    case 3: {
        s32 volume;
        if (mTimer == 0) {
            volume = 0;
        } else {
            f32 halfway = mLength / 2.0f;
            f32 elapsed = mLength - mTimer;
            if (elapsed < halfway) {
                volume = 100.0f * (elapsed / halfway);
            } else {
                volume = 100.0f * (1.0f - (elapsed - halfway) / halfway);
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
        if (mKind == 1) {
            playSound("ワープポッド移動");
        }

        break;
    }
    }

    if (mEndAnimStarted == 1 && !isAnimationRun("ポッドワープ終了")) {
        return false;
    }

    if (mTimer == 0) {
        if (mKind == 1 && getPlayerMode() != 6) {
            if (mEndAnimStarted != 1) {
                playSound("ワープポッド出");
            }

            mEndAnimStarted = 1;
            mActor->mHiddenByDemo = false;
            changeAnimation("ポッドワープ終了");
            WarpPodMgr* manager = MR::getWarpPodManager();
            if (manager != nullptr) {
                manager->endEventCamera();
            }
        } else {
            return false;
        }
    } else {
        mTimer--;
    }

    return true;
}

bool MarioWarp::close() {
    getPlayer()->mMovementStates.onGround = true;
    getPlayer()->mMovementStates.jumping = false;
    Mario* player = getPlayer();
    player->mMoveLockTimer = 16;
    stopAnimation(nullptr);
    changeAnimation(nullptr, "基本");
    switch (mKind) {
    case 0:
        playSound("惑星貫通終了");
        break;
    case 3:
        playSound("引き戻し泡破裂");
        stopEffectForce("引き戻し泡");
        playEffect("引き戻し泡破裂");
        break;
    case 1:
        stopEffect("ワープポッドブラー");
        break;
    case 2:
        changeAnimation("しゃがみジャンプ着地");
        break;
    }

    mActor->mBindOn = 1;
    WarpCubeMgr* cubes = static_cast< WarpCubeMgr* >(MR::getAreaObjManager("WarpCube"));
    if (cubes != nullptr) {
        cubes->endEventCamera();
    }

    if (mKind != 1) {
        WarpPodMgr* pods = MR::getWarpPodManager();
        if (pods != nullptr) {
            pods->endEventCamera();
        }
    } else {
        if (mTimer != 0 || getPlayerMode() == 6) {
            mActor->mHiddenByDemo = false;
            WarpPodMgr* pods = MR::getWarpPodManager();
            if (pods != nullptr) {
                pods->endEventCamera();
            }
        }

        WarpPodMgr* pods = MR::getWarpPodManager();
        if (pods != nullptr) {
            pods->notifyWarpEnd(static_cast< WarpPod* >(mPod));
        }
    }

    switch (mKind) {
    case 0: {
        playSound("声中ジャンプ");
        const MarioConstTable* table = mActor->getConst().getTable();
        getPlayer()->tryForcePowerJump(mEndUp * table->mWarpPodJumpY + mEndFront * table->mWarpPodJumpX, false);
        break;
    }

    case 3: {
        MR::endGlobalEventCamera("引き戻し", -1, true);
        if (getPlayerMode() != 1) {
            mActor->mBlurState = 0;
        }

        getPlayer()->mMovementStates.onGround = false;
        getPlayer()->mMovementStates.jumping = true;
        getPlayer()->mMovementStates.spinBlocked = true;
        getPlayer()->mAirWalkTimer = 0;
        getPlayer()->mJumpVec = Vec3f(0.0f, 0.0f, 0.0f);
        changeAnimation("落下");
        Mario* player = getPlayer();
        player->mInvincibleJump = 0;
        player->mJumpKind = 0;
        break;
    }
    case 1:
        if (getPlayerMode() != 1) {
            mActor->mBlurState = 0;
        }

        if (getPlayerMode() != 6) {
            playSound("声中ジャンプ");
            getPlayer()->tryForcePowerJump(mEndUp * 12.0f + mEndFront * 5.0f, false);
        }

        break;
    case 2:
        getPlayer()->setFrontVecKeepUp(mEndFront);
        break;
    }

    if (getPlayerMode() != 6) {
        getPlayer()->mMovementStates.inputLocked = true;
    } else {
        getPlayer()->mMovementStates.inputLocked = false;
    }

    mPod = nullptr;
    return true;
}
