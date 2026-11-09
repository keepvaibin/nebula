#pragma once

#include "Game/LiveActor/LiveActor.hpp"

class StageSwitchCtrl;

class GroupSwitchWatcher : public LiveActor {
public:
    GroupSwitchWatcher(const char*);

    virtual void init(const TableIter&) override;
    virtual void control() override;

    StageSwitchCtrl* mSwitchCtrl;
    s32 mNumSwitches;
    s32 mLogicType;   // -1: every switch after B; 0: any one of them
    s32 mActionType;  // -1: A turns on once and the watcher ends; else A follows the group

private:
    bool isGroupOn() const;
};
