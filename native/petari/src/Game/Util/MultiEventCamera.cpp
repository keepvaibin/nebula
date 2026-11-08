#include "Game/Util/MultiEventCamera.hpp"
#include "Game/LiveActor/ActorCameraInfo.hpp"
#include "Game/Util/CameraUtil.hpp"
#include "Game/Util/PlayerUtil.hpp"
#include <cstdio>
#include "Game/Util/StringUtil.hpp"

MultiEventCamera::MultiEventCamera() : mName(), mCameraNum(), mCameraIndex(-1), mFrame(), mFrameNum(), mCameraType(), mCameraInfo(), mTarget() {
}

bool MultiEventCamera::isEnd() const {
    return mCameraIndex < 0;
}

void MultiEventCamera::setUp(const char* pName, const ActorCameraInfo* pCameraInfo, s32 cameraNum) {
    mName = pName;
    mCameraInfo = pCameraInfo;
    mCameraNum = cameraNum;

    for (s32 i = 0; i < mCameraNum; i++) {
        declareEventCamera(i);
    }
}

void MultiEventCamera::setEndCameraTypeSoon() {
    mCameraType = CameraType_Soon;
}

void MultiEventCamera::setEndCameraTypeAtLanding() {
    mCameraType = CameraType_AtLanding;
}

void MultiEventCamera::start(s32 param1) {
    CameraTargetArg target = CameraTargetArg();

    start(target, param1);
}

void MultiEventCamera::start(const CameraTargetArg& rParam1, s32 param2) {
    if (mCameraNum <= 0) {
        return;
    }

    mFrame = 0;
    mCameraIndex = param2;
    mTarget = rParam1;

    startCamera();
}

void MultiEventCamera::update() {
    if (isEnd()) {
        return;
    }

    if (mFrameNum <= mFrame && mCameraIndex < mCameraNum - 1) {
        mFrame = 0;
        mCameraIndex++;

        startCamera();
    }

    mFrame++;
}

void MultiEventCamera::nextForce() {
    if (isEnd()) {
        return;
    }

    if (mCameraIndex < mCameraNum - 1) {
        mFrame = 0;
        mCameraIndex++;

        startCamera();
    } else {
        endCamera(mCameraType);
    }
}

void MultiEventCamera::endForce() {
    if (isEnd()) {
        return;
    }

    endCamera(mCameraType);
}

void MultiEventCamera::endForceSoon() {
    if (isEnd()) {
        return;
    }

    endCamera(CameraType_Soon);
}

void MultiEventCamera::endForceAtLanding() {
    if (isEnd()) {
        return;
    }

    endCamera(CameraType_AtLanding);
}

void MultiEventCamera::changeTargetPlayer() {
    CameraTargetArg target = CameraTargetArg();

    MR::setCameraTargetToPlayer(&target);
    changeTarget(target);
}

bool MultiEventCamera::isActive(s32 param1) const {
    MR::FormattedName name("%s:%03d:%02d番目", mName, mCameraInfo->mCameraSetID, param1);

    return MR::isEventCameraActive(mCameraInfo, name.c_str());
}

void MultiEventCamera::declareEventCamera(s32 param1) {
    MR::FormattedName name("%s:%03d:%02d番目", mName, mCameraInfo->mCameraSetID, param1);

    return MR::declareEventCamera(mCameraInfo, name.c_str());
}

void MultiEventCamera::startCamera() {
    MR::FormattedName name("%s:%03d:%02d番目", mName, mCameraInfo->mCameraSetID, mCameraIndex);

    MR::startEventCamera(mCameraInfo, name.c_str(), mTarget);

    mFrameNum = MR::getEventCameraFrames(mCameraInfo, name.c_str());
}

void MultiEventCamera::endCamera(s32 type) {
    MR::FormattedName name("%s:%03d:%02d番目", mName, mCameraInfo->mCameraSetID, mCameraIndex);

    switch (type) {
    case CameraType_Soon:
        MR::endEventCamera(mCameraInfo, name.c_str(), true);
        break;
    case CameraType_AtLanding:
        MR::endEventCameraAtLanding(mCameraInfo, name.c_str(), -1);
        break;
    }

    mCameraIndex = -1;
}

void MultiEventCamera::changeTarget(const CameraTargetArg& rTarget) {
    MR::FormattedName name("%s:%03d:%02d番目", mName, mCameraInfo->mCameraSetID, mCameraIndex);

    MR::changeEventCameraTarget(mCameraInfo, name.c_str(), rTarget);
}
