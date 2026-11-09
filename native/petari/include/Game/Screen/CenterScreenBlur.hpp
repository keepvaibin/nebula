#pragma once

#include "Game/LiveActor/LiveActor.hpp"

class CenterScreenBlur : public LiveActor {
public:
    /// @brief Creates a new `CenterScreenBlur`.
    CenterScreenBlur();

    /// @brief Intializes the `CenterScreenBlur` while being placed into a scene.
    /// @param rIter The reference to an iterator over a `Table`.
    virtual void init(const TableIter& rIter) override;

    /// @brief Draws the `CenterScreenBlur` to the screen.
    virtual void draw() const override;

    virtual void appear() override;

    void start(s32, f32, u8, s32, s32);
    void exeFadeIn();
    void exeKeep();
    void exeFadeOut();

    s32 mTime;
    s32 mFadeIn;
    s32 mFadeOut;
    f32 mOffset;
    u8 mAlpha;
    f32 mBlendRate;
};
