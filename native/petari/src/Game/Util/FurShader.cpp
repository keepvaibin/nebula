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
#include <NebulaGpuEnums.h>

CShader::CShader(const J3DModelData* pModelData, const ResTIMG* pTimg) : mIndexArray(), mLengthMap(pTimg) {
    _1C = 0.0f;
    _8 = 0.0f;
    mIndexArray = new CIndex[pModelData->mVertexData.mVtxNum];
    _22 = 8;
    _21 = 8;
    _20 = 8;
    _23 = 0;
    _24 = 0;
    _25 = 0;

    GpuVtxAttrFmtList* list = pModelData->mVertexData.mVtxAttrFmtList;

    while (list->attr != GPU_VA_NULL) {
        if (list->attr == GPU_VA_POS) {
            _21 = list->frac;
            _24 = list->type;
        } else if (list->attr == GPU_VA_NRM) {
            _22 = list->frac;
            _25 = list->type;
        } else if (list->attr == GPU_VA_TEX0) {
            _20 = list->frac;
            _23 = list->type;
        }

        list++;
    }
}

CShader::~CShader() {
    delete mIndexArray;
}

namespace {
    // The model's vertex arrays (host order, as the importer cooks VTX1) and the fur positions written for them.
    inline f32 loadF32(const void* pArray, u32 index) {
        return static_cast< const f32* >(pArray)[index];
    }

    inline s16 loadS16(const void* pArray, u32 index) {
        return static_cast< const s16* >(pArray)[index];
    }

    inline void loadVec(TVec3f* pOut, const void* pArray, u32 index, u8 type, u8 frac) {
        if (type == GPU_S16) {
            TVec3s fixed(loadS16(pArray, index), loadS16(pArray, index + 1), loadS16(pArray, index + 2));
            MR::fixed16ToFloat(pOut, fixed, frac);
        } else {
            pOut->set< f32 >(loadF32(pArray, index), loadF32(pArray, index + 1), loadF32(pArray, index + 2));
        }
    }

    inline void loadTexCoord(f32* pU, f32* pV, const void* pArray, u32 index, u8 type, u8 frac) {
        if (type == GPU_S16) {
            f32 scale = 1 << frac;
            *pU = f32(loadS16(pArray, index * 2)) / scale;
            *pV = f32(loadS16(pArray, index * 2 + 1)) / scale;
        } else {
            *pU = loadF32(pArray, index * 2);
            *pV = loadF32(pArray, index * 2 + 1);
        }
    }
}  // namespace

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
            if (_23 == GPU_S16 || _23 == GPU_F32) {
                loadTexCoord(&u, &v, pTexCoords, index, _23, _20);
            }

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
            f32 length;

            switch (_23) {
            case GPU_S16: {
                f32 u;
                f32 v;
                loadTexCoord(&u, &v, pTexCoords, texCoordIndex, _23, _20);
                u = u / f32(spanU);
                v = v / f32(spanV);

                if (u < 0.0f) {
                    u += 1.0f;
                }

                if (v < 0.0f) {
                    v += 1.0f;
                }

                length = mLengthMap.refer(u, v);
                break;
            }

            case GPU_F32: {
                f32 u;
                f32 v;
                loadTexCoord(&u, &v, pTexCoords, texCoordIndex, _23, _20);
                length = mLengthMap.refer(u, v);
                break;
            }
            }

            TVec3f position;
            TVec3f normal;
            TVec3f result;

            if (_25 == GPU_S16 || _25 == GPU_F32) {
                loadVec(&normal, pNormals, normalOffset, _25, _22);
            }

            MR::normalize(&normal);
            MR::isNan(normal);

            if (_24 == GPU_S16 || _24 == GPU_F32) {
                loadVec(&position, pPositions, offset, _24, _21);
            }

            MR::isNan(position);

            if (length == 0.0f) {
                result = position + normal * -1.0f;
            } else {
                result = position + normal * (_1C * length);
            }

            MR::isNan(result);

            switch (_24) {
            case GPU_S16: {
                TVec3s fixed;
                MR::floatToFixed16(&fixed, result, _21);
                s16* pFixedOutput = static_cast< s16* >(pOutput) + offset;
                pFixedOutput[0] = fixed.x;
                pFixedOutput[1] = fixed.y;
                pFixedOutput[2] = fixed.z;
                break;
            }
            case GPU_F32: {
                f32* pFloatOutput = static_cast< f32* >(pOutput) + offset;
                pFloatOutput[0] = result.x;
                pFloatOutput[1] = result.y;
                pFloatOutput[2] = result.z;
                break;
            }
            }
        }
    }

    // The shell's positions as values, for the draws that read them.
    J3DNativeShape::sendPositions(pOutput, count, static_cast< GpuCompType >(_24), GPU_POS_XYZ, _21);
    pModel->mVertexBuffer.setCurrentVtxPos(pOutput);
}

void CShader::setup(J3DModelData* pData) {
}

namespace {
    // Display lists in host order, as the importer cooks SHP1.
    inline s32 getCount(u8* pRead) {
        return *reinterpret_cast< u16* >(pRead + 1);
    }
};  // namespace

