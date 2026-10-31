#include "Game/MapObj/BigBubbleDrawer.hpp"
#include <JSystem/J3DGraphBase/J3DNativeState.hpp>
#include <JSystem/J3DGraphBase/J3DNativeShape.hpp>
#include "Game/LiveActor/Nerve.hpp"
#include "Game/Util.hpp"
#include <JSystem/JGeometry/TMatrix.hpp>
#include <JSystem/JUtility/JUTTexture.hpp>
#include <nebula_game_api.h>
#include <cstring>

extern "C" const NebulaHostServices* nebula_host(void);

namespace {
    // The bubble's material (nebula_game_api.h NebulaMaterialProgram): the rainbow texture (0, coordinate 0 through
    // texture matrix 0), register 0 mixed in by the bubble environment map's alpha (1, coordinate 1 from the normal
    // through texture matrix 1), register 1's colour added by its alpha; blended by alpha, depth tested and not written,
    // back faces culled.
    NebulaMaterialProgram sBubbleProgram;
    bool sBubbleRegistered = false;

    void setStage(NebulaMaterialTevStage& rStage, u8 coord, u8 map, u8 c0, u8 c1, u8 c2, u8 c3, u8 colorClamp, u8 a0,
                  u8 a1, u8 a2, u8 a3) {
        rStage.tex_coord = coord;
        rStage.tex_map = map;
        rStage.channel = GX_COLOR_NULL;
        rStage.color_in[0] = c0;
        rStage.color_in[1] = c1;
        rStage.color_in[2] = c2;
        rStage.color_in[3] = c3;
        rStage.alpha_in[0] = a0;
        rStage.alpha_in[1] = a1;
        rStage.alpha_in[2] = a2;
        rStage.alpha_in[3] = a3;
        rStage.color_op = rStage.alpha_op = GX_TEV_ADD;
        rStage.color_scale = rStage.alpha_scale = GX_CS_SCALE_1;
        rStage.color_clamp = colorClamp;
        rStage.alpha_clamp = GX_FALSE;
        rStage.color_reg = rStage.alpha_reg = GX_TEVPREV;
    }

    void sendBubbleProgram(JUTTexture* pRainbow, JUTTexture* pBubble) {
        const NebulaHostServices* pHost = nebula_host();
        if (!sBubbleRegistered) {
            NebulaMaterialProgram& rProgram = sBubbleProgram;
            memset(&rProgram, 0, sizeof(rProgram));
            setStage(rProgram.tev[0], GX_TEXCOORD0, GX_TEXMAP0, GX_CC_TEXC, GX_CC_ZERO, GX_CC_ZERO, GX_CC_ZERO, GX_FALSE,
                     GX_CA_TEXA, GX_CA_ZERO, GX_CA_ZERO, GX_CA_ZERO);
            setStage(rProgram.tev[1], GX_TEXCOORD1, GX_TEXMAP1, GX_CC_ZERO, GX_CC_C0, GX_CC_TEXA, GX_CC_CPREV, GX_TRUE,
                     GX_CA_TEXA, GX_CA_ZERO, GX_CA_ZERO, GX_CA_ZERO);
            setStage(rProgram.tev[2], GX_TEXCOORD_NULL, GX_TEXMAP_NULL, GX_CC_CPREV, GX_CC_C1, GX_CC_A1, GX_CC_ZERO, GX_TRUE,
                     GX_CA_APREV, GX_CA_KONST, GX_CA_A1, GX_CA_ZERO);
            rProgram.tex_gen[0].type = GX_TG_MTX2x4;
            rProgram.tex_gen[0].source = GX_TG_TEX0;
            rProgram.tex_gen[0].matrix = GX_TEXMTX0;
            rProgram.tex_gen[0].post_matrix = GX_PTIDENTITY;
            rProgram.tex_gen[1].type = GX_TG_MTX3x4;
            rProgram.tex_gen[1].source = GX_TG_NRM;
            rProgram.tex_gen[1].matrix = GX_TEXMTX1;
            rProgram.tex_gen[1].post_matrix = GX_PTIDENTITY;
            rProgram.tev_stages = 3;
            rProgram.tex_gens = 2;
            rProgram.channels = 1;
            rProgram.cull = GX_CULL_BACK;
            rProgram.z_compare_location = 0;
            rProgram.loads = NEBULA_MATERIAL_LOADS_TEV;
            rProgram.alpha_compare.comp0 = rProgram.alpha_compare.comp1 = GX_ALWAYS;
            rProgram.alpha_compare.op = GX_AOP_AND;
            rProgram.blend.type = GX_BM_BLEND;
            rProgram.blend.source = GX_BL_SRCALPHA;
            rProgram.blend.destination = GX_BL_INVSRCALPHA;
            rProgram.blend.logic_op = GX_LO_CLEAR;
            rProgram.z_mode.enable = GX_TRUE;
            rProgram.z_mode.func = GX_LEQUAL;
            rProgram.z_mode.update = GX_FALSE;
            pHost->material_program(pHost->user, reinterpret_cast< uintptr_t >(&sBubbleProgram), &rProgram);
            sBubbleRegistered = true;
        }
        NebulaMaterialValues values;
        memset(&values, 0, sizeof(values));
        values.tex_coord_scale[0].scale_s = pRainbow->getWidth();
        values.tex_coord_scale[0].scale_t = pRainbow->getHeight();
        values.tex_coord_scale[1].scale_s = pBubble->getWidth();
        values.tex_coord_scale[1].scale_t = pBubble->getHeight();
        J3DNativeShape::flushState();
        pHost->material(pHost->user, reinterpret_cast< uintptr_t >(&sBubbleProgram), &values);
        static const u8 kPlaces[8] = {GX_TEXMTX0, GX_TEXMTX1, GX_IDENTITY, GX_IDENTITY,
                                      GX_IDENTITY, GX_IDENTITY, GX_IDENTITY, GX_IDENTITY};
        J3DNativeShape::setCurrent(GX_PNMTX0, kPlaces);
    }
}  // namespace

