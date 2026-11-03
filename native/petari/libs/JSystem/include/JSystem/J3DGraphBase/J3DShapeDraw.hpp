#pragma once

#include <NebulaGpu.h>
#include <nebula_game_api.h>
#include <nebula_math.h>

// A shape group's mesh as the cook made it: its distinct corners (the arrays' entries they read) and its triangles.
struct J3DShapeMesh {
    NebulaShapeCorner* corners() {
        return reinterpret_cast< NebulaShapeCorner* >(this + 1);
    }
    const u16* indices() {
        return reinterpret_cast< const u16* >(corners() + mCornerCount);
    }

    u32 mAttributes;  // NEBULA_SHAPE_*
    u32 mCornerCount;
    u32 mIndexCount;
    u32 mPlaces;  // the position matrix places its corners read (bit k: place k; 0: none of their own)
};

class J3DShapeDraw {
public:
    explicit J3DShapeDraw(J3DShapeMesh* pMesh);
    void draw() const;

    virtual ~J3DShapeDraw();

    J3DShapeMesh* getMesh() const {
        return mMesh;
    }

private:
    J3DShapeMesh* mMesh;
};
