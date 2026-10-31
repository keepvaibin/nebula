#include "Game/MapObj/ClipFieldFillDraw.hpp"
#include <JSystem/J3DGraphBase/J3DNativeState.hpp>
#include <JSystem/J3DGraphBase/J3DNativeView.hpp>
#include <JSystem/J3DGraphBase/J3DNativeShape.hpp>
#include <JSystem/J3DGraphBase/J3DNativeDraw.hpp>
#include "Game/Scene/SceneFunction.hpp"
#include "Game/Scene/SceneObjHolder.hpp"
#include "Game/Screen/ScreenAlphaCapture.hpp"
#include "Game/Util/CameraUtil.hpp"
#include "Game/Util/DirectDraw.hpp"
#include "Game/Util/DrawUtil.hpp"
#include "Game/Util/JMapUtil.hpp"
#include "Game/Util/ObjUtil.hpp"
#include "Game/Util/ScreenUtil.hpp"
#include <JSystem/JUtility/JUTTexture.hpp>
#include <nebula_game_api.h>
#include <cstring>

extern "C" const NebulaHostServices* nebula_host(void);

namespace {
    // The fill's materials (nebula_game_api.h NebulaMaterialProgram), by alpha order and blend: register 0's colour,
    // its alpha where the screen alpha mask (texture 0) compares above konst 0's; drawn where that alpha is above zero,
    // depth tested always and not written, back faces culled. Registered when an instance first draws with one.
    struct FillProgram {
        u32 mKey;
        NebulaMaterialProgram mProgram;
    };
    FillProgram sFillPrograms[8];
    u32 sFillProgramCount = 0;

    void sendFillProgram(bool textureFirst, u8 blendType, u8 source, u8 destination, u8 logicOp) {
        const u32 key = 1u << 31 | static_cast< u32 >(textureFirst) << 24 | static_cast< u32 >(blendType) << 18 |
                        static_cast< u32 >(source) << 12 | static_cast< u32 >(destination) << 6 | logicOp;
        FillProgram* pEntry = nullptr;
        for (u32 i = 0; i < sFillProgramCount; i++) {
            if (sFillPrograms[i].mKey == key) {
                pEntry = &sFillPrograms[i];
            }
        }
        const NebulaHostServices* pHost = nebula_host();
        if (pEntry == nullptr) {
            pEntry = &sFillPrograms[sFillProgramCount < 8 ? sFillProgramCount++ : 7];
            pEntry->mKey = key;
            NebulaMaterialProgram& rProgram = pEntry->mProgram;
            memset(&rProgram, 0, sizeof(rProgram));
            NebulaMaterialTevStage& rStage = rProgram.tev[0];
            rStage.tex_coord = GX_TEXCOORD0;
            rStage.tex_map = GX_TEXMAP0;
            rStage.channel = GX_COLOR_NULL;
            rStage.color_in[0] = GX_CC_C0;
            rStage.color_in[1] = rStage.color_in[2] = rStage.color_in[3] = GX_CC_ZERO;
            rStage.color_op = GX_TEV_ADD;
            rStage.alpha_in[0] = textureFirst ? GX_CA_TEXA : GX_CA_KONST;
            rStage.alpha_in[1] = textureFirst ? GX_CA_KONST : GX_CA_TEXA;
            rStage.alpha_in[2] = GX_CA_A0;
            rStage.alpha_in[3] = GX_CA_ZERO;
            rStage.alpha_op = GX_TEV_COMP_RGB8_GT;
            rStage.color_scale = rStage.alpha_scale = GX_CS_SCALE_1;
            rStage.color_clamp = rStage.alpha_clamp = GX_TRUE;
            rStage.color_reg = rStage.alpha_reg = GX_TEVPREV;
            rProgram.tex_gen[0].type = GX_TG_MTX2x4;
            rProgram.tex_gen[0].source = GX_TG_TEX0;
            rProgram.tex_gen[0].matrix = GX_IDENTITY;
            rProgram.tex_gen[0].post_matrix = GX_PTIDENTITY;
            rProgram.tev_stages = 1;
            rProgram.tex_gens = 1;
            rProgram.channels = 1;
            rProgram.cull = GX_CULL_BACK;
            rProgram.dither = GX_FALSE;
            rProgram.z_compare_location = 0;
            rProgram.loads = NEBULA_MATERIAL_LOADS_TEV | NEBULA_MATERIAL_LOADS_DITHER;
            rProgram.alpha_compare.comp0 = GX_GREATER;
            rProgram.alpha_compare.op = GX_AOP_AND;
            rProgram.alpha_compare.comp1 = GX_ALWAYS;
            rProgram.blend.type = blendType;
            rProgram.blend.source = source;
            rProgram.blend.destination = destination;
            rProgram.blend.logic_op = logicOp;
            rProgram.z_mode.enable = GX_TRUE;
            rProgram.z_mode.func = GX_ALWAYS;
            rProgram.z_mode.update = GX_FALSE;
            pHost->material_program(pHost->user, reinterpret_cast< uintptr_t >(pEntry), &rProgram);
        }
        NebulaMaterialValues values;
        memset(&values, 0, sizeof(values));
        const JUTTexture* pMask = MR::getScreenAlphaTexture(1);
        values.tex_coord_scale[0].scale_s = pMask->getWidth();
        values.tex_coord_scale[0].scale_t = pMask->getHeight();
        J3DNativeShape::flushState();
        pHost->material(pHost->user, reinterpret_cast< uintptr_t >(pEntry), &values);
        static const u8 kPlaces[8] = {GX_IDENTITY, GX_IDENTITY, GX_IDENTITY, GX_IDENTITY,
                                      GX_IDENTITY, GX_IDENTITY, GX_IDENTITY, GX_IDENTITY};
        J3DNativeShape::setCurrent(GX_PNMTX0, kPlaces);
    }
}  // namespace

