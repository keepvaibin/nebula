#pragma once

#include "Game/Screen/LayoutActor.hpp"

class BackButton;
class ButtonPaneController;

class GalaxyConfirmLayout : public LayoutActor {
public:
    /// @brief Creates a new `GalaxyConfirmLayout`.
    GalaxyConfirmLayout();

    virtual void init(const TableIter& rIter) override;
    virtual void appear() override;
    virtual void control() override;

    bool isSelected() const;
    bool isSelectedYes() const;
    void requestMovementOn();
    void exeSelecting();
    void exeSelectedBackButton();
    void exeSelected();

private:
    ButtonPaneController* mPaneCtrl;
    BackButton* mBackButton;
};
