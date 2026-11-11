#include "Game/Screen/CaptureScreenDirector.hpp"
#include "Game/Render/FrameList.hpp"
#include "Game/Scene/SceneFunction.hpp"
#include "Game/System/Arenas.hpp"
#include "Game/Util/ObjUtil.hpp"
#include "Game/Util/ScreenUtil.hpp"
#include "Game/Util/StringUtil.hpp"
#include "Game/System/NebulaArena.hpp"
#include "Game/Render/GameTexture.hpp"
#include "Game/System/NebulaFrameClock.hpp"
#include <cstring>
#include "Game/System/LumaCore.hpp"

namespace {
    const TimingInfo cTimingInfo[] = {{"GameScreen", 32, 1}, {"Indirect", 1, 0},    {"Camera", 4, 1},
                                      {"WipeMap", 5, 1},     {"SystemWipe", 20, 1}, {"Unused", 16, 0}};
};  // namespace

CaptureScreenDirector::CaptureScreenDirector() : NameObj("画面キャプチャ"), mRequestedTiming(nullptr), mTimingType("Indirect"), mTexture(nullptr), mIsCaptureRequested(false), mRequestedInfo(nullptr), mTimingInfo(nullptr) {
    lumacore::setClass(this, lumacore::kClassId< CaptureScreenDirector >);
    NebulaArena::CurrentScope heapRestorer(Arenas::permanent());

    mTexture = GameTexture::createTarget(1.0f, 1.0f);
    mTimingInfo = findFromName(mTimingType);
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

    // A timing name the table lacks asks for nothing.
    if (pInfo == nullptr) {
        return;
    }

    if (getCurrentTiming() == nullptr || getCurrentTiming()->mTiming < pInfo->mTiming) {
        mRequestedTiming = pName;
        mRequestedInfo = pInfo;
        mIsCaptureRequested = true;
    }
}

void CaptureScreenDirector::invalidateCaptureTiming(const char* pName) {
    const TimingInfo* pInfo = getCurrentTiming();

    if (pInfo != nullptr) {
        if (MR::isEqualString(pInfo->mName, pName)) {
            mRequestedTiming = nullptr;
            mRequestedInfo = nullptr;
        }
    }
}

const ResTIMG* CaptureScreenDirector::getResTIMG() const {
    return mTexture->info();
}

const TimingInfo* CaptureScreenDirector::getUsingTiming() const {
    if (mRequestedTiming != nullptr) {
        return mRequestedInfo;
    }

    return mTimingInfo;
}

const TimingInfo* CaptureScreenDirector::getCurrentTiming() const {
    if (mRequestedTiming != nullptr) {
        return mRequestedInfo;
    }

    return nullptr;
}

const TimingInfo* CaptureScreenDirector::findFromName(const char* pName) const {
    for (u32 i = 0; i < ARRAY_SIZE(::cTimingInfo); i++) {
        if (MR::isEqualString(::cTimingInfo[i].mName, pName)) {
            return &::cTimingInfo[i];
        }
    }

    return nullptr;
}

CaptureScreenActor::CaptureScreenActor(u32 drawType, const char* pCameraName) : NameObj("画面キャプチャ"), mCameraName(pCameraName) {
    lumacore::setClass(this, lumacore::kClassId< CaptureScreenActor >);
    MR::connectToScene(this, MR::MovementType_None, MR::CalcAnimType_None, MR::DrawBufferType_None, drawType);
}

void CaptureScreenActor::draw() const {
    MR::captureScreenIfAllow(mCameraName);
}
