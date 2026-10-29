#pragma once

#include <revolution.h>

class J3DMaterial;
class J3DTexture;
struct NebulaMaterial;

// A J3D material as the renderer takes it (nebula_game_api.h NebulaMaterial): every block's settings as values.
namespace J3DNativeMaterial {
    // `pTexture`: the texture table the material's texture numbers index (its packet's; j3dSys's for shared lists).
    void build(J3DMaterial* pMaterial, J3DTexture* pTexture, NebulaMaterial* pOut);
    // The material the next shape draws use, a record after every command written before it.
    void send(J3DMaterial* pMaterial, J3DTexture* pTexture);
};  // namespace J3DNativeMaterial
