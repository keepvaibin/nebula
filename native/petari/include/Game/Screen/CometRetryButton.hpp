#pragma once

#include "Game/Screen/LayoutActor.hpp"

class ButtonPaneController;
class PlayerMissLeft;

class CometRetryButton : public LayoutActor {
public:
    /// @brief Creates a new `CometRetryButton`.
    /// @param pName The pointer to the null-terminated name of the object.
    CometRetryButton(const char* pName);

    virtual void init(const TableIter& rIter) override;
    virtual void draw() const override;
    virtual void appear() override;
    virtual void kill() override;
    virtual void control() override;

    void exeMissLeftWait();
    void exeWait();
    void exeDecided();
    void exeEnd();

private:
    ButtonPaneController* mButtonYesPaneCtrl;
    ButtonPaneController* mButtonNoPaneCtrl;
    PlayerMissLeft* mPlayerMissLeft;
};
