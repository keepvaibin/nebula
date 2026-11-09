#pragma once

#include "Game/LiveActor/LiveActor.hpp"
#include "Game/Util/Color.hpp"

class Material;
class Model;
class Mesh;
class GameTexture;

class NormalMapBase : public LiveActor {
public:
    NormalMapBase(const char*);

    virtual void draw() const;
    virtual void calcAnim();
    virtual void control();
    virtual void drawSetting(MtxPtr) const;
    virtual void drawMaterialSetting(Material*) const;
    virtual void setStageForObject() const;

    void setup(const char*);
    void initNormalMap();
    void setupLighting();
    void setupTexture();

    void updateLightMtx(MtxPtr) const;
    bool isNormalMapMaterial(const char*) const;
    void standardDraw(Model*) const;
    void shapeAnalyzeDraw(Model*) const;

    void drawShape(Model*, Mesh*) const;
    void loadTexture1(Material*) const;
    void createGradTexture();
    void swapColorGB(GameTexture*);
    void loadData(const char*, u32);

    void loadDiffuseGradTex(u32 slot) const;
    void loadNormalTex(const GameTexture*, u32 slot) const;

    GameTexture* mNormalTex;
    GameTexture** mTextures;  // one per model texture
    void* mImagePtr;
    s32 mGradTexMode;
    Mtx mDrawMtx;
    u8 mBackLightMode;
    Color10 mStageColor0;
    Color10 mStageColor1;
    u16 mDrawFlags;
    u16 mTexFlags;
    u8 mLightAMode;
    u8 mLightBMode;
    u16 mHardLightColorMask;
    u16 mHardLightAlphaMask;
    u8 mAmbColor;
    u8 mMatColor;
    u8 mAmbAlpha;
    u8 mMatAlpha;
    u16 mUseModelLightReg;
    u16 mUseModelStageColor;
    u8 mLightingHighLevel;
    u8 mLightingLowLevel;
};
