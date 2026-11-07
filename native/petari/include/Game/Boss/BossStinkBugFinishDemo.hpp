#pragma once

#include "Game/Boss/BossStinkBugActionBase.hpp"

class DemoPositionController;
class TableIter;

class BossStinkBugFinishDemo : public BossStinkBugActionBase {
public:
    BossStinkBugFinishDemo(BossStinkBug* pHost, const TableIter& rIter);

    virtual void appear();

    void exeDemo();
    void exeTryStart();
    void exeAppearPowerStar();

    DemoPositionController* mDemoPositionController;
};
