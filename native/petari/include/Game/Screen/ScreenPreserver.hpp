#pragma once

#include "Game/NameObj/NameObj.hpp"

class ScreenPreserver : public NameObj {
public:
    /// @brief Creates a new `ScreenPreserver`.
    ScreenPreserver();

    virtual void draw() const override;

    void captureIfAllow();
    void activate();
    void deactivate();

    bool mIsActive;
};
