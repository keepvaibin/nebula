#pragma once

#include "Game/Screen/ImageEffectBase.hpp"

class GameTexture;

class DepthOfFieldBlur : public ImageEffectBase {
public:
    /// @brief Creates a new `DepthOfFieldBlur`.
    /// @param pName A pointer to the null-terminated name of the object.
    DepthOfFieldBlur(const char* pName);

    /// @brief Draws the `DepthOfFieldBlur` to the screen.
    virtual void draw() const;

    void drawZAlphaTex() const;
    void createBlurTexture() const;
    void drawFinal() const;

    GameTexture* mFrameKey;       // the frame, in the blur source's pass
    GameTexture* mFrameDepthKey;  // its depth, in the depth pass
    GameTexture* mHalfTexture;  // half-size frame, filled with the depth
    GameTexture* mQuarterTexture;  // quarter-size frame; the blur result
    GameTexture* mDepthRampTexture;  // 128x2 ramp from distance to blur strength
    GameTexture* mQuarter;  // the blur's source
    GameTexture* mBlurScratch;
    f32 mIntensity;
    s32 mBlurMaxDist;
    s32 mBlurMinDist;
};
