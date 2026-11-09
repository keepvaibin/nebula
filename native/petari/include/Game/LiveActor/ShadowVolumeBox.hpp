#pragma once

#include "Game/LiveActor/ShadowVolumeDrawer.hpp"
#include "Game/Util/DirectDraw.hpp"

class ShadowVolumeBox : public ShadowVolumeDrawer {
public:
    ShadowVolumeBox();

    virtual void loadModelDrawMtx(TypedDraw& rDraw) const override;
    virtual void drawShape(TypedDraw& rDraw) const override;
    virtual u32 shapeVariant() const override {
        return mVariant;
    }

    void setSize(const Vec3f&);
    void makeVertexBuffer() const;

    Vec3f mSize;
    mutable Vec3f mPoints[14];
    // Which of the eight arrangements of mPoints makeVertexBuffer built (the drop direction's side of each face pair).
    mutable u32 mVariant = 0;
};
