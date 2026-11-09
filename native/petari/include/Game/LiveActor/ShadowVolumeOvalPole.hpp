#pragma once

#include "Game/LiveActor/ShadowVolumeModel.hpp"

class ShadowVolumeOvalPole : public ShadowVolumeModel {
public:
    ShadowVolumeOvalPole();

    virtual ~ShadowVolumeOvalPole() override;
    virtual void loadModelDrawMtx(TypedDraw& rDraw) const override;

    void setSize(const Vec3f&);

    Vec3f mSize;
};