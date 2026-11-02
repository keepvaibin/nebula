#include "JSystem/J3DGraphBase/J3DNativeShape.hpp"
#include "Game/Util/FurShader.hpp"
#include "Game/Util/MathUtil.hpp"
#include <JSystem/J3DGraphAnimator/J3DModelData.hpp>
#include <JSystem/J3DGraphBase/J3DMaterial.hpp>
#include <JSystem/J3DGraphBase/J3DShape.hpp>
#include <JSystem/J3DGraphBase/J3DShapeDraw.hpp>
#include <JSystem/JUtility/JUTNameTab.hpp>
#include <JSystem/JUtility/JUTTexture.hpp>
#include <cstring>

CShader::CShader(const J3DModelData* pModelData, const ResTIMG* pTimg) : mIndexArray(), mLengthMap(pTimg) {
    _1C = 0.0f;
    _8 = 0.0f;
    mIndexArray = new CIndex[pModelData->mVertexData.mVtxNum];
}

CShader::~CShader() {
    delete mIndexArray;
}

void CShader::calc(J3DModel* pModel) {
    pModel->mVertexBuffer.swapTransformedVtxPos();
    J3DVertexData* pData = pModel->mVertexBuffer.getVertexData();
    void* pPositions = pModel->mVertexBuffer.mCurrentVtxPos;
    u32 count = pData->mVtxNum;
    void* pNormals = pModel->mVertexBuffer.mCurrentVtxNrm;
    void* pTexCoords = pData->mVtxTexCoordArray[0];
    void* pOutput = pModel->mVertexBuffer.mTransformedVtxPosArray[0];
    f32 maxV = 0.0f;
    f32 maxU = 0.0f;
    f32 minV = 0.0f;
    f32 minU = 0.0f;

    for (u32 i = 0; i < count; i++) {
        u16 index = mIndexArray[i]._2;

        if (index != 0xFFFF) {
            f32 u = 0.0f;
            f32 v = 0.0f;
            u = static_cast< const f32* >(pTexCoords)[index * 2];
            v = static_cast< const f32* >(pTexCoords)[index * 2 + 1];

            if (u > maxU) {
                maxU = u;
            }

            if (u < minU) {
                minU = u;
            }

            if (v > maxV) {
                maxV = v;
            }

            if (v < minV) {
                minV = v;
            }
        }
    }

    f32 rangeU = maxU - minU;
    f32 rangeV = maxV - minV;
    s32 spanU = 0.45f + rangeU;
    s32 spanV = 0.45f + rangeV;

    for (u32 i = 0; i < count; i++) {
        u16 normalIndex = mIndexArray[i]._0;
        u16 texCoordIndex = mIndexArray[i]._2;

        if (normalIndex != 0xFFFF && texCoordIndex != 0xFFFF) {
            u32 offset = i * 3;
            s32 normalOffset = normalIndex * 3;
            f32 length = 0.0f;

            f32 u = static_cast< const f32* >(pTexCoords)[texCoordIndex * 2] / f32(spanU);
            f32 v = static_cast< const f32* >(pTexCoords)[texCoordIndex * 2 + 1] / f32(spanV);

            if (u < 0.0f) {
                u += 1.0f;
            }

            if (v < 0.0f) {
                v += 1.0f;
            }

            length = mLengthMap.refer(u, v);

            TVec3f position;
            TVec3f normal;
            TVec3f result;

            const f32* pNormal = static_cast< const f32* >(pNormals) + normalOffset;
            normal.set< f32 >(pNormal[0], pNormal[1], pNormal[2]);

            MR::normalize(&normal);
            MR::isNan(normal);

            const f32* pPosition = static_cast< const f32* >(pPositions) + offset;
            position.set< f32 >(pPosition[0], pPosition[1], pPosition[2]);

            MR::isNan(position);

            if (length == 0.0f) {
                result = position + normal * -1.0f;
            } else {
                result = position + normal * (_1C * length);
            }

            MR::isNan(result);

            f32* pFloatOutput = static_cast< f32* >(pOutput) + offset;
            pFloatOutput[0] = result.x;
            pFloatOutput[1] = result.y;
            pFloatOutput[2] = result.z;
        }
    }

    // The shell's positions as values, for the draws that read them.
    J3DNativeShape::sendPositions(pOutput, count);
    pModel->mVertexBuffer.setCurrentVtxPos(pOutput);
}

