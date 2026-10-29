#pragma once

#include "JSystem/J3DGraphLoader/J3DModelLoader.hpp"

class J3DShape;
class J3DShapeMtx;
class J3DShapeDraw;
struct ResNTAB;

struct J3DShapeInitData {
    /* 0x00 */ u8 mShapeMtxType;
    /* 0x02 */ NEBULA_BE(u16) mMtxGroupNum;
    /* 0x04 */ NEBULA_BE(u16) mVtxDescListIndex;
    /* 0x06 */ NEBULA_BE(u16) mMtxInitDataIndex;
    /* 0x08 */ NEBULA_BE(u16) mDrawInitDataIndex;
    /* 0x0C */ NEBULA_BE(f32) mRadius;
    /* 0x10 */ NEBULA_DISC(Vec) mMin;
    /* 0x1C */ NEBULA_DISC(Vec) mMax;
};

struct J3DShapeMtxInitData {
    /* 0x00 */ NEBULA_BE(u16) mUseMtxIndex;
    /* 0x02 */ NEBULA_BE(u16) mUseMtxCount;
    /* 0x04 */ NEBULA_BE(u32) mFirstUseMtxIndex;
};

struct J3DShapeDrawInitData {
    /* 0x00 */ NEBULA_BE(u32) mDisplayListSize;
    /* 0x04 */ NEBULA_BE(u32) mDisplayListIndex;
};

struct J3DShapeFactory {
    J3DShapeFactory(J3DShapeBlock const&);
    J3DShape* create(int, u32, GXVtxDescList*);
    J3DShapeMtx* newShapeMtx(u32, int, int) const;
    J3DShapeDraw* newShapeDraw(int, int) const;
    s32 calcSize(int, u32);
    s32 calcSizeShapeMtx(u32, int, int) const;

    /* 0x00 */ J3DShapeInitData* mShapeInitData;
    /* 0x04 */ NEBULA_BE(u16)* mIndexTable;
    /* 0x08 */ NEBULA_DISC(GXVtxDescList)* mVtxDescList;
    /* 0x0C */ NEBULA_BE(u16)* mMtxTable;
    /* 0x10 */ u8* mDisplayListData;
    /* 0x14 */ J3DShapeMtxInitData* mMtxInitData;
    /* 0x18 */ J3DShapeDrawInitData* mDrawInitData;

    u32 getMtxGroupNum(int no) const {
        return mShapeInitData[mIndexTable[no]].mMtxGroupNum;
    }
#ifdef TARGET_PC
    // A native copy of the shape's list from the file (GD and J3DShape read GXVtxDescList natively).
    GXVtxDescList* getVtxDescList(int no) const;
#else
    GXVtxDescList* getVtxDescList(int no) const {
        return (GXVtxDescList*)((u8*)mVtxDescList + mShapeInitData[mIndexTable[no]].mVtxDescListIndex);
    }
#endif
    f32 getRadius(int no) const {
        return mShapeInitData[mIndexTable[no]].mRadius;
    }
    NEBULA_DISC(Vec)& getMin(int no) const {
        return mShapeInitData[mIndexTable[no]].mMin;
    }
    NEBULA_DISC(Vec)& getMax(int no) const {
        return mShapeInitData[mIndexTable[no]].mMax;
    }
};
