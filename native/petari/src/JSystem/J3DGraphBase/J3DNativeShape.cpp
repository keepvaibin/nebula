#include "JSystem/J3DGraphBase/J3DNativeState.hpp"
#include "JSystem/J3DGraphBase/J3DNativeDraw.hpp"
#include "JSystem/J3DGraphBase/J3DNativeMaterial.hpp"
#include <JSystem/J3DGraphBase/J3DNativeTexGen.hpp>
#include "JSystem/J3DGraphBase/J3DNativeShape.hpp"
#include "JSystem/J3DGraphAnimator/J3DModelData.hpp"
#include "JSystem/J3DGraphBase/J3DShape.hpp"
#include "JSystem/J3DGraphBase/J3DShapeDraw.hpp"
#include "JSystem/J3DGraphBase/J3DShapeMtx.hpp"
#include "JSystem/J3DGraphBase/J3DSys.hpp"
#include "JSystem/J3DGraphBase/J3DVertex.hpp"
#include <nebula_game_api.h>
#include "Game/Util/RenderSignals.hpp"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern "C" const NebulaHostServices* nebula_host(void);

namespace {
    void fail(const char* pWhat, const void* pShape) {
        char text[160];
        nebula_snprintf(text, sizeof(text), "J3D shape %p: %s", pShape, pWhat);
        const NebulaHostServices* pHost = nebula_host();
        pHost->fatal(pHost->user, text);
    }

    u64 keyOf(const void* p) {
        return static_cast< u64 >(reinterpret_cast< uintptr_t >(p));
    }

    // The arrays a model's shapes read (J3DVertexData::mNebulaArrayCount): position, normal, NBT, colour 0 and 1,
    // texture coordinates 0-7.
    enum {
        kPosition,
        kNormal,
        kNbt,
        kColor0,
        kColor1,
        kTexCoord0,
        kArrayCount = kTexCoord0 + 8,
    };

    const void* arrayOf(const J3DVertexData* pData, u32 array) {
        switch (array) {
        case kPosition:
            return pData->mVtxPosArray;
        case kNormal:
            return pData->mVtxNrmArray;
        case kNbt:
            return pData->mVtxNBTArray;
        case kColor0:
        case kColor1:
            return pData->mVtxColorArray[array - kColor0];
        default:
            return pData->mVtxTexCoordArray[array - kTexCoord0];
        }
    }

    // The model's arrays, values as the cook made them, once.
    void sendArrays(J3DVertexData* pData) {
        if (pData->mNebulaArraysSent) {
            return;
        }
        pData->mNebulaArraysSent = true;
        const NebulaHostServices* pHost = nebula_host();
        for (u32 array = 0; array < kArrayCount; array++) {
            const void* pArray = arrayOf(pData, array);
            if (pArray == nullptr || pData->mNebulaArrayCount[array] == 0) {
                continue;
            }
            NebulaShapeArray values;
            values.key = keyOf(pArray);
            values.kind = array == kPosition ? NEBULA_SHAPE_ARRAY_POSITION :
                          array == kNbt || (array == kNormal && pData->mNormalFloats == 9) ? NEBULA_SHAPE_ARRAY_NBT :
                          array == kNormal ? NEBULA_SHAPE_ARRAY_NORMAL :
                          array <= kColor1 ? NEBULA_SHAPE_ARRAY_COLOR : NEBULA_SHAPE_ARRAY_TEXCOORD;
            values.count = pData->mNebulaArrayCount[array];
            values.values = pArray;
            pHost->shape_array(pHost->user, &values);
        }
    }

    // The position and normal matrices draws carry, by place; the texture matrices, current places and texgens given
    // since the last record.
    struct Place {
        NebulaDrawMatrix mMatrix;
        bool mSet;
    };
    Place sPositions[J3DNativeShape::kPositions];
    Place sNormals[J3DNativeShape::kPositions];
    // Places given since the last record: the next record carries them whether its corners read them or not.
    u32 sGivenPlaces = 0;
    u8 sCurrentPosition = 0;
    NebulaDrawMatrix sTextures[64];
    u32 sTextureCount = 0;
    NebulaShapeCurrent sCurrent;
    bool sCurrentSet = false;
    NebulaShapeTexGen sTexGens[8];
    u32 sTexGenCount = 0;
    // A record's matrices: its position and normal matrices, then the texture matrices.
    NebulaDrawMatrix sRecord[2 * J3DNativeShape::kPositions + 64];
    // The next draw's block: a shape of one matrix takes it once; a skinned shape keeps the last one.
    u32 sNextPalette = 0;
    u16 sNextPaletteMatrix = 0;
    u32 sLastPalette = 0;

