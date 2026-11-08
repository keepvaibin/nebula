#include "Game/MapObj/SpinDriverCamera.hpp"
#include "Game/Camera/CameraTargetArg.hpp"
#include "Game/Camera/CameraTargetMtx.hpp"
#include "Game/LiveActor/ActorCameraInfo.hpp"
#include "Game/Util/ActorCameraUtil.hpp"
#include "Game/Util/PlacementUtil.hpp"
#include "Game/Util/MathUtil.hpp"
#include "Game/Util/MtxUtil.hpp"
#include "Game/Util/MultiEventCamera.hpp"

SpinDriverCamera::SpinDriverCamera() : mCamera(), mTargetMtx(), mCameraInfo(), mAppearCameraFrame() {
}

void SpinDriverCamera::startAppearCamera(LiveActor* pActor, const Vec3f& rUp, const Vec3f& rFront, const Vec3f& rPos) {
    if (mCameraInfo == nullptr) {
        return;
    }

    mAppearCameraFrame = MR::getMultiActorCameraFrames(pActor, mCameraInfo, "出現イベント用");

    if (mAppearCameraFrame <= 0) {
        return;
    }

    MR::startMultiActorCameraTargetOther(pActor, mCameraInfo, "出現イベント用", CameraTargetArg(mTargetMtx));

    Pos3f upPos;
    MR::makeMtxUpFrontPos(&upPos, rUp, rFront, rPos);
    mTargetMtx->setMtx(upPos.toMtxPtr());
}

void SpinDriverCamera::endAppearCamera(LiveActor* pActor) {
    if (mCameraInfo == nullptr) {
        return;
    }

    if (mAppearCameraFrame <= 0) {
        return;
    }

    MR::endMultiActorCamera(pActor, mCameraInfo, "出現イベント用", false);
}

s32 SpinDriverCamera::getAppearCameraFrames() const {
    return mAppearCameraFrame;
}

void SpinDriverCamera::start(const Vec3f& rUp, const Vec3f& rFront, const Vec3f& rPos) {
    if (mCamera == nullptr) {
        return;
    }

    mCamera->start(CameraTargetArg(mTargetMtx), 0);

    Pos3f mtx;
    MR::makeMtxUpFrontPos(&mtx, rUp, rFront, rPos);
    mTargetMtx->setMtx(mtx.toMtxPtr());
}

void SpinDriverCamera::update(const Vec3f& rDir, const Vec3f& rPos) {
    if (mCamera == nullptr || mCamera->isEnd()) {
        return;
    }

    mCamera->update();
    updateTargetMatrix(rDir, rPos);
}

void SpinDriverCamera::cancel() {
    if (mCamera == nullptr) {
        return;
    }

    mCamera->endForceSoon();
}

void SpinDriverCamera::end() {
    if (mCamera == nullptr) {
        return;
    }

    mCamera->endForceAtLanding();
    mCamera->changeTargetPlayer();
}

// Turns the target's up onto rDir and moves it to rPos; does nothing while rDir is near zero.
void SpinDriverCamera::updateTargetMatrix(const Vec3f& rDir, const Vec3f& rPos) {
    if (MR::isNearZero(rDir)) {
        return;
    }

    Pos3f targetMtx = mTargetMtx->mMatrix;

    Vec3f yDir;
    targetMtx.getYDir(yDir);

    Vec3f dir;
    MR::normalize(rDir, &dir);

    Pos3f rotMtx;
    rotMtx.identity();
    rotMtx.setRotate(yDir, dir);

    targetMtx.concat(rotMtx, targetMtx);
    targetMtx.setTrans(rPos);

    mTargetMtx->setMtx(targetMtx);
}

bool SpinDriverCamera::isUseAppearCamera(LiveActor* pActor) const {
    if (mCameraInfo == nullptr) {
        return false;
    }

    return MR::getMultiActorCameraFrames(pActor, mCameraInfo, "出現イベント用") > 0;
}

void SpinDriverCamera::init(const TableIter& rIter, LiveActor* pActor) {
    ActorCameraInfo info(rIter);

    if (info.mCameraSetID == -1) {
        return;
    }

    s32 cameraNum = 1;
    MR::getPlacementArg3NoInit(rIter, &cameraNum);

    mCamera = new MultiEventCamera();
    mCamera->setUp(pActor->mName, new ActorCameraInfo(rIter), cameraNum);
    mCamera->setEndCameraTypeAtLanding();

    mTargetMtx = new CameraTargetMtx("カメラターゲットダミー");
}

void SpinDriverCamera::initAppearCamera(const TableIter& rIter, LiveActor* pActor) {
    ActorCameraInfo info(rIter);

    if (info.mCameraSetID == -1) {
        return;
    }

    MR::initMultiActorCamera(pActor, rIter, &mCameraInfo, "出現イベント用");

    mAppearCameraFrame = MR::getMultiActorCameraFrames(pActor, mCameraInfo, "出現イベント用");
}
