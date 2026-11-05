#include "JSystem/J3DGraphBase/J3DShape.hpp"
#include "JSystem/J3DGraphBase/J3DNativeShape.hpp"
#include "JSystem/J3DGraphBase/J3DShapeMtx.hpp"
#include "JSystem/J3DGraphAnimator/J3DModel.hpp"
#include "JSystem/J3DGraphBase/J3DPacket.hpp"
#include "JSystem/J3DGraphBase/J3DShapeDraw.hpp"
#include "JSystem/J3DGraphBase/J3DSys.hpp"
#include "JSystem/J3DGraphBase/J3DVertex.hpp"
#include "JSystem/J3DGraphBase/J3DTransform.hpp"
#include <stdint.h>
#include <string.h>

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
    mDrawMtx = nullptr;
    mNrmMtx = nullptr;
    mCurrentViewNo = &j3dDefaultViewNo;
    mHasNBT = false;
    mHasPNMTXIdx = false;
    mNebulaRegistered = false;
    mNebulaWhole = false;
}

// The normal matrices this shape reads, scaled for its bump map, into `pBump`.
void J3DShape::calcNBTScale(const Vec& rScale, f32 (*pNormal)[3][3], f32 (*pBump)[3][3]) {
    for (u16 g = 0; g < mMtxGroupNum; g++) {
        const J3DShapeMtx* pMtx = mShapeMtx[g];
        for (u16 k = 0; k < pMtx->getUseMtxNum(); k++) {
            const u16 index = pMtx->getUseMtxIndex(k);
            if (index != 0xFFFF) {
                memcpy(pBump[index], pNormal[index], sizeof(pBump[index]));
                J3DScaleNrmMtx33(pBump[index], rScale);
            }
        }
    }
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
}

void J3DShape::drawGroups(void (*pCallback)(J3DShape*)) const {
    prepareGeometry();
    J3DShape* pSelf = const_cast< J3DShape* >(this);
    const u32 palette = j3dSys.getModel()->drawPalette(mDrawMtx[*mCurrentViewNo], mNrmMtx[*mCurrentViewNo]);
    if (pCallback != nullptr) {
        pCallback(pSelf);
    }
    if (mNebulaWhole) {
        J3DNativeShape::setPalette(palette, 0);
        J3DNativeShape::drawWhole(this);
        return;
    }
    for (u16 n = mMtxGroupNum, i = 0; i < n; i++) {
        if (mShapeDraw[i] != nullptr) {
            J3DNativeShape::setPalette(palette, mShapeMtx[i] != nullptr && !mShapeMtx[i]->isMulti() ? mShapeMtx[i]->getUseMtxIndex(0) : 0);
            mShapeDraw[i]->draw();
        }
    }
}

void J3DShape::drawFast() const {
    drawGroups();
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

    for (u16 n = mMtxGroupNum, i = 0; i < n; i++)
        if (mShapeDraw[i] != nullptr)
            mShapeDraw[i]->draw();
}

