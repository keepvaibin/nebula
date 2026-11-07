#pragma once

#include "Game/Boss/BossStinkBugActionBase.hpp"

class DemoPositionController;
class TableIter;

class BossStinkBugFlyDemo : public BossStinkBugActionBase {
public:
    BossStinkBugFlyDemo(BossStinkBug* pHost, const TableIter& rIter);

    virtual void appear();

    void exeDemo();
    void exeTryStart();

    DemoPositionController* mDemoPositionController;
};
