#pragma once

#include "JSystem/J3DGraphBase/J3DTevs.hpp"
#include "JSystem/J3DGraphBase/J3DTexture.hpp"

// A material's colours and channels. Which colours and whether it loads lights depends on its kind.
class J3DColorBlock {
public:
    enum Kind {
        Kind_LightOff,   // material colours
        Kind_AmbientOn,  // and ambient colours
        Kind_LightOn,    // and lights
    };

    explicit J3DColorBlock(Kind kind);

    void reset(J3DColorBlock* pBlock);
    Kind getKind() const {
        return mKind;
    }

    void setMatColor(u32 idx, J3DColor const* color) {
        mMatColor[idx] = *color;
    }
    void setMatColor(u32 idx, J3DColor color) {
        mMatColor[idx] = color;
    }
    J3DColor* getMatColor(u32 idx) {
        return &mMatColor[idx];
    }
    void setAmbColor(u32 idx, J3DColor const* color) {
        setAmbColor(idx, *color);
    }
    void setAmbColor(u32 idx, J3DColor color) {
        if (mKind != Kind_LightOff) {
            mAmbColor[idx] = color;
        }
    }
    J3DColor* getAmbColor(u32 idx) {
        return mKind != Kind_LightOff ? &mAmbColor[idx] : nullptr;
    }
    void setColorChanNum(u8 num) {
        mColorChanNum = num;
    }
    void setColorChanNum(u8 const* num) {
        mColorChanNum = *num;
    }
    u8 getColorChanNum() const {
        return mColorChanNum;
    }
    void setColorChan(u32 idx, J3DColorChan const& chan) {
        mColorChan[idx] = chan;
    }
    void setColorChan(u32 idx, J3DColorChan const* chan) {
        mColorChan[idx] = *chan;
    }
    J3DColorChan* getColorChan(u32 idx) {
        return &mColorChan[idx];
    }
    void setLight(u32 idx, J3DLightObj* pLight) {
        if (mKind == Kind_LightOn) {
            mLight[idx] = pLight;
        }
    }
    J3DLightObj* getLight(u32 idx) {
        return mKind == Kind_LightOn ? mLight[idx] : nullptr;
    }
    void setCullMode(u8 const* mode) {
        mCullMode = *mode;
    }
    void setCullMode(u8 mode) {
        mCullMode = mode;
    }
    u8 getCullMode() const {
        return mCullMode;
    }

private:
    Kind mKind;
    J3DColor mMatColor[2];
    J3DColor mAmbColor[2];
    u8 mColorChanNum;
    J3DColorChan mColorChan[4];
    J3DLightObj* mLight[8];
    u8 mCullMode;
};

// A material's texture coordinates and their matrices. `capacity` of each; `hasScale`: its normal, binormal scale.
class J3DTexGenBlock {
public:
    J3DTexGenBlock(u32 capacity, bool hasScale);

    void reset(J3DTexGenBlock* pBlock);
    void calc(f32 const (*)[4]);
    void calcWithoutViewMtx(f32 const (*)[4]);
    void calcPostTexMtx(f32 const (*)[4]);
    void calcPostTexMtxWithoutViewMtx(f32 const (*)[4]);

    void setTexGenNum(u32 const* num) {
        mTexGenNum = *num;
    }
    void setTexGenNum(u32 num) {
        mTexGenNum = num;
    }
    u32 getTexGenNum() const {
        return mTexGenNum;
    }
    void setTexCoord(u32 idx, J3DTexCoord const* coord) {
        mTexCoord[idx] = *coord;
    }
    J3DTexCoord* getTexCoord(u32 idx) {
        return &mTexCoord[idx];
    }
    void setTexMtx(u32 idx, J3DTexMtx* mtx) {
        mTexMtx[idx] = mtx;
    }
    J3DTexMtx* getTexMtx(u32 idx) {
        return mTexMtx[idx];
    }
    void setNBTScale(J3DNBTScale const* scale) {
        setNBTScale(*scale);
    }
    void setNBTScale(J3DNBTScale scale) {
        if (mHasScale) {
            mNBTScale = scale;
        }
    }
    J3DNBTScale* getNBTScale() {
        return mHasScale ? &mNBTScale : nullptr;
    }

private:
    u32 mCapacity;
    bool mHasScale;
    u32 mTexGenNum;
    J3DTexCoord mTexCoord[8];
    J3DTexMtx* mTexMtx[8];
    J3DNBTScale mNBTScale;
};

// What a material's stage block holds at run time (the cook made its shader): its textures and colour registers.
class J3DRegisterBlock {
public:
    // `texCapacity` textures; `hasTextures` false for a material with no block of its own; `hasColors` false for the
    // one-stage block, which keeps the colours set before.
    J3DRegisterBlock(u8 texCapacity, bool hasTextures, bool hasColors);

