#include "JSystem/J3DGraphLoader/J3DShapeFactory.hpp"
#include "JSystem/J3DGraphBase/J3DShape.hpp"
#include "JSystem/J3DGraphBase/J3DShapeDraw.hpp"
#include "JSystem/J3DGraphBase/J3DShapeMtx.hpp"
#include "JSystem/JKernel/JKRHeap.hpp"
#include "JSystem/JSupport/JSupport.hpp"
#include <stdint.h>

enum {
    J3DMdlDataFlag_ConcatView = 0x10,
};

enum {
    J3DShapeMtxType_Mtx = 0x00,
    J3DShapeMtxType_BBoard = 0x01,
    J3DShapeMtxType_YBBoard = 0x02,
    J3DShapeMtxType_Multi = 0x03,
};

J3DShapeFactory::J3DShapeFactory(J3DShapeBlock const& block) {
    mShapeInitData = JSUConvertOffsetToPtr< J3DShapeInitData >(&block, JSU_OFFSET_VALUE(block.mpShapeInitData));
    mIndexTable = JSUConvertOffsetToPtr< NEBULA_BE(u16) >(&block, JSU_OFFSET_VALUE(block.mpIndexTable));
    mVtxDescList = JSUConvertOffsetToPtr< NEBULA_DISC(GXVtxDescList) >(&block, JSU_OFFSET_VALUE(block.mpVtxDescList)),
    mMtxTable = JSUConvertOffsetToPtr< NEBULA_BE(u16) >(&block, JSU_OFFSET_VALUE(block.mpMtxTable));
    mDisplayListData = JSUConvertOffsetToPtr< u8 >(&block, JSU_OFFSET_VALUE(block.mpDisplayListData)),
    mMtxInitData = JSUConvertOffsetToPtr< J3DShapeMtxInitData >(&block, JSU_OFFSET_VALUE(block.mpMtxInitData)),
    mDrawInitData = JSUConvertOffsetToPtr< J3DShapeDrawInitData >(&block, JSU_OFFSET_VALUE(block.mpDrawInitData));
}

#ifdef TARGET_PC
GXVtxDescList* J3DShapeFactory::getVtxDescList(int no) const {
    const BEData< GXVtxDescList >* src =
        (const BEData< GXVtxDescList >*)((const u8*)mVtxDescList + mShapeInitData[mIndexTable[no]].mVtxDescListIndex);
    int num = 0;
    while (src[num].attr != GX_VA_NULL) {
        num++;
    }
    GXVtxDescList* list = new GXVtxDescList[num + 1];
    for (int i = 0; i <= num; i++) {
        list[i] = src[i];
    }
    return list;
}
#endif

J3DShape* J3DShapeFactory::create(int no, u32 flag, GXVtxDescList* vtxDesc) {
    J3DShape* shape = new J3DShape;
    shape->mMtxGroupNum = getMtxGroupNum(no);
    shape->mRadius = mShapeInitData[mIndexTable[no]].mRadius;
    shape->mVtxDesc = getVtxDescList(no);
    shape->mShapeMtx = new J3DShapeMtx*[shape->mMtxGroupNum];
    shape->mShapeDraw = new J3DShapeDraw*[shape->mMtxGroupNum];
    shape->mMin = getMin(no);
    shape->mMax = getMax(no);

    for (s32 i = 0; i < shape->mMtxGroupNum; i++) {
        shape->mShapeMtx[i] = newShapeMtx(flag, no, i);
        shape->mShapeDraw[i] = newShapeDraw(no, i);
    }

    shape->mIndex = no;
    return shape;
}

