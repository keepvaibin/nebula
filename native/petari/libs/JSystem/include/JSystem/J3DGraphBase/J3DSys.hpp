#pragma once

#include <NebulaGpu.h>
#include <nebula_math.h>

enum J3DSysDrawBuf {
    J3DSysDrawBuf_Opa,
    J3DSysDrawBuf_Xlu,

    J3DSysDrawBuf_MAX
};

struct J3DTexCoordScaleInfo {
    u16 field_0x00;
    u16 field_0x02;
    u16 field_0x04;
    u16 field_0x06;
};

enum J3DError {
    kJ3DError_Success = 0,
    kJ3DError_Alloc = 4,
};

class J3DMtxCalc;
class J3DModel;
class J3DMatPacket;
class J3DShapePacket;
class J3DShape;
class J3DDrawBuffer;
class J3DTexture;

enum J3DSysFlag {
    J3DSysFlag_PostTexMtx = 0x40000000,
};

class J3DSys {
public:
    Mtx mViewMtx;
    J3DMtxCalc* mCurrentMtxCalc;
    u32 mFlags;
    J3DModel* mModel;
    J3DMatPacket* mMatPacket;
    J3DShapePacket* mShapePacket;
    J3DShape* mShape;
    J3DDrawBuffer* mDrawBuffer[J3DSysDrawBuf_MAX];
    u32 mDrawMode;
    u32 mMaterialMode;
    J3DTexture* mTexture;
    u8 unk_0x5c[0x60 - 0x5C];
    u8 unk_0xe4[0x104 - 0xE4];
    Mtx* mModelDrawMtx;
    Mtx33* mModelNrmMtx;
    void* mVtxPos;
    void* mVtxNrm;
    NebulaColor8* mVtxCol;
    Vec* mNBTScale;

    J3DSys();

    void loadPosMtxIndx(int, u16) const;
    void loadNrmMtxIndx(int, u16) const;
    void drawInit();
    void resetDrawState();
    void reinitGenMode();
    void reinitLighting();
    void reinitTransform();
    void reinitTexture();
    void reinitPixelProc();

    MtxPtr getViewMtx() {
        return mViewMtx;
    }

    void onFlag(u32 flag) {
        mFlags |= flag;
    }
    void offFlag(u32 flag) {
        mFlags &= ~flag;
    }
    bool checkFlag(u32 flag) {
        return mFlags & flag ? true : false;
    }

    J3DMatPacket* getMatPacket() {
        return mMatPacket;
    }
    void setMaterialMode(u32 mode) {
        mMaterialMode = mode;
    }

    void* getVtxPos() {
        return mVtxPos;
    }
    void setVtxPos(void* pVtxPos) {
        mVtxPos = pVtxPos;
    }

    void* getVtxNrm() {
        return mVtxNrm;
    }
    void setVtxNrm(void* pVtxNrm) {
        mVtxNrm = pVtxNrm;
    }

    void* getVtxCol() {
        return mVtxCol;
    }
    void setVtxCol(NebulaColor8* pVtxCol) {
        mVtxCol = pVtxCol;
    }

    void setModelDrawMtx(Mtx* pMtxArr) {
        mModelDrawMtx = pMtxArr;
    }

    void setModelNrmMtx(Mtx33* pMtxArr) {
        mModelNrmMtx = pMtxArr;
    }

    void setNBTScale(Vec* scale) {
        mNBTScale = scale;
    }
    Vec* getNBTScale() {
        return mNBTScale;
    }

    void setMatPacket(J3DMatPacket* pPacket) {
        mMatPacket = pPacket;
    }

    void setShapePacket(J3DShapePacket* pPacket) {
        mShapePacket = pPacket;
    }

    J3DModel* getModel() {
        return mModel;
    }
    void setModel(J3DModel* pModel) {
        mModel = pModel;
    }

    J3DShapePacket* getShapePacket() {
        return mShapePacket;
    }
    Mtx& getModelDrawMtx(u16 no) {
        return mModelDrawMtx[no];
    }
    void setTexture(J3DTexture* pTex) {
        mTexture = pTex;
    }
    J3DTexture* getTexture() {
        return mTexture;
    }

    J3DDrawBuffer* getDrawBuffer(int type) {
        return mDrawBuffer[type];
    }

    static Mtx mCurrentMtx;
    static Vec mCurrentS;
    static Vec mParentS;
    static J3DTexCoordScaleInfo sTexCoordScaleTable[8];
};

extern u32 j3dDefaultViewNo;
extern J3DSys j3dSys;
