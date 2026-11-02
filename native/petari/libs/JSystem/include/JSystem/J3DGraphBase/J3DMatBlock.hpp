#pragma once

#include "JSystem/J3DGraphBase/J3DTevs.hpp"
#include "JSystem/J3DGraphBase/J3DTexture.hpp"

class J3DColorBlock {
public:

    virtual void reset(J3DColorBlock*) {
    }

    virtual u32 getType() = 0;
    virtual void setMatColor(u32, J3DGXColor const*) {
    }

    virtual void setMatColor(u32, J3DGXColor) {
    }

    virtual J3DGXColor* getMatColor(u32) {
        return nullptr;
    }

    virtual void setAmbColor(u32, J3DGXColor const*) {
    }

    virtual void setAmbColor(u32, J3DGXColor) {
    }

    virtual J3DGXColor* getAmbColor(u32) {
        return nullptr;
    }

    virtual void setColorChanNum(u8) {
    }

    virtual void setColorChanNum(u8 const*) {
    }

    virtual u8 getColorChanNum() const {
        return 0;
    }

    virtual void setColorChan(u32, J3DColorChan const&) {
    }

    virtual void setColorChan(u32, J3DColorChan const*) {
    }

    virtual J3DColorChan* getColorChan(u32) {
        return nullptr;
    }

    virtual void setLight(u32, J3DLightObj*) {
    }

    virtual J3DLightObj* getLight(u32) {
        return nullptr;
    }

    virtual void setCullMode(u8 const*) {
    }

    virtual void setCullMode(u8) {
    }

    virtual u8 getCullMode() const {
        return 2;
    }

    virtual u32 getMatColorOffset() const {
        return 0;
    }

    virtual u32 getColorChanOffset() const {
        return 0;
    }

    virtual void setMatColorOffset(u32) {
    }

    virtual void setColorChanOffset(u32) {
    }

    virtual ~J3DColorBlock() {
    }
};

class J3DColorBlockLightOff : public J3DColorBlock {
public:
    J3DColorBlockLightOff() {
        initialize();
    }

    void initialize();
    virtual void reset(J3DColorBlock*);
    virtual u32 getType() {
        return 'CLOF';
    }

    virtual void setMatColor(u32 idx, J3DGXColor const* color) {
        mMatColor[idx] = *color;
    }

    virtual void setMatColor(u32 idx, J3DGXColor color) {
        mMatColor[idx] = color;
    }

    virtual J3DGXColor* getMatColor(u32 idx) {
        return &mMatColor[idx];
    }

    virtual void setColorChanNum(u8 num) {
        mColorChanNum = num;
    }

    virtual void setColorChanNum(u8 const* num) {
        mColorChanNum = *num;
    }

    virtual u8 getColorChanNum() const {
        return mColorChanNum;
    }

    virtual void setColorChan(u32 idx, J3DColorChan const& chan) {
        mColorChan[idx] = chan;
    }

    virtual void setColorChan(u32 idx, J3DColorChan const* chan) {
        mColorChan[idx] = *chan;
    }

    virtual J3DColorChan* getColorChan(u32 idx) {
        return &mColorChan[idx];
    }

    virtual void setCullMode(u8 const* mode) {
        mCullMode = *mode;
    }

    virtual void setCullMode(u8 mode) {
        mCullMode = mode;
    }

    virtual u8 getCullMode() const {
        return mCullMode;
    }

    virtual u32 getMatColorOffset() const {
        return mMatColorOffset;
    }

    virtual u32 getColorChanOffset() const {
        return mColorChanOffset;
    }

    virtual void setMatColorOffset(u32 offset) {
        mMatColorOffset = offset;
    }

    virtual void setColorChanOffset(u32 offset) {
        mColorChanOffset = offset;
    }

    virtual ~J3DColorBlockLightOff() {
    }

    /* 0x04 */ J3DGXColor mMatColor[2];
    /* 0x0C */ u8 mColorChanNum;
    /* 0x0E */ J3DColorChan mColorChan[4];
    /* 0x16 */ u8 mCullMode;
    /* 0x18 */ u32 mMatColorOffset;
    /* 0x1C */ u32 mColorChanOffset;
};  // Size: 0x20

class J3DColorBlockAmbientOn : public J3DColorBlockLightOff {
public:
    J3DColorBlockAmbientOn() {
        initialize();
    }

    void initialize();
    virtual void reset(J3DColorBlock*);
    virtual u32 getType() {
        return 'CLAB';
    }

