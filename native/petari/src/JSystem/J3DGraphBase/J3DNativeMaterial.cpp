#include <JSystem/J3DGraphBase/J3DNativeChannel.hpp>
#include <JSystem/J3DGraphBase/J3DNativeTexGen.hpp>
#include "JSystem/J3DGraphBase/J3DNativeMaterial.hpp"
#include "JSystem/J3DGraphBase/J3DMatBlock.hpp"
#include "JSystem/J3DGraphBase/J3DMaterial.hpp"
#include "JSystem/J3DGraphAnimator/J3DModelData.hpp"
#include "JSystem/J3DGraphBase/J3DNativeShape.hpp"
#include "JSystem/J3DGraphBase/J3DPacket.hpp"
#include "JSystem/J3DGraphBase/J3DSys.hpp"
#include "JSystem/J3DGraphBase/J3DTexture.hpp"
#include "JSystem/JKernel/JKRHeap.hpp"
#include <nebula_game_api.h>
#include "Game/Util/RenderSignals.hpp"
#include <string.h>

extern "C" const NebulaHostServices* nebula_host(void);
extern "C" void nebula_log(const char* format, ...);

// A display list's material: its program and its values (nebula_game_api.h).
struct NebulaMaterialParts {
    NebulaMaterialProgram program;
    NebulaMaterialValues values;
    u32 nativeShader;  // its J3D material's NebulaShaderId (0: the GX path)
};

namespace {
    // The J3D material set (tools/dev/gen_j3d_materials.py): a program's FNV-1a hash -> its material (1-based).
    struct J3DShaderEntry {
        u64 hash;
        u32 material;
    };
    const J3DShaderEntry sJ3DShaders[] = {
#include "J3DMaterialShaders.inc"
        {0xFFFFFFFFFFFFFFFFull, 0u},
    };

    u32 nativeShaderOf(const NebulaMaterialProgram& rProgram) {
        // The program's shader-relevant bytes (tools/dev/gen_j3d_materials.py LOOKUP_ZERO): without its culling,
        // dither, early depth, origin, differ flags, fog range adjustment, blend, depth or texture matrix places.
        NebulaMaterialProgram key = rProgram;
        key.cull = key.dither = key.z_compare_location = key.origin = 0;
        key.diff_flags = 0;
        u8* pFog = reinterpret_cast< u8* >(&key.fog);
        memset(pFog + 1, 0, sizeof(key.fog) - 1);
        memset(&key.blend, 0, sizeof(key.blend));
        memset(&key.z_mode, 0, sizeof(key.z_mode));
        memset(key.texture_matrix, 0, sizeof(key.texture_matrix));
        const u8* pBytes = reinterpret_cast< const u8* >(&key);
        u64 hash = 0xCBF29CE484222325ull;
        for (u32 i = 0; i < sizeof(key); i++) {
            hash = (hash ^ pBytes[i]) * 0x100000001B3ull;
        }
        u32 lo = 0;
        u32 hi = sizeof(sJ3DShaders) / sizeof(sJ3DShaders[0]) - 1;
        while (lo < hi) {
            const u32 mid = (lo + hi) / 2;
            if (sJ3DShaders[mid].hash < hash) {
                lo = mid + 1;
            } else {
                hi = mid;
            }
        }
        if (sJ3DShaders[lo].hash != hash) {
            return 0;
        }
        // Materials whose native draws still differ after the A/B input dump (E04-NATIVE-SHADERS-PLAN.md exclusion
        // list, each with its dump): drawn by their GX programs until fixed, by program hash (the set's numbers move
        // as it grows). The list is empty before step 4.
        static const u64 kExcluded[] = {
            0,  // none (the penguin coach, 586, was fixed by review-586 1dfec1f5)
        };
        for (u32 i = 0; i < sizeof(kExcluded) / sizeof(kExcluded[0]); i++) {
            if (kExcluded[i] != 0 && hash == kExcluded[i]) {
                return 0;
            }
        }
        return NEBULA_SHADER_J3D_FIRST + sJ3DShaders[lo].material - 1;
    }

    // The register file as the J3D materials' constants lay it out (j3d_material.hlsli MaterialConstants): what the
    // J3D materials and the game's state changes set last. A native J3D material takes the parts its program does not
    // load from here (the TEV register PREV always: J3D loads registers 1-3), as GX kept them.
    f32 sRegisters[84];

    // The native material the draws use now (J3D: sCurrent, its program and values; or a site's own), and the state
    // changes made since it was sent (replayed should the GX path take over its draws).
    enum CurrentKind {
        CURRENT_NONE,
        CURRENT_J3D,
        CURRENT_SITE,
    };
    CurrentKind sCurrentKind = CURRENT_NONE;
    // A site's material whose constants begin with the register file (constants_only bit 1): its copy, resent with
    // the register file's values when the game changes one.
    NebulaNativeMaterial sSiteRegisters;
    bool sSiteFollows = false;
    NebulaNativeMaterial sCurrent;
    const NebulaMaterialProgram* sCurrentProgram = NULL;

    void setColor(f32* pOut, const u8* pColor, u32 count) {
        for (u32 c = 0; c < count; c++) {
            pOut[c] = pColor[c] / 255.0f;
        }
    }

    // The parts `rProgram` loads (NEBULA_MATERIAL_LOADS_*), into the register file. An instance's differed list
    // (diff_flags) loads only the parts its flags name, as J3D's list does and the drawing state takes it
    // (draw_state.cpp set_material): TEV and konst colours under TevReg, material colours under MatColor, fog under
    // Fog; never the ambient colours, indirect matrices or alpha references, which stay as the shared list and the
    // state changes since left them (an actor's light loader sets the ambient colour between the two lists).
    void loadRegisters(const NebulaMaterialProgram& rProgram, const NebulaMaterialValues& rValues) {
        f32* pC = sRegisters;
        const u32 diff = rProgram.diff_flags;
        const bool whole = diff == 0;
        const u16 loads = rProgram.loads;
        if ((whole || (diff & J3DDiffFlag_TevReg) != 0) && (loads & NEBULA_MATERIAL_LOADS_TEV_COLOR) != 0) {
            for (u32 r = 0; r < 3; r++) {
                for (u32 c = 0; c < 4; c++) {
                    pC[4 + r * 4 + c] = rValues.tev_color[r][c] / 255.0f;
                }
            }
        }
        if ((whole || (diff & J3DDiffFlag_TevReg) != 0) && (loads & NEBULA_MATERIAL_LOADS_KONST_COLOR) != 0) {
            for (u32 k = 0; k < 4; k++) {
                setColor(pC + 16 + k * 4, rValues.konst_color[k], 4);
            }
        }
        for (u32 ch = 0; ch < 2; ch++) {
            if ((whole || (diff & J3DDiffFlag_MatColor) != 0) && (loads & NEBULA_MATERIAL_LOADS_CHANNELS) != 0) {
                setColor(pC + 32 + ch * 4, rValues.material_color[ch], 4);
            }
            if (whole && (loads & NEBULA_MATERIAL_LOADS_AMBIENT) != 0) {
                setColor(pC + 40 + ch * 4, rValues.ambient_color[ch], 4);
            }
        }
        if (whole && (loads & NEBULA_MATERIAL_LOADS_INDIRECT) != 0) {
            for (u32 m = 0; m < 3; m++) {
                f32 scale = 1.0f;
                for (s32 e = rValues.ind_matrix[m].scale; e > 0; e--) {
                    scale *= 2.0f;
                }
                for (s32 e = rValues.ind_matrix[m].scale; e < 0; e++) {
                    scale *= 0.5f;
                }
                for (u32 row = 0; row < 2; row++) {
                    for (u32 c = 0; c < 3; c++) {
                        pC[48 + (m * 2 + row) * 4 + c] = rValues.ind_matrix[m].m[row][c] * scale;
                    }
                }
            }
        }
        if ((whole || (diff & J3DDiffFlag_Fog) != 0) && (loads & NEBULA_MATERIAL_LOADS_FOG) != 0) {
            pC[72] = rValues.fog.start;
            pC[73] = rValues.fog.end;
            pC[74] = rValues.fog.near_z;
            pC[75] = rValues.fog.far_z;
            setColor(pC + 76, rValues.fog.color, 3);
            pC[79] = rProgram.fog.type;
        }
        if (whole) {
            pC[80] = rValues.alpha_ref[0] / 255.0f;
            pC[81] = rValues.alpha_ref[1] / 255.0f;
        }
    }

