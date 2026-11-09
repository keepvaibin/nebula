#include "Game/Screen/CaptureScreenDirector.hpp"
#include "Game/Render/FrameList.hpp"
#include "Game/Scene/SceneFunction.hpp"
#include "Game/Util/MemoryUtil.hpp"
#include "Game/Util/ObjUtil.hpp"
#include "Game/Util/ScreenUtil.hpp"
#include "Game/System/NebulaArena.hpp"
#include "Game/Render/GameTexture.hpp"
#include "Game/System/NebulaFrameClock.hpp"
#include <cstring>

namespace {
    const TimingInfo cTimingInfo[] = {{"GameScreen", 32, true, 1}, {"Indirect", 1, false, 0},    {"Camera", 4, true, 1},
                                      {"WipeMap", 5, true, 1},     {"SystemWipe", 20, false, 1}, {"Unused", 16, false, 0}};
};  // namespace

CaptureScreenDirector::CaptureScreenDirector() : NameObj("画面キャプチャ"), mRequestedTiming(nullptr), mTimingType("Indirect"), mTexture(nullptr), mIsCaptureRequested(false) {
    MR::CurrentHeapRestorer heapRestorer(MR::getPermanentHeap());

    mTexture = GameTexture::createTarget(1.0f, 1.0f);
}

void CaptureScreenDirector::captureIfAllow(const char* pName) {
    const TimingInfo* pInfo = findFromName(pName);

    if (getUsingTiming() == pInfo && (getUsingTiming()->mRequestOnly == 0 || mIsCaptureRequested)) {
        capture();

        mIsCaptureRequested = false;
    }
}

void CaptureScreenDirector::capture() {
    mTexture->copyFrame();
}

void CaptureScreenDirector::requestCaptureTiming(const char* pName) {
    const TimingInfo* pInfo = findFromName(pName);

    if (getCurrentTiming() == nullptr || getCurrentTiming()->mTiming < pInfo->mTiming) {
        mRequestedTiming = pName;
        mIsCaptureRequested = true;
    }
}

void CaptureScreenDirector::invalidateCaptureTiming(const char* pName) {
    const TimingInfo* pInfo = getCurrentTiming();

    if (pInfo != nullptr) {
        if (strcmp(pInfo->mName, pName) == 0) {
            mRequestedTiming = nullptr;
        }
    }
}

const ResTIMG* CaptureScreenDirector::getResTIMG() const {
    return mTexture->info();
}

const TimingInfo* CaptureScreenDirector::getUsingTiming() const {
    if (mRequestedTiming != nullptr) {
        return findFromName(mRequestedTiming);
    }

    return findFromName(mTimingType);
}

const TimingInfo* CaptureScreenDirector::getCurrentTiming() const {
    if (mRequestedTiming != nullptr) {
        return findFromName(mRequestedTiming);
    }

    return nullptr;
}

const TimingInfo* CaptureScreenDirector::findFromName(const char* pName) const {
    for (u32 i = 0; i < ARRAY_SIZE(::cTimingInfo); i++) {
        if (strcmp(::cTimingInfo[i].mName, pName) == 0) {
            return &::cTimingInfo[i];
        }
    }

    return nullptr;
}

CaptureScreenActor::CaptureScreenActor(u32 drawType, const char* pCameraName) : NameObj("画面キャプチャ"), mCameraName(pCameraName) {
    MR::connectToScene(this, MR::MovementType_None, MR::CalcAnimType_None, MR::DrawBufferType_None, drawType);
}

void CaptureScreenActor::draw() const {
    MR::captureScreenIfAllow(mCameraName);
}
