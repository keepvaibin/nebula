#pragma once

#include "Game/Screen/ImageEffectBase.hpp"

class GameTexture;

class ScreenBlurEffect : public ImageEffectBase {
public:
    /// @brief Creates a new `ScreenBlurEffect`.
    /// @param pName A pointer to the null-terminated name of the object.
    ScreenBlurEffect(const char* pName);

    /// @brief Draws the `ScreenBlurEffect` to the screen.
    virtual void draw() const override;

    virtual void calcAnimSub() override;
    virtual void notifyTurnOn() override;

    void drawBlur() const;
    void captureBlur() const;

    GameTexture* mFullTexture;  // full-size frame
    GameTexture* mHalfTexture;  // half-size frame, filled with the depth
    GameTexture* mFrameDepthKey;  // the frame's depth, in the depth pass
    f32 mScale;
    bool mSkipDraw;  // off until the first animation update
    f32 mIntensity;
};
