#pragma once

#include "Game/MapObj/RailMoveObj.hpp"

class IceVolcanoUpDownPlane : public RailMoveObj {
public:
    IceVolcanoUpDownPlane(const char*);

    virtual ~IceVolcanoUpDownPlane();

    void move();
    bool tryCalcNearestPosToPlayer(Vec3f*) const;
    Vec3f* getSoundCalcPos();

    Vec3f mNearestPosToPlayer;
};
