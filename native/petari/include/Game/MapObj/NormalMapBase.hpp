#pragma once

#include "Game/LiveActor/LiveActor.hpp"
#include "Game/Util/Color.hpp"

class BtkPlayer;
class J3DMaterial;
class J3DModel;
class J3DShape;
class JUTTexture;

class NormalMapBase : public LiveActor {
public:
    NormalMapBase(const char*);

    virtual ~NormalMapBase();
    virtual void draw() const;
    virtual void calcAnim();
    virtual void control();
    virtual void drawSetting(MtxPtr) const;
    virtual void drawMaterialSetting(J3DMaterial*) const;
    virtual void setStageForObject() const;
    virtual void setStageForObject_Material() const;

    void setup(const char*);
    void initNormalMap();
    void setupLighting();
    void setupTexture();

    void updateBtkMtx();
    void loadTexMtxBtk(J3DMaterial*) const;
    void updateLightMtx(MtxPtr) const;
    bool isNormalMapMaterial(const char*) const;
    void standardDraw(J3DModel*) const;
    void shapeAnalyzeDraw(J3DModel*) const;

    void drawShape(J3DModel*, J3DShape*) const;
    void loadTexture1(J3DMaterial*) const;
    void createGradTexture();
    void swapColorGB(JUTTexture*);
    void loadData(const char*, u32);

    void loadDiffuseGradTex(u32 slot) const;
    void loadNormalTex(const JUTTexture*, u32 slot) const;

    u32 _8C;
    JUTTexture* _90;
    JUTTexture* mNormalTex;
    JUTTexture* mTextures[16];
    u32 _D8;
    void* mImagePtr;
    u32 _E0;
    s32 mGradTexMode;
    u32 _E8;
    s32 _EC;
    u32 _F0;
    Mtx _F4;
    u8 mBackLightMode;
    u8 _125;
    u8 _126;
    u8 _127;
    Color10 mStageColor0;
    Color10 mStageColor1;
    u16 _138;
    u16 _13A;
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
    BtkPlayer* mBtkPlayer;
    u8 _150;
    u8 _151;
    u16 mBtkMtxCount;
    u16 mBtkMtxIndices[24];
    Mtx _184[0x10];
    u8 _484[0x34];
    JUTTexture* _4B8;
    int mHeight;
    int mWidth;
    u8 _4C4;
    u8 _4C5;
};
