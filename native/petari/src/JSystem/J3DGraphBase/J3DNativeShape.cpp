#include "JSystem/J3DGraphBase/J3DNativeShape.hpp"
#include "JSystem/J3DGraphAnimator/J3DModelData.hpp"
#include "JSystem/J3DGraphBase/J3DShape.hpp"
#include "JSystem/J3DGraphBase/J3DShapeDraw.hpp"
#include "JSystem/J3DGraphBase/J3DSys.hpp"
#include "JSystem/J3DGraphBase/J3DVertex.hpp"
#include <nebula_game_api.h>
#include <nebula_gx_fifo.h>
#include <revolution/gx/GXRegs.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern "C" const NebulaHostServices* nebula_host(void);

namespace {
    // A growable array in the CRT's heap: what is built here is copied by the host and freed.
    template < typename T >
    struct Grow {
        T* mData;
        u32 mCount;
        u32 mCapacity;

        Grow() : mData(nullptr), mCount(0), mCapacity(0) {}
        ~Grow() { free(mData); }

        T* push() {
            if (mCount == mCapacity) {
                const u32 capacity = mCapacity != 0 ? mCapacity * 2 : 256;
                T* pData = static_cast< T* >(realloc(mData, capacity * sizeof(T)));
                if (pData == nullptr) {
                    return nullptr;
                }
                mData = pData;
                mCapacity = capacity;
            }
            return &mData[mCount++];
        }
    };

    void fail(const char* pWhat, const void* pShape) {
        char text[160];
        snprintf(text, sizeof(text), "J3D shape %p: %s", pShape, pWhat);
        const NebulaHostServices* pHost = nebula_host();
        pHost->fatal(pHost->user, text);
    }

    u64 keyOf(const void* p) {
        return static_cast< u64 >(reinterpret_cast< uintptr_t >(p));
    }

    u16 be16(const u8* p) {
        return static_cast< u16 >((p[0] << 8) | p[1]);
    }

    u32 be32(const u8* p) {
        return (static_cast< u32 >(p[0]) << 24) | (static_cast< u32 >(p[1]) << 16) | (static_cast< u32 >(p[2]) << 8) | p[3];
    }

    u32 typeBytes(u32 type) {
        return type == GX_U8 || type == GX_S8 ? 1 : type == GX_U16 || type == GX_S16 ? 2 : 4;
    }

    // One component as the arrays hold it (big-endian), times `scale`.
    f32 component(const u8* p, u32 type, f32 scale) {
        switch (type) {
        case GX_U8:
            return p[0] * scale;
        case GX_S8:
            return static_cast< s8 >(p[0]) * scale;
        case GX_U16:
            return be16(p) * scale;
        case GX_S16:
            return static_cast< s16 >(be16(p)) * scale;
        default: {
            const u32 bits = be32(p);
            f32 value;
            memcpy(&value, &bits, sizeof(value));
            return value;
        }
        }
    }

    // A colour entry as R << 24 | G << 16 | B << 8 | A (each format widened by repeating its high bits).
    u32 color(const u8* p, u32 type) {
        switch (type) {
        case GX_RGB565: {
            const u32 v = be16(p);
            const u32 r = (v >> 11) & 0x1F, g = (v >> 5) & 0x3F, b = v & 0x1F;
            return ((r << 3 | r >> 2) << 24) | ((g << 2 | g >> 4) << 16) | ((b << 3 | b >> 2) << 8) | 0xFF;
        }
        case GX_RGB8:
        case GX_RGBX8:
            return (static_cast< u32 >(p[0]) << 24) | (static_cast< u32 >(p[1]) << 16) | (static_cast< u32 >(p[2]) << 8) | 0xFF;
        case GX_RGBA4: {
            const u32 v = be16(p);
            const u32 r = (v >> 12) & 0xF, g = (v >> 8) & 0xF, b = (v >> 4) & 0xF, a = v & 0xF;
            return ((r | r << 4) << 24) | ((g | g << 4) << 16) | ((b | b << 4) << 8) | (a | a << 4);
        }
        case GX_RGBA6: {
            const u32 v = (static_cast< u32 >(p[0]) << 16) | (static_cast< u32 >(p[1]) << 8) | p[2];
            const u32 r = (v >> 18) & 0x3F, g = (v >> 12) & 0x3F, b = (v >> 6) & 0x3F, a = v & 0x3F;
            return ((r << 2 | r >> 4) << 24) | ((g << 2 | g >> 4) << 16) | ((b << 2 | b >> 4) << 8) | (a << 2 | a >> 4);
        }
        default:
            return be32(p);
        }
    }

