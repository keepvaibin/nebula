#pragma once

#include "Game/Render/TypedDraw.hpp"
#include <nebula_geometry.hpp>
#include <JSystem/JUtility/JUTTexture.hpp>
#include <NebulaGpu.h>
#include <nebula_math.h>

class OceanRing;

class OceanRingPartDrawer {
public:
    OceanRingPartDrawer(const OceanRing*, int, int, bool, f32*, f32*, f32*);

    void initMesh(f32*, f32*, f32*);
    void draw(TypedDraw& rDraw) const;
    void drawGD(f32*, f32*, f32*);
    void drawDynamic(TypedDraw& rDraw) const;
    void drawDynamicBloom(TypedDraw& rDraw) const;

    const OceanRing* mOceanRing;
    Vec3f mPosition;
    int mStartIndex;
    int mNumPoints;
    bool mIsClosed;
    f32 mTexU0;
    f32 mTexU1;
    f32 mTexU2;
    // The ring's surface as the renderer draws it from far away.
    SiteMesh< VertexPCT3 > mMesh;
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
    SiteMaterial loadMaterial() const;
    SiteMaterial loadMaterialBloom() const;

    OceanRingPartDrawer* getDrawer(int idx) const {
        return mPartDrawers[idx];
    }

    inline f32 someInline(f32 a1, f32 a2, f32 a3) const {
        return (a1 - a2) / a3;
    }

    const OceanRing* mRing;
    s32 mDrawerCount;
    OceanRingPartDrawer** mPartDrawers;
    f32 mTex0U;
    f32 mTex0V;
    f32 mTex1U;
    f32 mTex1V;
    f32 mTex2V;
    JUTTexture* mWaterTex;
    JUTTexture* mWaterIndTex;
    // The ring's surface as the renderer draws it from far away.
    SiteMesh< VertexPCT3 > mMesh;
};
