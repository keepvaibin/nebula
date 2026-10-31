#include "Game/MapObj/FallOutFieldDraw.hpp"
#include <JSystem/J3DGraphBase/J3DNativeShape.hpp>
#include <JSystem/JUtility/JUTTexture.hpp>
#include <nebula_game_api.h>
#include <cstring>

extern "C" const NebulaHostServices* nebula_host(void);
#include <JSystem/J3DGraphBase/J3DNativeState.hpp>
#include <JSystem/J3DGraphBase/J3DNativeView.hpp>
#include "Game/LiveActor/Nerve.hpp"
#include "Game/Map/StageSwitch.hpp"
#include "Game/MapObj/ClipAreaHolder.hpp"
#include "Game/Scene/SceneFunction.hpp"
#include "Game/Scene/SceneObjHolder.hpp"
#include "Game/Screen/ImageEffectLocalUtil.hpp"
#include "Game/Screen/ScreenAlphaCapture.hpp"
#include "Game/Util/CameraUtil.hpp"
#include "Game/Util/Color.hpp"
#include "Game/Util/JMapUtil.hpp"
#include "Game/Util/ObjUtil.hpp"
#include "Game/Util/ScreenUtil.hpp"

void FallOutFieldDraw_FORCE_MATCH_SDATA2() {
    (void)0.25f;
}

namespace {
    inline void drawFillScreen(GXColor color) {
        J3DNativeState::setTevColor(GX_TEVREG0, color);
        ImageEffectLocalUtil::sendTextureVertex(1, 0);
    }
};  // namespace

namespace {
    static GXTevOp sAlphaTevOperater = GX_TEV_COMP_A8_GT;
    static GXBlendMode sFillBlendMode = GX_BM_BLEND;
    static GXBlendFactor sFillSrcFactor = GX_BL_SRCALPHA;
    static GXBlendFactor sFillDstFactor = GX_BL_INVSRCALPHA;
    static GXLogicOp sFillLogicOp = GX_LO_NOOP;
    // static const s32 sBlurCount = _;
    // static const f32 sBlurRadius = _;
    // static const f32 sBlurIntensity = _;
    static Color8 sFillColor = Color8(0, 0, 0, 255);
    static Color8 sEdgeColor = Color8(0, 46, 200, 128);
};  // namespace

FallOutFieldDraw::FallOutFieldDraw(const char* pName) : NameObj(pName), _C() {
    MR::createClipAreaHolder();
    MR::connectToScene(this, MR::MovementType_None, MR::CalcAnimType_None, MR::DrawBufferType_None, MR::DrawType_FallOutFieldDraw);
    MR::createScreenAlphaSceneObj(2, 0.5f);
    MR::createScreenAlphaSceneObj(3, 0.25f);
    MR::createScreenAlphaSceneObj(4, 0.25f);
}

namespace {
    // The fill's materials (nebula_game_api.h NebulaMaterialProgram), by alpha order: register 0's colour, its alpha where
    // the screen alpha mask (texture 0) compares above konst 0's; drawn where above zero, depth always passing and
    // written, setupDrawTexture's coordinate (texture matrix 0), no culling, no fog, no dither.
    NebulaMaterialProgram sFillPrograms[2];
    bool sFillRegistered[2] = {false, false};

