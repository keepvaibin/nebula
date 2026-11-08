#pragma once

#include "Game/AreaObj/AreaObj.hpp"

class TripodBossStepStartArea : public AreaObj {
public:
    TripodBossStepStartArea(int, const char*);

    virtual void init(const TableIter&);
    virtual void movement();
    virtual const char* getManagerName() const;

    s32 mSequenceNo;
};
