#pragma once

#include "Game/MapObj/MapObjActor.hpp"

class TimeAppearObj : public MapObjActor {
public:
    TimeAppearObj(const char*);

    virtual void init(const TableIter&) override;
    virtual void appear() override;

    void exeHide();
    void exeEnd();

    s32 mTimer;
};
