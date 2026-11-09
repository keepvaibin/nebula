#pragma once

#include "Game/Render/TypedDraw.hpp"

#include "Game/LiveActor/ShadowDrawer.hpp"

class ShadowSurfaceDrawInit : public NameObj {
public:
    ShadowSurfaceDrawInit(const char*);

    virtual ~ShadowSurfaceDrawInit() override;

    static void initDraw();
    // The surface shadows' material (runtime/src/render/materials/flat_color.hlsl) in `rColor`: blended by alpha,
    // back faces culled, depth tested.
    static void sendColor(const Color8& rColor);
    static const SiteMaterial& material();
};

class ShadowSurfaceDrawer : public ShadowDrawer {
public:
    ShadowSurfaceDrawer(const char*);

    virtual ~ShadowSurfaceDrawer() override;
    // A surface shadow stands where the drop meets the water: that point jumping starts a new object.
    virtual void calcDrawAnchor(Vec3f* pOut) const override;
};

inline ShadowSurfaceDrawer::~ShadowSurfaceDrawer() {
}
