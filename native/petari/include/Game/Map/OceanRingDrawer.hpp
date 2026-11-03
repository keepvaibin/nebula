#pragma once

#include "Game/Util/NativeMesh.hpp"
#include <JSystem/JGeometry.hpp>
#include <JSystem/JUtility/JUTTexture.hpp>
#include <NebulaGpu.h>
#include <nebula_math.h>

class OceanRing;

class OceanRingPartDrawer {
public:
    OceanRingPartDrawer(const OceanRing*, int, int, bool, f32*, f32*, f32*);

    void initMesh(f32*, f32*, f32*);
    void draw() const;
    void drawGD(f32*, f32*, f32*);
    void drawDynamic() const;
    void drawDynamicBloom() const;

    const OceanRing* mOceanRing;
    TVec3f mPosition;
    int _10;
    int _14;
    bool _18;
    u8 _19;
    u8 _1A;
    u8 _1B;
    f32 _1C;
    f32 _20;
    f32 _24;
    // The ring's surface as the renderer draws it from far away.
    NativeMesh mMesh;
};

class OceanRingDrawer {
public:
    OceanRingDrawer(const OceanRing*);

    void update();
    void draw() const;
    void drawBloom() const;
    void initParts();
    void initMesh();
    void drawGD();
    void loadMaterial() const;
    void loadMaterialBloom() const;

    OceanRingPartDrawer* getDrawer(int idx) const {
        return mPartDrawers[idx];
    }

    inline f32 someInline(f32 a1, f32 a2, f32 a3) const {
        return (a1 - a2) / a3;
    }

    const OceanRing* mRing;
    s32 mDrawerCount;
    OceanRingPartDrawer** mPartDrawers;
    f32 _C;
    f32 _10;
    f32 _14;
    f32 _18;
    f32 _1C;
    f32 _20;
    JUTTexture* mWaterTex;
    JUTTexture* mWaterIndTex;
    // The ring's surface as the renderer draws it from far away.
    NativeMesh mMesh;
};
