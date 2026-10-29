#include "JSystem/J3DGraphBase/J3DShape.hpp"
#include "JSystem/J3DGraphBase/J3DGD.hpp"
#include "JSystem/J3DGraphBase/J3DPacket.hpp"
#include "JSystem/J3DGraphBase/J3DShapeDraw.hpp"
#include "JSystem/J3DGraphBase/J3DSys.hpp"
#include "JSystem/J3DGraphBase/J3DVertex.hpp"
#include <stdint.h>

void J3DShape::initialize() {
    mMaterial = nullptr;
    mIndex = -1;
    mMtxGroupNum = 0;
    mFlags = 0;
    mRadius = 0.0f;
    mMin.x = 0.0f;
    mMin.y = 0.0f;
    mMin.z = 0.0f;
    mMax.x = 0.0f;
    mMax.y = 0.0f;
    mMax.z = 0.0f;
    mVtxDesc = nullptr;
    mShapeMtx = nullptr;
    mShapeDraw = nullptr;
    mVertexData = nullptr;
    mDrawMtxData = nullptr;
    mScaleFlagArray = nullptr;
    mDrawMtx = nullptr;
    mNrmMtx = nullptr;
    mCurrentViewNo = &j3dDefaultViewNo;
    mHasNBT = false;
    mHasPNMTXIdx = false;
    mNebulaRegistered = false;
}

void J3DShape::addTexMtxIndexInDL(GXAttr attr, u32 valueBase) {
    u32 kSize[] = {0, 1, 1, 2};

    s32 pnmtxidxOffs = -1;
    s32 attrOffs = -1;
    s32 stride = 0;
    bool found = false;

    for (GXVtxDescList* vtxDesc = getVtxDesc(); vtxDesc->attr != GX_VA_NULL; vtxDesc++) {
        if (vtxDesc->attr == GX_VA_PNMTXIDX)
            pnmtxidxOffs = stride;

        if (attr < vtxDesc->attr && !found) {
            attrOffs = stride;
            found = true;
        }

        stride = stride + kSize[vtxDesc->type];
    }

    if (pnmtxidxOffs == -1)
        return;

    for (u16 i = 0; i < (u16)getMtxGroupNum(); i++)
        getShapeDraw(i)->addTexMtxIndexInDL(stride, attrOffs, (s32)valueBase);
}

void J3DShape::addTexMtxIndexInVcd(GXAttr attr) {

    s32 attrIdx = -1;
    s32 attrOffs = -1;
    s32 stride = 0;

    GXVtxDescList* vtxDesc = getVtxDesc();
    s32 attrCount = 0;

    for (; vtxDesc->attr != GX_VA_NULL; attrCount++, vtxDesc++) {
        if (vtxDesc->attr == GX_VA_PNMTXIDX)
            attrIdx = stride;
    }

    if (attrIdx == -1)
        return;

    GXVtxDescList* newVtxDesc = new GXVtxDescList[attrCount + 2];
    bool inserted = false;

    vtxDesc = getVtxDesc();
    GXVtxDescList* dst = newVtxDesc;
    for (; vtxDesc->attr != GX_VA_NULL; dst++, vtxDesc++) {
        if ((attr < vtxDesc->attr) && !inserted) {
            dst->attr = attr;
            dst->type = GX_DIRECT;
            attrOffs = stride;
            dst++;

            inserted = true;
        }

        dst->attr = vtxDesc->attr;
        dst->type = vtxDesc->type;
        stride += vtxDesc->type == GX_INDEX16 ? sizeof(u16) : vtxDesc->type != GX_NONE;
    }

    dst->attr = GX_VA_NULL;
    dst->type = GX_NONE;
    mVtxDesc = newVtxDesc;
}

void J3DShape::calcNBTScale(const Vec& param_0, f32 (*param_1)[3][3], f32 (*param_2)[3][3]) {
    for (u16 i = 0; i < mMtxGroupNum; i++)
        mShapeMtx[i]->calcNBTScale(param_0, param_1, param_2);
}

u16 J3DShape::countBumpMtxNum() const {
    u16 num = 0;
    for (u16 i = 0; i < mMtxGroupNum; i++)
        num += mShapeMtx[i]->getUseMtxNum();

    return num;
}


