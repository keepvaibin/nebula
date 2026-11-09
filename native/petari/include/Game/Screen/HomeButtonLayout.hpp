#pragma once

#include "Game/Screen/LayoutActor.hpp"

class HomeButtonLayout : public LayoutActor {
public:
    /// @brief Creates a new `HomeButtonLayout`.
    HomeButtonLayout();

    virtual void init(const TableIter& rIter) override;
    virtual void movement() override;

    void forceToDeactive();
    bool isActive() const;
    bool tryPressedHome();

    void exeDeactive();
    void exeActive();

    bool mIsInitialized;
    bool mForcedClosed;
};
