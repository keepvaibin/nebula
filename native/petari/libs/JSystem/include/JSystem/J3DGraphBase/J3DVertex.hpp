#pragma once

#include <NebulaGpu.h>
#include <nebula_math.h>

class J3DModel;
class J3DAnmVtxColor;
class J3DVertexBuffer;

struct J3DVtxColorCalc {
    void calc(J3DModel*);
    virtual void calc(J3DVertexBuffer*);

    void* vtable;  // inlined vtable?
    u32 mFlags;
    J3DAnmVtxColor* mpVtxColor;
};

class J3DDrawMtxData {
public:
    J3DDrawMtxData();

    ~J3DDrawMtxData();

    u16 mEntryNum;
    u16 mDrawFullWgtMtxNum;
    u8* mDrawMtxFlag;
    u16* mDrawMtxIndex;
};

class J3DVertexData {
public:
    J3DVertexData();
    ~J3DVertexData() {
    }

    void* getVtxPosArray() const {
        return mVtxPosArray;
    }

    void* getVtxNrmArray() const {
        return mVtxNrmArray;
    }

    NebulaColor8* getVtxColorArray(u8 idx) const {
        return mVtxColorArray[idx];
    }

    void* getVtxTexCoordArray(u8 idx) const {
        return mVtxTexCoordArray[idx];
    }

    void* getVtxNBTArray() const {
        return mVtxNBTArray;
    }

    u32 getNrmNum() const {
        return mNrmNum;
    }

    u32 getVtxNum() const {
        return mVtxNum;
    }

    u32 getNormalFloats() const {
        return mNormalFloats;
    }

    friend class J3DModelLoader;

    u32 mVtxNum;
    u32 mNrmNum;
    u32 mColNum;
    u32 mTexCoordNum;
    u32 mPacketNum;
    // A native copy on native builds (J3DModelLoader::readVertex); the vertex arrays stay as the file has them.
    void* mVtxPosArray;
    void* mVtxNrmArray;
    void* mVtxNBTArray;
    NebulaColor8* mVtxColorArray[2];
    void* mVtxTexCoordArray[8];
    // Entries of each array the renderer has as values (J3DNativeShape): position, normal, NBT, colour 0-1,
    // texture coordinates 0-7.
    u32 mNebulaArrayCount[13];  // entries of each array, position first
    u32 mNormalFloats;          // floats per normal entry: 3, or 9 (NBT)
    bool mNebulaArraysSent;
};

class J3DVertexBuffer {
public:
    J3DVertexBuffer() {
        init();
    }

    J3DVertexBuffer& operator=(const J3DVertexBuffer& other) {
        mVtxData = other.mVtxData;

        for (s32 i = 0; i < 2; i++) {
            mVtxPosArray[i] = other.mVtxPosArray[i];
            mVtxNrmArray[i] = other.mVtxNrmArray[i];
            mVtxColArray[i] = other.mVtxColArray[i];
            mTransformedVtxPosArray[i] = other.mTransformedVtxPosArray[i];
            mTransformedVtxNrmArray[i] = other.mTransformedVtxNrmArray[i];
        }

        mCurrentVtxPos = other.mCurrentVtxPos;
        mCurrentVtxNrm = other.mCurrentVtxNrm;
        mCurrentVtxCol = other.mCurrentVtxCol;
        return *this;
    }

    void setVertexData(J3DVertexData*);
    void init();
    ~J3DVertexBuffer();
    void setArray() const;
    s32 copyLocalVtxPosArray(u32);
    s32 copyLocalVtxNrmArray(u32);
    s32 copyLocalVtxArray(u32);
    s32 allocTransformedVtxPosArray();
    s32 allocTransformedVtxNrmArray();

    void setCurrentVtxPos(void* pVtxPos) {
        mCurrentVtxPos = pVtxPos;
    }

    void* getCurrentVtxPos() {
        return mCurrentVtxPos;
    }

    void setCurrentVtxNrm(void* pVtxNrm) {
        mCurrentVtxNrm = pVtxNrm;
    }

    void* getCurrentVtxNrm() {
        return mCurrentVtxNrm;
    }

    void setCurrentVtxCol(NebulaColor8* pVtxCol) {
        mCurrentVtxCol = pVtxCol;
    }

    void frameInit() {
        setCurrentVtxPos(mVtxPosArray[0]);
        setCurrentVtxNrm(mVtxNrmArray[0]);
        setCurrentVtxCol(mVtxColArray[0]);
    }

    void* getTransformedVtxPos(int idx) {
        return mTransformedVtxPosArray[idx];
    }

    void* getTransformedVtxNrm(int idx) {
        return mTransformedVtxNrmArray[idx];
    }

    J3DVertexData* getVertexData() const {
        return mVtxData;
    }

    void swapTransformedVtxPos() {
        void* tmp = mTransformedVtxPosArray[0];
        mTransformedVtxPosArray[0] = mTransformedVtxPosArray[1];
        mTransformedVtxPosArray[1] = tmp;
    }

    void swapTransformedVtxNrm() {
        void* tmp = mTransformedVtxNrmArray[0];
        mTransformedVtxNrmArray[0] = mTransformedVtxNrmArray[1];
        mTransformedVtxNrmArray[1] = tmp;
    }

    void swapVtxPosArrayPointer() {
        void* temp = mVtxPosArray[0];
        mVtxPosArray[0] = mVtxPosArray[1];
        mVtxPosArray[1] = temp;
    }

    void swapVtxNrmArrayPointer() {
        void* temp = mVtxNrmArray[0];
        mVtxNrmArray[0] = mVtxNrmArray[1];
        mVtxNrmArray[1] = temp;
    }

    void* getVtxPosArrayPointer(int index) {
        return mVtxPosArray[index];
    }

    void* getVtxNrmArrayPointer(int index) {
        return mVtxNrmArray[index];
    }

    J3DVertexData* mVtxData;
    void* mVtxPosArray[2];
    void* mVtxNrmArray[2];
    NebulaColor8* mVtxColArray[2];
    void* mTransformedVtxPosArray[2];
    void* mTransformedVtxNrmArray[2];
    void* mCurrentVtxPos;
    void* mCurrentVtxNrm;
    NebulaColor8* mCurrentVtxCol;
};
