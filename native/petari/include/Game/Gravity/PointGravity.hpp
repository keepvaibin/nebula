#pragma once

#include "Game/Gravity/PlanetGravity.hpp"

class PointGravity : public PlanetGravity {
public:
    PointGravity();

    virtual void updateMtx(const Pos3f& rMtx) override;
    virtual bool calcOwnGravityVector(Vec3f* pDest, f32* pScalar, const Vec3f& rPosition) const override;

    void setOrig(const Vec3f& rOrig) {
        mOrigPosition = rOrig;
    }

    Vec3f mOrigPosition;
    Vec3f mTranslation;
};