    // A state change of a register the J3D constants hold, into the register file: whether it was one.
    bool setRegister(const NebulaStateOp& rOp) {
        f32* pC = sRegisters;
        const u8* a = rOp.a;
        switch (rOp.kind) {
        case NEBULA_STATE_TEV_COLOR:
            setColor(pC + (a[0] & 3) * 4, a + 1, 4);
            return true;
        case NEBULA_STATE_TEV_COLOR_S10: {
            f32* pReg = pC + (a[0] & 3) * 4;
            pReg[0] = static_cast< s16 >(rOp.size[0]) / 255.0f;
            pReg[1] = static_cast< s16 >(rOp.size[1]) / 255.0f;
            pReg[2] = rOp.f[0] / 255.0f;
            pReg[3] = rOp.f[1] / 255.0f;
            return true;
        }
        case NEBULA_STATE_TEV_KCOLOR:
            setColor(pC + 16 + (a[0] & 3) * 4, a + 1, 4);
            return true;
        case NEBULA_STATE_MATERIAL_COLOR:
        case NEBULA_STATE_AMBIENT_COLOR: {
            // The channel's whole colour.
            f32* pColor = pC + (rOp.kind == NEBULA_STATE_MATERIAL_COLOR ? 32 : 40) + (a[0] & 1) * 4;
            for (u32 c = 0; c < 4; c++) {
                pColor[c] = a[1 + c] / 255.0f;
            }
            return true;
        }
        case NEBULA_STATE_FOG:
            pC[72] = rOp.f[0];
            pC[73] = rOp.f[1];
            pC[74] = rOp.f[2];
            pC[75] = rOp.f[3];
            setColor(pC + 76, a + 1, 3);
            pC[79] = a[0];  // the fog type (j3d_material.hlsli j3d_fog): a value every J3D shader takes
            return true;
        case NEBULA_STATE_ALPHA_COMPARE:
            pC[80] = a[1] / 255.0f;
            pC[81] = a[4] / 255.0f;
            return true;
        case NEBULA_STATE_IND_TEX_MATRIX: {
            // GXSetIndTexMtx: the matrix (or a dynamic S or T matrix in its place), its terms by the scale exponent.
            const u32 id = a[0] >= 9 ? a[0] - 9 : a[0] >= 5 ? a[0] - 5 : a[0] >= 1 ? a[0] - 1 : 3;
            if (id >= 3) {
                return false;
            }
            f32 scale = 1.0f;
            for (s32 e = static_cast< s8 >(a[1]); e > 0; e--) {
                scale *= 2.0f;
            }
            for (s32 e = static_cast< s8 >(a[1]); e < 0; e++) {
                scale *= 0.5f;
            }
            for (u32 w = 0; w < 3; w++) {
                for (u32 row = 0; row < 2; row++) {
                    pC[48 + (id * 2 + row) * 4 + w] = rOp.f[2 * w + row] * scale;
                }
            }
            return true;
        }
        default:
            return false;
        }
    }

    // Whether a state change alters what the current native material's shader computes (its stages, channels,
    // coordinates, fog type or alpha test) rather than a value it takes or its fixed-function state.
    bool changesProgram(const NebulaStateOp& rOp) {
        switch (rOp.kind) {
        case NEBULA_STATE_GEN_MODE:
        case NEBULA_STATE_GEN_MODE2:
        case NEBULA_STATE_NUM_TEX_GENS:
        case NEBULA_STATE_NUM_CHANS:
        case NEBULA_STATE_NUM_TEV_STAGES:
        case NEBULA_STATE_NUM_IND_STAGES:
        case NEBULA_STATE_CHANNEL:
        case NEBULA_STATE_TEV_COLOR_CALC:
        case NEBULA_STATE_TEV_ALPHA_CALC:
        case NEBULA_STATE_TEV_DIRECT:
        case NEBULA_STATE_TEV_INDIRECT:
        case NEBULA_STATE_TEV_ORDER:
        case NEBULA_STATE_TEV_COLOR_IN:
        case NEBULA_STATE_TEV_ALPHA_IN:
        case NEBULA_STATE_TEV_COLOR_OP:
        case NEBULA_STATE_TEV_ALPHA_OP:
        case NEBULA_STATE_TEV_KCOLOR_SEL:
        case NEBULA_STATE_TEV_KALPHA_SEL:
        case NEBULA_STATE_TEV_SWAP_MODE:
        case NEBULA_STATE_TEV_SWAP_TABLE:
        case NEBULA_STATE_TEV_STAGE_ORDER:
        case NEBULA_STATE_TEX_COORD_GEN:
        case NEBULA_STATE_IND_TEX_SCALE:
        case NEBULA_STATE_IND_TEX_ORDER:
        case NEBULA_STATE_Z_TEXTURE:
            return true;
        case NEBULA_STATE_ALPHA_COMPARE:
            return sCurrentKind == CURRENT_J3D &&
                   (rOp.a[0] != sCurrentProgram->alpha_compare.comp0 || rOp.a[2] != sCurrentProgram->alpha_compare.op ||
                    rOp.a[3] != sCurrentProgram->alpha_compare.comp1);
        default:
            return false;
        }
    }

