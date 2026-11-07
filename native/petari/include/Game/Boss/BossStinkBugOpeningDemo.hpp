#pragma once

#include "Game/Boss/BossStinkBugActionBase.hpp"

class TableIter;
class DemoPositionController;
class BossStinkBugOpeningDemo : public BossStinkBugActionBase {
public:
    BossStinkBugOpeningDemo(BossStinkBug* pStinkBug, const TableIter& rIter);

    virtual void appear();

    void exeDemo();
    void exeTryStart();

    DemoPositionController* mDemoPositionController;
};