    // The format the model's attribute list gives `attr` (the defaults J3D's vertex format has without one).
    struct Format {
        u32 mCnt;
        u32 mType;
        u32 mFrac;
    };

    Format formatOf(const GXVtxAttrFmtList* pList, GXAttr attr) {
        Format format;
        format.mCnt = attr == GX_VA_POS ? GX_POS_XYZ : attr == GX_VA_NRM ? GX_NRM_XYZ :
                      attr == GX_VA_CLR0 || attr == GX_VA_CLR1 ? GX_CLR_RGBA : GX_TEX_ST;
        format.mType = attr == GX_VA_CLR0 || attr == GX_VA_CLR1 ? GX_RGBA8 : GX_F32;
        format.mFrac = 0;
        for (; pList != nullptr && pList->attr != GX_VA_NULL; pList++) {
            const GXAttr listed = pList->attr == GX_VA_NBT ? GX_VA_NRM : pList->attr;
            if (listed == attr) {
                format.mCnt = pList->cnt;
                format.mType = pList->type;
                format.mFrac = pList->frac;
            }
        }
        return format;
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

    // Entries of an array as values: J3D's strides (J3DShape's array setup), GX's dequantisation (fractions for every
    // position and texture type, fixed ones for normals).
    void sendArray(const void* pArray, u32 array, u32 count, const Format& rFormat, bool nbtNormals) {
        const u8* pBytes = static_cast< const u8* >(pArray);
        const u32 bytes = typeBytes(rFormat.mType);
        u32 kind = NEBULA_SHAPE_ARRAY_TEXCOORD;
        u32 floats = 2;
        u32 stride = rFormat.mType == GX_F32 ? 8 : 4;
        if (array == kPosition) {
            kind = NEBULA_SHAPE_ARRAY_POSITION;
            floats = 3;
            stride = rFormat.mType == GX_F32 ? 12 : 6;
        } else if (array == kNormal || array == kNbt) {
            kind = nbtNormals ? NEBULA_SHAPE_ARRAY_NBT : NEBULA_SHAPE_ARRAY_NORMAL;
            floats = nbtNormals ? 9 : 3;
            stride = (rFormat.mType == GX_F32 ? 12 : 6) * (array == kNbt ? 3 : 1);
        } else if (array == kColor0 || array == kColor1) {
            kind = NEBULA_SHAPE_ARRAY_COLOR;
            floats = 1;
            stride = 4;
        }
        void* pValues = malloc(static_cast< size_t >(count) * floats * 4 + 4);
        if (pValues == nullptr) {
            fail("no memory for its arrays", pArray);
            return;
        }
        if (kind == NEBULA_SHAPE_ARRAY_COLOR) {
            u32* pOut = static_cast< u32* >(pValues);
            for (u32 i = 0; i < count; i++) {
                pOut[i] = color(pBytes + i * stride, rFormat.mType);
            }
        } else {
            f32* pOut = static_cast< f32* >(pValues);
            f32 scale = 1.0f / static_cast< f32 >(1 << rFormat.mFrac);
            u32 components = kind == NEBULA_SHAPE_ARRAY_POSITION ? (rFormat.mCnt == GX_POS_XYZ ? 3 : 2) :
                             kind == NEBULA_SHAPE_ARRAY_TEXCOORD ? (rFormat.mCnt == GX_TEX_ST ? 2 : 1) : 3;
            if (kind == NEBULA_SHAPE_ARRAY_NORMAL || kind == NEBULA_SHAPE_ARRAY_NBT) {
                const u32 shift = rFormat.mType == GX_U8 ? 7 : rFormat.mType == GX_S8 ? 6 : rFormat.mType == GX_U16 ? 15 : 14;
                scale = rFormat.mType == GX_F32 ? 1.0f : 1.0f / static_cast< f32 >(1 << shift);
            }
            const u32 vectors = kind == NEBULA_SHAPE_ARRAY_NBT ? 3 : 1;
            for (u32 i = 0; i < count; i++) {
                f32* pEntry = pOut + i * floats;
                for (u32 v = 0; v < vectors; v++) {
                    for (u32 c = 0; c < floats / vectors; c++) {
                        pEntry[v * 3 + c] = c < components ?
                                                component(pBytes + i * stride + (v * components + c) * bytes, rFormat.mType, scale) :
                                                0.0f;
                    }
                }
            }
        }
        NebulaShapeArray values;
        values.key = keyOf(pArray);
        values.kind = kind;
        values.count = count;
        values.values = pValues;
        const NebulaHostServices* pHost = nebula_host();
        pHost->shape_array(pHost->user, &values);
        free(pValues);
    }

    // Where each attribute sits in a display list's vertices (GX's order) and how wide it is.
    struct Layout {
        u32 mStride;
        s32 mOffset[GX_VA_TEX7 + 1];  // -1 where absent
        u32 mBytes[GX_VA_TEX7 + 1];
        bool mNbt;
        bool mNbtThree;
    };

    bool layoutOf(const J3DShape* pShape, const J3DVertexData* pData, Layout* pLayout) {
        u32 type[GX_VA_TEX7 + 1];
        memset(type, 0, sizeof(type));
        pLayout->mNbt = false;
        for (const GXVtxDescList* pDesc = pShape->mVtxDesc; pDesc != nullptr && pDesc->attr != GX_VA_NULL; pDesc++) {
            const GXAttr attr = pDesc->attr == GX_VA_NBT ? GX_VA_NRM : pDesc->attr;
            if (attr > GX_VA_TEX7 || pDesc->type == GX_NONE) {
                continue;
            }
            if (attr >= GX_VA_POS && pDesc->type == GX_DIRECT) {
                return false;  // values inside the list: J3D's shapes index every array
            }
            type[attr] = pDesc->type;
            pLayout->mNbt = pLayout->mNbt || pDesc->attr == GX_VA_NBT;
        }
        const Format normal = formatOf(pData->mVtxAttrFmtList, GX_VA_NRM);
        pLayout->mNbtThree = normal.mCnt == GX_NRM_NBT3;
        // J3D's vertex format: NBT normals for a bump-mapped shape or a listed NBT format.
        const bool nbt = pLayout->mNbt || normal.mCnt != GX_NRM_XYZ;
        u32 at = 0;
        for (u32 attr = 0; attr <= GX_VA_TEX7; attr++) {
            pLayout->mOffset[attr] = -1;
            pLayout->mBytes[attr] = 0;
            if (type[attr] == GX_NONE) {
                continue;
            }
            u32 bytes = attr < GX_VA_POS ? 1 : type[attr] == GX_INDEX8 ? 1 : 2;
            if (attr == GX_VA_NRM && nbt && pLayout->mNbtThree) {
                bytes *= 3;
            }
            pLayout->mOffset[attr] = static_cast< s32 >(at);
            pLayout->mBytes[attr] = bytes;
            at += bytes;
        }
        pLayout->mNbt = nbt;
        pLayout->mStride = at;
        return pLayout->mOffset[GX_VA_POS] >= 0;
    }

    u16 indexAt(const u8* pVertex, const Layout& rLayout, u32 attr, u32 which) {
        if (rLayout.mOffset[attr] < 0) {
            return 0;
        }
        const u8* p = pVertex + rLayout.mOffset[attr];
        const u32 width = attr == GX_VA_NRM && rLayout.mNbtThree && rLayout.mNbt ? rLayout.mBytes[attr] / 3 : rLayout.mBytes[attr];
        p += which * width;
        return width == 1 ? p[0] : be16(p);
    }

    // Distinct corners by their bytes (open addressing over indices into the corner array).
    struct CornerSet {
        Grow< NebulaShapeCorner > mCorners;
        u32* mSlots;
        u32 mSlotCount;

        CornerSet() : mSlots(nullptr), mSlotCount(0) {}
        ~CornerSet() { free(mSlots); }

        static u32 hash(const NebulaShapeCorner& rCorner) {
            const u8* p = reinterpret_cast< const u8* >(&rCorner);
            u32 h = 2166136261u;
            for (u32 i = 0; i < sizeof(NebulaShapeCorner); i++) {
                h = (h ^ p[i]) * 16777619u;
            }
            return h;
        }

        bool grow() {
            const u32 count = mSlotCount != 0 ? mSlotCount * 2 : 1024;
            u32* pSlots = static_cast< u32* >(malloc(count * sizeof(u32)));
            if (pSlots == nullptr) {
                return false;
            }
            memset(pSlots, 0xFF, count * sizeof(u32));
            for (u32 i = 0; i < mCorners.mCount; i++) {
                u32 s = hash(mCorners.mData[i]) & (count - 1);
                while (pSlots[s] != 0xFFFFFFFF) {
                    s = (s + 1) & (count - 1);
                }
                pSlots[s] = i;
            }
            free(mSlots);
            mSlots = pSlots;
            mSlotCount = count;
            return true;
        }

        // The corner's index, added when new; -1 past 65,536 corners or without memory.
        s32 find(const NebulaShapeCorner& rCorner) {
            if ((mCorners.mCount + 1) * 2 > mSlotCount && !grow()) {
                return -1;
            }
            u32 s = hash(rCorner) & (mSlotCount - 1);
            while (mSlots[s] != 0xFFFFFFFF) {
                if (memcmp(&mCorners.mData[mSlots[s]], &rCorner, sizeof(rCorner)) == 0) {
                    return static_cast< s32 >(mSlots[s]);
                }
                s = (s + 1) & (mSlotCount - 1);
            }
            if (mCorners.mCount >= 0x10000) {
                return -1;
            }
            NebulaShapeCorner* pNew = mCorners.push();
            if (pNew == nullptr) {
                return -1;
            }
            *pNew = rCorner;
            mSlots[s] = mCorners.mCount - 1;
            return static_cast< s32 >(mCorners.mCount - 1);
        }
    };

    void noteUse(u32* pNeeded, u32 array, u16 index) {
        if (static_cast< u32 >(index) + 1 > pNeeded[array]) {
            pNeeded[array] = static_cast< u32 >(index) + 1;
        }
    }

    // A display list's corner as the arrays name it.
    void readCorner(const u8* pVertex, const Layout& rLayout, NebulaShapeCorner* pCorner, u32* pNeeded) {
        memset(pCorner, 0, sizeof(*pCorner));
        pCorner->position_matrix = static_cast< u8 >(rLayout.mOffset[GX_VA_PNMTXIDX] >= 0 ? pVertex[rLayout.mOffset[GX_VA_PNMTXIDX]] : 0);
        for (u32 t = 0; t < 8; t++) {
            const s32 at = rLayout.mOffset[GX_VA_TEX0MTXIDX + t];
            pCorner->texture_matrix[t] = static_cast< u8 >(at >= 0 ? pVertex[at] : 0);
        }
        pCorner->position = indexAt(pVertex, rLayout, GX_VA_POS, 0);
        noteUse(pNeeded, kPosition, pCorner->position);
        if (rLayout.mOffset[GX_VA_NRM] >= 0) {
            const u32 array = rLayout.mNbt ? kNbt : kNormal;
            pCorner->normal = indexAt(pVertex, rLayout, GX_VA_NRM, 0);
            pCorner->tangent = rLayout.mNbt && rLayout.mNbtThree ? indexAt(pVertex, rLayout, GX_VA_NRM, 1) : pCorner->normal;
            pCorner->binormal = rLayout.mNbt && rLayout.mNbtThree ? indexAt(pVertex, rLayout, GX_VA_NRM, 2) : pCorner->normal;
            noteUse(pNeeded, array, pCorner->normal);
            noteUse(pNeeded, array, pCorner->tangent);
            noteUse(pNeeded, array, pCorner->binormal);
        }
        for (u32 c = 0; c < 2; c++) {
            if (rLayout.mOffset[GX_VA_CLR0 + c] >= 0) {
                pCorner->color[c] = indexAt(pVertex, rLayout, GX_VA_CLR0 + c, 0);
                noteUse(pNeeded, kColor0 + c, pCorner->color[c]);
            }
        }
        for (u32 t = 0; t < 8; t++) {
            if (rLayout.mOffset[GX_VA_TEX0 + t] >= 0) {
                pCorner->texcoord[t] = indexAt(pVertex, rLayout, GX_VA_TEX0 + t, 0);
                noteUse(pNeeded, kTexCoord0 + t, pCorner->texcoord[t]);
            }
        }
    }

    // GX's second quad opcode (drawn as quads).
    const u8 kQuads2 = 0x88;

    // A group's display list as corners and triangles: GX's quads, strips and fans as the renderer has always split
    // them (a strip's odd triangles turned to keep its winding).
    bool readGroup(const J3DShapeDraw* pDraw, const Layout& rLayout, CornerSet* pSet, Grow< u16 >* pIndices, u32* pNeeded) {
        const u8* pList = static_cast< const u8* >(pDraw->getDisplayList());
        const u32 size = pDraw->getDisplayListSize();
        u32 at = 0;
        while (at < size) {
            const u8 op = pList[at++];
            if (op == 0x00) {
                continue;
            }
            const u8 primitive = op & 0xF8;
            if ((op & 0x80) == 0 || (op & 7) != GX_VTXFMT0 || primitive == GX_LINES || primitive == GX_LINESTRIP ||
                primitive == GX_POINTS || at + 2 > size) {
                return false;
            }
            const u32 count = be16(pList + at);
            at += 2;
            if (at + count * rLayout.mStride > size) {
                return false;
            }
            const u8* pVertices = pList + at;
            at += count * rLayout.mStride;
            s32 corner[3];
            u32 triangles = 0;
            if (primitive == GX_TRIANGLES) {
                triangles = count / 3;
            } else if (primitive == GX_QUADS || primitive == kQuads2) {
                triangles = count / 4 * 2;
            } else if (count >= 3) {
                triangles = count - 2;
            }
            for (u32 t = 0; t < triangles; t++) {
                u32 v[3];
                if (primitive == GX_TRIANGLES) {
                    v[0] = 3 * t;
                    v[1] = 3 * t + 1;
                    v[2] = 3 * t + 2;
                } else if (primitive == GX_QUADS || primitive == kQuads2) {
                    const u32 q = 4 * (t / 2);
                    v[0] = q;
                    v[1] = q + 1 + (t & 1);
                    v[2] = q + 2 + (t & 1);
                } else if (primitive == GX_TRIANGLESTRIP) {
                    v[0] = (t & 1) != 0 ? t + 1 : t;
                    v[1] = (t & 1) != 0 ? t : t + 1;
                    v[2] = t + 2;
                } else {
                    v[0] = 0;
                    v[1] = t + 1;
                    v[2] = t + 2;
                }
                for (u32 k = 0; k < 3; k++) {
                    NebulaShapeCorner c;
                    readCorner(pVertices + v[k] * rLayout.mStride, rLayout, &c, pNeeded);
                    corner[k] = pSet->find(c);
                    if (corner[k] < 0) {
                        return false;
                    }
                }
                for (u32 k = 0; k < 3; k++) {
                    u16* pIndex = pIndices->push();
                    if (pIndex == nullptr) {
                        return false;
                    }
                    *pIndex = static_cast< u16 >(corner[k]);
                }
            }
        }
        return true;
    }
    // The matrices and current slots the next draw reads.
    NebulaShapeMatrix sMatrices[64];
    u32 sMatrixCount = 0;
    NebulaShapeCurrent sCurrent;
    bool sCurrentSet = false;
    NebulaShapeTexGen sTexGens[8];
    u32 sTexGenCount = 0;

    NebulaShapeMatrix* nextMatrix(u32 kind, u32 id, u16 modelMatrix) {
        if (sMatrixCount == sizeof(sMatrices) / sizeof(sMatrices[0])) {
            fail("more matrices before one draw than a draw can carry", nullptr);
            return nullptr;
        }
        NebulaShapeMatrix* pOut = &sMatrices[sMatrixCount++];
        memset(pOut, 0, sizeof(*pOut));
        pOut->kind = static_cast< u8 >(kind);
        pOut->id = static_cast< u8 >(id);
        pOut->model_matrix = modelMatrix;
        return pOut;
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
    void loadPosition(const f32 (*pMtx)[4], u32 id, u16 modelMatrix) {
        if (NebulaShapeMatrix* pOut = nextMatrix(NEBULA_SHAPE_MATRIX_POSITION, id, modelMatrix)) {
            copyRows(pOut->values, &pMtx[0][0], 3, 4, 4, 4);
        }
    }

    void loadNormal(const f32 (*pMtx)[4], u32 id, u16 modelMatrix) {
        if (NebulaShapeMatrix* pOut = nextMatrix(NEBULA_SHAPE_MATRIX_NORMAL, id, modelMatrix)) {
            copyRows(pOut->values, &pMtx[0][0], 3, 3, 4, 3);
        }
    }

    void loadNormal33(const f32 (*pMtx)[3], u32 id, u16 modelMatrix) {
        if (NebulaShapeMatrix* pOut = nextMatrix(NEBULA_SHAPE_MATRIX_NORMAL, id, modelMatrix)) {
            copyRows(pOut->values, &pMtx[0][0], 3, 3, 3, 3);
        }
    }

    // A normal matrix as a texture matrix: its fourth column zero.
    void loadNormalToTexture(const f32 (*pMtx)[4], u32 id) {
        if (NebulaShapeMatrix* pOut = nextMatrix(NEBULA_SHAPE_MATRIX_TEXTURE, id, 0)) {
            copyRows(pOut->values, &pMtx[0][0], 3, 3, 4, 4);
        }
    }

    void loadNormal33ToTexture(const f32 (*pMtx)[3], u32 id) {
        if (NebulaShapeMatrix* pOut = nextMatrix(NEBULA_SHAPE_MATRIX_TEXTURE, id, 0)) {
            copyRows(pOut->values, &pMtx[0][0], 3, 3, 3, 4);
        }
    }

    void loadTexture(const f32 (*pMtx)[4], u32 id, GXTexMtxType type) {
        const u32 kind = id >= GX_PTTEXMTX0 ? NEBULA_SHAPE_MATRIX_POST_TEXTURE :
                         type == GX_MTX2x4  ? NEBULA_SHAPE_MATRIX_TEXTURE_2X4 :
                                              NEBULA_SHAPE_MATRIX_TEXTURE;
        if (NebulaShapeMatrix* pOut = nextMatrix(kind, id, 0)) {
            copyRows(pOut->values, &pMtx[0][0], kind == NEBULA_SHAPE_MATRIX_TEXTURE_2X4 ? 2 : 3, 4, 4, 4);
        }
    }

    void sendPosition(const f32 (*pMtx)[4], u32 id) {
        loadPosition(pMtx, id, 0);
        flushState();
    }

    void sendNormal(const f32 (*pMtx)[4], u32 id) {
        loadNormal(pMtx, id, 0);
        flushState();
    }

    void sendTexture(const f32 (*pMtx)[4], u32 id, GXTexMtxType type) {
        loadTexture(pMtx, id, type);
        flushState();
    }

    void setTexGen(u8 coord, u8 type, u8 source, bool normalize, u8 postMatrix) {
        if (sTexGenCount == 8) {
            fail("more texture coordinate generators before one draw than a draw can carry", nullptr);
            return;
        }
        NebulaShapeTexGen& rOut = sTexGens[sTexGenCount++];
        rOut.coord = coord;
        rOut.type = type;
        rOut.source = source;
        rOut.normalize = normalize ? 1 : 0;
        rOut.post_matrix = postMatrix;
        rOut.reserved[0] = rOut.reserved[1] = rOut.reserved[2] = 0;
    }

    void setCurrent(u8 position, const u8* pTexture) {
        sCurrent.position = position;
        for (u32 t = 0; t < 8; t++) {
            sCurrent.texture[t] = pTexture[t];
        }
        sCurrentSet = true;
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

    void registerShape(J3DShape* pShape) {
        pShape->mNebulaRegistered = true;
        J3DVertexData* pData = pShape->mVertexData;
        Layout layout;
        if (pData == nullptr || !layoutOf(pShape, pData, &layout)) {
            fail("its vertices are not indexed into its model's arrays", pShape);
            return;
        }
        u32 attributes = 0;
        if (layout.mOffset[GX_VA_NRM] >= 0) {
            attributes |= NEBULA_SHAPE_NORMAL | (layout.mNbt ? NEBULA_SHAPE_NBT : 0) |
                          (layout.mNbt && layout.mNbtThree ? NEBULA_SHAPE_NBT_THREE : 0);
        }
        attributes |= layout.mOffset[GX_VA_CLR0] >= 0 ? NEBULA_SHAPE_COLOR0 : 0;
        attributes |= layout.mOffset[GX_VA_CLR1] >= 0 ? NEBULA_SHAPE_COLOR1 : 0;
        attributes |= layout.mOffset[GX_VA_PNMTXIDX] >= 0 ? NEBULA_SHAPE_POSITION_MATRIX : 0;
        for (u32 t = 0; t < 8; t++) {
            attributes |= layout.mOffset[GX_VA_TEX0 + t] >= 0 ? NEBULA_SHAPE_TEXCOORD0 << t : 0;
            attributes |= layout.mOffset[GX_VA_TEX0MTXIDX + t] >= 0 ? NEBULA_SHAPE_TEXTURE_MATRIX0 << t : 0;
        }

        u32 needed[kArrayCount];
        memset(needed, 0, sizeof(needed));
        const NebulaHostServices* pHost = nebula_host();
        for (u16 g = 0; g < pShape->mMtxGroupNum; g++) {
            const J3DShapeDraw* pDraw = pShape->mShapeDraw[g];
            if (pDraw == nullptr) {
                continue;
            }
            CornerSet corners;
            Grow< u16 > indices;
            if (!readGroup(pDraw, layout, &corners, &indices, needed)) {
                fail("its display list holds more than triangles of one vertex format, or over 65,536 corners", pShape);
                return;
            }
            NebulaShapeGeometry geometry;
            memset(&geometry, 0, sizeof(geometry));
            geometry.key = keyOf(pDraw);
            geometry.attributes = attributes;
            for (u32 a = 0; a < 12; a++) {
                const u32 array = a == 0 ? kPosition : a == 1 ? (layout.mNbt ? kNbt : kNormal) : a - 2 + kColor0;
                geometry.arrays[a] = keyOf(arrayOf(pData, array));
            }
            geometry.corner_count = corners.mCorners.mCount;
            geometry.index_count = indices.mCount;
            geometry.corners = corners.mCorners.mData;
            geometry.indices = indices.mData;
            pHost->shape_geometry(pHost->user, &geometry);
        }

        // The model's arrays as far as its registered shapes read them (sent again when a shape reads further).
        for (u32 array = 0; array < kArrayCount; array++) {
            const void* pArray = arrayOf(pData, array);
            if (pArray == nullptr || needed[array] <= pData->mNebulaArrayCount[array]) {
                continue;
            }
            const GXAttr attr = array == kPosition ? GX_VA_POS : array <= kNbt ? GX_VA_NRM :
                                array <= kColor1 ? static_cast< GXAttr >(GX_VA_CLR0 + array - kColor0) :
                                                   static_cast< GXAttr >(GX_VA_TEX0 + array - kTexCoord0);
            const Format format = formatOf(pData->mVtxAttrFmtList, attr);
            sendArray(pArray, array, needed[array], format, array == kNbt || (array == kNormal && format.mCnt != GX_NRM_XYZ));
            pData->mNebulaArrayCount[array] = needed[array];
        }
    }

    void draw(const J3DShapeDraw* pDraw) {
        const NebulaHostServices* pHost = nebula_host();
        pHost->shape_draw(pHost->user, keyOf(pDraw), __NebulaGXWriter.ptr, keyOf(j3dSys.getVtxPos()),
                          keyOf(j3dSys.getVtxNrm()), keyOf(j3dSys.getVtxCol()), sMatrices, sMatrixCount,
                          sCurrentSet ? &sCurrent : nullptr, sTexGens, sTexGenCount);
        sMatrixCount = 0;
        sCurrentSet = false;
        sTexGenCount = 0;
    }

    void flushState() {
        if (sMatrixCount == 0 && !sCurrentSet && sTexGenCount == 0) {
            return;
        }
        const NebulaHostServices* pHost = nebula_host();
        pHost->shape_draw(pHost->user, 0, __NebulaGXWriter.ptr, 0, 0, 0, sMatrices, sMatrixCount,
                          sCurrentSet ? &sCurrent : nullptr, sTexGens, sTexGenCount);
        sMatrixCount = 0;
        sCurrentSet = false;
        sTexGenCount = 0;
    }

    void sendPositions(const void* pArray, u32 count, GXCompType type, GXCompCnt cnt, u8 frac) {
        Format format;
        format.mCnt = cnt;
        format.mType = type;
        format.mFrac = frac;
        sendArray(pArray, kPosition, count, format, false);
    }
};  // namespace J3DNativeShape

void J3DNativeShapeSendPosition(const f32 (*pMtx)[4], u32 id) {
    J3DNativeShape::sendPosition(pMtx, id);
}

void J3DNativeShapeSendNormal(const f32 (*pMtx)[4], u32 id) {
    J3DNativeShape::sendNormal(pMtx, id);
}