    // A program-changing state op applied to a J3D material's program (the registers as GX's change leaves them): a
    // state list over a material (J3DModelX's lists, Mario's dynamic lists) makes another program, drawn by its own
    // generated shader. False for a change the program record cannot hold.
    bool foldOp(NebulaMaterialProgram& rP, const NebulaStateOp& rOp) {
        const u8* a = rOp.a;
        switch (rOp.kind) {
        case NEBULA_STATE_GEN_MODE:
            rP.tex_gens = a[0];
            rP.channels = a[1];
            rP.tev_stages = a[2];
            return true;
        case NEBULA_STATE_GEN_MODE2:
            rP.tex_gens = a[0];
            rP.channels = a[1];
            rP.tev_stages = a[2];
            rP.ind_stages = a[3];
            rP.cull = a[4];
            return true;
        case NEBULA_STATE_NUM_TEX_GENS:
            rP.tex_gens = a[0];
            return true;
        case NEBULA_STATE_NUM_CHANS:
            rP.channels = a[0];
            return true;
        case NEBULA_STATE_NUM_TEV_STAGES:
            rP.tev_stages = a[0];
            return true;
        case NEBULA_STATE_NUM_IND_STAGES:
            rP.ind_stages = a[0];
            return true;
        case NEBULA_STATE_CHANNEL: {
            // The record's order is COLOR0, ALPHA0, COLOR1, ALPHA1: the channel's colour, then its alpha.
            if ((a[7] & 3) == 0) {
                return false;
            }
            const s8 place = static_cast< s8 >(2 * (a[0] & 1));
            const s8 places[2] = {(a[7] & 1) != 0 ? place : static_cast< s8 >(-1),
                                  (a[7] & 2) != 0 ? static_cast< s8 >(place + 1) : static_cast< s8 >(-1)};
            for (u32 k = 0; k < 2; k++) {
                if (places[k] < 0) {
                    continue;
                }
                rP.channel[places[k]].enable = a[1];
                rP.channel[places[k]].ambient_source = a[2];
                rP.channel[places[k]].material_source = a[3];
                rP.channel[places[k]].light_mask = a[4];
                rP.channel[places[k]].diffuse = a[5];
                rP.channel[places[k]].attenuation = a[6];
            }
            return true;
        }
        case NEBULA_STATE_TEV_COLOR_CALC:
        case NEBULA_STATE_TEV_ALPHA_CALC: {
            if (a[0] >= 16) {
                return false;
            }
            NebulaMaterialTevStage& rS = rP.tev[a[0]];
            const bool color = rOp.kind == NEBULA_STATE_TEV_COLOR_CALC;
            for (u32 k = 0; k < 4; k++) {
                (color ? rS.color_in : rS.alpha_in)[k] = a[1 + k];
            }
            (color ? rS.color_op : rS.alpha_op) = a[5];
            (color ? rS.color_bias : rS.alpha_bias) = a[6];
            (color ? rS.color_scale : rS.alpha_scale) = a[7];
            (color ? rS.color_clamp : rS.alpha_clamp) = a[8];
            (color ? rS.color_reg : rS.alpha_reg) = a[9];
            if (!color) {
                rS.ras_swap = a[10];
                rS.tex_swap = a[11];
            }
            return true;
        }
        case NEBULA_STATE_TEV_COLOR_IN:
        case NEBULA_STATE_TEV_ALPHA_IN:
            if (a[0] >= 16) {
                return false;
            }
            for (u32 k = 0; k < 4; k++) {
                (rOp.kind == NEBULA_STATE_TEV_COLOR_IN ? rP.tev[a[0]].color_in : rP.tev[a[0]].alpha_in)[k] = a[1 + k];
            }
            return true;
        case NEBULA_STATE_TEV_COLOR_OP:
        case NEBULA_STATE_TEV_ALPHA_OP: {
            if (a[0] >= 16) {
                return false;
            }
            NebulaMaterialTevStage& rS = rP.tev[a[0]];
            const bool color = rOp.kind == NEBULA_STATE_TEV_COLOR_OP;
            (color ? rS.color_op : rS.alpha_op) = a[1];
            (color ? rS.color_bias : rS.alpha_bias) = a[2];
            (color ? rS.color_scale : rS.alpha_scale) = a[3];
            (color ? rS.color_clamp : rS.alpha_clamp) = a[4];
            (color ? rS.color_reg : rS.alpha_reg) = a[5];
            return true;
        }
        case NEBULA_STATE_TEV_DIRECT:
        case NEBULA_STATE_TEV_INDIRECT: {
            if (a[0] >= 16) {
                return false;
            }
            NebulaMaterialTevStage& rS = rP.tev[a[0]];
            const bool ind = rOp.kind == NEBULA_STATE_TEV_INDIRECT;
            rS.ind_stage = ind ? a[1] : 0;
            rS.ind_format = ind ? a[2] : 0;
            rS.ind_bias = ind ? a[3] : 0;
            rS.ind_matrix = ind ? a[4] : 0;
            rS.ind_wrap_s = ind ? a[5] : 0;
            rS.ind_wrap_t = ind ? a[6] : 0;
            rS.ind_prev = ind ? a[7] : 0;
            rS.ind_lod = ind ? a[8] : 0;
            rS.ind_alpha = ind ? a[9] : 0;
            return true;
        }
        case NEBULA_STATE_TEV_ORDER:
            if (a[0] >= 15) {
                return false;
            }
            for (u32 k = 0; k < 2; k++) {
                rP.tev[a[0] + k].tex_coord = a[1 + k * 3];
                rP.tev[a[0] + k].tex_map = a[2 + k * 3];
                rP.tev[a[0] + k].channel = a[3 + k * 3];
            }
            return true;
        case NEBULA_STATE_TEV_STAGE_ORDER:
            if (a[0] >= 16) {
                return false;
            }
            rP.tev[a[0]].tex_coord = a[1];
            rP.tev[a[0]].tex_map = a[4] != 0 ? GX_TEXMAP_NULL : a[2];
            rP.tev[a[0]].channel = a[3];
            return true;
        case NEBULA_STATE_TEV_KCOLOR_SEL:
        case NEBULA_STATE_TEV_KALPHA_SEL:
            if (a[0] >= 16) {
                return false;
            }
            (rOp.kind == NEBULA_STATE_TEV_KCOLOR_SEL ? rP.tev[a[0]].konst_color : rP.tev[a[0]].konst_alpha) = a[1];
            return true;
        case NEBULA_STATE_TEV_SWAP_MODE:
            if (a[0] >= 16) {
                return false;
            }
            rP.tev[a[0]].ras_swap = a[1];
            rP.tev[a[0]].tex_swap = a[2];
            return true;
        case NEBULA_STATE_TEV_SWAP_TABLE:
            if (a[0] >= 4) {
                return false;
            }
            for (u32 k = 0; k < 4; k++) {
                rP.swap_table[a[0]][k] = a[1 + k];
            }
            return true;
        case NEBULA_STATE_TEX_COORD_GEN:
            if (a[0] >= 8) {
                return false;
            }
            // The op carries the generation in the record's encoding (J3DNativeTexGen) and the post row.
            rP.tex_gen[a[0]].type = a[1];
            rP.tex_gen[a[0]].source = a[2];
            rP.tex_gen[a[0]].normalize = a[3];
            rP.tex_gen[a[0]].post_matrix = a[4];
            rP.tex_gen[a[0]].reserved[0] = 1;
            if (a[5] != 0) {
                rP.tex_gen[a[0]].matrix = static_cast< u8 >(3 * a[6]);  // the op's slot as the record's row
            }
            return true;
        case NEBULA_STATE_IND_TEX_SCALE:
            if (a[0] >= 4) {
                return false;
            }
            rP.ind_order[a[0]].scale_s = a[1];
            rP.ind_order[a[0]].scale_t = a[2];
            return true;
        case NEBULA_STATE_IND_TEX_ORDER:
            if (a[0] >= 4) {
                return false;
            }
            rP.ind_order[a[0]].coord = a[1];
            rP.ind_order[a[0]].map = a[2];
            return true;
        case NEBULA_STATE_ALPHA_COMPARE:
            rP.alpha_compare.comp0 = a[0];
            rP.alpha_compare.op = a[2];
            rP.alpha_compare.comp1 = a[3];
            return true;
        default:
            return false;
        }
    }

    // The program a state list folded into the current J3D material (state), and whether one change could not fold.
    NebulaMaterialProgram sFold;
    bool sFolding = false;
    bool sFoldFailed = false;
    bool sFoldCull = false;  // the list set the culling (GXSetGenMode2's)

    // The current material ends: the draws that follow have none (the host draws nothing without one) until the next.
    void endCurrent() {
        static NebulaNativeMaterial sNone;
        const NebulaHostServices* pHost = nebula_host();
        pHost->native_material(pHost->user, &sNone);
        sCurrentKind = CURRENT_NONE;
    }


    // Each material the game sends (NativeShader::send, this file's sends; NULL a drawing-state one).
    // The shader of the material watchMaterial last saw (J3DNativeMaterial::currentShader).
    u32 sWatchedShader = 0;

    void watchMaterial(const NebulaNativeMaterial* pNative) {
        sWatchedShader = pNative != NULL ? pNative->shader : 0;
        sCurrentKind = pNative == NULL || pNative->shader == NEBULA_SHADER_NONE ? CURRENT_NONE
                       : pNative == &sCurrent                                   ? CURRENT_J3D
                                                                                : CURRENT_SITE;
        sSiteFollows = sCurrentKind == CURRENT_SITE && (pNative->constants_only & 2) != 0;
        if (sSiteFollows) {
            sSiteRegisters = *pNative;
        }
    }

    // Tests (GALAXY_DUMP_DRAW_INPUTS): tags the draws of the material whose native shader is the asked one (65535:
    // every J3D material), on either path, for the renderer's input dump.
    void dumpTag(u32 shader) {
        static s32 sWanted = -1;
        if (sWanted < 0) {
            const NebulaHostServices* pHost = nebula_host();
            sWanted = static_cast< s32 >(pHost->option(pHost->user, NEBULA_OPTION_TEST_DUMP_DRAW_INPUTS));
        }
        if (sWanted != 0 &&
            (shader == static_cast< u32 >(sWanted) || (sWanted == 65535 && shader >= NEBULA_SHADER_J3D_FIRST))) {
            NebulaRender::dumpTag(shader);
        }
    }

