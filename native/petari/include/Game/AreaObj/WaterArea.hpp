#pragma once

#include "Game/AreaObj/AreaObj.hpp"

class WaterArea : public AreaObj {
public:
    WaterArea(int, const char*);

    virtual void init(const TableIter&);

};

class WaterAreaMgr : public AreaObjMgr {
public:
    WaterAreaMgr(s32, const char*);
};
