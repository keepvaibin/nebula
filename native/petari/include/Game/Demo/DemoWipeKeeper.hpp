#pragma once

#include "Game/Demo/DemoExecutor.hpp"

class DemoWipeInfo {
public:
    DemoWipeInfo();

    const char* mPartName;
    const char* mWipeName;
    s32 mWipeType;
    s32 mWipeFrame;
};

class DemoWipeKeeper : public DemoSheetKeeperBase, public DemoSheetKeeperInfoHolder< DemoWipeInfo > {
public:
    DemoWipeKeeper(DemoExecutor* pExecutor);

    virtual const char* getName() const;

    virtual const char* getTypeString() const;

    virtual void start();
    virtual void update();

    virtual void executeType(const DemoWipeInfo* pInfo);
};
