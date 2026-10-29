#include "Game/Util/NativeMesh.hpp"
#include "JSystem/J3DGraphBase/J3DNativeShape.hpp"
#include "JSystem/JKernel/JKRHeap.hpp"
#include <nebula_gx_fifo.h>
#include <revolution/gx/GXRegs.h>
#include <string.h>

extern "C" const NebulaHostServices* nebula_host(void);

namespace {
    // Floats per entry of a slot's own array (colours are one word, kept in a float's room).
    u32 width(u32 slot) {
        if (slot <= NativeMesh::SLOT_NORMAL) {
            return 3;
        }
        return slot <= NativeMesh::SLOT_COLOR1 ? 1 : 2;
    }

    u32 kindOf(u32 slot) {
        if (slot == NativeMesh::SLOT_POSITION) {
            return NEBULA_SHAPE_ARRAY_POSITION;
        }
        if (slot == NativeMesh::SLOT_NORMAL) {
            return NEBULA_SHAPE_ARRAY_NORMAL;
        }
        return slot <= NativeMesh::SLOT_COLOR1 ? NEBULA_SHAPE_ARRAY_COLOR : NEBULA_SHAPE_ARRAY_TEXCOORD;
    }

    // Whether attributes `attributes` read slot `slot`.
    bool reads(u32 attributes, u32 slot) {
        switch (slot) {
        case NativeMesh::SLOT_POSITION:
            return true;
        case NativeMesh::SLOT_NORMAL:
            return (attributes & NEBULA_SHAPE_NORMAL) != 0;
        case NativeMesh::SLOT_COLOR0:
            return (attributes & NEBULA_SHAPE_COLOR0) != 0;
        case NativeMesh::SLOT_COLOR1:
            return (attributes & NEBULA_SHAPE_COLOR1) != 0;
        default:
            return (attributes & (NEBULA_SHAPE_TEXCOORD0 << (slot - NativeMesh::SLOT_TEXCOORD0))) != 0;
        }
    }

    // The slot's entry in a corner.
    u16* entry(NebulaShapeCorner* pCorner, u32 slot) {
        switch (slot) {
        case NativeMesh::SLOT_POSITION:
            return &pCorner->position;
        case NativeMesh::SLOT_NORMAL:
            return &pCorner->normal;
        case NativeMesh::SLOT_COLOR0:
            return &pCorner->color[0];
        case NativeMesh::SLOT_COLOR1:
            return &pCorner->color[1];
        default:
            return &pCorner->texcoord[slot - NativeMesh::SLOT_TEXCOORD0];
        }
    }

    void fail(const char* pWhat) {
        OSPanic(__FILE__, __LINE__, "NativeMesh: %s", pWhat);
    }
}  // namespace

NativeMesh::NativeMesh()
    : mAttributes(0), mMaxCorners(0), mOwned(0), mValueCount(0), mCorners(nullptr), mCornerCount(0), mIndices(nullptr),
      mIndexCount(0), mPrimitive(nullptr), mPrimitiveType(0), mPrimitiveCount(0), mPrimitiveAt(0) {
    for (u32 i = 0; i < 12; i++) {
        mArrays[i] = 0;
        mValues[i] = nullptr;
    }
    memset(&mCorner, 0, sizeof(mCorner));
}

u64 NativeMesh::key(u32 slot) const {
    // Inside the mesh object, so no other array is named the same while it lives.
    return static_cast< u64 >(reinterpret_cast< uintptr_t >(this)) + 1 + slot;
}

void NativeMesh::init(u32 attributes, u32 maxCorners) {
    mAttributes = attributes;
    mMaxCorners = maxCorners;
    mOwned = 0;
    for (u32 slot = 0; slot < 12; slot++) {
        if (reads(attributes, slot)) {
            mOwned |= 1u << slot;
            mArrays[slot] = key(slot);
            mValues[slot] = new f32[width(slot) * maxCorners];
        }
    }
    mValueCount = 0;
    mCorners = new NebulaShapeCorner[maxCorners];
    mCornerCount = 0;
    mIndices = new u16[3 * maxCorners];
    mIndexCount = 0;
    mPrimitive = new u16[maxCorners];
    memset(&mCorner, 0, sizeof(mCorner));
}

void NativeMesh::useArray(u32 slot, const void* pKey) {
    if ((mOwned & (1u << slot)) != 0) {
        delete[] mValues[slot];
        mValues[slot] = nullptr;
        mOwned &= ~(1u << slot);
    }
    mArrays[slot] = static_cast< u64 >(reinterpret_cast< uintptr_t >(pKey));
}

void NativeMesh::begin(GXPrimitive primitive, u16 count) {
    mPrimitiveType = static_cast< u8 >(primitive);
    mPrimitiveCount = count;
    mPrimitiveAt = 0;
}

void NativeMesh::position(f32 x, f32 y, f32 z) {
    f32* p = mValues[SLOT_POSITION] + 3 * mValueCount;
    p[0] = x;
    p[1] = y;
    p[2] = z;
}

void NativeMesh::normal(f32 x, f32 y, f32 z) {
    f32* p = mValues[SLOT_NORMAL] + 3 * mValueCount;
    p[0] = x;
    p[1] = y;
    p[2] = z;
}

