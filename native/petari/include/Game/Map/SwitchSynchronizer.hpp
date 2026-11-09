#pragma once

#include "Game/NameObj/NameObj.hpp"

class StageSwitchCtrl;

class SwitchSynchronizer : public NameObj {
public:
    SwitchSynchronizer(const char* pName);

    virtual void init(const TableIter&) override;
    virtual void movement() override;

    StageSwitchCtrl* mSwitchCtrl;
    bool mIsCheckSameSwitchState;
};
