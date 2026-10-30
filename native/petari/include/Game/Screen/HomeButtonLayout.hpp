#pragma once

#include "Game/Screen/LayoutActor.hpp"

class HomeButtonLayout : public LayoutActor {
public:
    /// @brief Creates a new `HomeButtonLayout`.
    HomeButtonLayout();

    virtual void init(const JMapInfoIter& rIter);
    virtual void movement();
    virtual void draw() const;

    void forceToDeactive();
    bool isActive() const;
    bool tryCorePadTriggerHome();

    void exeDeactive();
    void exeActive();

    bool mIsInitialized;
    /* 0x24 */ bool _24;
    /* 0x25 */ bool _25;
};
