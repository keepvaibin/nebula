#pragma once

#include <nebula_game_api.h>
#include <revolution.h>

#ifdef __cplusplus
// Vertices the game draws as it sends them, as typed values (nebula_game_api.h immediate): a primitive (GXBegin's)
// and each vertex's values, one record per primitive after every command written before it.
namespace J3DNativeDraw {
    // A primitive of `count` vertices, each giving the values `attributes` names (NEBULA_SHAPE_POSITION_MATRIX,
    // _NORMAL, _COLOR0, _COLOR1, _TEXCOORD0 << n; every vertex has a position) in GX's order: position matrix,
    // position, normal, colour 0, colour 1, texture coordinates. The record goes when the last vertex is complete.
    void begin(GXPrimitive primitive, u16 count, u32 attributes);
    void matrix(u8 id);
    void position(f32 x, f32 y, f32 z);
    void normal(f32 x, f32 y, f32 z);
    // R << 24 | G << 16 | B << 8 | A.
    void color(u32 rgba);
    void color(u8 r, u8 g, u8 b, u8 a);
    void texCoord(f32 s, f32 t);
};  // namespace J3DNativeDraw

extern "C" {
#endif
// The same for the C sources (RVLFaceLib).
void J3DNativeDrawBegin(GXPrimitive primitive, u16 count, u32 attributes);
void J3DNativeDrawPosition(f32 x, f32 y, f32 z);
void J3DNativeDrawNormal(f32 x, f32 y, f32 z);
void J3DNativeDrawColor(u32 rgba);
void J3DNativeDrawTexCoord(f32 s, f32 t);
#ifdef __cplusplus
}
#endif
