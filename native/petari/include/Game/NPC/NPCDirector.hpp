#pragma once

#include "Game/NameObj/NameObj.hpp"

class NPCItemParameterReader;
class ResourceHolder;

class NPCDirector : public NameObj {
public:
    NPCDirector();

    virtual void init(const TableIter& rIter) override;

    NPCItemParameterReader* mItemParameterReader;
    ResourceHolder* mDataResourceHolder;
};
