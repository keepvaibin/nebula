#include "Game/Util/FurShader.hpp"
#include "Game/Util/MathUtil.hpp"
#include "Game/Render/Material.hpp"
#include "Game/Render/ModelData.hpp"
#include "Game/Render/GameTexture.hpp"
#include <nebula_cooked_texture.h>
#include "Game/Util/StringUtil.hpp"
#include <cmath>
#include <cstring>
#include "Game/Util/Tunable.hpp"

NEBULA_TUNABLE_NAMED(f32, cNum_createFurMesh_f1, "createFurMesh.f1", 0.45f);
NEBULA_TUNABLE_NAMED(f32, cNum_createFurMesh_f2, "createFurMesh.f2", 0.45f);
NEBULA_TUNABLE_NAMED(f32, cNum_refer_f1, "refer.f1", 255.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_getTexelOrder_f1, "getTexelOrder.f1", 65536.0f);

namespace {
    // Corners of a mesh by the bits of their position: a model's seams share a position, and each position grows one way.
    struct FurPosition {
        const f32* mPlace;
        s32 mWinner;  // the vertex whose normal and coordinate it grows by; -1 none
    };

    class FurPositions {
    public:
        explicit FurPositions(u32 count) {
            u32 size = 16;
            while (size < count * 2) {
                size *= 2;
            }
            mMask = size - 1;
            mEntries = new FurPosition[size];
            for (u32 i = 0; i < size; i++) {
                mEntries[i].mPlace = nullptr;
                mEntries[i].mWinner = -1;
            }
        }
        ~FurPositions() {
            delete[] mEntries;
        }

        FurPosition* find(const f32* pPlace, bool add) {
            u32 hash = 0x811C9DC5u;
            for (u32 c = 0; c < 3; c++) {
                u32 word;
                memcpy(&word, pPlace + c, sizeof(word));
                hash = (hash ^ word) * 0x01000193u;
            }
            for (u32 i = hash & mMask;; i = (i + 1) & mMask) {
                FurPosition& rEntry = mEntries[i];
                if (rEntry.mPlace == nullptr) {
                    if (!add) {
                        return nullptr;
                    }
                    rEntry.mPlace = pPlace;
                    return &rEntry;
                }
                if (memcmp(rEntry.mPlace, pPlace, 12) == 0) {
                    return &rEntry;
                }
            }
        }

    private:
        FurPosition* mEntries;
        u32 mMask;
    };

    u32 meshIndex(const u8* pIndices, u32 bytes, u32 at) {
        if (bytes == 2) {
            u16 index;
            memcpy(&index, pIndices + at * 2, sizeof(index));
            return index;
        }
        u32 index;
        memcpy(&index, pIndices + at * 4, sizeof(index));
        return index;
    }
}  // namespace

