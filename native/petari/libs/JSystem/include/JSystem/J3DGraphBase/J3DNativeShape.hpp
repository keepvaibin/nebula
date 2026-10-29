#pragma once

#include <revolution.h>

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
    // A position array the game wrote (`count` entries in its model's position format) as values.
    void sendPositions(const void* pArray, u32 count, GXCompType type, GXCompCnt cnt, u8 frac);
};  // namespace J3DNativeShape
