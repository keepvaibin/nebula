#pragma once

#include "Game/LiveActor/ShadowVolumeModel.hpp"

class ShadowVolumeCylinder : public ShadowVolumeModel {
public:
    ShadowVolumeCylinder();

    virtual ~ShadowVolumeCylinder();
    virtual void loadModelDrawMtx(TypedDraw& rDraw) const;

    void setRadius(f32);

    f32 mRadius;
};