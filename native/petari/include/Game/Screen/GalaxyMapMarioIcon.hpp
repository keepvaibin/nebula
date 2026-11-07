#pragma once

#include "Game/Screen/LayoutActor.hpp"

class GalaxyMapMarioIcon : public LayoutActor {
public:
    GalaxyMapMarioIcon(LayoutActor*, const char*);

    virtual void init(const TableIter& rIter);
    virtual void control();

    void showBlink();

private:
    LayoutActor* _20;
    const char* _24;
};