    // Whether J3D materials draw as their native shaders (tests can limit them to a range).
    bool nativeDrawn(u32 shader) {
        const NebulaHostServices* pHost = nebula_host();
        // Tests: only a range of the J3D materials (GALAXY_TEST_NATIVE_J3D).
        static const u32 sRange = pHost->option(pHost->user, NEBULA_OPTION_TEST_NATIVE_J3D);
        const u32 number = shader - NEBULA_SHADER_J3D_FIRST + 1;
        return sRange == 0 || (number >= (sRange >> 16) && number <= (sRange & 0xFFFF));
    }

    u8 nativeFactor(u8 gx, bool source) {
        // GX's colour factors name the other side's colour (a source factor 2 is the destination's colour).
        if (gx == 2 || gx == 3) {
            return source ? static_cast< u8 >(gx == 2 ? NEBULA_FACTOR_DST_COLOR : NEBULA_FACTOR_INV_DST_COLOR)
                          : static_cast< u8 >(gx == 2 ? NEBULA_FACTOR_SRC_COLOR : NEBULA_FACTOR_INV_SRC_COLOR);
        }
        return gx;
    }

    // A material with no shader in the set (its program registered: a material dump collects it for the generator):
    // not drawn. Each once in the log.
    void notDrawn(u32 shader, u64 key) {
        static u64 sLogged[256];
        static u32 sLoggedCount = 0;
        bool seen = false;
        for (u32 k = 0; k < sLoggedCount; k++) {
            seen = seen || sLogged[k] == key;
        }
        if (!seen && sLoggedCount < 256) {
            sLogged[sLoggedCount++] = key;
            nebula_log("J3DNativeMaterial: material %016llx (shader %u) has no native program; not drawn\n",
                       static_cast< unsigned long long >(key), static_cast< u32 >(shader));
        }
        endCurrent();
        watchMaterial(NULL);
        dumpTag(shader);
    }

    // The draw's native material from the list's program and values (j3d_material.hlsli's constants).
    void sendNative(const NebulaMaterialProgram& rProgram, const NebulaMaterialValues& rValues, u32 shader,
                    bool keepDepthCull = false) {
        // Texture matrices into their places (as the material's display list loads them).
        const bool differed = rProgram.diff_flags != 0;
        if (!differed || ((rProgram.diff_flags >> 8) & 15) != 0) {
            for (u32 i = 0; i < 8; i++) {
                const u8 id = rProgram.texture_matrix[i].id;
                if (!rProgram.texture_matrix[i].present || (!differed && !rProgram.texture_matrix[i].listed)) {
                    continue;
                }
                const f32(*pMtx)[4] = reinterpret_cast< const f32(*)[4] >(rValues.texture_matrix[i]);
                if (id >= 64) {
                    J3DNativeShape::setPostTexture((id - 64) / 3, pMtx);
                } else if (id >= 30) {
                    J3DNativeShape::setTexture((id - 30) / 3, pMtx, rProgram.texture_matrix[i].rows == 2 ? 2 : 3);
                }
            }
        }
        NebulaNativeMaterial& material = sCurrent;
        // An instance's differed list loads only the textures it changes (diffTexNo); the others stay as the shared
        // list that drew just before loaded them: their images and coordinate scales carry over.
        static NebulaNativeMaterial sShared;
        const bool keepTextures = differed && sCurrentKind == CURRENT_J3D && sCurrent.shader == shader;
        if (keepTextures) {
            memcpy(sShared.textures, material.textures, sizeof(material.textures));
            memcpy(sShared.constants + 84, material.constants + 84, 32 * sizeof(f32));
        }
        memset(&material, 0, sizeof(material));
        material.shader = shader;
        material.blend_op = rProgram.blend.type;  // NEBULA_BLEND_*
        material.blend_src = rProgram.blend.source;  // NEBULA_FACTOR_*
        material.blend_dst = rProgram.blend.destination;
        material.logic_op = rProgram.blend.logic_op;
        material.depth_test = rProgram.z_mode.enable;
        material.depth_func = rProgram.z_mode.func;
        material.depth_write = rProgram.z_mode.update;
        material.early_depth = rProgram.z_compare_location;
        material.cull = rProgram.cull == NEBULA_CULL_ALL ? NEBULA_CULL_NONE : rProgram.cull;  // NEBULA_CULL_*
        material.color_write = 2;
        material.alpha_write = 2;
        if (keepDepthCull) {
            material.depth_test = NEBULA_NATIVE_KEPT;
            material.cull = NEBULA_NATIVE_KEPT;
        }
        if (differed) {
            // An instance's differed list carries no culling, and its blend and depth modes only under Blend
            // (J3D diffBlend), as the drawing state takes it (draw_state.cpp set_material): the rest stays as the
            // shared list and the state changes since set it.
            material.cull = NEBULA_NATIVE_KEPT;
            if ((rProgram.diff_flags & J3DDiffFlag_Blend) == 0) {
                material.blend_op = NEBULA_NATIVE_KEPT;
                material.depth_test = NEBULA_NATIVE_KEPT;
            }
        }
        loadRegisters(rProgram, rValues);
        memcpy(material.constants, sRegisters, sizeof(sRegisters));
        // Each coordinate's GX scale and each map's size (j3d_material.hlsli tex_scale): a sample reads coordinate c
        // of map m at c's scale over m's size, as GX scales coordinates per coordinate.
        for (u32 k = 0; k < 8; k++) {
            f32* pScale = material.constants + 84 + k * 4;
            const NebulaMaterialTexture& rTex = rValues.texture[k];
            if (keepTextures && rTex.image == 0) {
                memcpy(pScale, sShared.constants + 84 + k * 4, 4 * sizeof(f32));
                continue;
            }
            pScale[2] = rTex.width != 0 ? rTex.width : 1.0f;
            pScale[3] = rTex.height != 0 ? rTex.height : 1.0f;
            pScale[0] = rValues.tex_coord_scale[k].scale_s != 0 ? rValues.tex_coord_scale[k].scale_s : pScale[2];
            pScale[1] = rValues.tex_coord_scale[k].scale_t != 0 ? rValues.tex_coord_scale[k].scale_t : pScale[3];
        }
        for (u32 map = 0; map < 8; map++) {
            const NebulaMaterialTexture& rTex = rValues.texture[map];
            if (rTex.image == 0) {
                if (keepTextures) {
                    material.textures[map] = sShared.textures[map];
                }
                continue;
            }
            material.textures[map].image = rTex.image;
            material.textures[map].repeat_s = rTex.wrap_s;
            material.textures[map].repeat_t = rTex.wrap_t;
            material.textures[map].mag_linear = rTex.mag_linear;
            material.textures[map].min_linear = rTex.min_linear;
            material.textures[map].mip_filter = rTex.mip_filter;
            material.textures[map].anisotropy = rTex.anisotropy;
            material.textures[map].edge_lod = rTex.edge_lod;
            material.textures[map].lod_bias = rTex.lod_bias;
            material.textures[map].min_lod = rTex.min_lod;
            material.textures[map].max_lod = rTex.max_lod;
        }
        // Its texgens' post-transform rows (a shape's own texgens replace them at its draw). An instance's differed
        // list loads texgens only when its flags name them (TexGenNum with 0x1000: diffTexGen, as the GX path's
        // differed records): otherwise the texgens stay as the draw has them (a shape's own, ShapePacketUserData's
        // environment maps, set before the packet's differed list).
        const bool keepTexGens = differed && !(((rProgram.diff_flags >> 8) & 15) != 0 && (rProgram.diff_flags & 0x1000) != 0);
        for (u32 i = 0; i < 8; i++) {
            material.texgen_post[i] = keepTexGens ? static_cast< u8 >(NEBULA_NATIVE_KEPT)
                                      : i < rProgram.tex_gens
                                          ? static_cast< u8 >((rProgram.tex_gen[i].post_matrix & 0x3F) |
                                                              (rProgram.tex_gen[i].normalize != 0 ? 0x80 : 0))
                                          : 61;
        }
        // The lights the material's colour block loads (J3DColorBlockLightOn), as its list loaded them: a whole
        // material's with its channels, a differed list's under ColorChan or LightObjNum (diffLight), as the drawing
        // state takes them (native_material.cpp load_matrices).
        const bool lights = differed ? (rProgram.diff_flags & J3DDiffFlag_ColorChan) != 0 ||
                                           ((rProgram.diff_flags >> 4) & 15) != 0
                                     : (rProgram.loads & NEBULA_MATERIAL_LOADS_CHANNELS) != 0;
        if (lights && (rProgram.loads & NEBULA_MATERIAL_LOADS_LIGHTS) != 0) {
            material.light_mask = rValues.light_mask;
            memcpy(material.light, rValues.light, sizeof(material.light));
        }
        J3DNativeShape::flushState();
        const NebulaHostServices* pHost = nebula_host();
        pHost->native_material(pHost->user, &material);
        watchMaterial(&material);
        dumpTag(shader);
        sCurrentProgram = &rProgram;
    }
}  // namespace

