#pragma once

#include <revolution/gx.h>
#include <revolution/mtx.h>
#include <revolution/os.h>

class J3DMaterial;
class J3DModelData;
class J3DTexture;
class J3DDisplayListObj;
struct NebulaMaterialParts;
struct NebulaNativeMaterial;
struct NebulaMaterialProgram;
struct NebulaMaterialValues;
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
    // The register file (j3d_material.hlsli MaterialConstants' first 84 floats: colour registers, constant colours, material and
    // ambient colours, indirect matrices, fog, alpha references) as the materials and state changes left it.
    const f32* registers();
    // A material another part of the game sends (NativeShader::send; NULL a drawing-state one: a registered program's
    // values): the draws after it no longer use the current J3D material.
    void watch(const NebulaNativeMaterial* pNative);
    // Tests (GALAXY_DUMP_DRAW_INPUTS): tags the next draws when `shader` is the asked native shader.
    void dumpTag(u32 shader);
    // A material another system builds whole (nw4r's layout materials), its program and values in storage that lasts
    // until the next material: its generated native shader when the material set has its program (depth and culling
    // kept from the drawing state), else the program registered with the host (once) and drawn by the drawing state.
    void sendWhole(const NebulaMaterialProgram& rProgram, const NebulaMaterialValues& rValues);
    // A pass over the current J3D material's draws that replaces its stages (j3d_material.hlsli j3d_override: `mode`
    // 1-4, alphas A0 and A1, drawn above `ref`, -1 always): whether the material draws natively and took it (else the
    // caller loads its GX program). Mode 0 ends it.
    bool override(u32 mode, f32 a0, f32 a1, f32 ref);
};  // namespace J3DNativeMaterial
