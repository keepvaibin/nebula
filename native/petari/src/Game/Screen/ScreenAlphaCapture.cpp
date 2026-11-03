#include "Game/Screen/ScreenAlphaCapture.hpp"
#include <JSystem/J3DGraphBase/J3DNativeTarget.hpp>
#include <JSystem/J3DGraphBase/J3DNativeState.hpp>
#include "Game/Scene/SceneObjHolder.hpp"
#include "Game/Util/DrawUtil.hpp"
#include "Game/Util/MemoryUtil.hpp"
#include "Game/System/NebulaArena.hpp"
#include <JSystem/JUtility/JUTTexture.hpp>
#include "Game/System/NebulaFrameClock.hpp"

namespace {
    ScreenAlphaCapture* getScreenAlphaCapture() {
        return MR::getSceneObj< ScreenAlphaCapture >(SceneObj_ScreenAlphaCapture);
    }
};  // namespace

ScreenAlphaCapture::ScreenAlphaCapture(const char* pName) : NameObj(pName) {
    for (s32 i = 0; i < ARRAY_SIZE(mTexture); i++) {
        mTexture[i] = nullptr;
    }
}

void ScreenAlphaCapture::init(const JMapInfoIter& rIter) {
}

void ScreenAlphaCapture::createScreenAlpha(s32 textureIndex, f32 screenScale) {
    MR::CurrentHeapRestorer heapRestorer(MR::getSceneHeap());

    mTexture[textureIndex] = new JUTTexture(static_cast< s32 >(NebulaFrameClock::cScreenUnitsAcross) * screenScale, static_cast< s32 >(NebulaFrameClock::cScreenUnitsDown) * screenScale);
    mTexture[textureIndex]->mWrapS = NEBULA_WRAP_CLAMP;
    mTexture[textureIndex]->mWrapT = NEBULA_WRAP_CLAMP;
    mTexture[textureIndex]->mMinType = NEBULA_FILTER_LINEAR;
    mTexture[textureIndex]->mMagType = NEBULA_FILTER_LINEAR;
}

void ScreenAlphaCapture::captureScreenAlpha(s32 textureIndex) {
    J3DNativeTarget::take(mTexture[textureIndex]->mImage);
}

JUTTexture* ScreenAlphaCapture::getTexture(s32 index) {
    return mTexture[index];
}

namespace MR {
    void createScreenAlphaSceneObj(s32 textureIndex, f32 screenScale) {
        MR::createSceneObj(SceneObj_ScreenAlphaCapture);
        ::getScreenAlphaCapture()->createScreenAlpha(textureIndex, screenScale);
    }

    void captureScreenAlpha(s32 textureIndex) {
        ::getScreenAlphaCapture()->captureScreenAlpha(textureIndex);
    }

    JUTTexture* getScreenAlphaTexture(s32 index) {
        return ::getScreenAlphaCapture()->getTexture(index);
    }
};  // namespace MR
