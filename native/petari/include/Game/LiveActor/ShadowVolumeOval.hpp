#pragma once

#include "Game/LiveActor/ShadowVolumeModel.hpp"

class ShadowVolumeOval : public ShadowVolumeModel {
public:
    ShadowVolumeOval();

    virtual void loadModelDrawMtx(TypedDraw& rDraw) const override;
    virtual bool isDraw() const override;
    virtual void calcDrawAnchor(Vec3f* pOut) const override;

    void setSize(const Vec3f&);

    Vec3f mSize;
};
