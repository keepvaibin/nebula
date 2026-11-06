#pragma once

#include "Game/LiveActor/ShadowVolumeDrawer.hpp"
#include "Game/Util/DirectDraw.hpp"

class ShadowVolumeBox : public ShadowVolumeDrawer {
public:
    ShadowVolumeBox();

    virtual void loadModelDrawMtx(TypedDraw& rDraw) const;
    virtual void drawShape(TypedDraw& rDraw) const;

    void setSize(const Vec3f&);
    void makeVertexBuffer() const;

    Vec3f mSize;
    mutable Vec3f mPoints[14];
};
