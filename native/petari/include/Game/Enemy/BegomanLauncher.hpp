#pragma once

#include "Game/LiveActor/LiveActor.hpp"

class BegomanBase;

class BegomanLauncher : public LiveActor {
public:
    BegomanLauncher(const char* pName);

    virtual void init(const JMapInfoIter& rIter);
    virtual void initAfterPlacement();
    virtual void kill();

    void onSwitchAppear();
    void exeWaitAfterSwitchOn();
    void exeWait();
    void exeWaitForLaunch();
    void exeLaunch();
    void exeAppear();
    void exeDisappear();

    s32 mBegomanCount;
    s32 mAppearDelay;
    BegomanBase** mBegomanArray;
    s32 mLaunchType;
};
