#pragma once

#include "Game/LiveActor/ShadowSurfaceDrawer.hpp"

class ShadowSurfaceCircle : public ShadowSurfaceDrawer {
public:
    ShadowSurfaceCircle();

    virtual ~ShadowSurfaceCircle() override;

    virtual void draw() const override;

    void setRadius(f32);

    f32 mRadius;
};
