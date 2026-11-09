#pragma once

#include "Game/Screen/LayoutActor.hpp"

class IconComet : public LayoutActor {
public:
    IconComet();

    virtual void init(const TableIter&) override;

    bool appearIfLanding(const char*);
    void appearByCometNameId(int);
    void exeWait();
};