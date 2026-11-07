#pragma once

#include "Game/MapObj/AnmModelObj.hpp"

class LavaBreakColumn : public AnmModelSwitchMoveEndKill {
public:
    LavaBreakColumn(const char*);

    virtual void init(const TableIter&);
    virtual void makeActorAppeared();
    virtual void control();

    Vec3f mEffectPos;
    bool mIsPillarFallen;
};
