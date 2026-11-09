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

    GameTexture* mFullTexture;     // screen size
    GameTexture* mHalfTexture;     // half the screen's size
    GameTexture* mQuarterTexture;  // a quarter of it
};