namespace {
    u64 keyOf(const void* p) {
        return static_cast< u64 >(reinterpret_cast< uintptr_t >(p));
    }

    void copyColor(u8* pOut, const GXColor& rColor) {
        pOut[0] = rColor.r;
        pOut[1] = rColor.g;
        pOut[2] = rColor.b;
        pOut[3] = rColor.a;
    }

    // Map `map`'s texture as J3D's loadTexNo sets it up (the image and palette inside the texture's resource).
    void texture(J3DTexture* pTexture, u32 map, u16 texNo, NebulaMaterialValues* pOut, u16* pSizes) {
        ResTIMG* pImg = pTexture->getResTIMG(texNo);
        NebulaMaterialTexture& rOut = pOut->texture[map];
        rOut.image = keyOf(reinterpret_cast< u8* >(pImg) + pImg->mImageDataOffset);
        rOut.width = pImg->mWidth;
        rOut.height = pImg->mHeight;
        rOut.wrap_s = pImg->mWrapS;
        rOut.wrap_t = pImg->mWrapT;
        // GXTexFilter: near 0, linear 1, then by level near/linear: 2-3 the nearest level, 4-5 between levels.
        rOut.min_linear = pImg->mMinType == 1 || pImg->mMinType == 3 || pImg->mMinType == 5;
        rOut.mag_linear = pImg->mMagType != 0;
        rOut.mip_filter = pImg->mMinType < 2 ? 0 : pImg->mMinType < 4 ? 1 : 2;
        rOut.edge_lod = pImg->mDoEdgeLod;
        rOut.bias_clamp = pImg->mBiasClamp;
        rOut.anisotropy = pImg->mMaxAnisotropy;
        rOut.min_lod = static_cast< s8 >(pImg->mMinLod) * 0.125f;
        rOut.max_lod = static_cast< s8 >(pImg->mMaxLod) * 0.125f;
        rOut.lod_bias = pImg->mLodBias * 0.01f;
        // The texture coordinate scale J3D keeps per map (J3DSys::sTexCoordScaleTable): the image's size.
        pSizes[map * 2] = pImg->mWidth;
        pSizes[map * 2 + 1] = pImg->mHeight;
    }

    // A texture coordinate's scale entry as loadTexCoordScale writes it from its map's table entry.
    void coordScale(NebulaMaterialValues* pOut, u32 coord, u32 map) {
        const J3DTexCoordScaleInfo& rInfo = J3DSys::sTexCoordScaleTable[map & 7];
        pOut->tex_coord_scale[coord & 7].scale_s = rInfo.field_0x00;
        pOut->tex_coord_scale[coord & 7].scale_t = rInfo.field_0x02;
        pOut->tex_coord_scale[coord & 7].bias_s = rInfo.field_0x04 == 1;
        pOut->tex_coord_scale[coord & 7].bias_t = rInfo.field_0x06 == 1;
    }
}  // namespace

namespace J3DNativeMaterial {
    void dumpTag(u32 shader) {
        ::dumpTag(shader);
    }

