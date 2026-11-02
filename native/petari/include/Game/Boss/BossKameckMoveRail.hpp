#pragma once

#include "Game/LiveActor/LiveActor.hpp"

class BossKameckMoveRail : public LiveActor {
public:
    BossKameckMoveRail(const char*);

    virtual void init(const JMapInfoIter&);

    void calcMovePosition(TVec3f*, s32) const;
    s32 calcRandomIndex(s32) const;
    s32 calcRandomStoppableIndex(s32) const;

    s32 _8C;
    s32 _90;
};
