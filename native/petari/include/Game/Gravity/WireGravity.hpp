#pragma once

#include "Game/Gravity/PlanetGravity.hpp"
#include "Game/Util/Array.hpp"

class WireGravity : public PlanetGravity {
public:
    WireGravity();

    virtual bool calcOwnGravityVector(Vec3f* pDest, f32* pScalar, const Vec3f& rPosition) const override;
#if defined(NEBULA_PLAIN_MODULE)
    virtual bool calcBound(Vec3f* pCenter, f32* pRadius) const override;
#endif

    void setPointListSize(u32);
    void addPoint(const Vec3f&);

    MR::Vector< MR::AssignableArray< Vec3f > > mPoints;
};