ClipFieldFillDraw::ClipFieldFillDraw(const char* pName)
    : NameObj(pName), mFillBlendMode(GX_BM_BLEND), mFillSrcFactor(GX_BL_SRCALPHA), mFillDstFactor(GX_BL_ONE), mFillLogicOp(GX_LO_NOOP),
      _1C(255, 230, 80, 200), _20(255, 255, 255, 255), _24(), _25() {
    MR::connectToScene(this, MR::MovementType_None, MR::CalcAnimType_None, MR::DrawBufferType_None, MR::DrawType_ClipFieldFillDraw);
    MR::createScreenAlphaSceneObj(1, 1.0f);
}

void ClipFieldFillDraw::init(const JMapInfoIter& rIter) {
    if (MR::isValidInfo(rIter)) {
        return;
    }
}

void ClipFieldFillDraw::setModeSubColor() {
    mFillBlendMode = GX_BM_SUBTRACT;
    mFillDstFactor = GX_BL_ONE;
    mFillSrcFactor = GX_BL_ONE;
    mFillLogicOp = GX_LO_NOOP;
}

void ClipFieldFillDraw::setUpFillScreen() const {
    Mtx44 projectionMtx;
    NebulaMtxOrtho(projectionMtx, 0.0f, MR::getScreenHeight(), 0.0f, MR::getScreenWidth(), -1.0f, 1.0f);
    J3DNativeView::setProjection(projectionMtx, GX_ORTHOGRAPHIC);

    TMtx34f posMtx;
    posMtx.identity();
    J3DNativeShape::setPosition(0, (MtxPtr)&posMtx);
    J3DNativeState::setCurrentMtx(GX_PNMTX0);

    J3DNativeState::setChanCtrl(GX_COLOR0A0, GX_FALSE, GX_SRC_REG, GX_SRC_REG, GX_LIGHT_NULL, GX_DF_NONE, GX_AF_NONE);
    J3DNativeState::setChanCtrl(GX_COLOR1A1, GX_FALSE, GX_SRC_REG, GX_SRC_REG, GX_LIGHT_NULL, GX_DF_NONE, GX_AF_NONE);
    J3DNativeState::setColorUpdate(GX_TRUE);
    J3DNativeState::setAlphaUpdate(GX_FALSE);
    sendFillProgram(_25, static_cast< u8 >(mFillBlendMode), static_cast< u8 >(mFillSrcFactor), static_cast< u8 >(mFillDstFactor),
                    static_cast< u8 >(mFillLogicOp));
    J3DNativeState::setTevKColor(GX_KCOLOR0, Color8(0, 0, 0, 1));
    J3DNativeState::setTevKAlphaSel(GX_TEVSTAGE0, GX_TEV_KASEL_K0_A);
}

