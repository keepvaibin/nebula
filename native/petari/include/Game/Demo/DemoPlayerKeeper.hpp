#pragma once

#include "Game/Demo/DemoExecutor.hpp"

class DemoPlayerInfo {
public:
    DemoPlayerInfo();

    const char* mPartName;
    const char* mPosName;
    const char* mBckName;
};

class DemoPlayerKeeper {
public:
    DemoPlayerKeeper(const DemoExecutor*);
    void update();
    bool isExistPosName() const;
    void executePlayer(const DemoPlayerInfo*) const;

    const DemoExecutor* mExecutor;
    s32 mNumPlayerInfos;
    DemoPlayerInfo* mPlayerInfos;
};
