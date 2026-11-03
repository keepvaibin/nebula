#pragma once

#include "Game/Screen/ImageEffectBase.hpp"

class JUTTexture;

class ScreenBlurEffect : public ImageEffectBase {
public:
    /// @brief Creates a new `ScreenBlurEffect`.
    /// @param pName A pointer to the null-terminated name of the object.
    ScreenBlurEffect(const char* pName);

    /// @brief Draws the `ScreenBlurEffect` to the screen.
    virtual void draw() const;

    virtual void calcAnimSub();
    virtual void notifyTurnOn();

    void drawBlur() const;
    void captureBlur() const;

    JUTTexture* _14;
    JUTTexture* _18;
    JUTTexture* mFrameDepthKey;  // the frame's depth, in the depth pass
    f32 mScale;
    bool _20;
    f32 mIntensity;
};
