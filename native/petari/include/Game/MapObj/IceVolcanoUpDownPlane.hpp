#pragma once

#include "Game/MapObj/RailMoveObj.hpp"

class IceVolcanoUpDownPlane : public RailMoveObj {
public:
    IceVolcanoUpDownPlane(const char*);

    void move() override;
    bool tryCalcNearestPosToPlayer(Vec3f*) const;
    Vec3f* getSoundCalcPos() override;

    Vec3f mNearestPosToPlayer;
};
