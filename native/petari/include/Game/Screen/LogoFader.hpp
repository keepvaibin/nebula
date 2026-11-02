#pragma once

#include "Game/Screen/LayoutActor.hpp"

class LogoFader : public LayoutActor {
public:
    /// @brief Creates a new `LogoFader`.
    /// @param pName The pointer to the null-terminated name of the object.
    LogoFader(const char* pName);

    virtual void draw() const;

    void exeBlank();
    void exeDisplay();
    void exeFadeIn();
    void exeFadeOut();
    void setBlank();
    void startFadeIn();
    void startFadeOut();
    bool isFadeEnd() const;

    bool _20;
    u32 mMaxStep;
    f32 mRate;
};