    void sendFillProgram(bool textureFirst) {
        NebulaMaterialProgram& rProgram = sFillPrograms[textureFirst ? 1 : 0];
        const NebulaHostServices* pHost = nebula_host();
        if (!sFillRegistered[textureFirst ? 1 : 0]) {
            memset(&rProgram, 0, sizeof(rProgram));
            NebulaMaterialTevStage& rStage = rProgram.tev[0];
            rStage.tex_coord = GX_TEXCOORD0;
            rStage.tex_map = GX_TEXMAP0;
            rStage.channel = GX_COLOR0A0;
            rStage.color_in[0] = GX_CC_C0;
            rStage.color_in[1] = rStage.color_in[2] = rStage.color_in[3] = GX_CC_ZERO;
            rStage.color_op = GX_TEV_ADD;
            rStage.alpha_in[0] = textureFirst ? GX_CA_TEXA : GX_CA_KONST;
            rStage.alpha_in[1] = textureFirst ? GX_CA_KONST : GX_CA_TEXA;
            rStage.alpha_in[2] = GX_CA_A0;
            rStage.alpha_in[3] = GX_CA_ZERO;
            rStage.alpha_op = static_cast< u8 >(::sAlphaTevOperater);
            rStage.color_scale = rStage.alpha_scale = GX_CS_SCALE_1;
            rStage.color_clamp = rStage.alpha_clamp = GX_TRUE;
            rStage.color_reg = rStage.alpha_reg = GX_TEVPREV;
            rProgram.tex_gen[0].type = GX_TG_MTX2x4;
            rProgram.tex_gen[0].source = GX_TG_TEX0;
            rProgram.tex_gen[0].matrix = GX_TEXMTX0;
            rProgram.tex_gen[0].post_matrix = GX_PTIDENTITY;
            rProgram.tev_stages = 1;
            rProgram.tex_gens = 1;
            rProgram.channels = 1;
            rProgram.cull = GX_CULL_NONE;
            rProgram.dither = GX_FALSE;
            rProgram.z_compare_location = 0;
            rProgram.loads = NEBULA_MATERIAL_LOADS_TEV | NEBULA_MATERIAL_LOADS_FOG | NEBULA_MATERIAL_LOADS_DITHER;
            rProgram.fog.type = GX_FOG_NONE;
            rProgram.alpha_compare.comp0 = GX_GREATER;
            rProgram.alpha_compare.op = GX_AOP_AND;
            rProgram.alpha_compare.comp1 = GX_ALWAYS;
            rProgram.blend.type = static_cast< u8 >(::sFillBlendMode);
            rProgram.blend.source = static_cast< u8 >(::sFillSrcFactor);
            rProgram.blend.destination = static_cast< u8 >(::sFillDstFactor);
            rProgram.blend.logic_op = static_cast< u8 >(::sFillLogicOp);
            rProgram.z_mode.enable = GX_TRUE;
            rProgram.z_mode.func = GX_ALWAYS;
            rProgram.z_mode.update = GX_TRUE;
            pHost->material_program(pHost->user, reinterpret_cast< uintptr_t >(&rProgram), &rProgram);
            sFillRegistered[textureFirst ? 1 : 0] = true;
        }
        NebulaMaterialValues values;
        memset(&values, 0, sizeof(values));
        const JUTTexture* pMask = MR::getScreenAlphaTexture(2);
        values.tex_coord_scale[0].scale_s = pMask->getWidth();
        values.tex_coord_scale[0].scale_t = pMask->getHeight();
        J3DNativeShape::flushState();
        pHost->material(pHost->user, reinterpret_cast< uintptr_t >(&rProgram), &values);
        static const u8 kPlaces[8] = {GX_TEXMTX0, GX_IDENTITY, GX_IDENTITY, GX_IDENTITY,
                                      GX_IDENTITY, GX_IDENTITY, GX_IDENTITY, GX_IDENTITY};
        J3DNativeShape::setCurrent(GX_PNMTX0, kPlaces);
    }
}  // namespace

void FallOutFieldDraw::setUpFillScreen() const {
    J3DNativeState::setColorUpdate(GX_TRUE);
    J3DNativeState::setAlphaUpdate(GX_FALSE);
    J3DNativeView::setZScaleOffset(0.0f, 1.0f);
    sendFillProgram(_C);
    J3DNativeState::setTevKColor(GX_KCOLOR0, Color8(0, 0, 0, 1));
    J3DNativeState::setTevKAlphaSel(GX_TEVSTAGE0, GX_TEV_KASEL_K0_A);
}

namespace {
    // The passes' stages (ImageEffectLocalUtil::setCallerStage): the alpha mask by comparing the screen's alpha with
    // konst 0 (the order by the draw's arg 0), the mask's alpha alone (the blur), the edge colour over the mask.
    NebulaMaterialTevStage sMaskStages[2];
    NebulaMaterialTevStage sAlphaStage;
    NebulaMaterialTevStage sEdgeStage;
    bool sStagesMade = false;

    void makeStage(NebulaMaterialTevStage& rStage, u8 c0, u8 c1, u8 c2, u8 c3, u8 colorOp, u8 a0, u8 a1, u8 a2, u8 a3,
                   u8 alphaOp) {
        memset(&rStage, 0, sizeof(rStage));
        rStage.tex_coord = GX_TEXCOORD0;
        rStage.tex_map = GX_TEXMAP0;
        rStage.channel = GX_COLOR0A0;
        rStage.color_in[0] = c0;
        rStage.color_in[1] = c1;
        rStage.color_in[2] = c2;
        rStage.color_in[3] = c3;
        rStage.color_op = colorOp;
        rStage.alpha_in[0] = a0;
        rStage.alpha_in[1] = a1;
        rStage.alpha_in[2] = a2;
        rStage.alpha_in[3] = a3;
        rStage.alpha_op = alphaOp;
        rStage.color_scale = rStage.alpha_scale = GX_CS_SCALE_1;
        rStage.color_clamp = rStage.alpha_clamp = GX_TRUE;
        rStage.color_reg = rStage.alpha_reg = GX_TEVPREV;
    }

    void makeStages() {
        if (sStagesMade) {
            return;
        }
        makeStage(sMaskStages[0], GX_CC_KONST, GX_CC_TEXA, GX_CC_ZERO, GX_CC_ZERO, GX_TEV_COMP_RGB8_GT, GX_CA_KONST,
                  GX_CA_TEXA, GX_CA_A0, GX_CA_ZERO, GX_TEV_COMP_A8_GT);
        makeStage(sMaskStages[1], GX_CC_TEXA, GX_CC_KONST, GX_CC_ZERO, GX_CC_ZERO, GX_TEV_COMP_RGB8_GT, GX_CA_TEXA,
                  GX_CA_KONST, GX_CA_A0, GX_CA_ZERO, GX_TEV_COMP_A8_GT);
        makeStage(sAlphaStage, GX_CC_TEXA, GX_CC_ZERO, GX_CC_ZERO, GX_CC_ZERO, GX_TEV_ADD, GX_CA_TEXA, GX_CA_ZERO,
                  GX_CA_ZERO, GX_CA_ZERO, GX_TEV_ADD);
        makeStage(sEdgeStage, GX_CC_C1, GX_CC_ZERO, GX_CC_ZERO, GX_CC_ZERO, GX_TEV_ADD, GX_CA_ZERO, GX_CA_A1, GX_CA_TEXA,
                  GX_CA_ZERO, GX_TEV_ADD);
        sStagesMade = true;
    }
}  // namespace

