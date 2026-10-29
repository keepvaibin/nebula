#pragma once

#include <revolution.h>

#ifdef __cplusplus
class J3DModelData;
class J3DShape;
class J3DShapeDraw;

// A J3D shape as the renderer draws it: its groups as typed geometry and its model's arrays as values
// (nebula_game_api.h shape_geometry, shape_array), each group's draw a record in the renderer's stream (shape_draw).
namespace J3DNativeShape {
    // Every shape of a model whose data is final (after the game's own edits of it).
    void registerModel(J3DModelData* pModelData);
    // A shape's groups and the arrays they read, once (J3DShape::prepareDraw).
    void registerShape(J3DShape* pShape);
    // Group `pDraw` drawn with the arrays j3dSys names.
    void draw(const J3DShapeDraw* pDraw);
    // Matrices the next draw reads, by GX's matrix id (nebula_game_api.h NebulaShapeMatrix): `modelMatrix` the model's
    // draw matrix it is (its index + 1), 0 for any other.
    void loadPosition(const f32 (*pMtx)[4], u32 id, u16 modelMatrix = 0);
    void loadNormal(const f32 (*pMtx)[4], u32 id, u16 modelMatrix = 0);
    void loadNormal33(const f32 (*pMtx)[3], u32 id, u16 modelMatrix = 0);
    void loadNormalToTexture(const f32 (*pMtx)[4], u32 id);
    void loadNormal33ToTexture(const f32 (*pMtx)[3], u32 id);
    // A texture matrix as GXLoadTexMtxImm takes it (`type` GX_MTX3x4 or GX_MTX2x4; ids from 64 post-transform).
    void loadTexture(const f32 (*pMtx)[4], u32 id, GXTexMtxType type);
    // The same, sent now (a record before every command written after it): matrices the game loads as it draws.
    void sendPosition(const f32 (*pMtx)[4], u32 id);
    void sendNormal(const f32 (*pMtx)[4], u32 id);
    void sendTexture(const f32 (*pMtx)[4], u32 id, GXTexMtxType type);
    // The matrices the next draw's corners take when they carry none.
    void setCurrent(u8 position, const u8* pTexture);
    // Texture coordinate `coord` of the next draw generated as given (GXSetTexCoordGen's values).
    void setTexGen(u8 coord, u8 type, u8 source, bool normalize, u8 postMatrix);
    // Matrices, current matrices and texgens set since the last draw, as a record now (before a material record,
    // which must not overtake them).
    void flushState();
    // A position array the game wrote (`count` entries in its model's position format) as values.
    void sendPositions(const void* pArray, u32 count, GXCompType type, GXCompCnt cnt, u8 frac);
};  // namespace J3DNativeShape

extern "C" {
#endif
// The matrix loads for the C sources (RVLFaceLib).
void J3DNativeShapeSendPosition(const f32 (*pMtx)[4], u32 id);
void J3DNativeShapeSendNormal(const f32 (*pMtx)[4], u32 id);
#ifdef __cplusplus
}
#endif
