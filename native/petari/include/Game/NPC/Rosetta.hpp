#pragma once

#include "Game/NPC/NPCActor.hpp"
#include "Game/Util/NPCUtil.hpp"

class NerveExecutor;
class TurnJointCtrl;

class Rosetta : public NPCActor {
public:
    /// @param pName The pointer to the null-terminated name of the object.
    Rosetta(const char* pName);

    virtual void init(const TableIter& rIter);
    virtual void control();
    virtual void calcAndSetBaseMtx();
    virtual bool receiveMsgPlayerAttack(u32 msg, HitSensor*, HitSensor*);

    bool branchFunc(u32);
    bool eventFunc(u32);

    void startDemo(NerveExecutor*);
    void endDemo();
    void endDemoWithInterpole();
    bool canUpdateStarePos() const;
    void exeDemo();
    void exeReaction();

private:
    FadeStarter mFadeStarter;
    NerveExecutor* mDemoExecutor;
    NerveExecutor* mTalkDemoExecutor;
    TurnJointCtrl* mTurnJointCtrl;
    s32 mObjArg0;
};
