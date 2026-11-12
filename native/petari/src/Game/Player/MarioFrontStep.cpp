#include "Game/Player/MarioFrontStep.hpp"
#include "Game/LiveActor/Nerve.hpp"
#include "Game/Map/HitInfo.hpp"
#include "Game/Player/Mario.hpp"
#include "Game/Player/MarioActor.hpp"
#include "Game/Player/MarioModule.hpp"
#include "Game/Player/MarioState.hpp"
#include "Game/Util/MapUtil.hpp"
#include "Game/Util/MathUtil.hpp"
#include "Game/Util/MtxUtil.hpp"

bool Mario::doFrontStep() {
    if (getCurrentStatus() == MarioStatus_FrontStep) {
        return false;
    }

    if (getCurrentStatus() == MarioStatus_SideStep) {
        return false;
    }

    if (checkWallCode(CollisionWallCode_NotSideStep, false)) {
        return false;
    }

    if (checkWallCode(CollisionWallCode_NoAction, false)) {
        return false;
    }

    if (mActor->mTakeCount != 0) {
        return false;
    }

    if (mHitStates.karikariClinging) {
        return false;
    }

    if (mMovementStates.jumping) {
        return false;
    }

    if (getPlayerMode() == PlayerMode_Hopper) {
        return false;
    }

    if (mFloorCode == 2) {
        return false;
    }

    if (calcAngleD(mGroundNorm) >= 30.0f) {
        return false;
    }

    if (mAirGravityVec.dot(mGroundDown) >= 0.25f) {
        return false;
    }

    Triangle triangle = Triangle();
    MarioActor* pMario = mActor;
    Vec3f* vec = &pMario->mCenterPos;
    if (!MR::getFirstPolyOnLineToMap(nullptr, &triangle, *vec, mFrontVec * 200.0f)) {
        return false;
    }

    setFrontVecKeepUp(-*MR::getNormal(&triangle));

    stopWalk();
    forceStopTornado();

    changeStatus(mFrontStep);

    return true;
}

bool MarioFrontStep::start() {
    changeAnimation("前壁ウエイト");

    mActor->setBlendMtxTimer(10);

    getPlayer()->lockGroundCheck(this, true);

    return true;
}

/*
 */

MarioFrontStep::MarioFrontStep(MarioActor* pActor) : MarioState(pActor, MarioStatus_FrontStep) {
}

bool MarioFrontStep::update() {
    getPlayer()->stopWalk();

    if (getPlayer()->mHitStates.karikariClinging) {
        return false;
    }

    MarioActor* pMario = mActor;
    Vec3f* vec = &pMario->mCenterPos;
    if (!MR::isExistMapCollision(*vec, getFrontVec() * 200.0f)) {
        return false;
    }

    if (!(getStickP() < 0.1f)) {
        Vec3f worldDir(getWorldPadDir());
        MR::vecKillElement(worldDir, getPlayer()->getAirGravityVec(), &worldDir);

        MR::normalizeOrZero(&worldDir);

        Vec3f vec;
        f32 val = MR::vecKillElement(worldDir, -getPlayer()->mFrontVec, &vec);
        if (!(val < -0.866f) && val > 0.0f) {
            return false;
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

    if (!getPlayer()->getMovementStates().onGround) {
        getPlayer()->tryDrop();
        return false;
    }

    return true;
}

bool MarioFrontStep::close() {
    stopAnimation("前壁ウエイト");

    getPlayer()->lockGroundCheck(this, false);

    return true;
}

bool MarioFrontStep::postureCtrl(MtxPtr pMtx) {
    Vec3f blendVec;
    if (MR::vecBlendSphere(-getPlayer()->getAirGravityVec(), -getPlayer()->mFrontVec, &blendVec, 0.25f)) {
        MR::normalizeOrZero(&blendVec);
    } else {
        blendVec = -getPlayer()->getAirGravityVec();
    }

    MR::makeMtxUpFront(pMtx, blendVec, getPlayer()->mFrontVec);

    return true;
}
