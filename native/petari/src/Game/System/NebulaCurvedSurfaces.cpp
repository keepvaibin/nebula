#include "Game/System/NebulaCurvedSurfaces.hpp"

#ifdef TARGET_PC
#include "JSystem/J3DGraphAnimator/J3DModelData.hpp"
#include "JSystem/J3DGraphBase/J3DShape.hpp"
#include "JSystem/J3DGraphBase/J3DShapeDraw.hpp"
#include "JSystem/J3DGraphBase/J3DVertex.hpp"
#include <revolution/gx.h>
#include <revolution/os.h>
#include <stdlib.h>
#include <string.h>

namespace {
    // A growable array in the CRT's heap: the arrays here are CPU-only and short-lived, and the game's heaps are sized
    // as the console's.
    template < typename T >
    struct Grow {
        T* mData;
        u32 mCount;
        u32 mCapacity;

        Grow() : mData(nullptr), mCount(0), mCapacity(0) {}
        ~Grow() { free(mData); }

        bool push(const T& rValue) {
            if (mCount == mCapacity) {
                const u32 capacity = mCapacity != 0 ? mCapacity * 2 : 256;
                T* pData = static_cast< T* >(realloc(mData, capacity * sizeof(T)));
                if (pData == nullptr) {
                    return false;
                }
                mData = pData;
                mCapacity = capacity;
            }
            mData[mCount++] = rValue;
            return true;
        }
    };

    u16 be16(const u8* p) {
        return static_cast< u16 >((p[0] << 8) | p[1]);
    }

    u32 be32(const u8* p) {
        return (static_cast< u32 >(p[0]) << 24) | (static_cast< u32 >(p[1]) << 16) | (static_cast< u32 >(p[2]) << 8) | p[3];
    }

    struct Format {
        GXCompCnt mCount;
        GXCompType mType;
        u8 mFrac;
    };

    bool findFormat(const GXVtxAttrFmtList* pList, GXAttr attr, Format* pFormat) {
        for (; pList != nullptr && pList->attr != GX_VA_NULL; pList++) {
            if (pList->attr == attr) {
                pFormat->mCount = pList->cnt;
                pFormat->mType = pList->type;
                pFormat->mFrac = pList->frac;
                return true;
            }
        }
        return false;
    }

    u32 typeBytes(GXCompType type) {
        return type == GX_U8 || type == GX_S8 ? 1 : type == GX_U16 || type == GX_S16 ? 2 : 4;
    }

    // One component as GX dequantises it (big-endian).
    f32 component(const u8* p, GXCompType type, f32 scale) {
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

    // A shape's vertex layout in its display lists: each attribute's bytes (GX order) and where the position and normal
    // indices sit; false where a position or normal is missing or not indexed, or an attribute is sent directly
    // (its size would need the vertex format).
    struct Layout {
        u32 mStride;
        u32 mPosition;
        u32 mPositionBytes;
        u32 mNormal;
        u32 mNormalBytes;
    };

    bool layoutOf(const GXVtxDescList* pDesc, Layout* pLayout) {
        u32 sizes[GX_VA_TEX7 + 1];
        memset(sizes, 0, sizeof(sizes));
        bool position = false;
        bool normal = false;
        for (; pDesc != nullptr && pDesc->attr != GX_VA_NULL; pDesc++) {
            const GXAttr attr = pDesc->attr == GX_VA_NBT ? GX_VA_NRM : pDesc->attr;
            if (pDesc->type == GX_NONE || attr > GX_VA_TEX7) {
                continue;
            }
            if (attr <= GX_VA_TEX7MTXIDX) {
                sizes[attr] = 1;
            } else if (pDesc->type == GX_INDEX8 || pDesc->type == GX_INDEX16) {
                sizes[attr] = pDesc->type == GX_INDEX8 ? 1 : 2;
                position = position || attr == GX_VA_POS;
                normal = normal || attr == GX_VA_NRM;
            } else {
                return false;
            }
        }
        if (!position || !normal) {
            return false;
        }
        u32 at = 0;
        for (u32 attr = 0; attr <= GX_VA_TEX7; attr++) {
            if (attr == GX_VA_POS) {
                pLayout->mPosition = at;
                pLayout->mPositionBytes = sizes[attr];
            } else if (attr == GX_VA_NRM) {
                pLayout->mNormal = at;
                pLayout->mNormalBytes = sizes[attr];
            }
            at += sizes[attr];
        }
        pLayout->mStride = at;
        return true;
    }

    u16 indexAt(const u8* pVertex, u32 offset, u32 bytes) {
        return bytes == 1 ? pVertex[offset] : be16(pVertex + offset);
    }

    // The triangles of a display list (quads, strips and fans split), as position and normal index pairs.
    bool readDisplayList(const u8* pList, u32 size, const Layout& rLayout, Grow< u16 >* pCorners, u16* pMaxPosition,
                         u16* pMaxNormal) {
        u32 at = 0;
        while (at < size) {
            const u8 op = pList[at++];
            if (op == 0x00 || op == 0x48) {
                continue;  // NOP, vertex cache invalidation
            }
            if (op == 0x08) {
                at += 5;  // CP register
                continue;
            }
            if (op == 0x61 || op == 0x20 || op == 0x28 || op == 0x30 || op == 0x38) {
                at += 4;  // BP register, indexed XF loads
                continue;
            }
            if (op == 0x10) {
                if (at + 4 > size) {
                    return false;
                }
                at += 4 + 4 * ((be32(pList + at) >> 16) + 1);  // XF registers
                continue;
            }
            const u8 primitive = op & 0xF8;
            if (primitive != GX_QUADS && primitive != GX_TRIANGLES && primitive != GX_TRIANGLESTRIP &&
                primitive != GX_TRIANGLEFAN) {
                // Lines and points have no surface; anything else ends what can be read.
                if (primitive != GX_LINES && primitive != GX_LINESTRIP && primitive != GX_POINTS) {
                    return true;
                }
            }
            if (at + 2 > size) {
                return false;
            }
            const u32 count = be16(pList + at);
            at += 2;
            if (at + count * rLayout.mStride > size) {
                return false;
            }
            const u8* pVertices = pList + at;
            at += count * rLayout.mStride;
            const bool lines = primitive == GX_LINES || primitive == GX_LINESTRIP || primitive == GX_POINTS;
            if (lines) {
                continue;
            }
            u32 triangles = 0;
            if (primitive == GX_TRIANGLES) {
                triangles = count / 3;
            } else if (primitive == GX_QUADS) {
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
                } else if (primitive == GX_QUADS) {
                    const u32 q = 4 * (t / 2);
                    v[0] = q;
                    v[1] = q + 1 + (t & 1);
                    v[2] = q + 2 + (t & 1);
                } else if (primitive == GX_TRIANGLESTRIP) {
                    v[0] = t;
                    v[1] = t + 1;
                    v[2] = t + 2;
                } else {
                    v[0] = 0;
                    v[1] = t + 1;
                    v[2] = t + 2;
                }
                for (u32 k = 0; k < 3; k++) {
                    const u8* pVertex = pVertices + v[k] * rLayout.mStride;
                    const u16 position = indexAt(pVertex, rLayout.mPosition, rLayout.mPositionBytes);
                    const u16 normal = indexAt(pVertex, rLayout.mNormal, rLayout.mNormalBytes);
                    if (!pCorners->push(position) || !pCorners->push(normal)) {
                        return false;
                    }
                    *pMaxPosition = position > *pMaxPosition ? position : *pMaxPosition;
                    *pMaxNormal = normal > *pMaxNormal ? normal : *pMaxNormal;
                }
            }
        }
        return true;
    }
};  // namespace

