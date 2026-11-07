#pragma once

#include "Game/Boss/BossStinkBugActionBase.hpp"

class DemoPositionController;
class TableIter;

class BossStinkBugAngryDemo : public BossStinkBugActionBase {
public:
    BossStinkBugAngryDemo(BossStinkBug* pHost, const TableIter& rIter);

    virtual void appear();

    void exeDemo();
    void exeTryStart();

    DemoPositionController* mDemoPositionController;
};
