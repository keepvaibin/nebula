#include "Game/Screen/ImageEffectResource.hpp"
#include "Game/Util/MemoryUtil.hpp"
#include <JSystem/JKernel/JKRSolidHeap.hpp>
#include <JSystem/JUtility/JUTTexture.hpp>
#include <JSystem/JUtility/JUTVideo.hpp>

ImageEffectResource::ImageEffectResource()
    : _0(nullptr), _4(nullptr), _8(nullptr), _C(nullptr), _10(nullptr), _14(nullptr), _18(nullptr), _1C(nullptr), _20(nullptr) {
}

void ImageEffectResource::createBloomTexture() {
    if (_0 == nullptr) {
        _0 = createTexture(1.0f, GPU_TF_RGBA8);
    }

    if (_4 == nullptr) {
        _4 = createTexture(0.5f, GPU_TF_RGB565);
    }

    if (_C == nullptr) {
        _C = createTexture(0.25f, GPU_TF_RGB565);
    }

    if (_10 == nullptr) {
        _10 = createTexture(0.25f, GPU_TF_RGB565);
    }

    if (_14 == nullptr) {
        _14 = createTexture(0.125f, GPU_TF_RGB565);
    }

    if (_18 == nullptr) {
        _18 = createTexture(0.125f, GPU_TF_RGB565);
    }

    if (_1C == nullptr) {
        _1C = createTexture(0.25f, GPU_TF_I8);
    }

    if (_20 == nullptr) {
        _20 = createTexture(0.25f, GPU_TF_RGB565);
    }
}

void ImageEffectResource::createBlurTexture() {
    if (_0 == nullptr) {
        _0 = createTexture(1.0f, GPU_TF_RGBA8);
    }

    if (_8 == nullptr) {
        _8 = createTexture(0.5f, GPU_TF_I8);
    }
}

void ImageEffectResource::createDOFTexture() {
    if (_0 == nullptr) {
        _0 = createTexture(1.0f, GPU_TF_RGBA8);
    }

    if (_4 == nullptr) {
        _4 = createTexture(0.5f, GPU_TF_RGB565);
    }

    if (_C == nullptr) {
        _C = createTexture(0.25f, GPU_TF_RGB565);
    }

    if (_10 == nullptr) {
        _10 = createTexture(0.25f, GPU_TF_RGB565);
    }

    if (_8 == nullptr) {
        _8 = createTexture(0.5f, GPU_TF_I8);
    }
}

JUTTexture* ImageEffectResource::createTexture(f32 scale, GpuTexFmt format) {
    JUTTexture* pTexture;

    MR::CurrentHeapRestorer heapRestorer(MR::getSceneHeapGDDR3());

    pTexture = new JUTTexture(scale * (s32)JUTVideo::getManager()->getFbWidth(),
                              scale * (s32)JUTVideo::getManager()->getEfbHeight(), format);
    pTexture->mWrapS = GPU_CLAMP;
    pTexture->mWrapT = GPU_CLAMP;
    pTexture->mMinType = GPU_LINEAR;
    pTexture->mMagType = GPU_LINEAR;

    return pTexture;
}
