#pragma once

#include "Game/Util/Table.hpp"
#include <nebula_geometry.hpp>

class PlantPoint;
class RailRider;

class PlantRailInfo {
public:
    PlantRailInfo(const TableIter&, f32);

    void calcPosAndAxisY(Vec3f*, Vec3f*, f32) const;
    void calcNearestPointPos(Vec3f*, const Vec3f&, s32) const;

    inline PlantPoint* getPlantPoint(s32 idx) {
        return mPlantPoints[idx];
    }

    s32 mNumPlantPoints;
    PlantPoint** mPlantPoints;
    RailRider* mRailRider;
    f32 mRailSpeed;
};