    virtual void setAmbColor(u32 idx, J3DGXColor const* color) {
        mAmbColor[idx] = *color;
    }

    virtual void setAmbColor(u32 idx, J3DGXColor color) {
        mAmbColor[idx] = color;
    }

    virtual J3DGXColor* getAmbColor(u32 idx) {
        return &mAmbColor[idx];
    }

    virtual ~J3DColorBlockAmbientOn() {
    }

    /* 0x20 */ J3DGXColor mAmbColor[2];
};  // Size: 0x28

class J3DColorBlockLightOn : public J3DColorBlock {
public:
    J3DColorBlockLightOn() {
        initialize();
    }

    void initialize();
    virtual void reset(J3DColorBlock*);
    virtual u32 getType() {
        return 'CLON';
    }

    virtual void setMatColor(u32 idx, J3DGXColor const* color) {
        mMatColor[idx] = *color;
    }

    virtual void setMatColor(u32 idx, J3DGXColor color) {
        mMatColor[idx] = color;
    }

    virtual J3DGXColor* getMatColor(u32 idx) {
        return &mMatColor[idx];
    }

    virtual void setAmbColor(u32 idx, J3DGXColor const* color) {
        mAmbColor[idx] = *color;
    }

    virtual void setAmbColor(u32 idx, J3DGXColor color) {
        mAmbColor[idx] = color;
    }

    virtual J3DGXColor* getAmbColor(u32 idx) {
        return &mAmbColor[idx];
    }

    virtual void setColorChanNum(u8 num) {
        mColorChanNum = num;
    }

    virtual void setColorChanNum(u8 const* num) {
        mColorChanNum = *num;
    }

    virtual u8 getColorChanNum() const {
        return mColorChanNum;
    }

    virtual void setColorChan(u32 idx, J3DColorChan const& chan) {
        mColorChan[idx] = chan;
    }

    virtual void setColorChan(u32 idx, J3DColorChan const* chan) {
        mColorChan[idx] = *chan;
    }

    virtual J3DColorChan* getColorChan(u32 idx) {
        return &mColorChan[idx];
    }

    virtual void setLight(u32 idx, J3DLightObj* light) {
        mLight[idx] = light;
    }

    virtual J3DLightObj* getLight(u32 idx) {
        return mLight[idx];
    }

    virtual void setCullMode(u8 const* mode) {
        mCullMode = *mode;
    }

    virtual void setCullMode(u8 mode) {
        mCullMode = mode;
    }

    virtual u8 getCullMode() const {
        return mCullMode;
    }

    virtual u32 getMatColorOffset() const {
        return mMatColorOffset;
    }

    virtual u32 getColorChanOffset() const {
        return mColorChanOffset;
    }

    virtual void setMatColorOffset(u32 offset) {
        mMatColorOffset = offset;
    }

    virtual void setColorChanOffset(u32 offset) {
        mColorChanOffset = offset;
    }

    virtual ~J3DColorBlockLightOn() {
    }

    /* 0x04 */ J3DGXColor mMatColor[2];
    /* 0x0C */ J3DGXColor mAmbColor[2];
    /* 0x14 */ u8 mColorChanNum;
    /* 0x16 */ J3DColorChan mColorChan[4];
    /* 0x20 */ J3DLightObj* mLight[8];
    /* 0x40 */ u8 mCullMode;
    /* 0x44 */ u32 mMatColorOffset;
    /* 0x48 */ u32 mColorChanOffset;
};  // Size: 0x4C

class J3DTexGenBlock {
public:
    virtual void reset(J3DTexGenBlock*) {
    }

    virtual void calc(f32 const (*)[4]) = 0;
    virtual void calcWithoutViewMtx(f32 const (*)[4]) = 0;
    virtual void calcPostTexMtx(f32 const (*)[4]) = 0;
    virtual void calcPostTexMtxWithoutViewMtx(f32 const (*)[4]) = 0;

    virtual u32 getType() = 0;
    virtual void setTexGenNum(u32 const*) {
    }

    virtual void setTexGenNum(u32) {
    }

    virtual u32 getTexGenNum() const {
        return 0;
    }

    virtual void setTexCoord(u32, J3DTexCoord const*) {
    }

    virtual J3DTexCoord* getTexCoord(u32) {
        return nullptr;
    }

    virtual void setTexMtx(u32, J3DTexMtx*) {
    }

    virtual J3DTexMtx* getTexMtx(u32) {
        return nullptr;
    }

