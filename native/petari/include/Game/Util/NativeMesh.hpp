#pragma once

#include <nebula_game_api.h>
#include <revolution.h>

// Geometry the game builds itself (outside its models) as typed records (nebula_game_api.h shape_array,
// shape_geometry, shape_draw): the values its corners give as the mesh's own arrays, arrays the game keeps (and
// rewrites) by key, its primitives as corners and triangles, and each draw a record in draw order.
class NativeMesh {
public:
    // Array slots (NebulaShapeGeometry::arrays): position, normal, colour 0 and 1, texture coordinates 0-7 from 4.
    enum {
        SLOT_POSITION = 0,
        SLOT_NORMAL = 1,
        SLOT_COLOR0 = 2,
        SLOT_COLOR1 = 3,
        SLOT_TEXCOORD0 = 4,
    };

    NativeMesh();

    // Room for `maxCorners` corners reading `attributes` (NEBULA_SHAPE_* bits; every corner has a position), from
    // the current heap.
    void init(u32 attributes, u32 maxCorners);
    // Slot `slot` reads the game's array named `pKey` (its values sent with sendArray) by the indices corners give.
    void useArray(u32 slot, const void* pKey);

    // A primitive as GXBegin starts it: `count` corners follow, each its values or indices then corner().
    void begin(GXPrimitive primitive, u16 count);
    void position(f32 x, f32 y, f32 z);
    void normal(f32 x, f32 y, f32 z);
    void color(u8 r, u8 g, u8 b, u8 a);
    void texCoord(u32 coord, f32 s, f32 t);
    void index(u32 slot, u16 entry);
    void corner();

    // The mesh's own arrays and its corners and triangles, as records (once its primitives are given).
    void send();
    // A draw of the mesh, after every command written before it.
    void draw() const;

    // The values of an array the game keeps (named by `pKey`): `count` entries of `kind` (NEBULA_SHAPE_ARRAY_*:
    // 3 floats for positions and normals, an R << 24 | G << 16 | B << 8 | A word for colours, 2 floats for texture
    // coordinates).
    static void sendArray(const void* pKey, u32 kind, u32 count, const void* pValues);

private:
    u64 key(u32 slot) const;

    u32 mAttributes;
    u32 mMaxCorners;
    u32 mOwned;  // slots whose values corners give (bit per slot)
    u64 mArrays[12];
    f32* mValues[12];
    u32 mValueCount;
    NebulaShapeCorner* mCorners;
    u32 mCornerCount;
    u16* mIndices;
    u32 mIndexCount;
    NebulaShapeCorner mCorner;
    u16* mPrimitive;
    u8 mPrimitiveType;
    u16 mPrimitiveCount;
    u16 mPrimitiveAt;
};
