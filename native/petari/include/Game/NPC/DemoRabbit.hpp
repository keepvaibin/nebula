#pragma once

#include "Game/NPC/NPCActor.hpp"

class DemoRabbit : public NPCActor {
public:
    DemoRabbit(const char*);

    virtual void init(const TableIter& rIter) override;
    virtual void initAfterPlacement() override;
    virtual void control() override;

    void fadeOut();
    void fadeIn();
    void updateStopVelocity();
    void updateNormalVelocity();
    void updateRun(const Vec3f&, bool);
    void updateJump();
    bool tryGuide();
    bool tryWait();
    bool tryGoal();

    void exeAppear();
    void exeDemo();
    void exeTalk();
    void exeWait();
    void exeGoal();
    void exeGuide();
    void exeRunaway();
    void exeChange();
    void exeStartBGM();

    Vec3f mFrontVec;
    s32 mAirTimer;
};