// What the shape's vertex description and its model's formats imply for drawing it: the vertex data's position and
// normal formats (J3DDeformer reads them), NBT normals and per-vertex position matrices.
void J3DShape::initVtxAttr() {
    for (GXVtxAttrFmtList* vtxAttr = mVertexData->getVtxAttrFmtList(); vtxAttr->attr != GX_VA_NULL; vtxAttr++) {
        if (vtxAttr->attr == GX_VA_POS) {
            mVertexData->setVtxPosFrac((u8)vtxAttr->frac);
            mVertexData->setVtxPosType((GXCompType)vtxAttr->type);
        } else if (vtxAttr->attr == GX_VA_NRM) {
            mVertexData->setVtxNrmFrac((u8)vtxAttr->frac);
            mVertexData->setVtxNrmType((GXCompType)vtxAttr->type);
        }
    }

    mHasPNMTXIdx = false;
    for (GXVtxDescList* vtxDesc = mVtxDesc; vtxDesc->attr != GX_VA_NULL; vtxDesc++) {
        if (vtxDesc->attr == GX_VA_NBT && vtxDesc->type != GX_NONE) {
            mHasNBT = true;
        } else if (vtxDesc->attr == GX_VA_PNMTXIDX && vtxDesc->type != GX_NONE) {
            mHasPNMTXIdx = true;
        }
    }
}

void J3DShape::loadPreDrawSetting() const {
    prepareGeometry();
    mCurrentMtx.load();
}

void J3DShape::setArrayAndBindPipeline() const {
    J3DShapeMtx::setCurrentPipeline((mFlags & 0x1C) >> 2);
    j3dSys.setModelDrawMtx(mDrawMtx[*mCurrentViewNo]);
    j3dSys.setModelNrmMtx(mNrmMtx[*mCurrentViewNo]);
    J3DShapeMtx::sCurrentScaleFlag = mScaleFlagArray;
    J3DShapeMtx::sNBTFlag = mHasNBT;
    sEnvelopeFlag = mHasPNMTXIdx;
    J3DShapeMtx::sTexMtxLoadType = getTexMtxLoadType();
}

void J3DShape::drawFast() const {
    prepareGeometry();
    if (sEnvelopeFlag != 0 && !mHasPNMTXIdx)
        mCurrentMtx.load();

    setArrayAndBindPipeline();
    if (!checkFlag(J3DShpFlag_NoMtx)) {
        if (J3DShapeMtx::getLODFlag())
            J3DShapeMtx::resetMtxLoadCache();

        for (u16 n = mMtxGroupNum, i = 0; i < n; i++) {
            if (mShapeMtx[i] != nullptr)
                mShapeMtx[i]->load();
            if (mShapeDraw[i] != nullptr)
                mShapeDraw[i]->draw();
        }
    } else {
        J3DNativeShape::loadPosition(*j3dSys.getShapePacket()->getBaseMtxPtr(), GX_PNMTX0);
        J3DNativeShape::loadNormal(*j3dSys.getShapePacket()->getBaseMtxPtr(), GX_PNMTX0);
        for (u16 n = mMtxGroupNum, i = 0; i < n; i++) {
            if (mShapeDraw[i] != nullptr)
                mShapeDraw[i]->draw();
        }
    }
}

void J3DShape::draw() const {
    loadPreDrawSetting();
    drawFast();
}

void J3DShape::simpleDraw() const {
    loadPreDrawSetting();
    J3DShapeMtx::setCurrentPipeline((mFlags & 0x1C) >> 2);
    for (u16 n = mMtxGroupNum, i = 0; i < n; i++) {
        if (mShapeDraw[i] != nullptr) {
            mShapeDraw[i]->draw();
        }
    }
}

void J3DShape::simpleDrawCache() const {
    prepareGeometry();
    if (sEnvelopeFlag && !mHasPNMTXIdx)
        mCurrentMtx.load();

    for (u16 n = mMtxGroupNum, i = 0; i < n; i++)
        if (mShapeDraw[i] != nullptr)
            mShapeDraw[i]->draw();
}

bool J3DShape::sEnvelopeFlag;
