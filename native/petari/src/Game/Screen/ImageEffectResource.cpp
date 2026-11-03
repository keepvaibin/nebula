#include "Game/Screen/ImageEffectResource.hpp"
#include "Game/Util/MemoryUtil.hpp"
#include "Game/System/NebulaArena.hpp"
#include <JSystem/JUtility/JUTTexture.hpp>
#include "Game/System/NebulaFrameClock.hpp"

ImageEffectResource::ImageEffectResource()
    : _0(nullptr), _8(nullptr), _C(nullptr) {
}

void ImageEffectResource::createBlurTexture() {
    if (_0 == nullptr) {
        _0 = createTexture(1.0f);
    }

    if (_8 == nullptr) {
        _8 = createTexture(0.5f);
    }
}

void ImageEffectResource::createDOFTexture() {
    if (_0 == nullptr) {
        _0 = createTexture(1.0f);
    }

    if (_C == nullptr) {
        _C = createTexture(0.25f);
    }

    if (_8 == nullptr) {
        _8 = createTexture(0.5f);
    }
}

JUTTexture* ImageEffectResource::createTexture(f32 scale) {
    JUTTexture* pTexture;

    MR::CurrentHeapRestorer heapRestorer(MR::getSceneHeap());

    pTexture = new JUTTexture(scale * (s32)NebulaFrameClock::cScreenUnitsAcross, scale * (s32)NebulaFrameClock::cScreenUnitsDown);
    pTexture->mWrapS = NEBULA_WRAP_CLAMP;
    pTexture->mWrapT = NEBULA_WRAP_CLAMP;
    pTexture->mMinType = NEBULA_FILTER_LINEAR;
    pTexture->mMagType = NEBULA_FILTER_LINEAR;

    return pTexture;
}
