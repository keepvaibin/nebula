#include "Game/Screen/CaptureScreenDirector.hpp"
#include <JSystem/J3DGraphBase/J3DNativeState.hpp>
#include "Game/Scene/SceneFunction.hpp"
#include "Game/Util/MemoryUtil.hpp"
#include "Game/Util/ObjUtil.hpp"
#include "Game/Util/ScreenUtil.hpp"
#include "Game/System/NebulaArena.hpp"
#include <JSystem/JUtility/JUTTexture.hpp>
#include "Game/System/NebulaFrameClock.hpp"
#include <cstring>

namespace {
    const TimingInfo cTimingInfo[] = {{"GameScreen", 32, true, 1}, {"Indirect", 1, false, 0},    {"Camera", 4, true, 1},
                                      {"WipeMap", 5, true, 1},     {"SystemWipe", 20, false, 1}, {"Unused", 16, false, 0}};
};  // namespace

CaptureScreenDirector::CaptureScreenDirector() : NameObj("\211\346\226\312\203\114\203\203\203\166\203\140\203\203" /* 画面キャプチャ */), _C(nullptr), mTimingType("Indirect"), mTexture(nullptr), _18(false) {
    MR::CurrentHeapRestorer heapRestorer(MR::getPermanentHeap());

    mTexture = JUTTexture::createTarget(1.0f, 1.0f);
}

void CaptureScreenDirector::captureIfAllow(const char* pName) {
    const TimingInfo* pInfo = findFromName(pName);

    if (getUsingTiming() == pInfo && (getUsingTiming()->_C == 0 || _18)) {
        capture();

        _18 = false;
    }
}

void CaptureScreenDirector::capture() {
    mTexture->frameColor();
}

void CaptureScreenDirector::requestCaptureTiming(const char* pName) {
    const TimingInfo* pInfo = findFromName(pName);

    if (getCurrentTiming() == nullptr || getCurrentTiming()->mTiming < pInfo->mTiming) {
        _C = pName;
        _18 = true;
    }
}

void CaptureScreenDirector::invalidateCaptureTiming(const char* pName) {
    const TimingInfo* pInfo = getCurrentTiming();

    if (pInfo != nullptr) {
        if (strcmp(pInfo->mName, pName) == 0) {
            _C = nullptr;
        }
    }
}

const ResTIMG* CaptureScreenDirector::getResTIMG() const {
    return mTexture->mTIMG;
}

const TimingInfo* CaptureScreenDirector::getUsingTiming() const {
    if (_C != nullptr) {
        return findFromName(_C);
    }

    return findFromName(mTimingType);
}

const TimingInfo* CaptureScreenDirector::getCurrentTiming() const {
    if (_C != nullptr) {
        return findFromName(_C);
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

CaptureScreenActor::CaptureScreenActor(u32 drawType, const char* pCameraName) : NameObj("\211\346\226\312\203\114\203\203\203\166\203\140\203\203" /* 画面キャプチャ */), mCameraName(pCameraName) {
    MR::connectToScene(this, MR::MovementType_None, MR::CalcAnimType_None, MR::DrawBufferType_None, drawType);
}

void CaptureScreenActor::draw() const {
    MR::captureScreenIfAllow(mCameraName);
}
