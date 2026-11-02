#pragma once

#include "Game/Screen/ImageEffectBase.hpp"

class BloomEffectSimple : public ImageEffectBase {
public:
    /// @brief Creates a new `BloomEffectSimple`.
    BloomEffectSimple();

    /// @brief Draws the `BloomEffectSimple` to the screen.
    virtual void draw() const;

    /* 0x30 */ s32 mMaskFilter;
    /* 0x34 */ u8 mThreshold;
    /* 0x38 */ f32 mIntensity;
};
