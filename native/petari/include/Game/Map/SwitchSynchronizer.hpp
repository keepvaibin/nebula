#pragma once

#include "Game/NameObj/NameObj.hpp"

class StageSwitchCtrl;

class SwitchSynchronizer : public NameObj {
public:
    /// @brief Creates a new `SwitchSynchronizer`.
    /// @param pName A pointer to the null-terminated name of the object.
    SwitchSynchronizer(const char* pName);

    virtual void init(const JMapInfoIter&);
    virtual void movement();

    StageSwitchCtrl* mSwitchCtrl;
    bool mIsCheckSameSwitchState;
};
