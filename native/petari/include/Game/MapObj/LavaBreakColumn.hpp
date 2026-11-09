#pragma once

#include "Game/MapObj/AnmModelObj.hpp"

class LavaBreakColumn : public AnmModelSwitchMoveEndKill {
public:
    LavaBreakColumn(const char*);

    virtual void init(const TableIter&) override;
    virtual void makeActorAppeared() override;
    virtual void control() override;

    Vec3f mEffectPos;
    bool mIsPillarFallen;
};
