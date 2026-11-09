#pragma once

#include "Game/LiveActor/ShadowSurfaceDrawer.hpp"
#include <nebula_geometry.hpp>

class ShadowSurfaceBox : public ShadowSurfaceDrawer {
public:
    ShadowSurfaceBox();

    virtual ~ShadowSurfaceBox() override;

    virtual void draw() const override;

    void setSize(const Vec3f&);

    Vec3f mSize;
};
