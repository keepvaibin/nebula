#pragma once

#include "Game/LiveActor/LiveActor.hpp"
#include <JSystem/JGeometry/TMatrix.hpp>

class Rock;

class RockCreator : public LiveActor {
public:
    RockCreator(const char*);

    virtual void init(const JMapInfoIter& rIter);
    virtual void appear();
    virtual void kill();

    void invalidate();
    void initMapToolInfo(const JMapInfoIter& rIter);
    void create();

    void exeActive();
    void exeDeactive();

    inline bool isReadyToSpawn() const {
        return (mSpawnDelay != 0 ? mSpawnTimer % mSpawnDelay : mSpawnTimer) == 0;
    }

    Rock* mRocks;
    f32 mRockSpeed;
    bool mBreakModelOnRailGoal;
    s32 mSpawnDelay;
    s32 mNumRocks;
    u32 mSpawnTimer;
    bool mIsValid;
};
