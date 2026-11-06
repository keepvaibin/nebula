#pragma once

#include "Game/LiveActor/ShadowVolumeModel.hpp"

class ShadowVolumeSphere : public ShadowVolumeModel {
public:
    ShadowVolumeSphere();

    virtual ~ShadowVolumeSphere();
    virtual void loadModelDrawMtx(TypedDraw& rDraw) const;
    virtual bool isDraw() const;

    void setRadius(f32);

    f32 mRadius;
};