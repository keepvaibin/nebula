#pragma once

#include "Game/LiveActor/ShadowDrawer.hpp"

class ShadowSurfaceDrawInit : public NameObj {
public:
    ShadowSurfaceDrawInit(const char*);

    virtual ~ShadowSurfaceDrawInit();

    static void initDraw();
    // The surface shadows' material (runtime/src/render/materials/flat_color.hlsl) in `rColor`: blended by alpha,
    // back faces culled, depth tested.
    static void sendColor(const NebulaColor8& rColor);
};

class ShadowSurfaceDrawer : public ShadowDrawer {
public:
    ShadowSurfaceDrawer(const char*);

    virtual ~ShadowSurfaceDrawer();
};

inline ShadowSurfaceDrawer::~ShadowSurfaceDrawer() {
}