    NebulaDrawMatrix* nextTexture(u32 kind, u32 place) {
        J3DNativeDraw::flush();
        // A texture matrix given again for the same place before a record replaces the pending one, as a load into
        // a slot replaces what the slot held: only the last one before a draw is the one it reads.
        for (u32 i = 0; i < sTextureCount; i++) {
            if (sTextures[i].kind == kind && sTextures[i].place == place) {
                NebulaDrawMatrix* pOut = &sTextures[i];
                memset(pOut, 0, sizeof(*pOut));
                pOut->kind = static_cast< u8 >(kind);
                pOut->place = static_cast< u8 >(place);
                return pOut;
            }
        }
        if (sTextureCount == sizeof(sTextures) / sizeof(sTextures[0])) {
            fail("more texture matrices before one draw than a draw can carry", nullptr);
            return nullptr;
        }
        NebulaDrawMatrix* pOut = &sTextures[sTextureCount++];
        memset(pOut, 0, sizeof(*pOut));
        pOut->kind = static_cast< u8 >(kind);
        pOut->place = static_cast< u8 >(place);
        return pOut;
    }

    NebulaDrawMatrix* placeAt(Place* pPlaces, u32 kind, u32 place, u16 modelMatrix) {
        J3DNativeDraw::flush();
        if (place >= J3DNativeShape::kPositions) {
            fail("a position or normal matrix past place 9", nullptr);
            return nullptr;
        }
        Place& rPlace = pPlaces[place];
        memset(&rPlace.mMatrix, 0, sizeof(rPlace.mMatrix));
        rPlace.mMatrix.kind = static_cast< u8 >(kind);
        rPlace.mMatrix.place = static_cast< u8 >(place);
        rPlace.mMatrix.model_matrix = modelMatrix;
        rPlace.mSet = true;
        sGivenPlaces |= 1u << place;
        return &rPlace.mMatrix;
    }

    // The record's matrices: the position and normal matrices at the places `mask` names (bit k: place k) and those
    // given since the last record, and, with `textures`, the texture matrices given since the last record.
    u32 gather(u32 mask, bool textures) {
        mask |= sGivenPlaces;
        sGivenPlaces = 0;
        u32 count = 0;
        for (u32 k = 0; k < J3DNativeShape::kPositions; k++) {
            if ((mask & (1u << k)) == 0) {
                continue;
            }
            if (sPositions[k].mSet) {
                sRecord[count++] = sPositions[k].mMatrix;
            }
            if (sNormals[k].mSet) {
                sRecord[count++] = sNormals[k].mMatrix;
            }
        }
        if (textures) {
            memcpy(&sRecord[count], sTextures, sTextureCount * sizeof(sTextures[0]));
            count += sTextureCount;
            sTextureCount = 0;
        }
        return count;
    }

    void copyRows(f32* pOut, const f32* pIn, u32 rows, u32 columns, u32 stride, u32 outColumns) {
        for (u32 r = 0; r < rows; r++) {
            for (u32 c = 0; c < outColumns; c++) {
                pOut[r * outColumns + c] = c < columns ? pIn[r * stride + c] : 0.0f;
            }
        }
    }
}  // namespace

namespace J3DNativeShape {
    void setPosition(u32 index, const f32 (*pMtx)[4], u16 modelMatrix) {
        if (NebulaDrawMatrix* pOut = placeAt(sPositions, NEBULA_DRAW_MATRIX_POSITION, index, modelMatrix)) {
            copyRows(pOut->values, &pMtx[0][0], 3, 4, 4, 4);
        }
    }

    void setNormal(u32 index, const f32 (*pMtx)[4], u16 modelMatrix) {
        if (NebulaDrawMatrix* pOut = placeAt(sNormals, NEBULA_DRAW_MATRIX_NORMAL, index, modelMatrix)) {
            copyRows(pOut->values, &pMtx[0][0], 3, 3, 4, 3);
        }
    }

    void setNormal33(u32 index, const f32 (*pMtx)[3], u16 modelMatrix) {
        if (NebulaDrawMatrix* pOut = placeAt(sNormals, NEBULA_DRAW_MATRIX_NORMAL, index, modelMatrix)) {
            copyRows(pOut->values, &pMtx[0][0], 3, 3, 3, 3);
        }
    }

    void setTexture(u32 index, const f32 (*pMtx)[4], u32 rows) {
        const u32 kind = rows == 2 ? NEBULA_DRAW_MATRIX_TEXTURE_2X4 : NEBULA_DRAW_MATRIX_TEXTURE;
        if (NebulaDrawMatrix* pOut = nextTexture(kind, index)) {
            copyRows(pOut->values, &pMtx[0][0], rows == 2 ? 2 : 3, 4, 4, 4);
        }
    }