    virtual void setNBTScale(J3DNBTScale const* scale) {
    }

    virtual void setNBTScale(J3DNBTScale) {
    }

    virtual J3DNBTScale* getNBTScale() {
        return nullptr;
    }

    virtual u32 getTexMtxOffset() const {
        return 0;
    }

    virtual void setTexMtxOffset(u32) {
    }

    virtual ~J3DTexGenBlock() {
    }
};

class J3DTexGenBlockPatched : public J3DTexGenBlock {
public:
    J3DTexGenBlockPatched() {
        initialize();
    }

    void initialize();

    virtual void reset(J3DTexGenBlock*);
    virtual void calc(f32 const (*)[4]);
    virtual void calcWithoutViewMtx(f32 const (*)[4]);
    virtual void calcPostTexMtx(f32 const (*)[4]);
    virtual void calcPostTexMtxWithoutViewMtx(f32 const (*)[4]);
    virtual u32 getType() {
        return 'TGPT';
    }

    virtual void setTexGenNum(u32 const* num) {
        mTexGenNum = *num;
    }

    virtual void setTexGenNum(u32 num) {
        mTexGenNum = num;
    }

    virtual u32 getTexGenNum() const {
        return mTexGenNum;
    }

    virtual void setTexCoord(u32 idx, J3DTexCoord const* coord) {
        mTexCoord[idx] = *coord;
    }

    virtual J3DTexCoord* getTexCoord(u32 idx) {
        return &mTexCoord[idx];
    }

    virtual void setTexMtx(u32 idx, J3DTexMtx* mtx) {
        mTexMtx[idx] = mtx;
    }

    virtual J3DTexMtx* getTexMtx(u32 idx) {
        return mTexMtx[idx];
    }

    virtual u32 getTexMtxOffset() const {
        return mTexMtxOffset;
    }

    virtual void setTexMtxOffset(u32 offset) {
        mTexMtxOffset = offset;
    }

    virtual ~J3DTexGenBlockPatched() {
    }

    /* 0x04 */ u32 mTexGenNum;
    /* 0x08 */ J3DTexCoord mTexCoord[8];
    /* 0x38 */ J3DTexMtx* mTexMtx[8];
    /* 0x58 */ u32 mTexMtxOffset;
};  // Size: 0x5C

class J3DTexGenBlock4 : public J3DTexGenBlockPatched {
public:
    J3DTexGenBlock4() : mNBTScale() {
        initialize();
    }

    void initialize();

    virtual void reset(J3DTexGenBlock*);
    virtual u32 getType() {
        return 'TGB4';
    }

    virtual void setNBTScale(J3DNBTScale const* scale) {
        mNBTScale = *scale;
    }

    virtual void setNBTScale(J3DNBTScale scale) {
        mNBTScale = scale;
    }

    virtual J3DNBTScale* getNBTScale() {
        return &mNBTScale;
    }

    virtual ~J3DTexGenBlock4() {
    }

    /* 0x5C */ J3DNBTScale mNBTScale;
};  // Size: 0x6C

class J3DTexGenBlockBasic : public J3DTexGenBlockPatched {
public:
    J3DTexGenBlockBasic() : mNBTScale() {
        initialize();
    }

    void initialize();

    virtual void reset(J3DTexGenBlock*);
    virtual u32 getType() {
        return 'TGBC';
    }

    virtual void setNBTScale(J3DNBTScale const* scale) {
        mNBTScale = *scale;
    }

    virtual void setNBTScale(J3DNBTScale scale) {
        mNBTScale = scale;
    }

    virtual J3DNBTScale* getNBTScale() {
        return &mNBTScale;
    }

    virtual ~J3DTexGenBlockBasic() {
    }

    /* 0x5C */ J3DNBTScale mNBTScale;
};  // Size: 0x6C

// What a material's stage block holds at run time (the cook made its shader): its textures and colour registers.
class J3DTevBlock {
public:
    // `texCapacity` textures; `hasTextures` false for a material with no block of its own; `hasColors` false for the
    // one-stage block, which keeps the colours set before.
    J3DTevBlock(u8 texCapacity, bool hasTextures, bool hasColors);

