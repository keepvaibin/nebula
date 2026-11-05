#pragma once

#include "Game/LiveActor/ShadowSurfaceDrawer.hpp"
#include "Game/Util/Color.hpp"
#include <nebula_geometry.hpp>

class J3DModelData;

class ShadowSurfaceOval : public ShadowSurfaceDrawer {
public:
    ShadowSurfaceOval();

    virtual ~ShadowSurfaceOval();

    virtual void draw() const override;

    void setSize(const Vec3f&);
    void setColor(Color8);
    void setAlpha(u8);

    J3DModelData* mModelData;
    Vec3f mSize;
    Color8 mColor;
};
