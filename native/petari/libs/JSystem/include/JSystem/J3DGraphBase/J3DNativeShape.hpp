#pragma once

#include <revolution/gx.h>
#include <revolution/mtx.h>
#include <revolution/os.h>

#ifdef __cplusplus
struct NebulaDrawMatrix;
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
    // Geometry `key` the game registered itself drawn with the arrays named, its corners reading the position
    // matrices at `places` (bit k: place k; 0: the current place).
    void drawGeometry(u64 key, u64 position, u64 normal, u64 color, u32 places);
    // The matrices draws read, by their place in a draw (nebula_game_api.h NebulaDrawMatrix): position matrices 0-9
    // with their normal matrices (every draw carries the ones its corners read: setPosition and setNormal change what
    // the following draws carry), texture matrices 0-9 and the identity's place, post-transform texture matrices 0-19
    // and the identity's (each goes with the next draw or material record and stays until given again).
    // `modelMatrix`: the model's draw matrix it is (its index + 1), 0 for any other.
    enum { kPositions = 10, kIdentityTexture = 10, kIdentityPost = 20 };
    void setPosition(u32 index, const f32 (*pMtx)[4], u16 modelMatrix = 0);
    void setNormal(u32 index, const f32 (*pMtx)[4], u16 modelMatrix = 0);
    void setNormal33(u32 index, const f32 (*pMtx)[3], u16 modelMatrix = 0);
    // A texture matrix of `rows` rows (2 or 3) of four.
    void setTexture(u32 index, const f32 (*pMtx)[4], u32 rows);
    // A normal matrix as texture matrix `index` (its fourth column zero).
    void setNormalTexture(u32 index, const f32 (*pMtx)[4]);
    void setNormal33Texture(u32 index, const f32 (*pMtx)[3]);
    void setPostTexture(u32 index, const f32 (*pMtx)[4]);
    // The matrices the next draw's corners take when they carry none: the position matrix's place and texture
    // coordinates 0-7's texture matrix places (the model file's matrix ids, three apart, the identity's at 60).
    void setCurrent(u8 position, const u8* pTexture);
    // The position matrix corners without one take (J3DNativeState::setCurrentMtx).
    void setCurrentPosition(u32 index);
    // Texture coordinate `coord` of the next draw generated as given (GXSetTexCoordGen's values).
    void setTexGen(u8 coord, u8 type, u8 source, bool normalize, u8 postMatrix);
    // Texture matrices, current matrices and texgens set since the last draw, as a record now (before a material
    // record, which must not overtake them).
    void flushState();
    // An immediate's position and normal matrices: those at the places `mask` names (bit k: place k; 0: the current
    // place), in a buffer valid until the next record.
    u32 positionMatrices(u32 mask, const NebulaDrawMatrix** ppMatrices);
    // A position array the game wrote (`count` entries in its model's position format) as values.
    void sendPositions(const void* pArray, u32 count, GXCompType type, GXCompCnt cnt, u8 frac);
};  // namespace J3DNativeShape

extern "C" {
#endif
// The same for the C sources (RVLFaceLib).
void J3DNativeShapeSetPosition(u32 index, const f32 (*pMtx)[4]);
void J3DNativeShapeSetNormal(u32 index, const f32 (*pMtx)[4]);
#ifdef __cplusplus
}
#endif