void FallOutFieldDraw::setUpEdgeAndClearAlpha() const {
    J3DNativeState::setTevColor(GX_TEVREG1, ::sEdgeColor);
    J3DNativeState::setColorUpdate(GX_TRUE);
    J3DNativeState::setAlphaUpdate(GX_TRUE);
    J3DNativeState::setDstAlpha(GX_TRUE, 0);
}

void FallOutFieldDraw::init(const JMapInfoIter& rIter) {
    if (!MR::isValidInfo(rIter)) {
        return;
    }

    bool arg0;
    MR::getJMapInfoArg0WithInit(rIter, &arg0);
    _C = arg0;

    if (MR::isExistStageSwitchAppear(rIter)) {
        MR::listenNameObjStageSwitchOnOffAppear(this, MR::createStageSwitchCtrl(this, rIter), MR::Functor(this, &FallOutFieldDraw::activate),
                                                MR::Functor(this, &FallOutFieldDraw::deactivate));
        MR::deactivateClipArea();
    } else {
        MR::activateClipArea();
    }
}

void FallOutFieldDraw::activate() {
    MR::activateClipArea();
}

void FallOutFieldDraw::deactivate() {
    MR::deactivateClipArea();
}

void FallOutFieldDraw::draw() const {
    if (!MR::isActiveClipArea()) {
        return;
    }

    ImageEffectLocalUtil::setupDrawTexture();

    JUTTexture* pScreenAlphaTexture2 = MR::getScreenAlphaTexture(2);
    JUTTexture* pScreenAlphaTexture3 = MR::getScreenAlphaTexture(3);
    JUTTexture* pScreenAlphaTexture4 = MR::getScreenAlphaTexture(4);

    ImageEffectLocalUtil::capture(pScreenAlphaTexture2, 0, 1, GX_CTF_A8, true, 0, true);

    J3DNativeState::setTevKColor(GX_KCOLOR0, Color8(0, 0, 0, 1));
    J3DNativeState::setTevKAlphaSel(GX_TEVSTAGE0, GX_TEV_KASEL_K0_A);
    J3DNativeState::setTevKColorSel(GX_TEVSTAGE0, GX_TEV_KCSEL_K0_A);
    J3DNativeState::setTevColor(GX_TEVREG0, Color8(255, 255, 255, 255));

    J3DNativeState::setColorUpdate(GX_FALSE);
    J3DNativeState::setAlphaUpdate(GX_TRUE);
    J3DNativeState::setDstAlpha(GX_FALSE, 0);
    J3DNativeView::setZScaleOffset(0.0f, 0.0f);
    makeStages();
    ImageEffectLocalUtil::setCallerStage(&sMaskStages[_C ? 1 : 0]);

    ImageEffectLocalUtil::drawTexture(pScreenAlphaTexture2, 2, 0, 255, ImageEffectLocalUtil::TexDrawType_0);
    ImageEffectLocalUtil::capture(pScreenAlphaTexture3, 2, 0, GX_CTF_A8, true, 0, true);

    ImageEffectLocalUtil::setCallerStage(&sAlphaStage);

    ImageEffectLocalUtil::blurTexture(pScreenAlphaTexture3, 4, 4, 5, 0.004f, 1.0f);
    ImageEffectLocalUtil::capture(pScreenAlphaTexture4, 4, 4, GX_CTF_A8, false, 0, true);
    setUpEdgeAndClearAlpha();
    ImageEffectLocalUtil::setCallerStage(&sEdgeStage);
    ImageEffectLocalUtil::drawTexture(pScreenAlphaTexture4, 1, 0, 255, ImageEffectLocalUtil::TexDrawType_2);
    MR::loadScreenAlphaTexture(2, GX_TEXMAP0);
    setUpFillScreen();

    ::drawFillScreen(::sFillColor);

    MR::loadViewMtx();
    MR::loadProjectionMtx();

    J3DNativeState::setColorUpdate(GX_TRUE);
    J3DNativeState::setAlphaUpdate(GX_TRUE);
    J3DNativeState::setDstAlpha(GX_TRUE, 0);
    J3DNativeView::setZScaleOffset(1.0f, 0.0f);
}

namespace MR {
    NameObj* createFallOutFieldDraw(const char*) {
        return MR::createSceneObj(SceneObj_FallOutFieldDraw);
    }
};  // namespace MR
