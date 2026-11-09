#pragma once

#include "Game/Screen/LayoutActor.hpp"

class GalaxyMapTitle : public LayoutActor {
public:
    /// @brief Creates a new `GalaxyMapTitle`.
    GalaxyMapTitle();

    virtual void init(const TableIter& rIter) override;
    virtual void appear() override;

    void setModeNormal();
    void setModeHideInformation();
    void startAstroMap();
    void startGalaxyMap();
    void changeToAstroMap();
    void changeToGalaxyMap();
    void exeHide();
    void exeDisplay();
    void exeDisappear();
    void exeAppearToAnother();
    void exeDisappearToAnother();
    bool tryDisappear();

private:
    const char* mMessageID;
};