SiteMesh< VertexFur >* createFurMesh(ModelData* pData, u32 shape, const ResTIMG* pLength) {
    const Mesh* pShape = pData->getMeshAt(shape);
    const NebulaModelMesh& rMesh = *pShape->mFile;
    const FurLengthMap lengthMap(pLength);
    const f32* pPlaces = reinterpret_cast< const f32* >(pData->at(rMesh.positions_at));
    const f32* pAttributes = reinterpret_cast< const f32* >(pData->at(rMesh.attributes_at));
    const u8* pIndices = reinterpret_cast< const u8* >(pData->at(rMesh.indices_at));
    const bool grows = (rMesh.attributes & NEBULA_MESH_NORMAL) != 0 && nebula_mesh_has_coord(&rMesh, 0) != 0;
    const bool hasColor = (rMesh.attributes & NEBULA_MESH_COLOR0) != 0;
    const u32* pMatrices = rMesh.single_matrix == NEBULA_MODEL_NONE ? reinterpret_cast< const u32* >(pData->at(rMesh.matrices_at)) : nullptr;

    // The last corner to name a position in the mesh's order gives its growth.
    FurPositions positions(rMesh.vertex_count);
    if (grows) {
        for (u32 i = 0; i < rMesh.index_count; i++) {
            const u32 vertex = meshIndex(pIndices, rMesh.index_bytes, i);
            positions.find(pPlaces + vertex * 3, true)->mWinner = static_cast< s32 >(vertex);
        }
    }

    // A position another (not fur) material also uses is the border: the skin stays closed there.
    for (u32 other = 0; other < pData->getMeshCount(); other++) {
        if (other == shape) {
            continue;
        }
        const Mesh* pOther = pData->getMeshAt(other);
        if (MR::isEqualSubString(pData->getMaterialName(pOther->mMaterial->mIndex), "Fur")) {
            continue;
        }
        const NebulaModelMesh& rOther = *pOther->mFile;
        const f32* pOtherPlaces = reinterpret_cast< const f32* >(pData->at(rOther.positions_at));
        for (u32 v = 0; v < rOther.vertex_count; v++) {
            FurPosition* pEntry = positions.find(pOtherPlaces + v * 3, false);
            if (pEntry != nullptr) {
                pEntry->mWinner = -1;
            }
        }
    }

    // The coordinates' span, in whole tiles, puts the length map once over the mesh.
    f32 maxV = 0.0f;
    f32 maxU = 0.0f;
    f32 minV = 0.0f;
    f32 minU = 0.0f;
    for (u32 vertex = 0; grows && vertex < rMesh.vertex_count; vertex++) {
        const FurPosition* pEntry = positions.find(pPlaces + vertex * 3, false);
        if (pEntry != nullptr && pEntry->mWinner >= 0) {
            const f32* pWinner = pAttributes + pEntry->mWinner * 6;
            maxU = pWinner[4] > maxU ? pWinner[4] : maxU;
            minU = pWinner[4] < minU ? pWinner[4] : minU;
            maxV = pWinner[5] > maxV ? pWinner[5] : maxV;
            minV = pWinner[5] < minV ? pWinner[5] : minV;
        }
    }
    const s32 spanU = cNum_createFurMesh_f1 + (maxU - minU);
    const s32 spanV = cNum_createFurMesh_f2 + (maxV - minV);

    SiteMesh< VertexFur >* pOut = new SiteMesh< VertexFur >();
    VertexFur* pVertices;
    u32* pOutIndices;
    pOut->initIndexed(rMesh.vertex_count, rMesh.index_count, &pVertices, &pOutIndices);
    for (u32 vertex = 0; vertex < rMesh.vertex_count; vertex++) {
        VertexFur& rOut = pVertices[vertex];
        memset(&rOut, 0, sizeof(rOut));
        rOut.x = pPlaces[vertex * 3];
        rOut.y = pPlaces[vertex * 3 + 1];
        rOut.z = pPlaces[vertex * 3 + 2];
        const f32* pCorner = pAttributes + vertex * 6;
        if (grows) {
            rOut.nx = pCorner[0];
            rOut.ny = pCorner[1];
            rOut.nz = pCorner[2];
            rOut.s = pCorner[4];
            rOut.t = pCorner[5];
        }
        if (hasColor) {
            memcpy(&rOut.color, pCorner + 3, sizeof(rOut.color));
        }
        rOut.matrix = pMatrices != nullptr ? pMatrices[vertex] : rMesh.single_matrix;

        const FurPosition* pEntry = grows ? positions.find(pPlaces + vertex * 3, false) : nullptr;
        if (pEntry == nullptr || pEntry->mWinner < 0) {
            continue;
        }
        const f32* pWinner = pAttributes + pEntry->mWinner * 6;
        f32 u = pWinner[4] / f32(spanU);
        f32 v = pWinner[5] / f32(spanV);
        if (u < 0.0f) {
            u += 1.0f;
        }
        if (v < 0.0f) {
            v += 1.0f;
        }
        rOut.length = lengthMap.refer(u, v);
        Vec3f normal(pWinner[0], pWinner[1], pWinner[2]);
        MR::normalize(&normal);
        rOut.dx = normal.x;
        rOut.dy = normal.y;
        rOut.dz = normal.z;
    }
    for (u32 i = 0; i < rMesh.index_count; i++) {
        pOutIndices[i] = meshIndex(pIndices, rMesh.index_bytes, i);
    }
    pOut->send();
    return pOut;
}

FurLengthMap::FurLengthMap(const ResTIMG* pTexture) : mTexture(nullptr), mTexels(nullptr) {
    if (pTexture == nullptr) {
        return;
    }
    const NebulaTexture* pRecord = nebula_texture_record(pTexture->image());
    if (pRecord == nullptr || pRecord->layout != NEBULA_TEXTURE_L8) {
        return;
    }
    mTexture = pTexture;
    mTexels = reinterpret_cast< const u8* >(pRecord) + NEBULA_TEXTURE_HEADER;
}

f32 FurLengthMap::refer(f32 u, f32 v) const {
    if (mTexels == nullptr) {
        return 1.0f;
    }
    u32 x = getTexelOrder(mTexture->mWidth, u, static_cast< u8 >(mTexture->mWrapS));
    u32 y = getTexelOrder(mTexture->mHeight, v, static_cast< u8 >(mTexture->mWrapT));
    x = x < mTexture->mWidth ? x : mTexture->mWidth - 1;
    y = y < mTexture->mHeight ? y : mTexture->mHeight - 1;
    return f32(mTexels[y * mTexture->mWidth + x]) / cNum_refer_f1;
}

u32 FurLengthMap::getTexelOrder(u32 size, f32 coord, u8 wrap) const {
    // A coordinate that is not a number (a mesh with no coordinate span) reads the first texel.
    if (coord != coord) {
        coord = 0.0f;
    }

    if (coord > 1.0f) {
        switch (wrap) {
        case NEBULA_WRAP_CLAMP:
            coord = 1.0f;
            break;
        case NEBULA_WRAP_REPEAT:
        case NEBULA_WRAP_MIRROR:
            // Past 65536 or infinite, one subtraction of 1 at a time never ends; the fraction is taken at once.
            if (!(coord < cNum_getTexelOrder_f1)) {
                coord = std::isinf(coord) ? 0.0f : std::fmod(coord, 1.0f);
            }
            while (coord > 1.0f) {
                coord -= 1.0f;
            }
            while (coord < 0.0f) {
                coord += 1.0f;
            }
            break;
        }
    }
    return (s32)((size - 1) * coord);
}
