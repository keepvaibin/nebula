#pragma once

#include "Game/LiveActor/ShadowVolumeModel.hpp"

class ShadowVolumeCylinder : public ShadowVolumeModel {
public:
    ShadowVolumeCylinder();

    virtual ~ShadowVolumeCylinder() override;
    virtual void loadModelDrawMtx(TypedDraw& rDraw) const override;

    void setRadius(f32);

    f32 mRadius;
};