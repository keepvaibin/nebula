#pragma once

#include "Game/Screen/LayoutActor.hpp"

class IconAButton;

class PeachLetter : public LayoutActor {
public:
    /// @brief Creates a new `PeachLetter`.
    /// @param pName The pointer to the null-terminated name of the object.
    PeachLetter(const char* pName);

    virtual void init(const TableIter&) override;
    virtual void movement() override;
    virtual void appear() override;

    void exeAppear();
    void exeWait();
    void exeEnd();

private:
    /// @brief The pointer to the actor instance for the A Button.
    IconAButton* mAButtonIcon;
};
