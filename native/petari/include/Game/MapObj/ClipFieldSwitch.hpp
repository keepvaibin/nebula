#pragma once

#include "Game/LiveActor/LiveActor.hpp"

class ClipFieldSwitch : public LiveActor {
public:
    ClipFieldSwitch(const char*);

    virtual void init(const TableIter&) override;
    virtual void control() override;
};