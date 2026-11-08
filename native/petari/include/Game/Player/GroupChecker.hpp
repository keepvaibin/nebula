#pragma once

#include "Game/NameObj/NameObj.hpp"
#include "Game/Util/NameIndex.hpp"

// The names of the objects in an attribute group (an object belongs when its name is held).
class GroupChecker : public NameObj {
public:
    GroupChecker(const char*, u32);

    virtual ~GroupChecker();

    void add(const NameObj*);

    MR::NameIndex mNames;
};

class GroupCheckManager : public NameObj {
public:
    GroupCheckManager(const char*);

    virtual ~GroupCheckManager();

    void add(const NameObj*, s32);
    bool isExist(const NameObj*, s32);

    GroupChecker* mGroups[2];
};