    void build(J3DMaterial* pMaterial, J3DTexture* pTexture, NebulaMaterialParts* pParts) {
        memset(pParts, 0, sizeof(*pParts));
        NebulaMaterialProgram* pOut = &pParts->program;
        NebulaMaterialValues* pVal = &pParts->values;
        J3DTevBlock* pTev = pMaterial->mTevBlock;
        J3DIndBlock* pInd = pMaterial->mIndBlock;
        J3DTexGenBlock* pTexGen = pMaterial->mTexGenBlock;
        J3DColorBlock* pColor = pMaterial->mColorBlock;
        J3DPEBlock* pPE = pMaterial->mPEBlock;

        pOut->tev_stages = pTev->getTevStageNum();
        pOut->tex_gens = static_cast< u8 >(pTexGen->getTexGenNum());
        pOut->channels = pColor->getColorChanNum();
        pOut->ind_stages = pInd->getIndTexStageNum();
        {
            const GXCullMode mode = static_cast< GXCullMode >(pColor->getCullMode());
            pOut->cull = mode == GX_CULL_BACK ? NEBULA_CULL_BACK : mode == GX_CULL_FRONT ? NEBULA_CULL_FRONT
                         : mode == GX_CULL_ALL ? NEBULA_CULL_ALL : NEBULA_CULL_NONE;
        }

        // The null TEV block writes no textures, orders or stages (its stage count still sets the mode).
        const bool tev = pTev->getTevOrder(0) != NULL;
        if (tev) {
            pOut->loads |= NEBULA_MATERIAL_LOADS_TEV;
        }
        // Textures first: their sizes fill the coordinate scale table the stages then read.
        u16 sizes[16];
        memset(sizes, 0, sizeof(sizes));
        for (u32 map = 0; tev && map < 8; map++) {
            const u16 texNo = pTev->getTexNo(map);
            if (texNo != 0xFFFF) {
                texture(pTexture, map, texNo, pVal, sizes);
                J3DSys::sTexCoordScaleTable[map].field_0x00 = sizes[map * 2];
                J3DSys::sTexCoordScaleTable[map].field_0x02 = sizes[map * 2 + 1];
            }
        }
        for (u32 i = 0; tev && i < pOut->tev_stages; i++) {
            NebulaMaterialTevStage& rOut = pOut->tev[i];
            const J3DTevOrderInfo& rOrder = pTev->getTevOrder(i)->getTevOrderInfo();
            rOut.tex_coord = rOrder.mTexCoord;
            rOut.tex_map = rOrder.mTexMap;
            rOut.channel = rOrder.mColorChan;
            coordScale(pVal, rOrder.mTexCoord, rOrder.mTexMap);
            const J3DTevStage& rStage = *pTev->getTevStage(i);
            for (u32 k = 0; k < 4; k++) {
                rOut.color_in[k] = rStage.mColorIn[k];
                rOut.alpha_in[k] = rStage.mAlphaIn[k];
            }
            rOut.color_op = rStage.mColorOp;
            rOut.color_bias = rStage.mColorBias;
            rOut.color_scale = rStage.mColorScale;
            rOut.color_clamp = rStage.mColorClamp;
            rOut.color_reg = rStage.mColorReg;
            rOut.alpha_op = rStage.mAlphaOp;
            rOut.alpha_bias = rStage.mAlphaBias;
            rOut.alpha_scale = rStage.mAlphaScale;
            rOut.alpha_clamp = rStage.mAlphaClamp;
            rOut.alpha_reg = rStage.mAlphaReg;
            rOut.ras_swap = rStage.mRasSel;
            rOut.tex_swap = rStage.mTexSel;
            const J3DIndTevStage& rInd = *pTev->getIndTevStage(i);
            rOut.ind_stage = rInd.mIndStage;
            rOut.ind_format = rInd.mIndFormat;
            rOut.ind_bias = rInd.mBiasSel;
            rOut.ind_matrix = rInd.mMtxSel;
            rOut.ind_wrap_s = rInd.mWrapS;
            rOut.ind_wrap_t = rInd.mWrapT;
            rOut.ind_prev = rInd.mPrev;
            rOut.ind_lod = rInd.mLod;
            rOut.ind_alpha = rInd.mAlphaSel;
        }
        // Orders are written in pairs: an odd count's last stage pairs with the next entry the block holds.
        if (tev && (pOut->tev_stages & 1) != 0 && pOut->tev_stages < 16 && pTev->getType() != 'TVB1') {
            const J3DTevOrderInfo& rOrder = pTev->getTevOrder(pOut->tev_stages)->getTevOrderInfo();
            pOut->tev[pOut->tev_stages].tex_coord = rOrder.mTexCoord;
            pOut->tev[pOut->tev_stages].tex_map = rOrder.mTexMap;
            pOut->tev[pOut->tev_stages].channel = rOrder.mColorChan;
            coordScale(pVal, rOrder.mTexCoord, rOrder.mTexMap);
        }
        // Konst selectors for all 16 stages, as the blocks write them: their own entries repeated (two in J3DTevBlock2,
        // four in J3DTevBlock4, sixteen in J3DTevBlock16).
        const u32 tevType = pTev->getType();
        const u32 konst = tevType == 'TV16' ? 16 : tevType == 'TVB4' ? 4 : tevType == 'TVB2' ? 2 : 0;
        for (u32 i = 0; konst != 0 && i < 16; i++) {
            pOut->tev[i].konst_color = pTev->getTevKColorSel(i % konst);
            pOut->tev[i].konst_alpha = pTev->getTevKAlphaSel(i % konst);
        }
        // J3DTevBlock1 writes its one stage's order paired with a null second stage.
        if (tevType == 'TVB1') {
            pOut->tev[1].tex_coord = GX_TEXCOORD_NULL;
            pOut->tev[1].tex_map = GX_TEXMAP_NULL;
            pOut->tev[1].channel = GX_COLOR_NULL;
        }
        // The parts a TEV block variant keeps (J3DTevBlock1 has no colours, selectors or swap tables; the patched
        // block no swap tables): left as set before.
        if (pTev->getTevColor(0) != NULL) {
            pOut->loads |= NEBULA_MATERIAL_LOADS_TEV_COLOR;
            for (u32 i = 0; i < 3; i++) {
                const J3DGXColorS10& rColor = *pTev->getTevColor(i);
                pVal->tev_color[i][0] = rColor.r;
                pVal->tev_color[i][1] = rColor.g;
                pVal->tev_color[i][2] = rColor.b;
                pVal->tev_color[i][3] = rColor.a;
            }
        }
        if (pTev->getTevKColor(0) != NULL) {
            pOut->loads |= NEBULA_MATERIAL_LOADS_KONST_COLOR;
            for (u32 i = 0; i < 4; i++) {
                copyColor(pVal->konst_color[i], *pTev->getTevKColor(i));
            }
        }
        if (konst != 0) {
            pOut->loads |= NEBULA_MATERIAL_LOADS_KONST_SELECT | NEBULA_MATERIAL_LOADS_SWAP_TABLE;
            for (u32 i = 0; i < 4; i++) {
                const J3DTevSwapModeTable& rSwap = *pTev->getTevSwapModeTable(i);
                pOut->swap_table[i][0] = rSwap.getR();
                pOut->swap_table[i][1] = rSwap.getG();
                pOut->swap_table[i][2] = rSwap.getB();
                pOut->swap_table[i][3] = rSwap.getA();
            }
        }

        // J3DIndBlockFull writes all four orders and coordinate scales whatever its stage count (the null block nothing).
        if (pInd->getIndTexOrder(0) != NULL) {
            pOut->loads |= NEBULA_MATERIAL_LOADS_INDIRECT;
        }
        for (u32 i = 0; i < 4 && pInd->getIndTexOrder(0) != NULL; i++) {
            J3DIndTexOrder* pOrder = pInd->getIndTexOrder(i);
            J3DIndTexCoordScale* pScale = pInd->getIndTexCoordScale(i);
            pOut->ind_order[i].coord = pOrder->getCoord();
            pOut->ind_order[i].map = pOrder->getMap();
            pOut->ind_order[i].scale_s = pScale->getScaleS();
            pOut->ind_order[i].scale_t = pScale->getScaleT();
            coordScale(pVal, pOrder->getCoord(), pOrder->getMap());
        }
        for (u32 i = 0; i < pOut->ind_stages && i < 3; i++) {
            const J3DIndTexMtx& rMtx = *pInd->getIndTexMtx(i);
            memcpy(pVal->ind_matrix[i].m, rMtx.field_0x0, sizeof(pVal->ind_matrix[i].m));
            pVal->ind_matrix[i].scale = static_cast< s8 >(rMtx.field_0x18);
        }

        for (u32 i = 0; i < pOut->tex_gens && i < 8; i++) {
            const J3DTexCoord& rCoord = *pTexGen->getTexCoord(i);
            pOut->tex_gen[i].type = J3DNativeTexGen::kind(rCoord.getTexGenType(), rCoord.getTexGenSrc());
            pOut->tex_gen[i].source = J3DNativeTexGen::source(rCoord.getTexGenSrc());
            pOut->tex_gen[i].reserved[0] = 1;  // the renderer's encoding (gen_j3d_materials.py converts unmarked ones)
            pOut->tex_gen[i].matrix = static_cast< u8 >(rCoord.getTexMtxReg());
            // The post-transform matrix loadTexCoordGens names (identity unless j3dSys asks for post matrices).
            pOut->tex_gen[i].post_matrix =
                j3dSys.checkFlag(0x40000000) && rCoord.getTexGenMtx() != 60 ? static_cast< u8 >(i * 3) : 61;
        }

        // The null colour block writes no channels or colours.
        if (pColor->getColorChan(0) != NULL) {
            pOut->loads |= NEBULA_MATERIAL_LOADS_CHANNELS;
        }
        // Texture matrices as the texgen block's list loads them (J3DTexMtx::load, with this moment's j3dSys flags).
        for (u32 i = 0; i < 8; i++) {
            J3DTexMtx* pMtx = pTexGen->getTexMtx(i);
            if (pMtx == NULL) {
                continue;
            }
            const bool post = j3dSys.checkFlag(J3DSysFlag_PostTexMtx);
            pOut->texture_matrix[i].present = 1;
            pOut->texture_matrix[i].listed = i < pOut->tex_gens && pTexGen->getTexCoord(i)->getTexGenMtx() != GX_IDENTITY;
            pOut->texture_matrix[i].id = static_cast< u8 >(post ? i * 3 + 0x40 : i * 3 + 30);
            pOut->texture_matrix[i].rows = post || pMtx->getTexMtxInfo().mProjection != GX_MTX2x4 ? 3 : 2;
            memcpy(pVal->texture_matrix[i], pMtx->getMtx(), sizeof(pVal->texture_matrix[i]));
        }
        for (u32 i = 0; i < 4 && pColor->getColorChan(0) != NULL; i++) {
            const J3DColorChan& rChan = *pColor->getColorChan(i);
            pOut->channel[i].enable = rChan.getEnable();
            pOut->channel[i].material_source = rChan.getMatSrc();
            pOut->channel[i].ambient_source = rChan.getAmbSrc();
            pOut->channel[i].light_mask = rChan.getLightMask();
            pOut->channel[i].diffuse = rChan.getDiffuseFn();
            pOut->channel[i].attenuation = rChan.getAttnFn();
        }
        // The renderer's lighting encoding for all four (an absent block's zeros too: gen_j3d_materials.py does the same).
        J3DNativeChannel::toNative(*pOut);
        for (u32 i = 0; i < 2; i++) {
            if (pColor->getMatColor(i) != NULL) {
                copyColor(pVal->material_color[i], *pColor->getMatColor(i));
            }
            if (pColor->getAmbColor(i) != NULL) {
                pOut->loads |= NEBULA_MATERIAL_LOADS_AMBIENT;
                copyColor(pVal->ambient_color[i], *pColor->getAmbColor(i));
            }
        }
        // Lights only from the blocks that load them (J3DColorBlockLightOn).
        for (u32 i = 0; i < 8 && pColor->getType() == 'CLON'; i++) {
            pOut->loads |= NEBULA_MATERIAL_LOADS_LIGHTS;
            const J3DLightObj* pLight = pColor->getLight(i);
            if (pLight == NULL) {
                continue;
            }
            pVal->light_mask |= 1 << i;
            const J3DLightInfo& rInfo = pLight->mInfo;
            NebulaMaterialLight& rOut = pVal->light[i];
            rOut.position[0] = rInfo.mLightPosition.x;
            rOut.position[1] = rInfo.mLightPosition.y;
            rOut.position[2] = rInfo.mLightPosition.z;
            rOut.direction[0] = rInfo.mLightDirection.x;
            rOut.direction[1] = rInfo.mLightDirection.y;
            rOut.direction[2] = rInfo.mLightDirection.z;
            rOut.angle[0] = rInfo.mCosAtten.x;
            rOut.angle[1] = rInfo.mCosAtten.y;
            rOut.angle[2] = rInfo.mCosAtten.z;
            rOut.distance[0] = rInfo.mDistAtten.x;
            rOut.distance[1] = rInfo.mDistAtten.y;
            rOut.distance[2] = rInfo.mDistAtten.z;
            copyColor(rOut.color, rInfo.mColor);
        }

        // The preset blocks write fixed modes and no fog, and keep the dither (J3DPEBlockOpa, TexEdge, Xlu load).
        const u32 peType = pPE->getType();
        if (peType == 'PEOP' || peType == 'PEED' || peType == 'PEXL') {
            const bool edge = peType == 'PEED';
            const bool xlu = peType == 'PEXL';
            pOut->alpha_compare.comp0 = edge ? GX_GEQUAL : GX_ALWAYS;
            pVal->alpha_ref[0] = edge ? 0x80 : 0;
            pOut->alpha_compare.op = GX_AOP_AND;
            pOut->alpha_compare.comp1 = edge ? GX_LEQUAL : GX_ALWAYS;
            pVal->alpha_ref[1] = edge ? 0xFF : 0;
            pOut->blend.type = xlu ? NEBULA_BLEND_ADD : NEBULA_BLEND_REPLACE;
            pOut->blend.source = xlu ? NEBULA_FACTOR_SRC_ALPHA : NEBULA_FACTOR_ONE;
            pOut->blend.destination = xlu ? NEBULA_FACTOR_INV_SRC_ALPHA : NEBULA_FACTOR_ZERO;
            pOut->blend.logic_op = GX_LO_COPY;
            pOut->z_mode.enable = 1;
            pOut->z_mode.func = GX_LEQUAL;
            pOut->z_mode.update = xlu ? 0 : 1;
            pOut->z_compare_location = edge ? 0 : 1;
            return;
        }
        pOut->loads |= NEBULA_MATERIAL_LOADS_DITHER;
        if (J3DFog* pFog = pPE->getFog()) {
            pOut->loads |= NEBULA_MATERIAL_LOADS_FOG;
            const J3DFogInfo& rFog = *pFog->getFogInfo();
            pOut->fog.type = rFog.mType;
            pOut->fog.adjust_enable = rFog.mAdjEnable;
            pOut->fog.adjust_center = rFog.mCenter;
            pVal->fog.start = rFog.mStartZ;
            pVal->fog.end = rFog.mEndZ;
            pVal->fog.near_z = rFog.mNearZ;
            pVal->fog.far_z = rFog.mFarZ;
            copyColor(pVal->fog.color, rFog.mColor);
            for (u32 i = 0; i < 10; i++) {
                pOut->fog.adjust[i] = rFog.mFogAdjTable.r[i];
            }
        }
        const J3DAlphaComp& rAlpha = *pPE->getAlphaComp();
        pOut->alpha_compare.comp0 = rAlpha.getComp0();
        pVal->alpha_ref[0] = rAlpha.getRef0();
        pOut->alpha_compare.op = rAlpha.getOp();
        pOut->alpha_compare.comp1 = rAlpha.getComp1();
        pVal->alpha_ref[1] = rAlpha.getRef1();
        const J3DBlend& rBlend = *pPE->getBlend();
        {
            const u8 type = rBlend.mType;
            pOut->blend.type = type == GX_BM_BLEND ? NEBULA_BLEND_ADD : type == GX_BM_SUBTRACT ? NEBULA_BLEND_SUBTRACT
                               : type == GX_BM_LOGIC ? NEBULA_BLEND_LOGIC : NEBULA_BLEND_REPLACE;
        }
        pOut->blend.source = nativeFactor(rBlend.mSrcFactor, true);
        pOut->blend.destination = nativeFactor(rBlend.mDstFactor, false);
        pOut->blend.logic_op = rBlend.mOp;
        const J3DZMode& rZ = *pPE->getZMode();
        pOut->z_mode.enable = rZ.getCompareEnable();
        pOut->z_mode.func = rZ.getFunc();
        pOut->z_mode.update = rZ.getUpdateEnable();
        pOut->z_compare_location = pPE->getZCompLoc();
        pOut->dither = pPE->getDither();
    }

