#include "JSystem/J3DGraphBase/J3DShape.hpp"
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

void J3DShape::addTexMtxIndex(u32 coord, u32 valueBase) {
    for (u16 g = 0; g < mMtxGroupNum; g++) {
        J3DShapeMesh* pMesh = mShapeDraw[g]->getMesh();
        if ((pMesh->mAttributes & NEBULA_SHAPE_POSITION_MATRIX) == 0) {
            return;
        }
        pMesh->mAttributes |= static_cast< u32 >(NEBULA_SHAPE_TEXTURE_MATRIX0) << coord;
        NebulaShapeCorner* pCorners = pMesh->corners();
        for (u32 c = 0; c < pMesh->mCornerCount; c++) {
            const u32 id = valueBase + pCorners[c].position_matrix * 3;
            pCorners[c].texture_matrix[coord] = static_cast< u8 >(id >= 30 ? (id - 30) / 3 : 0);
        }
    }
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


// What its groups' meshes imply for drawing it: NBT normals and per-vertex position matrices.
void J3DShape::initVtxAttr() {
    const u32 attributes = mMtxGroupNum != 0 ? mShapeDraw[0]->getMesh()->mAttributes : 0;
    mHasNBT = (attributes & NEBULA_SHAPE_NBT) != 0;
    mHasPNMTXIdx = (attributes & NEBULA_SHAPE_POSITION_MATRIX) != 0;
}

void J3DShape::loadPreDrawSetting() const {
    prepareGeometry();
    mCurrentMtx.load();
}

void J3DShape::setArrayAndBindPipeline() const {
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
        for (u16 n = mMtxGroupNum, i = 0; i < n; i++) {
            if (mShapeMtx[i] != nullptr)
                mShapeMtx[i]->load();
            if (mShapeDraw[i] != nullptr)
                mShapeDraw[i]->draw();
        }
    } else {
        J3DNativeShape::setPosition(0, *j3dSys.getShapePacket()->getBaseMtxPtr());
        J3DNativeShape::setNormal(0, *j3dSys.getShapePacket()->getBaseMtxPtr());
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
