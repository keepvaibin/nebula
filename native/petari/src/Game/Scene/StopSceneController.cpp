#include "Game/Scene/StopSceneController.hpp"
#include "Game/Util/ObjUtil.hpp"

StopSceneController::StopSceneController() : NameObj("StopSceneController"), mDelayRequests(), mFrame() {
}

void StopSceneController::movementDelayRequests() {
    if ((mMovementFlag & MovementFlag_Off) != 0) {
        return;
    }

    for (DelayRequest& rRequest : mDelayRequests) {
        if (rRequest.mDelay == 0) {
            continue;
        }

        rRequest.mDelay--;

        if (rRequest.mDelay == 0) {
            MR::stopScene(rRequest.mFrame);
        }
    }
}

void StopSceneController::requestStopScene(s32 frame) {
    if (isSceneStopped()) {
        if (mFrame < frame) {
            mFrame = frame;
        }
    } else {
        mFrame = frame;
    }
}

void StopSceneController::requestStopSceneDelay(s32 frame, s32 delay) {
    for (DelayRequest& rRequest : mDelayRequests) {
        if (rRequest.mDelay != 0) {
            continue;
        }

        rRequest.mFrame = frame;
        rRequest.mDelay = delay;
        return;
    }
}

void StopSceneController::movement() {
    if (mFrame > 0) {
        mFrame--;
    }
}

bool StopSceneController::isSceneStopped() const {
    return mFrame > 0;
}
