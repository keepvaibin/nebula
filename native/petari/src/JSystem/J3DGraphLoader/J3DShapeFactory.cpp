#include "JSystem/J3DGraphLoader/J3DShapeFactory.hpp"
#include "JSystem/J3DGraphBase/J3DShape.hpp"
#include "JSystem/J3DGraphBase/J3DShapeDraw.hpp"
#include "JSystem/J3DGraphBase/J3DShapeMtx.hpp"
#include "Game/System/NebulaArena.hpp"
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
    mShapeInitData = JSUConvertOffsetToPtr< J3DShapeInitData >(&block, block.mpShapeInitData);
    mIndexTable = JSUConvertOffsetToPtr< u16 >(&block, block.mpIndexTable);
    mMtxTable = JSUConvertOffsetToPtr< u16 >(&block, block.mpMtxTable);
    mShapeDrawData = JSUConvertOffsetToPtr< u8 >(&block, block.mpShapeDrawData),
    mMtxInitData = JSUConvertOffsetToPtr< J3DShapeMtxInitData >(&block, block.mpMtxInitData),
    mDrawInitData = JSUConvertOffsetToPtr< J3DShapeDrawInitData >(&block, block.mpDrawInitData);
}

J3DShape* J3DShapeFactory::create(int no, u32 flag) {
    J3DShape* shape = new J3DShape;
    shape->mMtxGroupNum = getMtxGroupNum(no);
    shape->mRadius = mShapeInitData[mIndexTable[no]].mRadius;
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
    return new J3DShapeDraw(reinterpret_cast< J3DShapeMesh* >(&mShapeDrawData[drawInitData.mShapeDrawIndex]));
}