    void reset(J3DTevBlock* pBlock);
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
    void setTevColor(u32 idx, J3DGXColorS10 const* color) {
        setTevColor(idx, *color);
    }
    void setTevColor(u32 idx, J3DGXColorS10 color) {
        if (mHasColors) {
            mTevColor[idx] = color;
        }
    }
    J3DGXColorS10* getTevColor(u32 idx) {
        return mHasColors ? &mTevColor[idx] : nullptr;
    }
    void setTevKColor(u32 idx, J3DGXColor const* color) {
        setTevKColor(idx, *color);
    }
    void setTevKColor(u32 idx, J3DGXColor color) {
        if (mHasColors) {
            mTevKColor[idx] = color;
        }
    }
    J3DGXColor* getTevKColor(u32 idx) {
        return mHasColors ? &mTevKColor[idx] : nullptr;
    }

private:
    u16 mTexNo[8];
    J3DGXColorS10 mTevColor[4];
    J3DGXColor mTevKColor[4];
    u8 mTexCapacity;
    bool mHasTextures;
    bool mHasColors;
};

class J3DColorBlockNull : public J3DColorBlock {
public:
    virtual u32 getType() {
        return 'CLNL';
    }

    virtual ~J3DColorBlockNull() {
    }
};

class J3DTexGenBlockNull : public J3DTexGenBlock {
public:
    virtual void calc(f32 const (*)[4]) {
    }

    virtual void calcWithoutViewMtx(f32 const (*)[4]) {
    }

    virtual void calcPostTexMtx(f32 const (*)[4]) {
    }

    virtual void calcPostTexMtxWithoutViewMtx(f32 const (*)[4]) {
    }

    virtual u32 getType() {
        return 'TGNL';
    }

    virtual ~J3DTexGenBlockNull() {
    }
};

struct J3DIndTexMtx : public J3DIndTexMtxInfo {
    J3DIndTexMtx() {
        *(J3DIndTexMtxInfo*)this = j3dDefaultIndTexMtxInfo;
    }

    J3DIndTexMtx(const J3DIndTexMtxInfo& info) {
        *(J3DIndTexMtxInfo*)this = info;
    }

    J3DIndTexMtx(const J3DIndTexMtx& other) {
        __memcpy(this, &other, sizeof(J3DIndTexMtx));
    }

    ~J3DIndTexMtx() {
    }
};  // Size: 0x1C

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

class J3DPEBlock {
public:
    virtual void reset(J3DPEBlock*) {
    }

    virtual u32 getType() = 0;
    virtual void setFog(J3DFog) {
    }

    virtual void setFog(J3DFog*) {
    }

    virtual J3DFog* getFog() {
        return nullptr;
    }

    virtual void setAlphaComp(J3DAlphaComp const*) {
    }

    virtual void setAlphaComp(J3DAlphaComp const&) {
    }

    virtual J3DAlphaComp* getAlphaComp() {
        return nullptr;
    }

    virtual void setBlend(J3DBlend const*) {
    }

    virtual void setBlend(J3DBlend const&) {
    }

    virtual J3DBlend* getBlend() {
        return nullptr;
    }

    virtual void setZMode(J3DZMode const*) {
    }

    virtual void setZMode(J3DZMode) {
    }

    virtual J3DZMode* getZMode() {
        return nullptr;
    }

    virtual void setZCompLoc(u8 const*) {
    }

    virtual void setZCompLoc(u8) {
    }

    virtual u8 getZCompLoc() const {
        return 0;
    }

    virtual void setDither(u8 const*) {
    }

    virtual void setDither(u8) {
    }

    virtual u8 getDither() const {
        return 0;
    }

    virtual u32 getFogOffset() const {
        return 0;
    }

    virtual void setFogOffset(u32) {
    }

    virtual ~J3DPEBlock() {
    }
};

struct J3DPEBlockNull : public J3DPEBlock {

    virtual u32 getType() {
        return 'PENL';
    }

    virtual ~J3DPEBlockNull() {
    }
};

class J3DPEBlockOpa : public J3DPEBlock {
public:
    virtual u32 getType() {
        return 'PEOP';
    }

    virtual ~J3DPEBlockOpa() {
    }
};

class J3DPEBlockTexEdge : public J3DPEBlock {
public:
    virtual u32 getType() {
        return 'PEED';
    }

    virtual ~J3DPEBlockTexEdge() {
    }
};

class J3DPEBlockXlu : public J3DPEBlock {
public:
    virtual u32 getType() {
        return 'PEXL';
    }

    virtual ~J3DPEBlockXlu() {
    }
};

class J3DPEBlockFogOff : public J3DPEBlock {
public:
    J3DPEBlockFogOff() {
        initialize();
    }

    void initialize();

