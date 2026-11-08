#pragma once

#include "Game/Demo/DemoExecutor.hpp"

class DemoSoundInfo {
public:
    DemoSoundInfo();

    const char* mPartName;
    const char* mBgm;
    const char* mSystemSe;
    bool mReturnBgm;
    s32 mBgmWipeoutFrame;
};

class DemoSoundKeeper : public DemoSheetKeeperBase, public DemoSheetKeeperInfoHolder< DemoSoundInfo > {
public:
    DemoSoundKeeper(DemoExecutor* pExecutor);

    virtual const char* getName() const;

    virtual const char* getTypeString() const;

    virtual void update();

    virtual void executeType(const DemoSoundInfo* pInfo);
    bool isPermitBgmChange();
};
