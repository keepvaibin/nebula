#pragma once

#include <NebulaGpu.h>
#include <nebula_game_api.h>
#include <nebula_math.h>

class J3DMaterial;
class J3DModelData;
class J3DTexture;
class J3DMaterialSnapshot;
struct NebulaMaterialParts;
struct NebulaNativeMaterial;

// The parts a material loads (the others keep what was set before).
enum {
    J3D_LOADS_FOG = 1u << 0,
    J3D_LOADS_REGISTER_COLOR = 1u << 2,
    J3D_LOADS_KONST_COLOR = 1u << 3,
    J3D_LOADS_AMBIENT = 1u << 6,
    J3D_LOADS_LIGHTS = 1u << 7,
    J3D_LOADS_CHANNELS = 1u << 9,
    J3D_LOADS_INDIRECT = 1u << 10,
};

// What a material's draw takes besides its shader, which the cook chose (nebula_cooked_material.h).
struct J3DMaterialSetup {
    u8 tex_gens, cull, z_compare_location, fog_type;
    u16 loads;
    // 0: the material as its list sets it. Else an instance's differed list: only the parts these J3D flags name.
    u32 diff_flags;
    u8 tex_gen_post[8];  // each coordinate's post-transform row (61 the identity)
    struct { u8 comp0, op, comp1; } alpha_compare;
    struct { u8 type, source, destination, logic_op; } blend;  // NEBULA_BLEND_*, NEBULA_FACTOR_*
    struct { u8 enable, func, update; } z_mode;
    // Coordinate i's texture matrix, when the material has one: `id` its place (a post-transform one from 64), `rows`
    // 2 or 3, `listed` whether the material's own list loads it (a differed list loads every one).
    struct { u8 present, listed, id, rows; } texture_matrix[8];
};

// The material's values.
struct J3DMaterialValues {
    s16 register_color[4][4];
    u8 konst_color[4][4];
    u8 material_color[2][4], ambient_color[2][4];
    u8 light_mask;  // the lights below that are set
    u8 alpha_ref[2];
    struct { f32 m[2][3]; s8 scale; } ind_matrix[3];
    struct { u16 scale_s, scale_t; } tex_coord_scale[8];
    NebulaMaterialTexture texture[8];
    NebulaMaterialLight light[8];
    struct { f32 start, end, near_z, far_z; u8 color[4]; } fog;
    f32 texture_matrix[8][12];
};

// A J3D material as the renderer takes it: every
// block's settings as values.

namespace J3DNativeMaterial {
    // `pTexture`: the texture table the material's texture numbers index (its packet's; j3dSys's for shared lists).
    void build(J3DMaterial* pMaterial, J3DTexture* pTexture, NebulaMaterialParts* pOut);
    // The material the next shape draws use, a record after every command written before it.
    // A snapshot's storage, from the current heap (where a material list's buffers come from).
    NebulaMaterialParts* create();
    // The material as `pList` sets it now (made or patched), into its snapshot.
    // is new or changed. `diffFlags`: the J3D diff flags of a differed list (0 for a whole list).
    void capture(J3DMaterial* pMaterial, J3DTexture* pTexture, J3DMaterialSnapshot* pList, u32 diffFlags = 0);
    // The material `pList` sets (its snapshot; built now for a list from the model file, which has none).
    void send(J3DMaterial* pMaterial, J3DTexture* pTexture, J3DMaterialSnapshot* pList);
    // An instance's differed list (a shape packet's: the material with that instance's animated values), when it has
    // been made.
    void sendDiffered(J3DMaterialSnapshot* pList);
    // The values every material takes (the register file): each setter changes it, sendRegisters gives the current
    // material the new values.
    void setColorRegister(u32 index, NebulaColor8 color);
    void setColorRegisterWide(u32 index, const NebulaColor16& rColor);
    void setConstantColor(u32 index, NebulaColor8 color);
    void setLightMaterial(u32 channel, NebulaColor8 color);
    void setLightAmbient(u32 channel, NebulaColor8 color);
    void setIndirectMatrix(u32 index, const f32 offset[2][3], s8 scaleExp);
    void setFog(u32 type, f32 start, f32 end, f32 nearZ, f32 farZ, NebulaColor8 color);
    void setAlphaTest(u8 compare0, u8 reference0, u8 op, u8 compare1, u8 reference1);
    void sendRegisters();
    void applyPass(u8 pass);
    // The register file (j3d_material.hlsli MaterialConstants' first 84 floats: colour registers, constant colours, material and
    // ambient colours, indirect matrices, fog, alpha references) as the materials and state changes left it.
    const f32* registers();
    // A material another part of the game sends (NativeShader::send; NULL a drawing-state one: a registered program's
    // values): the draws after it no longer use the current J3D material.
    void watch(const NebulaNativeMaterial* pNative);
    // The native shader of the material the draws use now (0: the drawing state's program).
    // A material another system builds whole (nw4r's layout materials), its program and values in storage that lasts
    // until the next material: its generated native shader when the material set has its program (depth and culling
    // kept from the drawing state), else the program registered with the host (once) and drawn by the drawing state.
    void sendWhole(u32 shader, const J3DMaterialSetup& rSetup, const J3DMaterialValues& rValues);
    // The current J3D material with coordinate 0 its vertices' normals and texture 0 `pImage` (a `width` x `height`
    // RGB565 image, repeated, linear): J3DModelX list 6. False when the current material is not a native J3D one.
    // The current material with another image in a map (a character\'s swapped eyes or hands).
    void swapImage(u32 map, const void* pImage);
    bool overrideCoordinate(const void* pImage, u16 width, u16 height);
    // A pass over the current J3D material's draws that replaces its stages (j3d_material.hlsli j3d_override: `mode`
    // 1-4, alphas A0 and A1, drawn above `ref`, -1 always): whether the material draws natively and took it (else the
    // caller loads its program). Mode 0 ends it.
    bool override(u32 mode, f32 a0, f32 a1, f32 ref);
};  // namespace J3DNativeMaterial
