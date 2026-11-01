#include "Game/Screen/MovieDraw.hpp"
#include <revolution/gx.h>
#include <revolution/mtx.h>
#include <JSystem/J3DGraphBase/J3DNativeTexture.hpp>
#include <JSystem/J3DGraphBase/J3DNativeState.hpp>
#include <JSystem/J3DGraphBase/J3DNativeView.hpp>
#include <JSystem/J3DGraphBase/J3DNativeShape.hpp>
#include <JSystem/J3DGraphBase/J3DNativeDraw.hpp>
#include <nebula_game_api.h>
#include <cstring>

extern "C" const NebulaHostServices* nebula_host(void);

namespace {
    bool sLinear = false;

    // The frame's program: Y, U and V planes to RGB (the console THP library's four stages), and the state it leaves
    // for the draws after it (one stage replacing with texture 0, as the library's restore did). Registered with the
    // host when first used; the key is the program's address.
    NebulaMaterialProgram sFrameProgram;
    NebulaMaterialProgram sRestoreProgram;
    bool sRegistered = false;

    void setStage(NebulaMaterialTevStage& rStage, u8 coord, u8 map, u8 a, u8 b, u8 c, u8 d, u8 op, u8 scale, u8 clamp,
                  u8 aa, u8 ab, u8 ac, u8 ad, u8 alphaOp, u8 alphaClamp) {
        rStage.tex_coord = coord;
        rStage.tex_map = map;
        rStage.channel = GX_COLOR_NULL;
        rStage.color_in[0] = a;
        rStage.color_in[1] = b;
        rStage.color_in[2] = c;
        rStage.color_in[3] = d;
        rStage.color_op = op;
        rStage.color_bias = GX_TB_ZERO;
        rStage.color_scale = scale;
        rStage.color_clamp = clamp;
        rStage.color_reg = GX_TEVPREV;
        rStage.alpha_in[0] = aa;
        rStage.alpha_in[1] = ab;
        rStage.alpha_in[2] = ac;
        rStage.alpha_in[3] = ad;
        rStage.alpha_op = alphaOp;
        rStage.alpha_bias = GX_TB_ZERO;
        rStage.alpha_scale = GX_CS_SCALE_1;
        rStage.alpha_clamp = alphaClamp;
        rStage.alpha_reg = GX_TEVPREV;
    }

    // What both programs share: the default swap tables, no konst selections, identity texgens from texture
    // coordinate 0, no channels, no alpha test, depth always passing and unwritten, no blending, no culling.
    void initProgram(NebulaMaterialProgram& rProgram, u8 stages, u8 texGens, u8 logicOp) {
        memset(&rProgram, 0, sizeof(rProgram));
        for (u32 s = 0; s < 16; s++) {
            rProgram.tev[s].konst_color = GX_TEV_KCSEL_1_4;
            rProgram.tev[s].konst_alpha = GX_TEV_KASEL_1;
        }
        static const u8 kSwap[4][4] = {{GX_CH_RED, GX_CH_GREEN, GX_CH_BLUE, GX_CH_ALPHA},
                                       {GX_CH_RED, GX_CH_RED, GX_CH_RED, GX_CH_ALPHA},
                                       {GX_CH_GREEN, GX_CH_GREEN, GX_CH_GREEN, GX_CH_ALPHA},
                                       {GX_CH_BLUE, GX_CH_BLUE, GX_CH_BLUE, GX_CH_ALPHA}};
        memcpy(rProgram.swap_table, kSwap, sizeof(kSwap));
        rProgram.tev_stages = stages;
        rProgram.tex_gens = texGens;
        rProgram.cull = GX_CULL_NONE;
        rProgram.z_compare_location = 1;
        rProgram.loads = NEBULA_MATERIAL_LOADS_TEV | NEBULA_MATERIAL_LOADS_KONST_SELECT | NEBULA_MATERIAL_LOADS_TEV_COLOR |
                         NEBULA_MATERIAL_LOADS_KONST_COLOR;
        for (u8 t = 0; t < texGens; t++) {
            rProgram.tex_gen[t].type = GX_TG_MTX2x4;
            rProgram.tex_gen[t].source = GX_TG_TEX0;
            rProgram.tex_gen[t].matrix = GX_IDENTITY;
            rProgram.tex_gen[t].post_matrix = GX_PTIDENTITY;
        }
        rProgram.alpha_compare.comp0 = rProgram.alpha_compare.comp1 = GX_ALWAYS;
        rProgram.alpha_compare.op = GX_AOP_AND;
        rProgram.blend.type = GX_BM_NONE;
        rProgram.blend.source = GX_BL_ONE;
        rProgram.blend.destination = GX_BL_ZERO;
        rProgram.blend.logic_op = logicOp;
        rProgram.z_mode.enable = GX_ENABLE;
        rProgram.z_mode.func = GX_ALWAYS;
        rProgram.z_mode.update = GX_DISABLE;
    }

