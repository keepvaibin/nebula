#include "Game/Scene/StopSceneController.hpp"
#include "Game/System/NebulaArena.hpp"
#include "Game/Util/ObjUtil.hpp"
#include "Game/System/LumaCore.hpp"

StopSceneController::StopSceneController()
    : NameObj("StopSceneController"), mDelayRequests(nullptr), mDelayRequestNum(0), mDelayRequestMax(0), mFrame() {
    lumacore::setClass(this, lumacore::kClassId< StopSceneController >);
}

void StopSceneController::movementDelayRequests() {
    if ((mMovementFlag & MovementFlag_Off) != 0) {
        return;
    }

    s32 kept = 0;
    for (s32 i = 0; i < mDelayRequestNum; i++) {
        DelayRequest& rRequest = mDelayRequests[i];
        rRequest.mDelay--;

        if (rRequest.mDelay == 0) {
            MR::stopScene(rRequest.mFrame);
        } else {
            mDelayRequests[kept++] = rRequest;
        }
    }

    mDelayRequestNum = kept;
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

// Stops landing on one count apply as the longest of them: requestStopScene keeps the larger.
void StopSceneController::requestStopSceneDelay(s32 frame, s32 delay) {
    if (delay <= 0) {
        return;
    }

    for (s32 i = 0; i < mDelayRequestNum; i++) {
        DelayRequest& rRequest = mDelayRequests[i];
        if (rRequest.mDelay == delay) {
            if (rRequest.mFrame < frame) {
                rRequest.mFrame = frame;
            }
            return;
        }
    }

    if (mDelayRequestNum == mDelayRequestMax) {
        const s32 max = mDelayRequestMax > 0 ? mDelayRequestMax * 2 : 4;
        DelayRequest* pRequests = new (NebulaArena::findFromRoot(this), 0) DelayRequest[max];
        for (s32 i = 0; i < mDelayRequestNum; i++) {
            pRequests[i] = mDelayRequests[i];
        }
        delete[] mDelayRequests;
        mDelayRequests = pRequests;
        mDelayRequestMax = max;
    }

    DelayRequest& rRequest = mDelayRequests[mDelayRequestNum++];
    rRequest.mFrame = frame;
    rRequest.mDelay = delay;
}

void StopSceneController::movement() {
    if (mFrame > 0) {
        mFrame--;
    }
}

bool StopSceneController::isSceneStopped() const {
    return mFrame > 0;
}
