#pragma once

#include "Game/MapObj/RailMoveObj.hpp"

class IceVolcanoUpDownPlane : public RailMoveObj {
public:
    IceVolcanoUpDownPlane(const char*);

    virtual ~IceVolcanoUpDownPlane();

    void move();
    bool tryCalcNearestPosToPlayer(TVec3f*) const;
    TVec3f* getSoundCalcPos();

    TVec3f mNearestPosToPlayer;
};