    void setNormalTexture(u32 index, const f32 (*pMtx)[4]) {
        if (NebulaDrawMatrix* pOut = nextTexture(NEBULA_DRAW_MATRIX_TEXTURE, index)) {
            copyRows(pOut->values, &pMtx[0][0], 3, 3, 4, 4);
        }
    }

    void setNormal33Texture(u32 index, const f32 (*pMtx)[3]) {
        if (NebulaDrawMatrix* pOut = nextTexture(NEBULA_DRAW_MATRIX_TEXTURE, index)) {
            copyRows(pOut->values, &pMtx[0][0], 3, 3, 3, 4);
        }
    }

    void setPostTexture(u32 index, const f32 (*pMtx)[4]) {
        if (NebulaDrawMatrix* pOut = nextTexture(NEBULA_DRAW_MATRIX_POST_TEXTURE, index)) {
            copyRows(pOut->values, &pMtx[0][0], 3, 4, 4, 4);
        }
    }

    void setTexGen(u8 coord, u8 type, u8 source, bool normalize, u8 postMatrix) {
        J3DNativeDraw::flush();
        if (sTexGenCount == 8) {
            fail("more texture coordinate generators before one draw than a draw can carry", nullptr);
            return;
        }
        NebulaShapeTexGen& rOut = sTexGens[sTexGenCount++];
        rOut.coord = coord;
        rOut.type = J3DNativeTexGen::kind(type, source);
        rOut.source = J3DNativeTexGen::source(source);
        rOut.normalize = normalize ? 1 : 0;
        rOut.post_matrix = J3DNativeTexGen::post(postMatrix);
        rOut.reserved[0] = rOut.reserved[1] = rOut.reserved[2] = 0;
    }

    void setCurrent(u8 position, const u8* pTexture) {
        J3DNativeDraw::flush();
        sCurrent.position = position;
        for (u32 t = 0; t < 8; t++) {
            sCurrent.texture[t] = pTexture[t];
        }
        sCurrentSet = true;
        sCurrentPosition = sCurrent.position;
    }

    void setCurrentFromFile(u8 position, const u8* pIds) {
        u8 slots[8];
        for (u32 t = 0; t < 8; t++) {
            slots[t] = static_cast< u8 >(pIds[t] >= J3D_TEXMTX0 ? (pIds[t] - J3D_TEXMTX0) / 3 : 0);
        }
        setCurrent(static_cast< u8 >(position / 3), slots);
    }

    void setCurrentIdentity() {
        static const u8 kIdentity[8] = {J3DNativeShape::kIdentityTexture, J3DNativeShape::kIdentityTexture, J3DNativeShape::kIdentityTexture, J3DNativeShape::kIdentityTexture,
                                        J3DNativeShape::kIdentityTexture, J3DNativeShape::kIdentityTexture, J3DNativeShape::kIdentityTexture, J3DNativeShape::kIdentityTexture};
        setCurrent(0, kIdentity);
    }

    u8 currentPosition() {
        return sCurrentPosition;
    }

    void setCurrentPosition(u32 index) {
        J3DNativeDraw::flush();
        sCurrentPosition = static_cast< u8 >(index);
    }

    void registerModel(J3DModelData* pModelData) {
        if (pModelData == nullptr) {
            return;
        }
        for (u16 s = 0; s < pModelData->getShapeNum(); s++) {
            J3DShape* pShape = pModelData->getShapeNodePointer(s);
            if (pShape != nullptr) {
                registerShape(pShape);
            }
        }
    }

    void setPalette(u32 palette, u16 matrix) {
        sNextPalette = palette;
        sNextPaletteMatrix = matrix;
        sLastPalette = palette;
    }

