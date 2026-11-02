#pragma once

#include "Game/System/NerveExecutor.hpp"
#include "Game/Util/JMapInfo.hpp"

class BossStinkBug;
class BossStinkBugActionBase;
class BossStinkBugActionFlyHigh;
class BossStinkBugActionFlyLow;
class BossStinkBugActionGround;
class BossStinkBugAngryDemo;
class BossStinkBugFinishDemo;
class BossStinkBugFlyDemo;
class BossStinkBugOpeningDemo;
class HitSensor;

class BossStinkBugActionSequencer : public NerveExecutor {
public:
    BossStinkBugActionSequencer(BossStinkBug* pStinkbug, const JMapInfoIter& rIter);

    bool isValidFollowId(s32 id) const;
    void attackSensor(HitSensor* pSender, HitSensor* pReceiver);
    bool receiveMsgPlayerAttack(u32 msg, HitSensor* pSender, HitSensor* pReceiver);
    bool receiveOtherMsg(u32 msg, HitSensor* pSender, HitSensor* pReceiver);
    bool startAction();
    void exeWaitStart();
    void exeOpeningDemo();
    void exeGround();
    void exeFlyDemo();
    void exeFlyLow();
    void exeAngryDemo();
    void exeFlyHigh();
    void exeFinishDemo();
    void exeEnd();

    BossStinkBug* mStinkBug;
    BossStinkBugActionBase* mCurrentAction;
    BossStinkBugOpeningDemo* mOpeningDemo;
    BossStinkBugActionGround* mActionGround;
    BossStinkBugFlyDemo* mFlyDemo;
    BossStinkBugActionFlyLow* mActionFlyLow;
    BossStinkBugAngryDemo* mAngryDemo;
    BossStinkBugActionFlyHigh* mActionFlyHigh;
    BossStinkBugFinishDemo* mFinishDemo;
};
