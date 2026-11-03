#pragma once

#include "Game/Screen/ImageEffectBase.hpp"

class BloomEffectSimple : public ImageEffectBase {
public:
    /// @brief Creates a new `BloomEffectSimple`.
    BloomEffectSimple();

    /// @brief Draws the `BloomEffectSimple` to the screen.
    virtual void draw() const;

    s32 mMaskFilter;
    u8 mThreshold;
    f32 mIntensity;
};
