#pragma once

#include "Game/NameObj/NameObj.hpp"
#include <nebula_geometry.hpp>

class GravityInfo;
class PlanetGravity;

// Every registered gravity, highest priority first, and the sum of them at a point.
class PlanetGravityManager : public NameObj {
public:
    PlanetGravityManager(const char* pName);

    // Skips the gravities hosted by requester and those outside gravityType (a GravityType mask). pInfo gets the
    // highest priority gravity that applies.
    bool calcTotalGravityVector(Vec3f* pGravity, GravityInfo* pInfo, const Vec3f& rPosition, u32 gravityType, const void* pHost) const;
    void registerGravity(PlanetGravity* pGravity);

private:
    // Moves the newest entry ahead of every lower priority one (the rest are in order).
    void sortGravities();
    // Swaps entry i with entry i - 1.
    void bubbleDown(int i);

    PlanetGravity** mGravities;
    s32 mMaxGravities;
    s32 mNumGravities;
};