    void registerPrograms() {
        NebulaMaterialProgram& rFrame = sFrameProgram;
        initProgram(rFrame, 4, 2, GX_LO_CLEAR);
        // U (map 1) and V (map 2) offset and scaled by konst 0 and 1 against register 0, then Y (map 0) added and the
        // alpha folded in by konst 2: the library's coefficients.
        setStage(rFrame.tev[0], GX_TEXCOORD1, GX_TEXMAP1, GX_CC_ZERO, GX_CC_TEXC, GX_CC_KONST, GX_CC_C0, GX_TEV_ADD,
                 GX_CS_SCALE_1, GX_FALSE, GX_CA_ZERO, GX_CA_TEXA, GX_CA_KONST, GX_CA_A0, GX_TEV_SUB, GX_FALSE);
        rFrame.tev[0].konst_color = GX_TEV_KCSEL_K0;
        rFrame.tev[0].konst_alpha = GX_TEV_KASEL_K0_A;
        setStage(rFrame.tev[1], GX_TEXCOORD1, GX_TEXMAP2, GX_CC_ZERO, GX_CC_TEXC, GX_CC_KONST, GX_CC_CPREV, GX_TEV_ADD,
                 GX_CS_SCALE_2, GX_FALSE, GX_CA_ZERO, GX_CA_TEXA, GX_CA_KONST, GX_CA_APREV, GX_TEV_SUB, GX_FALSE);
        rFrame.tev[1].konst_color = GX_TEV_KCSEL_K1;
        rFrame.tev[1].konst_alpha = GX_TEV_KASEL_K1_A;
        setStage(rFrame.tev[2], GX_TEXCOORD0, GX_TEXMAP0, GX_CC_ZERO, GX_CC_TEXC, GX_CC_ONE, GX_CC_CPREV, GX_TEV_ADD,
                 GX_CS_SCALE_1, GX_TRUE, GX_CA_TEXA, GX_CA_ZERO, GX_CA_ZERO, GX_CA_APREV, GX_TEV_ADD, GX_TRUE);
        setStage(rFrame.tev[3], GX_TEXCOORD_NULL, GX_TEXMAP_NULL, GX_CC_APREV, GX_CC_CPREV, GX_CC_KONST, GX_CC_ZERO,
                 GX_TEV_ADD, GX_CS_SCALE_1, GX_TRUE, GX_CA_ZERO, GX_CA_ZERO, GX_CA_ZERO, GX_CA_ZERO, GX_TEV_ADD, GX_TRUE);
        rFrame.tev[3].konst_color = GX_TEV_KCSEL_K2;

        NebulaMaterialProgram& rRestore = sRestoreProgram;
        initProgram(rRestore, 1, 1, GX_LO_SET);
        setStage(rRestore.tev[0], GX_TEXCOORD0, GX_TEXMAP0, GX_CC_ZERO, GX_CC_ZERO, GX_CC_ZERO, GX_CC_TEXC, GX_TEV_ADD,
                 GX_CS_SCALE_1, GX_TRUE, GX_CA_ZERO, GX_CA_ZERO, GX_CA_ZERO, GX_CA_TEXA, GX_TEV_ADD, GX_TRUE);

        const NebulaHostServices* pHost = nebula_host();
        pHost->material_program(pHost->user, reinterpret_cast< uintptr_t >(&sFrameProgram), &sFrameProgram);
        pHost->material_program(pHost->user, reinterpret_cast< uintptr_t >(&sRestoreProgram), &sRestoreProgram);
        sRegistered = true;
    }

    void setPlane(NebulaMaterialTexture& rTexture, u8* pImage, u16 width, u16 height) {
        rTexture.image = reinterpret_cast< uintptr_t >(pImage);
        rTexture.width = width;
        rTexture.height = height;
        rTexture.wrap_s = rTexture.wrap_t = GX_CLAMP;
        rTexture.min_filter = rTexture.mag_filter = sLinear ? GX_LINEAR : GX_NEAR;
        rTexture.anisotropy = GX_ANISO_1;
    }
}  // namespace

namespace MovieDraw {
    void setLinearFilter(bool linear) {
        sLinear = linear;
    }