namespace {
    static Color8 sTevColor0(0xE0E0E0FF);
    Color8 sTevColor1(0xFF000000);
    TVec3f sEnvMatTrans(1.0f, 1.0f, 1.5f);
};  // namespace

BigBubbleDrawer::BigBubbleDrawer() {
    mBubbleTexture = nullptr;
    mRainbowTexture = nullptr;
    _8 = 0.0f;
    mBubbleTexture = new JUTTexture(MR::loadTexFromArc("BigBubble.arc", "Bubble.bti"), 0);
    mRainbowTexture = new JUTTexture(MR::loadTexFromArc("BigBubble.arc", "Rainbow.bti"), 0);
}

void BigBubbleDrawer::setUpDraw() const {
    setUpMaterial();
}

void BigBubbleDrawer::setUpVertexFormat() const {
}

void BigBubbleDrawer::setUpTexture() const {
    TPos3f mtx;
    mtx.identity();
    J3DNativeShape::setTexture(0, mtx, 2);

    mtx.set(MR::getCameraViewMtx());
    mtx.setTrans(::sEnvMatTrans);

    J3DNativeShape::setTexture(1, mtx, 3);
    mRainbowTexture->load(GX_TEXMAP0);
    mBubbleTexture->load(GX_TEXMAP1);
}

void BigBubbleDrawer::setUpTevStage() const {
    sendBubbleProgram(mRainbowTexture, mBubbleTexture);
    J3DNativeState::setTevColor(GX_TEVREG0, (GXColor&)::sTevColor0);
    J3DNativeState::setTevKAlphaSel(GX_TEVSTAGE2, GX_TEV_KASEL_7_8);
}

void BigBubbleDrawer::setUpPixelFormat() const {
    J3DNativeState::setColorUpdate(1);
    J3DNativeState::setAlphaUpdate(0);
    J3DNativeState::setCoPlanar(0);
    J3DNativeState::setClipMode(GX_CLIP_ENABLE);
}

void BigBubbleDrawer::setUpMaterial() const {
    setUpVertexFormat();
    J3DNativeState::setChanCtrl(GX_COLOR0A0, 0, GX_SRC_REG, GX_SRC_REG, 0, GX_DF_NONE, GX_AF_NONE);
    J3DNativeState::setChanCtrl(GX_COLOR1A1, 0, GX_SRC_REG, GX_SRC_REG, 0, GX_DF_NONE, GX_AF_NONE);
    setUpTexture();
    setUpTevStage();
    setUpPixelFormat();
    J3DNativeShape::setPosition(0, MR::getCameraViewMtx());

    Mtx normalMtx;
    NebulaMtxIdentity(normalMtx);
    J3DNativeShape::setNormal(0, normalMtx);
    J3DNativeState::setCurrentMtx(0);
    MR::loadProjectionMtx();
}