void CShader::setup(J3DModelData* pData) {
}

void CShader::makeIndexData(J3DShape* pShape) const {
    for (u16 group = 0; group < pShape->getMtxGroupNum(); group++) {
        J3DShapeMesh* pMesh = pShape->getShapeDraw(group)->getMesh();
        if ((pMesh->mAttributes & NEBULA_SHAPE_NORMAL) == 0 || (pMesh->mAttributes & NEBULA_SHAPE_TEXCOORD0) == 0) {
            return;
        }

        const NebulaShapeCorner* pCorners = pMesh->corners();
        const u16* pIndices = pMesh->indices();
        for (u32 i = 0; i < pMesh->mIndexCount; i++) {
            const NebulaShapeCorner& rCorner = pCorners[pIndices[i]];
            mIndexArray[rCorner.position]._0 = rCorner.normal;
            mIndexArray[rCorner.position]._2 = rCorner.texcoord[0];
        }
    }
}

void CShader::checkBorderVtx(J3DModelData* pData, u32 shapeIndex) {
    for (u32 shape = 0; shape < pData->getShapeNum(); shape++) {
        if (shape == shapeIndex) {
            continue;
        }

        J3DShape* pShape = pData->getShapeNodePointer(shape);

        if (strstr(pData->getMaterialName()->getName(pShape->mMaterial->mIndex), "Fur") != nullptr) {
            continue;
        }

        for (u16 group = 0; group < pShape->getMtxGroupNum(); group++) {
            J3DShapeMesh* pMesh = pShape->getShapeDraw(group)->getMesh();
            const NebulaShapeCorner* pCorners = pMesh->corners();
            for (u32 c = 0; c < pMesh->mCornerCount; c++) {
                mIndexArray[pCorners[c].position]._0 = 0xFFFF;
                mIndexArray[pCorners[c].position]._2 = 0xFFFF;
            }
        }
    }
}

CShader::CLengthMap::CLengthMap(const ResTIMG* pTimg) {
    _0 = pTimg;
    _4 = pTimg != nullptr ? reinterpret_cast< const u8* >(pTimg) + sizeof(ResTIMG) : nullptr;
    setLengthMap(pTimg);
}

void CShader::CLengthMap::setLengthMap(const ResTIMG* pTimg) {
    if (pTimg == nullptr) {
        _8 = 1;
        return;
    }

    const NebulaTexture* pRecord = nebula_texture_record(pTimg->image());
    if (pRecord == nullptr || pRecord->layout != NEBULA_TEXTURE_L8) {
        _8 = 1;
        return;
    }

    _0 = pTimg;
    _4 = reinterpret_cast< const u8* >(pRecord) + NEBULA_TEXTURE_HEADER;
    _8 = 0;
}

f32 CShader::CLengthMap::refer(f32 u, f32 v) const {
    if (_8) {
        return 1.0f;
    }

    u16 x = getTexelOrder(_0->mWidth, u, static_cast< u8 >(_0->mWrapS));
    u16 y = getTexelOrder(_0->mHeight, v, static_cast< u8 >(_0->mWrapT));
    x = x < _0->mWidth ? x : _0->mWidth - 1;
    y = y < _0->mHeight ? y : _0->mHeight - 1;
    return f32(_4[y * _0->mWidth + x]) / 255.0f;
}

u16 CShader::CLengthMap::getTexelOrder(u16 a1, f32 a2, u8 mode) const {
    if (a2 > 1.0f) {
        switch (mode) {
        case NEBULA_WRAP_CLAMP:
            a2 = 1.0f;
            break;
        case NEBULA_WRAP_REPEAT:
            while (a2 > 1.0f) {
                a2 -= 1.0f;
            }

            while (a2 < 0.0f) {
                a2 += 1.0f;
            }

            break;
        case NEBULA_WRAP_MIRROR:
            while (a2 > 1.0f) {
                a2 -= 1.0f;
            }

            while (a2 < 0.0f) {
                a2 += 1.0f;
            }
        }
    }

    return (s32)((a1 - 1) * a2);
}

CShader::CIndex::CIndex() {
    _0 = -1;
    _2 = -1;
}
