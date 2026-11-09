#pragma once

#include "Game/LiveActor/ShadowSurfaceDrawer.hpp"
#include "Game/Util/Color.hpp"
#include <nebula_geometry.hpp>

class ModelData;

class ShadowSurfaceOval : public ShadowSurfaceDrawer {
public:
    ShadowSurfaceOval();

    virtual ~ShadowSurfaceOval() override;

    virtual void draw() const override;

    void setSize(const Vec3f&);
    void setColor(Color8);
    void setAlpha(u8);

    ModelData* mModelData;
    Vec3f mSize;
    Color8 mColor;
};
