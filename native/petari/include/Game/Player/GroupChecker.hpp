#pragma once

#include "Game/NameObj/NameObj.hpp"

class HashSortTable;

class GroupChecker : public NameObj {
public:
    GroupChecker(const char*, u32);

    virtual ~GroupChecker();
    virtual void initAfterPlacement();

    void add(const NameObj*);

    HashSortTable* mHashTable;
};

class GroupCheckManager : public NameObj {
public:
    GroupCheckManager(const char*);

    virtual ~GroupCheckManager();

    void add(const NameObj*, s32);
    bool isExist(const NameObj*, s32);

    GroupChecker* mGroups[2];
    u32 _14;
};