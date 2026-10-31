#pragma once

#include <revolution/gx.h>
#include <revolution/mtx.h>
#include <revolution/os.h>

class J3DMaterial;
class J3DModelData;
class J3DTexture;
class J3DDisplayListObj;
struct NebulaMaterialParts;
struct NebulaStateOp;

// A J3D material as the renderer takes it (nebula_game_api.h NebulaMaterialProgram and NebulaMaterialValues): every
// block's settings as values.
namespace J3DNativeMaterial {
    // `pTexture`: the texture table the material's texture numbers index (its packet's; j3dSys's for shared lists).
    void build(J3DMaterial* pMaterial, J3DTexture* pTexture, NebulaMaterialParts* pOut);
    // The material the next shape draws use, a record after every command written before it.
    // A snapshot's storage, from the current heap (where a material list's buffers come from).
    NebulaMaterialParts* create();
    // The material as `pList` sets it now (made or patched), into its snapshot; its program goes to the host when it
    // is new or changed. `diffFlags`: the J3D diff flags of a differed list (0 for a whole list).
    void capture(J3DMaterial* pMaterial, J3DTexture* pTexture, J3DDisplayListObj* pList, u32 diffFlags = 0);
    // The material `pList` sets (its snapshot; built now for a list from the model file, which has none).
    void send(J3DMaterial* pMaterial, J3DTexture* pTexture, J3DDisplayListObj* pList);
    // An instance's differed list (a shape packet's: the material with that instance's animated values), when it has
    // been made.
    void sendDiffered(J3DDisplayListObj* pList);
    // Tests (NEBULA_OPTION_TEST_MATERIAL_SWEEP): every material of a model just loaded built, and the programs no
    // native shader is made for registered with the host (whose GALAXY_DUMP_MATERIAL_PROGRAMS file collects them).
    void sweep(J3DModelData* pModelData);
    // State changes the game makes (J3DStateList::send), before they go to the host: the register file the J3D
    // materials' constants take, a value change folded into the current native J3D material, and a change of what
    // its shader computes handing its draws back to the drawing state.
    void state(const NebulaStateOp* pOps, u32 count);
};  // namespace J3DNativeMaterial
