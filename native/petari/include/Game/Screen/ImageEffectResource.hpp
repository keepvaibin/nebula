#pragma once

#include <NebulaGpu.h>

class GameTexture;

class ImageEffectResource {
public:
    /// @brief Creates a new `ImageEffectResource`.
    ImageEffectResource();

    void createBlurTexture();
    void createDOFTexture();
    GameTexture* createTexture(f32);

    GameTexture* _0;
    GameTexture* _8;
    GameTexture* _C;
};
