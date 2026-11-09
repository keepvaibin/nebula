#pragma once

#include "Game/LiveActor/ShadowVolumeModel.hpp"

class ShadowVolumeSphere : public ShadowVolumeModel {
public:
    ShadowVolumeSphere();

    virtual ~ShadowVolumeSphere() override;
    virtual void loadModelDrawMtx(TypedDraw& rDraw) const override;
    virtual bool isDraw() const override;
    virtual void calcDrawAnchor(Vec3f* pOut) const override;

    void setRadius(f32);

    f32 mRadius;
};