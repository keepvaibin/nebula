#pragma once

#include "Game/LiveActor/LiveActor.hpp"

class StageSwitchCtrl;

class GroupSwitchWatcher : public LiveActor {
public:
    GroupSwitchWatcher(const char*);

    virtual void init(const JMapInfoIter&);
    virtual void control();

    StageSwitchCtrl* mSwitchCtrl;
    s32 mNumSwitches;
    s32 mLogicType;
    s32 mActionType;
};
