#pragma once

#include "Game/Render/TypedDraw.hpp"

class ModelData;
struct ResTIMG;

// A vertex of a fur mesh: the model's corner, its matrix, which way it grows and how far. `length` 0 sinks it under the
// skin instead. A corner on another material's border has no direction and stays.
struct VertexFur {
    f32 x, y, z;
    f32 nx, ny, nz;
    Rgba8 color;
    f32 s, t;
    u32 matrix;
    f32 dx, dy, dz;
    f32 length, unused0, unused1;
};

template <>
struct VertexLayout< VertexFur > {
    static constexpr u32 count = 6;
    static const NebulaLayoutInput* inputs() {
        static const NebulaLayoutInput list[count] = {
            NEBULA_INPUT3(NEBULA_INPUT_NORMAL, VertexFur, nx),
            NEBULA_INPUTC(NEBULA_INPUT_COLOR0, VertexFur, color),
            NEBULA_INPUT2(NEBULA_INPUT_COORD0, VertexFur, s),
            {NEBULA_INPUT_MATRIX, 0, 3, static_cast< u32 >(offsetof(VertexFur, matrix)), static_cast< u32 >(sizeof(VertexFur))},
            NEBULA_INPUT3(NEBULA_INPUT_BINORMAL, VertexFur, dx),
            NEBULA_INPUT3(NEBULA_INPUT_TANGENT, VertexFur, length),
        };
        return list;
    }
};

// The length texture: strands' lengths by coordinate, read when the mesh is made.
class FurLengthMap {
public:
    FurLengthMap(const ResTIMG* pTexture);

    f32 refer(f32 u, f32 v) const;

private:
    u32 getTexelOrder(u32 size, f32 coord, u8 wrap) const;

    const ResTIMG* mTexture;
    const u8* mTexels;
};

// Shape `shape` of `pData` as a fur mesh.
SiteMesh< VertexFur >* createFurMesh(ModelData* pData, u32 shape, const ResTIMG* pLength);
