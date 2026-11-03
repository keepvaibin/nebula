#pragma once

#include <NebulaGpu.h>

class JUTTexture;

class ImageEffectResource {
public:
    /// @brief Creates a new `ImageEffectResource`.
    ImageEffectResource();

    void createBlurTexture();
    void createDOFTexture();
    JUTTexture* createTexture(f32);

    JUTTexture* _0;
    JUTTexture* _8;
    JUTTexture* _C;
};