    NebulaMaterialParts* create() {
        return new (0x20) NebulaMaterialParts;
    }

    void capture(J3DMaterial* pMaterial, J3DTexture* pTexture, J3DDisplayListObj* pList, u32 diffFlags) {
        if (pList != NULL && pList->mpNebulaMaterial != NULL) {
            NebulaMaterialParts& rParts = *pList->mpNebulaMaterial;
            static NebulaMaterialProgram sRegistered;
            sRegistered = rParts.program;
            build(pMaterial, pTexture, &rParts);
            rParts.program.diff_flags = diffFlags;
            rParts.program.origin = diffFlags != 0 ? 2 : pTexture == j3dSys.getTexture() ? 3 : 1;
            rParts.nativeShader = nativeShaderOf(rParts.program);
            // The list's program for the host's material dump (GALAXY_DUMP_MATERIAL_PROGRAMS, the generator's input):
            // sent when the list's first capture makes it, or when a patch changes it (an animated material's captures
            // change only its values).
            if (!pList->mNebulaCaptured || memcmp(&sRegistered, &rParts.program, sizeof(sRegistered)) != 0) {
                const NebulaHostServices* pHost = nebula_host();
                pHost->material_program(pHost->user, keyOf(pList), &rParts.program);
            }
            pList->mNebulaCaptured = true;
        }
    }

    void send(J3DMaterial* pMaterial, J3DTexture* pTexture, J3DDisplayListObj* pList) {
        static NebulaMaterialParts sParts;
        J3DNativeShape::flushState();
        const NebulaHostServices* pHost = nebula_host();
        if (pList != NULL && pList->mNebulaCaptured) {
            const NebulaMaterialParts& rParts = *pList->mpNebulaMaterial;
            if (rParts.nativeShader != 0 && nativeDrawn(rParts.nativeShader)) {
                sendNative(rParts.program, rParts.values, rParts.nativeShader);
                return;
            }
            notDrawn(rParts.nativeShader, keyOf(pList));
            return;
        }
        // A list from the model file, which has no snapshot: its material built now, its program with it.
        build(pMaterial, pTexture, &sParts);
        const u32 shader = nativeShaderOf(sParts.program);
        if (shader != 0 && nativeDrawn(shader)) {
            sendNative(sParts.program, sParts.values, shader);
            return;
        }
        pHost->material_program(pHost->user, keyOf(&sParts), &sParts.program);
        notDrawn(shader, keyOf(pMaterial));
    }

    void sendDiffered(J3DDisplayListObj* pList) {
        if (pList == NULL || !pList->mNebulaCaptured) {
            return;
        }
        const NebulaMaterialParts& rParts = *pList->mpNebulaMaterial;
        if (rParts.nativeShader != 0 && nativeDrawn(rParts.nativeShader)) {
            sendNative(rParts.program, rParts.values, rParts.nativeShader);
            return;
        }
        J3DNativeShape::flushState();
        notDrawn(rParts.nativeShader, keyOf(pList));
    }

    const f32* registers() {
        return sRegisters;
    }

    void watch(const NebulaNativeMaterial* pNative) {
        watchMaterial(pNative);
    }

    u32 currentShader() {
        return sCurrentKind != CURRENT_NONE ? sWatchedShader : 0;
    }

