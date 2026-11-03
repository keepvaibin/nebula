#pragma once

#include "JSystem/J3DGraphLoader/J3DModelLoader.hpp"

class J3DShape;
class J3DShapeMtx;
class J3DShapeDraw;
struct ResNTAB;

struct J3DShapeInitData {
    u8 mShapeMtxType;
    u16 mMtxGroupNum;
    u16 mVtxDescListIndex;
    u16 mMtxInitDataIndex;
    u16 mDrawInitDataIndex;
    f32 mRadius;
    Vec mMin;
    Vec mMax;
};

struct J3DShapeMtxInitData {
    u16 mUseMtxIndex;
    u16 mUseMtxCount;
    u32 mFirstUseMtxIndex;
};

struct J3DShapeDrawInitData {
    u32 mShapeDrawSize;
    u32 mShapeDrawIndex;
};

struct J3DShapeFactory {
    J3DShapeFactory(J3DShapeBlock const&);
    J3DShape* create(int, u32);
    J3DShapeMtx* newShapeMtx(u32, int, int) const;
    J3DShapeDraw* newShapeDraw(int, int) const;

    J3DShapeInitData* mShapeInitData;
    u16* mIndexTable;
    u16* mMtxTable;
    u8* mShapeDrawData;
    J3DShapeMtxInitData* mMtxInitData;
    J3DShapeDrawInitData* mDrawInitData;

    u32 getMtxGroupNum(int no) const {
        return mShapeInitData[mIndexTable[no]].mMtxGroupNum;
    }
    f32 getRadius(int no) const {
        return mShapeInitData[mIndexTable[no]].mRadius;
    }
    Vec& getMin(int no) const {
        return mShapeInitData[mIndexTable[no]].mMin;
    }
    Vec& getMax(int no) const {
        return mShapeInitData[mIndexTable[no]].mMax;
    }
};
