#pragma once

#include "Game/LiveActor/LiveActor.hpp"

class MeteorStrike;

class MeteorStrikeLauncher : public LiveActor {
public:
    MeteorStrikeLauncher(const char*);

    virtual void init(const TableIter&) override;
    virtual void appear() override;
    virtual void kill() override;

    void initMapToolInfo(const TableIter&);
    bool initRail(const TableIter&);
    MeteorStrike* getUnusedMeteorStrike();
    bool create();

    void exeCreate();
    void exeInterval();

private:
    MeteorStrike** mMeteorStrikeArray;
    s32 mMeteorStrikeCount;
    s32 mIntervalStep;  // Obj_arg1, in seconds
    s32 mCreateStep;
    bool mIsMeteorStrike;
    bool mIsValidMultiMeteor;  // Obj_arg2
};
