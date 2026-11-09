#pragma once

#include <nebula_geometry.hpp>
#include <NebulaGpu.h>
#include <nebula_math.h>

class DynamicFurParam;
class FurParam;
class GameTexture;
struct SiteMaterial;
struct ResTIMG;

class FurDrawer {
public:
    // A value per shell, from the root to the tip along the curve.
    class CLayerParam {
    public:
        f32 calcValue(s32 layer, s32 numLayers) const;

        f32 mTip;
        f32 mRoot;
        f32 mCurve;
    };

    FurDrawer(u32 numLayers, ResTIMG* pBody);

    // The fur material of every shell: lights and fog from rParam (FurMulti gives every layer both).
    void setupMaterial(const DynamicFurParam& rParam) const;
    void setupLayerMaterial(s32 layer) const;
    // What setupMaterial and setupLayerMaterial have set.
    const SiteMaterial& material() const;
    // The strand map from the param's density, thickness and length mix; makes the texture on first use.
    void createFurMap(const FurParam& rParam);

    GameTexture* mBodyTexture;
    GameTexture* mFurTexture;
    s32 mNumLayers;
    CLayerParam mLength;
    f32* mShells;  // each layer's reach
    CLayerParam mBrightness;
    CLayerParam mAlpha;
    CLayerParam mColorBlend;
    f32 mFurUVScale;
    f32 mBodyUVScale;
    NebulaColor8 mColor;
};
