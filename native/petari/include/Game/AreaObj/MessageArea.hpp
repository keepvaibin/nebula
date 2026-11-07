#pragma once

#include "Game/AreaObj/AreaObj.hpp"

class MessageArea : public AreaObj {
public:
    MessageArea(int, const char*);

    virtual void init(const TableIter&);

    s32 mZoneID;
};
