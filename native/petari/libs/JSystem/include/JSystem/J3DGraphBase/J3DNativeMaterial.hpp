#pragma once

#include <revolution.h>

class J3DMaterial;
struct NebulaMaterial;

// A J3D material as the renderer takes it (nebula_game_api.h NebulaMaterial): every block's settings as values.
namespace J3DNativeMaterial {
    void build(J3DMaterial* pMaterial, NebulaMaterial* pOut);
};  // namespace J3DNativeMaterial
