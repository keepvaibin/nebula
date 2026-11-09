#pragma once

#include "Game/NPC/NPCActor.hpp"
#include "Game/Util/NPCUtil.hpp"

class NerveExecutor;
class TurnJointCtrl;

class Rosetta : public NPCActor {
public:
    /// @param pName The pointer to the null-terminated name of the object.
    Rosetta(const char* pName);

    virtual void init(const TableIter& rIter) override;
    virtual void control() override;
    virtual void calcAndSetBaseMtx() override;
    virtual bool receiveMsgPlayerAttack(u32 msg, HitSensor*, HitSensor*) override;

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
    TurnJointCtrl* mTurnJointCtrl;
    s32 mObjArg0;
};
