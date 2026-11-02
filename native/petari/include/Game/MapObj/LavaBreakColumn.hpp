#pragma once

#include "Game/MapObj/AnmModelObj.hpp"

class LavaBreakColumn : public AnmModelSwitchMoveEndKill {
public:
    LavaBreakColumn(const char*);

    virtual void init(const JMapInfoIter&);
    virtual void makeActorAppeared();
    virtual void control();

    TVec3f mEffectPos;
    bool mIsPillarFallen;
};
