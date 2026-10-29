#pragma once

#include <revolution.h>

class J3DMaterial;
class J3DTexture;
class J3DDisplayListObj;
struct NebulaMaterial;

// A J3D material as the renderer takes it (nebula_game_api.h NebulaMaterial): every block's settings as values.
namespace J3DNativeMaterial {
    // `pTexture`: the texture table the material's texture numbers index (its packet's; j3dSys's for shared lists).
    void build(J3DMaterial* pMaterial, J3DTexture* pTexture, NebulaMaterial* pOut);
    // The material the next shape draws use, a record after every command written before it.
    // A snapshot's storage, from the current heap (where a material list's buffers come from).
    NebulaMaterial* create();
    // The material as `pList` sets it now (made or patched), into its snapshot.
    // `diffFlags`: the J3D diff flags of a differed list (0 for a whole list).
    void capture(J3DMaterial* pMaterial, J3DTexture* pTexture, J3DDisplayListObj* pList, u32 diffFlags = 0);
    // The material `pList` sets (its snapshot; built now for a list from the model file, which has none).
    void send(J3DMaterial* pMaterial, J3DTexture* pTexture, J3DDisplayListObj* pList);
    // An instance's differed list (a shape packet's: the material with that instance's animated values), when it has
    // been made.
    void sendDiffered(J3DDisplayListObj* pList);
};  // namespace J3DNativeMaterial