J3DShapeMtx* J3DShapeFactory::newShapeMtx(u32 flag, int shapeNo, int mtxGroupNo) const {
    J3DShapeMtx* ret = NULL;
    J3DShapeInitData& shapeInitData = mShapeInitData[mIndexTable[shapeNo]];
    J3DShapeMtxInitData& mtxInitData = (&mMtxInitData[shapeInitData.mMtxInitDataIndex])[mtxGroupNo];

    switch (getMdlDataFlag_MtxLoadType(flag)) {
    case J3DMdlDataFlag_ConcatView:
        switch (shapeInitData.mShapeMtxType) {
        case J3DShapeMtxType_Mtx:
            ret = new J3DShapeMtxConcatView(mtxInitData.mUseMtxIndex);
            break;
        case J3DShapeMtxType_BBoard:
            ret = new J3DShapeMtxBBoardConcatView(mtxInitData.mUseMtxIndex);
            break;
        case J3DShapeMtxType_YBBoard:
            ret = new J3DShapeMtxYBBoardConcatView(mtxInitData.mUseMtxIndex);
            break;
        case J3DShapeMtxType_Multi:
            ret = new J3DShapeMtxMultiConcatView(mtxInitData.mUseMtxIndex, mtxInitData.mUseMtxCount, &mMtxTable[mtxInitData.mFirstUseMtxIndex]);
            break;
        default:
            break;
        }
        break;

    case 0:
    default:
        switch (shapeInitData.mShapeMtxType) {
        case J3DShapeMtxType_Mtx:
        case J3DShapeMtxType_BBoard:
        case J3DShapeMtxType_YBBoard:
            ret = new J3DShapeMtx(mtxInitData.mUseMtxIndex);
            break;
        case J3DShapeMtxType_Multi:
            ret = new J3DShapeMtxMulti(mtxInitData.mUseMtxIndex, mtxInitData.mUseMtxCount, &mMtxTable[mtxInitData.mFirstUseMtxIndex]);
            break;
        default:
            break;
        }
        break;
    }

    return ret;
}

J3DShapeDraw* J3DShapeFactory::newShapeDraw(int shapeNo, int mtxGroupNo) const {
    J3DShapeInitData& shapeInitData = mShapeInitData[mIndexTable[shapeNo]];
    const J3DShapeDrawInitData& drawInitData = (&mDrawInitData[shapeInitData.mDrawInitDataIndex])[mtxGroupNo];
    return new J3DShapeDraw(&mDisplayListData[drawInitData.mDisplayListIndex], drawInitData.mDisplayListSize);
}

s32 J3DShapeFactory::calcSize(int shapeNo, u32 flag) {
    s32 size = 0x68;

    u16 mtxGroupNo = getMtxGroupNum(shapeNo);
    size += mtxGroupNo * 4;
    size += mtxGroupNo * 4;

    for (u32 i = 0; i < mtxGroupNo; i++) {
        s32 shapeMtxSize = calcSizeShapeMtx(flag, shapeNo, i);
        size += shapeMtxSize;
        size += 0x0C;
    }

    return size;
}

s32 J3DShapeFactory::calcSizeShapeMtx(u32 flag, int shapeNo, int mtxGroupNo) const {
    J3DShapeInitData& shapeInitData = mShapeInitData[mIndexTable[shapeNo]];
    u32 ret = 0;

    u32 mtxLoadType = getMdlDataFlag_MtxLoadType(flag);
    switch (mtxLoadType) {
    case J3DMdlDataFlag_ConcatView:
        switch (shapeInitData.mShapeMtxType) {
        case J3DShapeMtxType_Mtx:
            ret = 0x08;
            break;
        case J3DShapeMtxType_BBoard:
            ret = 0x08;
            break;
        case J3DShapeMtxType_YBBoard:
            ret = 0x08;
            break;
        case J3DShapeMtxType_Multi:
            ret = 0xC;
            break;
        default:
            break;
        }
        break;

    case 0:
    default:
        switch (shapeInitData.mShapeMtxType) {
        case J3DShapeMtxType_Mtx:
        case J3DShapeMtxType_BBoard:
        case J3DShapeMtxType_YBBoard:
            ret = 0x08;
            break;
        case J3DShapeMtxType_Multi:
            ret = 0xC;
            break;
        default:
            break;
        }
        break;
    }

    return ret;
}
