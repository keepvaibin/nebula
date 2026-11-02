#pragma once

#include <NebulaGpu.h>

class JUTTexture;

class ImageEffectResource {
public:
    /// @brief Creates a new `ImageEffectResource`.
    ImageEffectResource();

    void createBlurTexture();
    void createDOFTexture();
    JUTTexture* createTexture(f32, GpuTexFmt);

    /* 0x00 */ JUTTexture* _0;
    /* 0x04 */ JUTTexture* _4;
    /* 0x08 */ JUTTexture* _8;
    /* 0x0C */ JUTTexture* _C;
};