    void drawFrame(u16 screenWidth, u16 screenHeight, u8* pY, u8* pU, u8* pV, s16 x, s16 y, s16 textureWidth,
                   s16 textureHeight, s16 width, s16 height) {
        if (!sRegistered) {
            registerPrograms();
        }
        // The screen in its own pixels, the frame as it is.
        J3DNativeState::setPixelFmt(GX_PF_RGB8_Z24, GX_ZC_LINEAR);
        Mtx44 projection;
        NebulaMtxOrtho(projection, 0.0f, screenHeight, 0.0f, screenWidth, 0.0f, -1.0f);
        J3DNativeView::setProjection(projection, GX_ORTHOGRAPHIC);
        J3DNativeView::setViewport(0.0f, 0.0f, screenWidth, screenHeight, 0.0f, 1.0f);
        J3DNativeView::setScissor(0, 0, screenWidth, screenHeight);
        Mtx identity;
        NebulaMtxIdentity(identity);
        J3DNativeShape::setPosition(0, identity);
        J3DNativeState::setCurrentMtx(GX_PNMTX0);
        J3DNativeState::setColorUpdate(GX_TRUE);
        J3DNativeState::setAlphaUpdate(GX_FALSE);
        J3DNativeState::setNumChans(0);
        J3DNativeState::invalidateTexAll();

        // The frame's planes, decoded into these buffers since the last frame as rows of texels (nebula_cooked_texture.h).
        const u16 chromaWidth = static_cast< u16 >(textureWidth >> 1);
        const u16 chromaHeight = static_cast< u16 >(textureHeight >> 1);
        J3DNativeTexture::registerImage(pY, NEBULA_TEXTURE_LINEAR_L8, textureWidth, textureHeight, 1);
        J3DNativeTexture::registerImage(pU, NEBULA_TEXTURE_LINEAR_L8, chromaWidth, chromaHeight, 1);
        J3DNativeTexture::registerImage(pV, NEBULA_TEXTURE_LINEAR_L8, chromaWidth, chromaHeight, 1);

        NebulaMaterialValues values;
        memset(&values, 0, sizeof(values));
        static const s16 kRegister0[4] = {-90, 0, -114, 135};
        static const u8 kKonst[3][4] = {{0, 0, 226, 88}, {179, 0, 0, 182}, {255, 0, 255, 128}};
        memcpy(values.tev_color[0], kRegister0, sizeof(kRegister0));
        memcpy(values.konst_color, kKonst, sizeof(kKonst));
        setPlane(values.texture[0], pY, textureWidth, textureHeight);
        setPlane(values.texture[1], pU, chromaWidth, chromaHeight);
        setPlane(values.texture[2], pV, chromaWidth, chromaHeight);
        values.tex_coord_scale[0].scale_s = textureWidth;
        values.tex_coord_scale[0].scale_t = textureHeight;
        values.tex_coord_scale[1].scale_s = chromaWidth;
        values.tex_coord_scale[1].scale_t = chromaHeight;
        J3DNativeShape::flushState();
        const NebulaHostServices* pHost = nebula_host();
        pHost->material(pHost->user, reinterpret_cast< uintptr_t >(&sFrameProgram), &values);
        // The coordinates' texture matrices: the identity's place.
        static const u8 kIdentity[8] = {GX_IDENTITY, GX_IDENTITY, GX_IDENTITY, GX_IDENTITY,
                                        GX_IDENTITY, GX_IDENTITY, GX_IDENTITY, GX_IDENTITY};
        J3DNativeShape::setCurrent(GX_PNMTX0, kIdentity);

        J3DNativeDraw::begin(GX_QUADS, 4, NEBULA_SHAPE_TEXCOORD0);
        J3DNativeDraw::position(static_cast< f32 >(x), static_cast< f32 >(y), 0.0f);
        J3DNativeDraw::texCoord(0.0f, 0.0f);
        J3DNativeDraw::position(static_cast< f32 >(static_cast< s16 >(x + width)), static_cast< f32 >(y), 0.0f);
        J3DNativeDraw::texCoord(1.0f, 0.0f);
        J3DNativeDraw::position(static_cast< f32 >(static_cast< s16 >(x + width)),
                                static_cast< f32 >(static_cast< s16 >(y + height)), 0.0f);
        J3DNativeDraw::texCoord(1.0f, 1.0f);
        J3DNativeDraw::position(static_cast< f32 >(x), static_cast< f32 >(static_cast< s16 >(y + height)), 0.0f);
        J3DNativeDraw::texCoord(0.0f, 1.0f);

        // What the draws after the frame start from (registers, konst colours and textures as the frame left them).
        J3DNativeShape::flushState();
        pHost->material(pHost->user, reinterpret_cast< uintptr_t >(&sRestoreProgram), &values);
    }
};  // namespace MovieDraw