    void reset(J3DRegisterBlock* pBlock);
    u8 getTexCapacity() const {
        return mTexCapacity;
    }
    bool hasTextures() const {
        return mHasTextures;
    }
    void setTexNo(u32 idx, u16 const* texNo) {
        setTexNo(idx, *texNo);
    }
    void setTexNo(u32 idx, u16 texNo) {
        if (idx < mTexCapacity) {
            mTexNo[idx] = texNo;
        }
    }
    u16 getTexNo(u32 idx) const {
        return idx < mTexCapacity ? mTexNo[idx] : static_cast< u16 >(0xFFFF);
    }
    void setRegisterColor(u32 idx, J3DColorWide const* color) {
        setRegisterColor(idx, *color);
    }
    void setRegisterColor(u32 idx, J3DColorWide color) {
        if (mHasColors) {
            mRegisterColor[idx] = color;
        }
    }
    J3DColorWide* getRegisterColor(u32 idx) {
        return mHasColors ? &mRegisterColor[idx] : nullptr;
    }
    void setConstantColor(u32 idx, J3DColor const* color) {
        setConstantColor(idx, *color);
    }
    void setConstantColor(u32 idx, J3DColor color) {
        if (mHasColors) {
            mConstantColor[idx] = color;
        }
    }
    J3DColor* getConstantColor(u32 idx) {
        return mHasColors ? &mConstantColor[idx] : nullptr;
    }

private:
    u16 mTexNo[8];
    J3DColorWide mRegisterColor[4];
    J3DColor mConstantColor[4];
    u8 mTexCapacity;
    bool mHasTextures;
    bool mHasColors;
};

struct J3DIndTexMtx : public J3DIndTexMtxInfo {
    J3DIndTexMtx() {
        *(J3DIndTexMtxInfo*)this = j3dDefaultIndTexMtxInfo;
    }

    J3DIndTexMtx(const J3DIndTexMtxInfo& info) {
        *(J3DIndTexMtxInfo*)this = info;
    }

    J3DIndTexMtx(const J3DIndTexMtx& other) {
        memcpy((void*)this, &other, sizeof(J3DIndTexMtx));
    }

    ~J3DIndTexMtx() {
    }
};

// A material's indirect matrices.
class J3DIndBlock {
public:
    explicit J3DIndBlock(bool full) : mFull(full), mIndTexStageNum(0) {}

    void reset(J3DIndBlock* pBlock);
    bool isFull() const {
        return mFull;
    }
    void setIndTexStageNum(u8 num) {
        if (mFull) {
            mIndTexStageNum = num;
        }
    }
    u8 getIndTexStageNum() const {
        return mIndTexStageNum;
    }
    void setIndTexMtx(u32 idx, J3DIndTexMtx const* mtx) {
        setIndTexMtx(idx, *mtx);
    }
    void setIndTexMtx(u32 idx, J3DIndTexMtx mtx) {
        if (mFull) {
            mIndTexMtx[idx] = mtx;
        }
    }
    J3DIndTexMtx* getIndTexMtx(u32 idx) {
        return mFull ? &mIndTexMtx[idx] : nullptr;
    }

private:
    bool mFull;
    u8 mIndTexStageNum;
    J3DIndTexMtx mIndTexMtx[3];
};

// How a material meets the target. A preset's modes come from its material mode (opaque, edge, translucent), with no
// state of its own; the others keep their own, the full one fog too.
class J3DOutputBlock {
public:
    enum Kind {
        Kind_Opaque,
        Kind_TexEdge,
        Kind_Translucent,
        Kind_FogOff,
        Kind_Full,
    };

    explicit J3DOutputBlock(Kind kind);

    void reset(J3DOutputBlock* pBlock);
    Kind getKind() const {
        return mKind;
    }
    bool isPreset() const {
        return mKind < Kind_FogOff;
    }

    void setFog(J3DFog fog) {
        if (mKind == Kind_Full) {
            mFog.setFogInfo(fog.getFogInfo());
        }
    }
    void setFog(J3DFog* pFog) {
        if (mKind == Kind_Full) {
            mFog.setFogInfo(pFog->getFogInfo());
        }
    }
    J3DFog* getFog() {
        return mKind == Kind_Full ? &mFog : nullptr;
    }
    void setAlphaComp(J3DAlphaComp const* alphaComp) {
        setAlphaComp(*alphaComp);
    }
    void setAlphaComp(J3DAlphaComp const& alphaComp) {
        if (!isPreset()) {
            mAlphaComp = alphaComp;
        }
    }
    J3DAlphaComp* getAlphaComp() {
        return isPreset() ? nullptr : &mAlphaComp;
    }
    void setBlend(J3DBlend const* blend) {
        setBlend(*blend);
    }
    void setBlend(J3DBlend const& blend) {
        if (!isPreset()) {
            mBlend = blend;
        }
    }
    J3DBlend* getBlend() {
        return isPreset() ? nullptr : &mBlend;
    }
    void setZMode(J3DZMode const* zMode) {
        setZMode(*zMode);
    }
    void setZMode(J3DZMode zMode) {
        if (!isPreset()) {
            mZMode = zMode;
        }
    }
    J3DZMode* getZMode() {
        return isPreset() ? nullptr : &mZMode;
    }
    void setZCompLoc(u8 const* zCompLoc) {
        setZCompLoc(*zCompLoc);
    }
    void setZCompLoc(u8 zCompLoc) {
        if (!isPreset()) {
            mZCompLoc = zCompLoc;
        }
    }
    u8 getZCompLoc() const {
        return isPreset() ? 0 : mZCompLoc;
    }

private:
    Kind mKind;
    J3DFog mFog;
    J3DAlphaComp mAlphaComp;
    J3DBlend mBlend;
    J3DZMode mZMode;
    u8 mZCompLoc;
};

