#include "Game/Player/MarioSideStep.hpp"
#include "Game/Map/HitInfo.hpp"
#include "Game/Player/Mario.hpp"
#include "Game/Player/MarioActor.hpp"
#include "Game/Player/MarioWall.hpp"
#include "Game/Util/DemoUtil.hpp"
#include "Game/Util/MathUtil.hpp"
#include "Game/Util/MtxUtil.hpp"
#include "Game/Util/Tunable.hpp"

NEBULA_TUNABLE_NAMED(f32, cNum_checkWallJumpCode_f1, "checkWallJumpCode.f1", 10.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_checkWallJumpCode_f2, "checkWallJumpCode.f2", 10.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_checkWallJumpCode_f3, "checkWallJumpCode.f3", 10.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_doSideStep_f1, "doSideStep.f1", 200.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_doSideStep_f2, "doSideStep.f2", 91.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_doSideStep_f3, "doSideStep.f3", 1.6534699f);
NEBULA_TUNABLE_NAMED(f32, cNum_doSideStep_f4, "doSideStep.f4", 1.4959966f);
NEBULA_TUNABLE_NAMED(f32, cNum_update_f1, "update.f1", 0.1f);
NEBULA_TUNABLE_NAMED(f32, cNum_update_f2, "update.f2", 0.866f);
NEBULA_TUNABLE_NAMED(f32, cNum_update_f3, "update.f3", 0.707f);
NEBULA_TUNABLE_NAMED(f32, cNum_update_f4, "update.f4", 0.25f);
NEBULA_TUNABLE_NAMED(f32, cNum_update_f5, "update.f5", 6.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_update_f6, "update.f6", 6.0f);

bool Mario::checkWallJumpCode() {
    if (checkWallFloorCode(11) || checkWallFloorCode(6) || checkWallFloorCode(34) || checkWallFloorCode(7) || checkWallFloorCode(8)) {
        if (mMovementStates.onGround || isAnimationRun(mHipDropLandAnim)) {
            stopWalk();
            push(getWallNorm() * cNum_checkWallJumpCode_f1);
            tryPushToVelocity();
            mJumpVec.set(-getPlayer()->getWallNorm() * cNum_checkWallJumpCode_f2);
            MR::vecKillElement(mJumpVec, getAirGravityVec(), &mJumpVec);
            mJumpVec -= getAirGravityVec() * cNum_checkWallJumpCode_f3;
        }
        mWall->initTriangleJump();
        u32 jumpType = -1;
        if (checkWallFloorCode(7)) {
            jumpType = 1;
        }
        if (checkWallFloorCode(6)) {
            jumpType = 0;
        }
        if (checkWallFloorCode(34)) {
            jumpType = 2;
        }
        if (mWall->startBackJump(jumpType)) {
            return true;
        }
    }
    return false;
}

MarioSideStep::MarioSideStep(MarioActor* actor) : MarioState(actor, MarioStatus_SideStep) {
}

bool Mario::doSideStep() {
    if (getCurrentStatus() == MarioStatus_SideStep) {
        return false;
    }
    if (checkWallCode(CollisionWallCode_NotSideStep, false)) {
        return false;
    }
    if (checkWallCode(CollisionWallCode_NoAction, false)) {
        return false;
    }
    if (mActor->mTakeCount) {
        return false;
    }
    if (mHitStates.karikariClinging) {
        return false;
    }
    if (mCeilDist < cNum_doSideStep_f1) {
        return false;
    }
    if (getPlayerMode() == PlayerMode_Hopper) {
        return false;
    }
    if (calcPolygonAngleD(mFrontWallTriangle) > cNum_doSideStep_f2) {
        return false;
    }
    f32 angle = MR::diffAngleAbs(getFrontWallNorm(), mGroundNorm);
    if (angle >= cNum_doSideStep_f3 || angle < cNum_doSideStep_f4) {
        return false;
    }
    bool cannotSideStep = !isSlipFloorCode(mFloorCode);
    if (cannotSideStep) {
        return false;
    }
    if (mFloorCode == 2) {
        return false;
    }
    if (!getPlayer()->mMovementStates.frontWall) {
        return false;
    }
    const Triangle* wall = getPlayer()->getWallPolygon();
    if (!wall) {
        return false;
    }
    if (!MR::isSameMtx(wall->getBaseMtx()->toMtxPtr(), wall->getPrevBaseMtx()->toMtxPtr())) {
        return false;
    }
    stopWalk();
    forceStopTornado();
    changeStatus(mSideStep);
    return true;
}

bool MarioSideStep::start() {
    if (!isAnimationRun("壁押し")) {
        playSound("声壁押し");
    }
    changeAnimation("壁押し", MarioIndexedAnime_WallPush);
    mActor->setBlendMtxTimer(10);
    return true;
}

bool MarioSideStep::close() {
    stopAnimation("壁ウエイト");
    stopAnimation("壁右歩き");
    stopAnimation("壁左歩き");
    stopAnimation("壁押し");
    getPlayer()->startBas(nullptr, false, 0.0f, 0.0f);
    return true;
}

bool MarioSideStep::update() {
    if (!getPlayer()->mMovementStates.frontWall) {
        return false;
    }
    if (getPlayer()->mHitStates.karikariClinging) {
        return false;
    }
    if (getPlayer()->mDrawStates.atEdge) {
        return false;
    }
    if (MR::isNormalTalking()) {
        return false;
    }
    if (mActor->mTakeCount) {
        return false;
    }
    Mario* player = getPlayer();
    bool cannotSideStep = !isSlipFloorCode(player->mFloorCode);
    if (cannotSideStep) {
        return false;
    }
    if (getStickP() < cNum_update_f1) {
        changeAnimation("壁ウエイト");
    } else {
        Vec3f direction(getWorldPadDir());
        MR::vecKillElement(direction, getPlayer()->getAirGravityVec(), &direction);
        MR::normalizeOrZero(&direction);
        Vec3f side;
        f32 forward = MR::vecKillElement(direction, getPlayer()->getWallNorm(), &side);
        if (forward < -cNum_update_f2) {
            if (!isAnimationRun("壁押し")) {
                playSound("声壁押し");
            }
            changeAnimation("壁押し", MarioIndexedAnime_WallPush);
        } else if (forward > 0.0f) {
            if (forward > cNum_update_f3) {
                getPlayer()->setFrontVecKeepUp(getPlayer()->getWallNorm());
            }
            getPlayer()->mMovementStates.frontWall = false;
            return false;
        } else {
            side *= (1.0f + cNum_update_f4 * forward) * getStickP();
            if (side.dot(getPlayer()->mSideVec) < 0.0f) {
                if (!isAnimationRun("壁右歩き")) {
                    changeAnimation("壁右歩き");
                }
            } else if (!isAnimationRun("壁左歩き")) {
                changeAnimation("壁左歩き");
            }
            addVelocity(side * cNum_update_f5 - getPlayer()->getWallNorm() * cNum_update_f6);
        }
    }
    if (checkTrgA()) {
        getPlayer()->resetInline();
        getPlayer()->setFrontVecKeepUp(-getPlayer()->getWallNorm());
        getPlayer()->tryJump();
        return false;
    }
    if (mActor->isRequestRush()) {
        getPlayer()->tryWallPunch();
        return false;
    }
    if (!getPlayer()->mMovementStates.onGround) {
        getPlayer()->keepDistFrontWall();
        getPlayer()->tryDrop();
        return false;
    }
    return getPlayer()->fixWallingPosition(false) != false;
}