    virtual void reset(J3DPEBlock*);
    virtual u32 getType() {
        return 'PEFG';
    }

    virtual void setAlphaComp(J3DAlphaComp const* alphaComp) {
        mAlphaComp = *alphaComp;
    }

    virtual void setAlphaComp(J3DAlphaComp const& alphaComp) {
        mAlphaComp = alphaComp;
    }

    virtual J3DAlphaComp* getAlphaComp() {
        return &mAlphaComp;
    }

    virtual void setBlend(J3DBlend const* blend) {
        mBlend = *blend;
    }

    virtual void setBlend(J3DBlend const& blend) {
        mBlend = blend;
    }

    virtual J3DBlend* getBlend() {
        return &mBlend;
    }

    virtual void setZMode(J3DZMode const* zMode) {
        mZMode = *zMode;
    }

    virtual void setZMode(J3DZMode zMode) {
        mZMode = zMode;
    }

    virtual J3DZMode* getZMode() {
        return &mZMode;
    }

    virtual void setZCompLoc(u8 const* zCompLoc) {
        mZCompLoc = *zCompLoc;
    }

    virtual void setZCompLoc(u8 zCompLoc) {
        mZCompLoc = zCompLoc;
    }

    virtual u8 getZCompLoc() const {
        return mZCompLoc;
    }

    virtual void setDither(u8 const* dither) {
        mDither = *dither;
    }

    virtual void setDither(u8 dither) {
        mDither = dither;
    }

    virtual u8 getDither() const {
        return mDither;
    }

    virtual ~J3DPEBlockFogOff() {
    }

    /* 0x04 */ J3DAlphaComp mAlphaComp;
    /* 0x08 */ J3DBlend mBlend;
    /* 0x0C */ J3DZMode mZMode;
    /* 0x0E */ u8 mZCompLoc;
    /* 0x0F */ u8 mDither;
};  // Size: 0x10

class J3DPEBlockFull : public J3DPEBlock {
public:
    J3DPEBlockFull() {
        initialize();
    }

    void initialize();

    virtual void reset(J3DPEBlock*);
    virtual u32 getType() {
        return 'PEFL';
    }

    virtual void setFog(J3DFog fog) {
        mFog.setFogInfo(fog.getFogInfo());
    }

    virtual void setFog(J3DFog* pFog) {
        mFog.setFogInfo(pFog->getFogInfo());
    }

    virtual J3DFog* getFog() {
        return &mFog;
    }

    virtual void setAlphaComp(J3DAlphaComp const* alphaComp) {
        mAlphaComp = *alphaComp;
    }

    virtual void setAlphaComp(J3DAlphaComp const& alphaComp) {
        mAlphaComp = alphaComp;
    }

    virtual J3DAlphaComp* getAlphaComp() {
        return &mAlphaComp;
    }

    virtual void setBlend(J3DBlend const* blend) {
        mBlend = *blend;
    }

    virtual void setBlend(J3DBlend const& blend) {
        mBlend = blend;
    }

    virtual J3DBlend* getBlend() {
        return &mBlend;
    }

    virtual void setZMode(J3DZMode const* zMode) {
        mZMode = *zMode;
    }

    virtual void setZMode(J3DZMode zMode) {
        mZMode = zMode;
    }

    virtual J3DZMode* getZMode() {
        return &mZMode;
    }

    virtual void setZCompLoc(u8 const* zCompLoc) {
        mZCompLoc = *zCompLoc;
    }

    virtual void setZCompLoc(u8 zCompLoc) {
        mZCompLoc = zCompLoc;
    }

    virtual u8 getZCompLoc() const {
        return mZCompLoc;
    }

    virtual void setDither(u8 const* dither) {
        mDither = *dither;
    }

    virtual void setDither(u8 dither) {
        mDither = dither;
    }

    virtual u8 getDither() const {
        return mDither;
    }

    virtual u32 getFogOffset() const {
        return mFogOffset;
    }

    virtual void setFogOffset(u32 fogOffset) {
        mFogOffset = fogOffset;
    }

    virtual ~J3DPEBlockFull() {
    }

    /* 0x04 */ J3DFog mFog;
    /* 0x30 */ J3DAlphaComp mAlphaComp;
    /* 0x34 */ J3DBlend mBlend;
    /* 0x38 */ J3DZMode mZMode;
    /* 0x3A */ u8 mZCompLoc;
    /* 0x3B */ u8 mDither;
    /* 0x3C */ u32 mFogOffset;
};  // Size: 0x40