    void useModelMatrices(J3DShape* pShape) {
        // A group's table leaves a place empty (0xFFFF) where it keeps the group before's matrix.
        u16 places[J3DNativeShape::kPositions];
        for (u32 k = 0; k < J3DNativeShape::kPositions; k++) {
            places[k] = 0;
        }
        for (u16 g = 0; g < pShape->mMtxGroupNum; g++) {
            const J3DShapeDraw* pDraw = pShape->mShapeDraw[g];
            const J3DShapeMtx* pMtx = pShape->mShapeMtx[g];
            if (pDraw == nullptr || pMtx == nullptr || pMtx->getType() != 'SMMC') {
                continue;
            }
            const u32 used = pMtx->getUseMtxNum();
            for (u32 k = 0; k < used && k < J3DNativeShape::kPositions; k++) {
                if (pMtx->getUseMtxIndex(k) != 0xFFFF) {
                    places[k] = pMtx->getUseMtxIndex(k);
                }
            }
            J3DShapeMesh* pMesh = pDraw->getMesh();
            if ((pMesh->mAttributes & NEBULA_SHAPE_MODEL_MATRICES) != 0) {
                continue;
            }
            // Corners without a place of their own take the shape's current one.
            const bool ownPlaces = (pMesh->mAttributes & NEBULA_SHAPE_POSITION_MATRIX) != 0;
            const u32 current = pShape->mCurrentMtx.mPosition / 3;
            pMesh->mAttributes |= NEBULA_SHAPE_MODEL_MATRICES | NEBULA_SHAPE_POSITION_MATRIX;
            NebulaShapeCorner* pCorners = pMesh->corners();
            for (u32 c = 0; c < pMesh->mCornerCount; c++) {
                pCorners[c].model_matrix = places[(ownPlaces ? pCorners[c].position_matrix : current) % J3DNativeShape::kPositions];
            }
        }
    }

    void registerShape(J3DShape* pShape) {
        pShape->mNebulaRegistered = true;
        useModelMatrices(pShape);
        J3DVertexData* pData = pShape->mVertexData;
        if (pData == nullptr) {
            fail("it has no vertex data", pShape);
            return;
        }
        const NebulaHostServices* pHost = nebula_host();
        for (u16 g = 0; g < pShape->mMtxGroupNum; g++) {
            const J3DShapeDraw* pDraw = pShape->mShapeDraw[g];
            if (pDraw == nullptr) {
                continue;
            }
            J3DShapeMesh* pMesh = pDraw->getMesh();
            NebulaShapeGeometry geometry;
            memset(&geometry, 0, sizeof(geometry));
            geometry.key = keyOf(pDraw);
            geometry.attributes = pMesh->mAttributes;
            for (u32 a = 0; a < 12; a++) {
                const u32 array = a == 0 ? kPosition : a == 1 ? ((pMesh->mAttributes & NEBULA_SHAPE_NBT) != 0 ? kNbt : kNormal) : a - 2 + kColor0;
                geometry.arrays[a] = keyOf(arrayOf(pData, array));
            }
            geometry.corner_count = pMesh->mCornerCount;
            geometry.index_count = pMesh->mIndexCount;
            geometry.corners = pMesh->corners();
            geometry.indices = pMesh->indices();
            pHost->shape_geometry(pHost->user, &geometry);
        }
        registerWhole(pShape);
        sendArrays(pData);
    }

    // A skinned shape's groups as one geometry, under the shape's own key: its corners name the model's matrices, so a
    // group is only a list of triangles in a shape's order.
    void registerWhole(J3DShape* pShape) {
        pShape->mNebulaWhole = false;
        if (pShape->mMtxGroupNum < 2 || pShape->mShapeDraw[0] == nullptr) {
            return;
        }
        u32 corners = 0;
        u32 indices = 0;
        const u32 attributes = pShape->mShapeDraw[0]->getMesh()->mAttributes;
        for (u16 g = 0; g < pShape->mMtxGroupNum; g++) {
            const J3DShapeDraw* pDraw = pShape->mShapeDraw[g];
            if (pDraw == nullptr || pDraw->getMesh()->mAttributes != attributes ||
                (attributes & NEBULA_SHAPE_MODEL_MATRICES) == 0) {
                return;
            }
            corners += pDraw->getMesh()->mCornerCount;
            indices += pDraw->getMesh()->mIndexCount;
        }
        if (corners > 0xFFFF) {
            return;
        }
        NebulaShapeCorner* pCorners = new NebulaShapeCorner[corners];
        u16* pIndices = new u16[indices];
        u32 cornerAt = 0;
        u32 indexAt = 0;
        for (u16 g = 0; g < pShape->mMtxGroupNum; g++) {
            J3DShapeMesh* pMesh = pShape->mShapeDraw[g]->getMesh();
            memcpy(pCorners + cornerAt, pMesh->corners(), pMesh->mCornerCount * sizeof(NebulaShapeCorner));
            const u16* pIn = pMesh->indices();
            for (u32 i = 0; i < pMesh->mIndexCount; i++) {
                pIndices[indexAt++] = static_cast< u16 >(pIn[i] + cornerAt);
            }
            cornerAt += pMesh->mCornerCount;
        }
        J3DVertexData* pData = pShape->mVertexData;
        NebulaShapeGeometry geometry;
        memset(&geometry, 0, sizeof(geometry));
        geometry.key = keyOf(pShape);
        geometry.attributes = attributes;
        for (u32 a = 0; a < 12; a++) {
            const u32 array = a == 0 ? kPosition : a == 1 ? ((attributes & NEBULA_SHAPE_NBT) != 0 ? kNbt : kNormal) :
                              a == 2 ? kColor0 : a == 3 ? kColor1 : kTexCoord0 + (a - 4);
            geometry.arrays[a] = keyOf(arrayOf(pData, array));
        }
        geometry.corner_count = corners;
        geometry.index_count = indices;
        geometry.corners = pCorners;
        geometry.indices = pIndices;
        const NebulaHostServices* pHost = nebula_host();
        pHost->shape_geometry(pHost->user, &geometry);
        delete[] pIndices;
        delete[] pCorners;
        pShape->mNebulaWhole = true;
    }

