#pragma once

#include "Game/Screen/LayoutActor.hpp"

class ButtonPaneController;

class GalaxySelectBackButton : public LayoutActor {
public:
    /// @brief Creates a new `GalaxySelectBackButton`.
    GalaxySelectBackButton();

    virtual void init(const TableIter&) override;
    virtual void appear() override;
    virtual void control() override;

    void disappear();
    void decide();
    bool isPointing() const;
    bool tryPointing();
    bool isAppearing() const;
    bool isDisappearing() const;
    void exeAppear();
    void exeDisappear();
    void exeDecide();

private:
    ButtonPaneController* mPaneCtrl;
};