void ClipFieldFillDraw::sendFillScreen(GXColor color) const {
    J3DNativeState::setTevColor(GX_TEVREG0, color);

    J3DNativeDraw::begin(GX_TRIANGLESTRIP, 4, NEBULA_SHAPE_TEXCOORD0);
    {
        u16 screenWidth = MR::getScreenWidth();
        u16 screenHeight = MR::getScreenHeight();

        J3DNativeDraw::position(0.0f, 0.0f, 0.0f);
        J3DNativeDraw::texCoord(0.0f, 0.0f);

        J3DNativeDraw::position(static_cast< f32 >(static_cast< u16 >(screenWidth)), 0.0f, 0.0f);
        J3DNativeDraw::texCoord(1.0f, 0.0f);

        J3DNativeDraw::position(0.0f, static_cast< f32 >(static_cast< u16 >(screenHeight)), 0.0f);
        J3DNativeDraw::texCoord(0.0f, 1.0f);

        J3DNativeDraw::position(static_cast< f32 >(static_cast< u16 >(screenWidth)), static_cast< f32 >(static_cast< u16 >(screenHeight)), 0.0f);
        J3DNativeDraw::texCoord(1.0f, 1.0f);
    }
}

void ClipFieldFillDraw::draw() const {
    MR::loadScreenAlphaTexture(1, GX_TEXMAP0);
    setUpFillScreen();
    sendFillScreen(_1C);
    MR::loadViewMtx();
    MR::loadProjectionMtx();
    J3DNativeView::setZScaleOffset(1.0f, 0.0f);
    MR::clearAlphaBuffer(0);
    MR::loadViewMtx();
    MR::loadProjectionMtx();
    J3DNativeState::setAlphaUpdate(GX_FALSE);
    J3DNativeState::setColorUpdate(GX_TRUE);

    if (_24) {
        TDDraw::setup(0, 1, 0);
        J3DNativeState::setCullMode(GX_CULL_FRONT);
        J3DNativeState::setZMode(GX_TRUE, GX_LEQUAL, GX_FALSE);
    }
}

namespace MR {
    NameObj* createClipFieldFillDraw(const char* pParam1) {
        return static_cast< ClipFieldFillDraw* >(MR::createSceneObj(SceneObj_ClipFieldFillDraw));
    }

    NameObj* createVolumeDrawSpotLight(const char* pParam1) {
        ClipFieldFillDraw* clipFieldFillDraw;

        clipFieldFillDraw = static_cast< ClipFieldFillDraw* >(createClipFieldFillDraw(pParam1));
        clipFieldFillDraw->_25 = false;
        clipFieldFillDraw->_24 = false;
        clipFieldFillDraw->setModeSubColor();
        clipFieldFillDraw->_1C = static_cast< u32 >(Color8(70, 35, 11, 80));
        clipFieldFillDraw->_20 = static_cast< u32 >(Color8(255, 255, 128, 80));

        return clipFieldFillDraw;
    }
};  // namespace MR