    void drawWhole(const J3DShape* pShape) {
        J3DNativeDraw::flush();
        const u32 count = gather(0, true);
        const NebulaHostServices* pHost = nebula_host();
        pHost->shape_draw(pHost->user, keyOf(pShape), keyOf(j3dSys.getVtxPos()), keyOf(j3dSys.getVtxNrm()),
                          keyOf(j3dSys.getVtxCol()), sRecord, count, sCurrentSet ? &sCurrent : nullptr, sTexGens,
                          sTexGenCount, J3DNativeState::pass(), sLastPalette, 0);
        sNextPalette = 0;
        sCurrentSet = false;
        sTexGenCount = 0;
    }

    void draw(const J3DShapeDraw* pDraw) {
        J3DNativeDraw::flush();
        const bool skinned = (pDraw->getMesh()->mAttributes & NEBULA_SHAPE_MODEL_MATRICES) != 0;
        const u32 palette = skinned ? sLastPalette : sNextPalette;
        const u32 places = pDraw->getMesh()->mPlaces;
        const u32 mask = palette != 0 ? 0u : places != 0 ? places : 1u << sCurrentPosition;
        const u32 count = gather(mask, true);
        const NebulaHostServices* pHost = nebula_host();
        pHost->shape_draw(pHost->user, keyOf(pDraw), keyOf(j3dSys.getVtxPos()),
                          keyOf(j3dSys.getVtxNrm()), keyOf(j3dSys.getVtxCol()), sRecord, count,
                          sCurrentSet ? &sCurrent : nullptr, sTexGens, sTexGenCount, J3DNativeState::pass(), palette,
                          sNextPaletteMatrix);
        sNextPalette = 0;
        sCurrentSet = false;
        sTexGenCount = 0;
    }

    void drawGeometry(u64 key, u64 position, u64 normal, u64 color, u32 places) {
        J3DNativeDraw::flush();
        const u32 count = gather(places != 0 ? places : 1u << sCurrentPosition, true);
        const NebulaHostServices* pHost = nebula_host();
        pHost->shape_draw(pHost->user, key, position, normal, color, sRecord, count, sCurrentSet ? &sCurrent : nullptr,
                          sTexGens, sTexGenCount, J3DNativeState::pass(), 0, 0);
        sCurrentSet = false;
        sTexGenCount = 0;
    }

    void flushState() {
        J3DNativeDraw::flush();
        if (sTextureCount == 0 && sGivenPlaces == 0 && !sCurrentSet && sTexGenCount == 0) {
            return;
        }
        const u32 count = gather(0, true);
        const NebulaHostServices* pHost = nebula_host();
        pHost->shape_draw(pHost->user, 0, 0, 0, 0, sRecord, count, sCurrentSet ? &sCurrent : nullptr, sTexGens,
                          sTexGenCount, J3DNativeState::pass(), 0, 0);
        sCurrentSet = false;
        sTexGenCount = 0;
    }

    u32 positionMatrices(u32 mask, const NebulaDrawMatrix** ppMatrices) {
        *ppMatrices = sRecord;
        return gather(mask != 0 ? mask : 1u << sCurrentPosition, false);
    }

    void sendPositions(const void* pArray, u32 count) {
        NebulaShapeArray values;
        values.key = keyOf(pArray);
        values.kind = NEBULA_SHAPE_ARRAY_POSITION;
        values.count = count;
        values.values = pArray;
        const NebulaHostServices* pHost = nebula_host();
        pHost->shape_array(pHost->user, &values);
    }
};  // namespace J3DNativeShape

void J3DNativeShapeSetPosition(u32 index, const f32 (*pMtx)[4]) {
    J3DNativeShape::setPosition(index, pMtx);
}

void J3DNativeShapeSetNormal(u32 index, const f32 (*pMtx)[4]) {
    J3DNativeShape::setNormal(index, pMtx);
}