    bool override(u32 mode, f32 a0, f32 a1, f32 ref) {
        if (sCurrentKind != CURRENT_J3D) {
            return false;
        }
        // j3d_material.hlsli MaterialConstants::ovr, after the register file and the coordinate scales.
        f32* pOverride = sCurrent.constants + 84 + 32;
        pOverride[0] = static_cast< f32 >(mode);
        pOverride[1] = a0;
        pOverride[2] = a1;
        pOverride[3] = ref;
        sCurrent.constants_only = 1;
        const NebulaHostServices* pHost = nebula_host();
        pHost->native_material(pHost->user, &sCurrent);
        return true;
    }

    bool overrideCoordinate(const void* pImage, u16 width, u16 height) {
        if (sCurrentKind != CURRENT_J3D) {
            return false;
        }
        // j3d_material.hlsli MaterialConstants::ovr_coord, after ovr; coordinate 0's scale as the new map's size.
        sCurrent.constants[84 + 32 + 4] = 1.0f;
        f32* pScale = sCurrent.constants + 84;
        pScale[0] = pScale[2] = width;
        pScale[1] = pScale[3] = height;
        memset(&sCurrent.textures[0], 0, sizeof(sCurrent.textures[0]));
        sCurrent.textures[0].image = keyOf(pImage);
        sCurrent.textures[0].repeat_s = 1;
        sCurrent.textures[0].repeat_t = 1;
        sCurrent.textures[0].mag_linear = 1;
        sCurrent.textures[0].min_linear = 1;
        sCurrent.constants_only = 0;
        const NebulaHostServices* pHost = nebula_host();
        pHost->native_material(pHost->user, &sCurrent);
        return true;
    }

    void sendWhole(const NebulaMaterialProgram& rProgram, const NebulaMaterialValues& rValues) {
        const u32 shader = nativeShaderOf(rProgram);
        if (shader != 0 && nativeDrawn(shader)) {
            sendNative(rProgram, rValues, shader, true);
            return;
        }
        // The program's identity: its bytes' FNV-1a hash (the host keeps one registration per key).
        const u8* pBytes = reinterpret_cast< const u8* >(&rProgram);
        u64 key = 0xCBF29CE484222325ull;
        for (u32 i = 0; i < sizeof(rProgram); i++) {
            key = (key ^ pBytes[i]) * 0x100000001B3ull;
        }
        key |= 1ull << 63;  // apart from the game's addresses, the other materials' keys
        static u64 sRegistered[2048];
        u32 slot = static_cast< u32 >(key) & 2047;
        while (sRegistered[slot] != 0 && sRegistered[slot] != key) {
            slot = (slot + 1) & 2047;
        }
        const NebulaHostServices* pHost = nebula_host();
        J3DNativeShape::flushState();
        if (sRegistered[slot] != key) {
            sRegistered[slot] = key;
            pHost->material_program(pHost->user, key, &rProgram);
        }
        notDrawn(shader, key);
    }

    void state(const NebulaStateOp* pOps, u32 count) {
        bool changed = false;
        for (u32 i = 0; i < count; i++) {
            const NebulaStateOp& rOp = pOps[i];
            if (sCurrentKind == CURRENT_J3D && changesProgram(rOp)) {
                if (!sFolding) {
                    sFold = *sCurrentProgram;
                    sFolding = true;
                    sFoldFailed = false;
                    sFoldCull = false;
                }
                sFoldFailed = !foldOp(sFold, rOp) || sFoldFailed;
                sFoldCull = sFoldCull || rOp.kind == NEBULA_STATE_GEN_MODE2;
            } else if (sCurrentKind != CURRENT_NONE && changesProgram(rOp)) {
                if (sCurrentKind == CURRENT_SITE) {
                    // A site's native material ended by a GX program change: its next draws fall back to the drawing
                    // state's program. Each pair once in the log (every one is a conversion left to finish).
                    static u32 sLogged[64];
                    static u32 sLoggedCount = 0;
                    const u32 pair = sCurrent.shader << 8 | rOp.kind;
                    bool seen = false;
                    for (u32 k = 0; k < sLoggedCount; k++) {
                        seen = seen || sLogged[k] == pair;
                    }
                    if (!seen && sLoggedCount < 64) {
                        sLogged[sLoggedCount++] = pair;
                        nebula_log("J3DNativeMaterial: native shader %u ended by GX state op %u\n",
                                   static_cast< u32 >(sCurrent.shader), static_cast< u32 >(rOp.kind));
                    }
                }
                endCurrent();
            }
            changed = setRegister(rOp) || changed;
        }
        // The folded program: its generated shader with the register file as the list left it; else the GX path as
        // before, the folded program registered so a material dump collects it for the set.
        if (sFolding) {
            sFolding = false;
            const u32 shader = sFoldFailed ? 0 : nativeShaderOf(sFold);
            if (shader != 0 && nativeDrawn(shader)) {
                sCurrent.shader = shader;
                if (sFoldCull) {
                    sCurrent.cull = sFold.cull == GX_CULL_BACK    ? NEBULA_CULL_BACK
                                    : sFold.cull == GX_CULL_FRONT ? NEBULA_CULL_FRONT
                                                                  : NEBULA_CULL_NONE;
                }
                memcpy(sCurrent.constants, sRegisters, sizeof(sRegisters));
                sCurrent.constants_only = 0;
                const NebulaHostServices* pHost = nebula_host();
                pHost->native_material(pHost->user, &sCurrent);
                watchMaterial(&sCurrent);
                dumpTag(shader);
                sCurrentProgram = &sFold;
                changed = false;
            } else {
                if (!sFoldFailed) {
                    static NebulaMaterialProgram sFoldRegistered;
                    sFoldRegistered = sFold;
                    sFoldRegistered.diff_flags = 0;
                    sFoldRegistered.origin = 0;
                    const NebulaHostServices* pHost = nebula_host();
                    pHost->material_program(pHost->user,
                                            static_cast< u64 >(reinterpret_cast< uintptr_t >(&sFoldRegistered)),
                                            &sFoldRegistered);
                }
                static u32 sFoldLogged[256];
                static u32 sFoldLoggedCount = 0;
                const u32 pair = sCurrent.shader << 1 | (sFoldFailed ? 1u : 0u);
                bool seen = false;
                for (u32 k = 0; k < sFoldLoggedCount; k++) {
                    seen = seen || sFoldLogged[k] == pair;
                }
                if (!seen && sFoldLoggedCount < 256) {
                    sFoldLogged[sFoldLoggedCount++] = pair;
                    nebula_log("J3DNativeMaterial: J3D shader %u's list %s; drawn by the GX path\n",
                               static_cast< u32 >(sCurrent.shader),
                               sFoldFailed ? "changes what a program record cannot hold" : "folds to a program not in the set");
                }
                endCurrent();
            }
        }
        // A value the current J3D material takes: the material again with its constants (its state, textures and
        // lights as set since).
        if (changed && sCurrentKind == CURRENT_SITE && sSiteFollows) {
            memcpy(sSiteRegisters.constants, sRegisters, sizeof(sRegisters));
            sSiteRegisters.constants_only = 3;
            const NebulaHostServices* pHost = nebula_host();
            pHost->native_material(pHost->user, &sSiteRegisters);
        }
        if (changed && sCurrentKind == CURRENT_J3D) {
            memcpy(sCurrent.constants, sRegisters, sizeof(sRegisters));
            sCurrent.constants_only = 1;
            const NebulaHostServices* pHost = nebula_host();
            pHost->native_material(pHost->user, &sCurrent);
        }
    }

    void sweep(J3DModelData* pModelData) {
        static s32 sWanted = -1;
        const NebulaHostServices* pHost = nebula_host();
        if (sWanted < 0) {
            sWanted = pHost->option(pHost->user, NEBULA_OPTION_TEST_MATERIAL_SWEEP) != 0 ? 1 : 0;
        }
        if (sWanted == 0) {
            return;
        }
        static NebulaMaterialParts sParts;
        for (u16 i = 0; i < pModelData->getMaterialNum(); i++) {
            J3DMaterial* pMaterial = pModelData->getMaterialNodePointer(i);
            build(pMaterial, pModelData->getTexture(), &sParts);
            if (nativeShaderOf(sParts.program) == 0) {
                pHost->material_program(pHost->user, keyOf(pMaterial), &sParts.program);
            }
        }
    }
};  // namespace J3DNativeMaterial