namespace NebulaCurvedSurfaces {
    void registerModel(J3DModelData* pModelData) {
        if (pModelData == nullptr) {
            return;
        }
        J3DVertexData& rVertex = pModelData->getVertexData();
        const void* pPositions = rVertex.getVtxPosArray();
        const void* pNormals = rVertex.getVtxNrmArray();
        Format position;
        Format normal;
        if (pPositions == nullptr || pNormals == nullptr ||
            !findFormat(rVertex.getVtxAttrFmtList(), GX_VA_POS, &position) ||
            !findFormat(rVertex.getVtxAttrFmtList(), GX_VA_NRM, &normal) || position.mCount != GX_POS_XYZ ||
            normal.mCount != GX_NRM_XYZ) {
            return;
        }
        Grow< u16 > corners;
        u16 maxPosition = 0;
        u16 maxNormal = 0;
        for (u16 s = 0; s < pModelData->getShapeNum(); s++) {
            J3DShape* pShape = pModelData->getShapeNodePointer(s);
            Layout layout;
            // Bump-mapped shapes carry their tangents where the renderer would put the displacement normal.
            if (pShape == nullptr || pShape->getNBTFlag() || !layoutOf(pShape->getVtxDesc(), &layout)) {
                continue;
            }
            for (u16 g = 0; g < pShape->getMtxGroupNum(); g++) {
                const J3DShapeDraw* pDraw = pShape->getShapeDraw(g);
                if (pDraw != nullptr &&
                    !readDisplayList(pDraw->getDisplayList(), pDraw->getDisplayListSize(), layout, &corners, &maxPosition,
                                     &maxNormal)) {
                    return;
                }
            }
        }
        if (corners.mCount == 0) {
            return;
        }
        const u32 positionCount = maxPosition + 1;
        const u32 normalCount = maxNormal + 1;
        f32* pDecoded = static_cast< f32* >(malloc((positionCount + normalCount) * 3 * sizeof(f32)));
        if (pDecoded == nullptr) {
            return;
        }
        const u32 positionStride = 3 * typeBytes(position.mType);
        const u32 normalStride = 3 * typeBytes(normal.mType);
        const f32 positionScale = 1.0f / static_cast< f32 >(1 << position.mFrac);
        // GX fixes a normal's scale by its type (6 fraction bits for 8-bit, 14 for 16-bit), whatever the format says.
        const f32 normalScale = normal.mType == GX_S8 ? 1.0f / 64.0f : normal.mType == GX_S16 ? 1.0f / 16384.0f : 1.0f;
        for (u32 i = 0; i < positionCount; i++) {
            const u8* p = static_cast< const u8* >(pPositions) + i * positionStride;
            for (u32 c = 0; c < 3; c++) {
                pDecoded[i * 3 + c] = component(p + c * typeBytes(position.mType), position.mType, positionScale);
            }
        }
        f32* pDecodedNormals = pDecoded + positionCount * 3;
        for (u32 i = 0; i < normalCount; i++) {
            const u8* p = static_cast< const u8* >(pNormals) + i * normalStride;
            for (u32 c = 0; c < 3; c++) {
                pDecodedNormals[i * 3 + c] = component(p + c * typeBytes(normal.mType), normal.mType, normalScale);
            }
        }
        __NebulaCurvedSurface(GX_PHY_ADDR(pPositions), pPositions, positionCount * positionStride, pDecoded,
                              positionCount, pDecodedNormals, normalCount, corners.mData, corners.mCount / 6);
        free(pDecoded);
    }
};  // namespace NebulaCurvedSurfaces
#endif
