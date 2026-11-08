#pragma once

#include "Game/Screen/ImageEffectBase.hpp"

class JUTTexture;

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

    JUTTexture* mFrameKey;       // the frame, in the blur source's pass
    JUTTexture* mFrameDepthKey;  // its depth, in the depth pass
    JUTTexture* mHalfTexture;  // half-size frame, filled with the depth
    JUTTexture* mQuarterTexture;  // quarter-size frame; the blur result
    JUTTexture* mDepthRampTexture;  // 128x2 ramp from distance to blur strength
    JUTTexture* mQuarter;  // the blur's source
    JUTTexture* mBlurScratch;
    f32 mIntensity;
    s32 mBlurMaxDist;
    s32 mBlurMinDist;
};
