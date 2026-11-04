#pragma once

#include <nebula_game_api.h>
#include <NebulaGpu.h>
#include <nebula_math.h>

#ifdef __cplusplus
// Vertices the game draws as it sends them, as typed values (nebula_game_api.h immediate): a primitive (u8's)
// and each vertex's values, one record per primitive after every command written before it.
namespace J3DNativeDraw {
    // Quads wait here while nothing changes between them, and go as one draw: anything that sends a change or another
    // draw calls this first.
    void flush();
    // `count` vertices of `primitive` (NebulaPrimitive) from arrays the caller keeps until it returns; `attributes`
    // NEBULA_SHAPE_*: colours and coordinates 0-7 it gives. Without position matrices they take the current one.
    void drawArrays(u8 primitive, u32 attributes, u32 count, const f32* pPositions, const u32* const pColors[2],
                    const f32* const pTexCoords[8], const f32* pNormals = nullptr);
    // `count` whole corners of quads or a strip, which the host takes as they are.
    void drawCorners(u8 primitive, u32 count, const NebulaParticleCorner* pCorners);
    // A primitive of `count` vertices, each giving the values `attributes` names (NEBULA_SHAPE_POSITION_MATRIX,
    // _NORMAL, _COLOR0, _COLOR1, _TEXCOORD0 << n; every vertex has a position) in order: position matrix,
    // position, normal, colour 0, colour 1, texture coordinates. The record goes when the last vertex is complete.
    void begin(NebulaPrimitive primitive, u16 count, u32 attributes);
    // The vertex's position matrix place (0-9, J3DNativeShape::setPosition).
    void position(f32 x, f32 y, f32 z);
    void normal(f32 x, f32 y, f32 z);
    // R << 24 | G << 16 | B << 8 | A.
    void color(u32 rgba);
    void color(u8 r, u8 g, u8 b, u8 a);
    void texCoord(f32 s, f32 t);
};  // namespace J3DNativeDraw

extern "C" {
#endif
// The same for the C sources (MiiLib).
void J3DNativeDrawBegin(NebulaPrimitive primitive, u16 count, u32 attributes);
void J3DNativeDrawPosition(f32 x, f32 y, f32 z);
void J3DNativeDrawNormal(f32 x, f32 y, f32 z);
void J3DNativeDrawColor(u32 rgba);
void J3DNativeDrawTexCoord(f32 s, f32 t);
#ifdef __cplusplus
}
#endif
