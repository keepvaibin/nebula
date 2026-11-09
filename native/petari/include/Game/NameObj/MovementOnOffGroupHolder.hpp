#pragma once

#include "Game/NameObj/NameObj.hpp"
#include "Game/Util/Array.hpp"

class NameObjGroup;

class MovementOnOffGroupHolder : public NameObj {
public:
    MovementOnOffGroupHolder(const char* pName);

    NameObjGroup* joinToGroup(const char*, NameObj*, u32);
    void onMovementGroup(const char*);
    NameObjGroup* createGroup(const char*, u32);
    NameObjGroup* findGroupFromName(const char*) const;

private:
    MR::InlineVector< NameObjGroup*, 32 > mGroups;
};
