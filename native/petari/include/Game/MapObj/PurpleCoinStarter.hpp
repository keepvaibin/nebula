#pragma once

#include "Game/NameObj/NameObj.hpp"

class PurpleCoinHolder;
class StageSwitchCtrl;

class PurpleCoinStarter : public NameObj {
public:
    PurpleCoinStarter(const char* pName);

    virtual void init(const TableIter& rIter) override;

    void setHost(PurpleCoinHolder* pHost);
    void start();

    PurpleCoinHolder* mHost;
    StageSwitchCtrl* mStageSwitchCtrl;
};
