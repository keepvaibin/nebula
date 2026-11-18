#pragma once

#include "Game/Gravity/PlanetGravity.hpp"

class PointGravity : public PlanetGravity {
public:
    PointGravity();

    virtual void updateMtx(const Pos3f& rMtx) override;
    virtual bool calcOwnGravityVector(Vec3f* pDest, f32* pScalar, const Vec3f& rPosition) const override;
#if defined(NEBULA_PLAIN_MODULE)
    virtual bool calcBound(Vec3f* pCenter, f32* pRadius) const override;
#endif

    void setOrig(const Vec3f& rOrig) {
        mOrigPosition = rOrig;
    }

    Vec3f mOrigPosition;
    Vec3f mTranslation;
};
