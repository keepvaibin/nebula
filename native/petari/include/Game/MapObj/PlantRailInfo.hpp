#pragma once

#include "Game/Util/JMapInfo.hpp"
#include <JSystem/JGeometry/TVec.hpp>

class PlantPoint;
class RailRider;

class PlantRailInfo {
public:
    PlantRailInfo(const JMapInfoIter&, f32);

    void calcPosAndAxisY(TVec3f*, TVec3f*, f32) const;
    void calcNearestPointPos(TVec3f*, const TVec3f&, s32) const;

    inline PlantPoint* getPlantPoint(s32 idx) {
        return mPlantPoints[idx];
    }

    s32 mNumPlantPoints;
    PlantPoint** mPlantPoints;
    RailRider* mRailRider;
    f32 mRailSpeed;
};
