#pragma once

#include "Game/LiveActor/LiveActor.hpp"

class MeteorStrike;

class MeteorStrikeLauncher : public LiveActor {
public:
    MeteorStrikeLauncher(const char*);

    virtual void init(const JMapInfoIter&);
    virtual void appear();
    virtual void kill();

    void initMapToolInfo(const JMapInfoIter&);
    bool initRail(const JMapInfoIter&);
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
