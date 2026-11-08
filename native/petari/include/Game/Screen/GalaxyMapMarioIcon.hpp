#pragma once

#include "Game/Screen/LayoutActor.hpp"

class GalaxyMapMarioIcon : public LayoutActor {
public:
    GalaxyMapMarioIcon(LayoutActor*, const char*);

    virtual void init(const TableIter& rIter);
    virtual void control();

    void showBlink();

private:
    LayoutActor* mTargetLayout;  // layout whose pane this icon follows
    const char* mTargetPaneName;  // the pane it follows
};
