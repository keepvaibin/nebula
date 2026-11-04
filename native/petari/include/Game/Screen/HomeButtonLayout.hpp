#pragma once

#include "Game/Screen/LayoutActor.hpp"

class HomeButtonLayout : public LayoutActor {
public:
    /// @brief Creates a new `HomeButtonLayout`.
    HomeButtonLayout();

    virtual void init(const JMapInfoIter& rIter);
    virtual void movement();

    void forceToDeactive();
    bool isActive() const;
    bool tryPressedHome();

    void exeDeactive();
    void exeActive();

    bool mIsInitialized;
    bool mForcedClosed;
};