void CShader::makeIndexData(J3DShape* pShape) const {
    GpuVtxDescList* pDesc = pShape->getVtxDesc();
    s32 stride = 0;
    s32 posOffset = -1;
    s32 nrmOffset = -1;
    s32 texOffset = -1;
    s32 sizes[4] = {0, 1, 1, 2};

    while (pDesc->attr != GPU_VA_NULL) {
        switch (pDesc->attr) {
        case GPU_VA_POS:
            posOffset = stride;
            break;
        case GPU_VA_NRM:
            nrmOffset = stride;
            break;
        case GPU_VA_TEX0:
            texOffset = stride;
            break;
        }

        stride += sizes[pDesc->type];
        pDesc++;
    }

    if (posOffset == -1) {
        return;
    }

    if (nrmOffset == -1) {
        return;
    }

    if (texOffset == -1) {
        return;
    }

    for (u16 group = 0; group < pShape->getMtxGroupNum(); group++) {
        s32 count;
        u8* pStart;
        u8* pRead;
        pStart = static_cast< u8* >(pShape->getShapeDraw(group)->getDisplayList());
        pRead = pStart;

        while (u32(pRead - pStart) < pShape->getShapeDraw(group)->getDisplayListSize()) {
            if (*pRead == 0) {
                break;
            }

            count = getCount(pRead);

            for (s32 i = 0; i < count; i++) {
                u8* pVertex = pRead + 3 + stride * i;
                u16 pos = *reinterpret_cast< u16* >(pVertex + posOffset);
                u16 nrm = *reinterpret_cast< u16* >(pVertex + nrmOffset);
                u16 tex = *reinterpret_cast< u16* >(pVertex + texOffset);
                mIndexArray[pos]._0 = nrm;
                mIndexArray[pos]._2 = tex;
            }

            pRead = pRead + stride * count + 3;
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

        GpuVtxDescList* pDesc = pShape->getVtxDesc();
        s32 stride = 0;
        s32 posOffset = -1;
        s32 sizes[4] = {0, 1, 1, 2};

        while (pDesc->attr != GPU_VA_NULL) {
            switch (pDesc->attr) {
            case GPU_VA_POS:
                posOffset = stride;
                break;
            case GPU_VA_NRM:
                break;
            case GPU_VA_TEX0:
                break;
            }

            stride += sizes[pDesc->type];
            pDesc++;
        }

        if (posOffset == -1) {
            continue;
        }

        for (u16 group = 0; group < pShape->getMtxGroupNum(); group++) {
            u8* const pStart = pShape->getShapeDraw(group)->getDisplayList();
            u8* pRead = pStart;

            while (u32(pRead - pStart) < pShape->getShapeDraw(group)->getDisplayListSize()) {
                if (*pRead == 0) {
                    break;
                }

                s32 count = *reinterpret_cast< u16* >(pRead + 1);

                for (s32 i = 0; i < count; i++) {
                    u16 pos = *reinterpret_cast< u16* >((pRead + stride * i) + (posOffset + 3));
                    mIndexArray[pos]._0 = 0xFFFF;
                    mIndexArray[pos]._2 = 0xFFFF;
                }

                pRead = pRead + stride * count + 3;
            }
        }
    }
}

CShader::CLengthMap::CLengthMap(const ResTIMG* pTimg) {
    _0 = pTimg;
    _4 = reinterpret_cast< const u8* >(pTimg) + sizeof(ResTIMG);
    setLengthMap(pTimg);
}

void CShader::CLengthMap::setLengthMap(const ResTIMG* pTimg) {
    if (pTimg == nullptr) {
        _8 = 1;
        return;
    }

    if (pTimg->mFormat != GPU_TF_I8) {
        _8 = 1;
        return;
    }

    _0 = pTimg;
    _4 = reinterpret_cast< const u8* >(pTimg) + sizeof(ResTIMG);
    _8 = 0;
}

f32 CShader::CLengthMap::refer(f32 u, f32 v) const {
    if (_8) {
        return 1.0f;
    }

    if (_0->mFormat != GPU_TF_I8) {
        return 1.0f;
    }

    u16 x = getTexelOrder(_0->mWidth, u, static_cast< GpuTexWrapMode >(_0->mWrapS));
    u16 y = getTexelOrder(_0->mHeight, v, static_cast< GpuTexWrapMode >(_0->mWrapT));
    return f32(_4[((x % 8) + (y % 4) * 8) + (x / 8 + (_0->mWidth / 8) * (y / 4)) * 32]) / 255.0f;
}

u16 CShader::CLengthMap::getTexelOrder(u16 a1, f32 a2, _GpuTexWrapMode mode) const {
    if (a2 > 1.0f) {
        switch (mode) {
        case GPU_CLAMP:
            a2 = 1.0f;
            break;
        case GPU_REPEAT:
            while (a2 > 1.0f) {
                a2 -= 1.0f;
            }

            while (a2 < 0.0f) {
                a2 += 1.0f;
            }

            break;
        case GPU_MIRROR:
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
