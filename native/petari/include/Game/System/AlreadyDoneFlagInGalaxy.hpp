#pragma once

#include "Game/Util/Array.hpp"

class TableIter;

class AlreadyDoneInfo {
public:
    AlreadyDoneInfo();

    void clear();
    void init(const char*, s32, s32);
    bool isEqual(const AlreadyDoneInfo&) const;
    void set(bool);
    u16 mask() const;

    bool operator==(const AlreadyDoneInfo& other) const {
        return isEqual(other);
    }

    u16 _0;
    u16 _2;
    u16 _4;
};

class AlreadyDoneFlagInGalaxy {
public:
    AlreadyDoneFlagInGalaxy(int);

    void clear();
    u32 setupFlag(const char*, const TableIter&, u32*);
    void updateValue(int, u32);

    MR::Vector< MR::AssignableArray< AlreadyDoneInfo > > mDoneInfos;
};