void NativeMesh::color(u8 r, u8 g, u8 b, u8 a) {
    const u32 word = static_cast< u32 >(r) << 24 | static_cast< u32 >(g) << 16 | static_cast< u32 >(b) << 8 | a;
    memcpy(mValues[SLOT_COLOR0] + mValueCount, &word, sizeof(word));
}

void NativeMesh::texCoord(u32 coord, f32 s, f32 t) {
    f32* p = mValues[SLOT_TEXCOORD0 + coord] + 2 * mValueCount;
    p[0] = s;
    p[1] = t;
}

void NativeMesh::index(u32 slot, u16 value) {
    *entry(&mCorner, slot) = value;
}

void NativeMesh::corner() {
    if (mValueCount == mMaxCorners || mPrimitiveAt == mPrimitiveCount) {
        fail("more corners than given room for");
        return;
    }
    // The slots the mesh owns read this corner's own entries.
    for (u32 slot = 0; slot < 12; slot++) {
        if ((mOwned & (1u << slot)) != 0) {
            *entry(&mCorner, slot) = static_cast< u16 >(mValueCount);
        }
    }
    u32 found = mCornerCount;
    if (mOwned == 0) {
        // Corners naming only the game's arrays repeat: each distinct one once.
        for (u32 i = 0; i < mCornerCount; i++) {
            if (memcmp(&mCorners[i], &mCorner, sizeof(mCorner)) == 0) {
                found = i;
                break;
            }
        }
    }
    if (found == mCornerCount) {
        mCorners[mCornerCount++] = mCorner;
    }
    mValueCount++;
    mPrimitive[mPrimitiveAt++] = static_cast< u16 >(found);
    memset(&mCorner, 0, sizeof(mCorner));
    if (mPrimitiveAt != mPrimitiveCount) {
        return;
    }

    // The primitive as triangles, as the renderer splits GX's (a strip's odd triangles turned to keep its winding).
    const u32 count = mPrimitiveCount;
    const u8 type = mPrimitiveType;
    u32 triangles = 0;
    if (type == GX_TRIANGLES) {
        triangles = count / 3;
    } else if (type == GX_QUADS) {
        triangles = count / 4 * 2;
    } else if (count >= 3) {
        triangles = count - 2;
    }
    for (u32 t = 0; t < triangles; t++) {
        u32 v[3];
        if (type == GX_TRIANGLES) {
            v[0] = 3 * t;
            v[1] = 3 * t + 1;
            v[2] = 3 * t + 2;
        } else if (type == GX_QUADS) {
            const u32 q = 4 * (t / 2);
            v[0] = q;
            v[1] = q + 1 + (t & 1);
            v[2] = q + 2 + (t & 1);
        } else if (type == GX_TRIANGLESTRIP) {
            v[0] = (t & 1) != 0 ? t + 1 : t;
            v[1] = (t & 1) != 0 ? t : t + 1;
            v[2] = t + 2;
        } else {
            v[0] = 0;
            v[1] = t + 1;
            v[2] = t + 2;
        }
        for (u32 k = 0; k < 3; k++) {
            mIndices[mIndexCount++] = mPrimitive[v[k]];
        }
    }
}

void NativeMesh::send() {
    const NebulaHostServices* pHost = nebula_host();
    for (u32 slot = 0; slot < 12; slot++) {
        if ((mOwned & (1u << slot)) != 0) {
            NebulaShapeArray array;
            array.key = mArrays[slot];
            array.kind = kindOf(slot);
            array.count = mValueCount;
            array.values = mValues[slot];
            pHost->shape_array(pHost->user, &array);
            delete[] mValues[slot];
            mValues[slot] = nullptr;
        }
    }
    NebulaShapeGeometry geometry;
    memset(&geometry, 0, sizeof(geometry));
    geometry.key = static_cast< u64 >(reinterpret_cast< uintptr_t >(this));
    geometry.attributes = mAttributes | ((mOwned & (1u << SLOT_POSITION)) != 0 ? NEBULA_SHAPE_OWN_POSITIONS : 0);
    for (u32 slot = 0; slot < 12; slot++) {
        geometry.arrays[slot] = reads(mAttributes, slot) ? mArrays[slot] : 0;
    }
    geometry.corner_count = mCornerCount;
    geometry.index_count = mIndexCount;
    geometry.corners = mCorners;
    geometry.indices = mIndices;
    pHost->shape_geometry(pHost->user, &geometry);
    delete[] mCorners;
    delete[] mIndices;
    delete[] mPrimitive;
    mCorners = nullptr;
    mIndices = nullptr;
    mPrimitive = nullptr;
}

void NativeMesh::draw() const {
    J3DNativeShape::flushState();
    const NebulaHostServices* pHost = nebula_host();
    pHost->shape_draw(pHost->user, static_cast< u64 >(reinterpret_cast< uintptr_t >(this)), __NebulaGXWriter.ptr,
                      mArrays[SLOT_POSITION], mArrays[SLOT_NORMAL], mArrays[SLOT_COLOR0], nullptr, 0, nullptr, nullptr,
                      0);
}

void NativeMesh::sendArray(const void* pKey, u32 kind, u32 count, const void* pValues) {
    const NebulaHostServices* pHost = nebula_host();
    NebulaShapeArray array;
    array.key = static_cast< u64 >(reinterpret_cast< uintptr_t >(pKey));
    array.kind = kind;
    array.count = count;
    array.values = pValues;
    pHost->shape_array(pHost->user, &array);
}
