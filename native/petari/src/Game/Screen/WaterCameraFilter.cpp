#include "Game/Screen/WaterCameraFilter.hpp"
#include <JSystem/J3DGraphBase/J3DNativeState.hpp>
#include <JSystem/J3DGraphBase/J3DNativeView.hpp>
#include <JSystem/J3DGraphBase/J3DNativeShape.hpp>
#include <JSystem/J3DGraphBase/J3DNativeDraw.hpp>
#include "Game/LiveActor/Nerve.hpp"
#include "Game/Map/WaterAreaHolder.hpp"
#include "Game/Scene/SceneFunction.hpp"
#include "Game/Screen/MissLayout.hpp"
#include "Game/Util/CameraUtil.hpp"
#include "Game/Util/DemoUtil.hpp"
#include "Game/Util/LiveActorUtil.hpp"
#include "Game/Util/MathUtil.hpp"
#include "Game/Util/MemoryUtil.hpp"
#include "Game/Util/MtxUtil.hpp"
#include "Game/Util/ObjUtil.hpp"
#include "Game/Util/PlayerUtil.hpp"
#include "Game/Util/ScreenUtil.hpp"
#include <JSystem/JGeometry/TMatrix.hpp>
#include <JSystem/JKernel/JKRSolidHeap.hpp>
#include <JSystem/JUtility/JUTTexture.hpp>
#include <JSystem/JUtility/JUTVideo.hpp>
#include <revolution/gx/GXEnum.h>
#include <revolution/gx/GXStruct.h>
#include <nebula_game_api.h>
#include <cstring>

extern "C" const NebulaHostServices* nebula_host(void);

namespace {
    // The filter's material (nebula_game_api.h NebulaMaterialProgram): the screen texture (1, coordinate 1) offset by
    // the filter texture (0, coordinate 0 through texture matrix 0) as a signed indirect warp, minus register 0's
    // colour; blended by register 0's alpha where it is above zero, depth neither tested nor written.
    NebulaMaterialProgram sFilterProgram;
    bool sFilterRegistered = false;

    void sendFilterProgram(JUTTexture* pFilter, JUTTexture* pScreen) {
        const NebulaHostServices* pHost = nebula_host();
        if (!sFilterRegistered) {
            NebulaMaterialProgram& rProgram = sFilterProgram;
            memset(&rProgram, 0, sizeof(rProgram));
            NebulaMaterialTevStage& rStage = rProgram.tev[0];
            rStage.tex_coord = GX_TEXCOORD1;
            rStage.tex_map = GX_TEXMAP1;
            rStage.channel = GX_COLOR_NULL;
            rStage.color_in[0] = GX_CC_C0;
            rStage.color_in[1] = rStage.color_in[2] = GX_CC_ZERO;
            rStage.color_in[3] = GX_CC_TEXC;
            rStage.color_op = GX_TEV_SUB;
            rStage.alpha_in[0] = GX_CA_A0;
            rStage.alpha_in[1] = rStage.alpha_in[2] = rStage.alpha_in[3] = GX_CA_ZERO;
            rStage.alpha_op = GX_TEV_ADD;
            rStage.color_scale = rStage.alpha_scale = GX_CS_SCALE_1;
            rStage.color_clamp = rStage.alpha_clamp = GX_TRUE;
            rStage.color_reg = rStage.alpha_reg = GX_TEVPREV;
            rStage.ind_stage = GX_INDTEXSTAGE0;
            rStage.ind_format = GX_ITF_8;
            rStage.ind_bias = GX_ITB_STU;
            rStage.ind_matrix = GX_ITM_0;
            rStage.ind_wrap_s = rStage.ind_wrap_t = GX_ITW_OFF;
            rStage.ind_alpha = GX_ITBA_OFF;
            rProgram.ind_order[0].coord = GX_TEXCOORD0;
            rProgram.ind_order[0].map = GX_TEXMAP0;
            rProgram.ind_order[0].scale_s = rProgram.ind_order[0].scale_t = GX_ITS_1;
            rProgram.tex_gen[0].type = GX_TG_MTX3x4;
            rProgram.tex_gen[0].source = GX_TG_TEX0;
            rProgram.tex_gen[0].matrix = GX_TEXMTX0;
            rProgram.tex_gen[0].post_matrix = GX_PTIDENTITY;
            rProgram.tex_gen[1].type = GX_TG_MTX3x4;
            rProgram.tex_gen[1].source = GX_TG_TEX1;
            rProgram.tex_gen[1].matrix = GX_IDENTITY;
            rProgram.tex_gen[1].post_matrix = GX_PTIDENTITY;
            rProgram.tev_stages = 1;
            rProgram.tex_gens = 2;
            rProgram.ind_stages = 1;
            rProgram.channels = 1;
            rProgram.cull = GX_CULL_NONE;
            rProgram.z_compare_location = 0;
            rProgram.loads = NEBULA_MATERIAL_LOADS_TEV | NEBULA_MATERIAL_LOADS_INDIRECT;
            rProgram.alpha_compare.comp0 = rProgram.alpha_compare.comp1 = GX_GREATER;
            rProgram.alpha_compare.op = GX_AOP_OR;
            rProgram.blend.type = GX_BM_BLEND;
            rProgram.blend.source = GX_BL_SRCALPHA;
            rProgram.blend.destination = GX_BL_INVSRCALPHA;
            rProgram.blend.logic_op = GX_LO_NOOP;
            rProgram.z_mode.enable = GX_FALSE;
            rProgram.z_mode.func = GX_ALWAYS;
            rProgram.z_mode.update = GX_FALSE;
            pHost->material_program(pHost->user, reinterpret_cast< uintptr_t >(&sFilterProgram), &rProgram);
            sFilterRegistered = true;
        }
        NebulaMaterialValues values;
        memset(&values, 0, sizeof(values));
        values.ind_matrix[0].m[0][0] = 0.05f;
        values.ind_matrix[0].m[1][1] = 0.05f;
        values.tex_coord_scale[0].scale_s = pFilter->getWidth();
        values.tex_coord_scale[0].scale_t = pFilter->getHeight();
        values.tex_coord_scale[1].scale_s = pScreen->getWidth();
        values.tex_coord_scale[1].scale_t = pScreen->getHeight();
        J3DNativeShape::flushState();
        pHost->material(pHost->user, reinterpret_cast< uintptr_t >(&sFilterProgram), &values);
        static const u8 kPlaces[8] = {GX_TEXMTX0, GX_IDENTITY, GX_IDENTITY, GX_IDENTITY,
                                      GX_IDENTITY, GX_IDENTITY, GX_IDENTITY, GX_IDENTITY};
        J3DNativeShape::setCurrent(GX_PNMTX0, kPlaces);
    }
}  // namespace

namespace NrvWaterCameraFilter {
    NEW_NERVE(WaterCameraFilterNrvAir, WaterCameraFilter, Air);
    NEW_NERVE(WaterCameraFilterNrvAirToWater, WaterCameraFilter, AirToWater);
    NEW_NERVE(WaterCameraFilterNrvWater, WaterCameraFilter, Water);
    NEW_NERVE(WaterCameraFilterNrvWaterToAir, WaterCameraFilter, WaterToAir);
};  // namespace NrvWaterCameraFilter

namespace {
    s32 getFrameBufferWidth() {
        return MR::getFrameBufferWidth();
    }

    s32 getScreenHeight() {
        return MR::getScreenHeight();
    }
};  // namespace

static GXColor sDefaultColor;

WaterCameraFilter::WaterCameraFilter()
    : LiveActor("\220\205\222\206\203\112\203\201\203\211\203\164\203\102\203\213\203\136\201\133" /* 水中カメラフィルター */), mWaterAlpha(), mWavePhase(), mWaterColor(::sDefaultColor), mScreenTex(), mFilterTex() {
}

void WaterCameraFilter::init(const JMapInfoIter& rIter) {
    MR::connectToScene(this, MR::MovementType_MapObj, MR::CalcAnimType_None, MR::DrawBufferType_None, MR::DrawType_WaterCameraFilter);
    MR::invalidateClipping(this);
    initNerve(GET_NERVE(WaterCameraFilter, WaterCameraFilterNrvAir));
    initScreenTex();
    JUTTexture* pFilterTex = new JUTTexture(MR::loadTexFromArc("WaterCameraFilter.arc", "WaterCameraFilter.bti"), 0);
    mFilterTex = pFilterTex;
    makeActorAppeared();
}

namespace {
    s32 getScreenTexHeight() {
        return MR::getScreenHeight();
    }
};  // namespace

void WaterCameraFilter::initScreenTex() {
    MR::CurrentHeapRestorer restorer(MR::getSceneHeapGDDR3());
    mScreenTex = new JUTTexture(::getFrameBufferWidth(), ::getScreenHeight(), GX_TF_RGB565);
}

void WaterCameraFilter::exeAirToWater() {
    if (!MR::isCameraInWater()) {
        setNerve(GET_NERVE(WaterCameraFilter, WaterCameraFilterNrvWaterToAir));
    } else {
        mWaterAlpha += 20.0f;
        if (MR::isDemoActive() || mWaterAlpha >= 255.0f) {
            mWaterAlpha = 255.0f;
            setNerve(GET_NERVE(WaterCameraFilter, WaterCameraFilterNrvWater));
        }
    }
}

void WaterCameraFilter::exeWaterToAir() {
    if (MR::isCameraInWater()) {
        setNerve(GET_NERVE(WaterCameraFilter, WaterCameraFilterNrvAirToWater));
    } else {
        mWaterAlpha -= 20.0f;
        if (MR::isDemoActive() || mWaterAlpha <= 0.0f) {
            mWaterAlpha = 0.0f;
            setNerve(GET_NERVE(WaterCameraFilter, WaterCameraFilterNrvAir));
        }
    }
}

static GXColor sWaterColor = {0x32, 0x32, 0, 0};

void WaterCameraFilter::control() {
    if (MR::isCameraInWater()) {
        mWavePhase += 0.5f;

        if (mWavePhase > 360.0f) {
            mWavePhase -= 360.0f;
        }

        f32 depth = MR::clamp(WaterAreaFunction::getCameraWaterDepth(), 0.0f, 3000.0f);
        Color8 color(MR::lerp(::sDefaultColor, ::sWaterColor, depth / 3000.0f));
        mWaterColor.mGXColor = color.mGXColor;
    }
}

void WaterCameraFilter::draw() const {
    if (isNerve(GET_NERVE(WaterCameraFilter, WaterCameraFilterNrvAir))) {
        return;
    }

    GXRenderModeObj* pRenderMode = JUTVideo::getManager()->getRenderMode();
    J3DNativeState::setCopyFilter(GX_FALSE, pRenderMode->sample_pattern, GX_FALSE, pRenderMode->vfilter);
    mScreenTex->capture(0, 0, GX_TF_RGB565, false, 0);
    J3DNativeState::setCopyFilter(GX_FALSE, pRenderMode->sample_pattern, GX_TRUE, pRenderMode->vfilter);
    loadMaterial();
    TVec2f pos;
    pos.x = 0.0f;
    pos.y = 0.0f;
    TVec3f* pPlayerCenter = MR::getPlayerCenterPos();
    MR::calcScreenPosition(&pos, *pPlayerCenter);
    pos.x /= MR::getScreenWidth();
    pos.y /= MR::getScreenHeight();

    f32 halfSize = 0.5f;
    f32 centerU = 1.0f - pos.x;
    f32 centerV = 1.0f - pos.y;

    J3DNativeDraw::begin(GX_TRIANGLESTRIP, 4, NEBULA_SHAPE_TEXCOORD0 | NEBULA_SHAPE_TEXCOORD0 << 1);
    {
        J3DNativeDraw::position(0.0f, 0.0f, 0.0f);
        J3DNativeDraw::texCoord(centerU - halfSize, centerV - halfSize);
        J3DNativeDraw::texCoord(0.0f, 0.0f);

        J3DNativeDraw::position(static_cast< f32 >(static_cast< u16 >(MR::getFrameBufferWidth())), 0.0f, 0.0f);
        J3DNativeDraw::texCoord(centerU + halfSize, centerV - halfSize);
        J3DNativeDraw::texCoord(1.0f, 0.0f);

        J3DNativeDraw::position(0.0f, static_cast< f32 >(static_cast< u16 >(MR::getScreenHeight())), 0.0f);
        J3DNativeDraw::texCoord(centerU - halfSize, centerV + halfSize);
        J3DNativeDraw::texCoord(0.0f, 1.0f);

        s32 height = MR::getScreenHeight();
        J3DNativeDraw::position(static_cast< f32 >(static_cast< u16 >(MR::getFrameBufferWidth())), static_cast< f32 >(static_cast< u16 >(height)), 0.0f);
        J3DNativeDraw::texCoord(centerU + halfSize, centerV + halfSize);
        J3DNativeDraw::texCoord(1.0f, 1.0f);
    }


    MR::loadProjectionMtx();
    MR::loadViewMtx();
}

void WaterCameraFilter::loadMaterial() const {
    Mtx44 projection;
    s32 height = MR::getScreenHeight();
    NebulaMtxOrtho(projection, 0.0f, height, 0.0f, MR::getFrameBufferWidth(), -10000.0f, 10000.0f);
    J3DNativeView::setProjection(projection, GX_ORTHOGRAPHIC);
    TMtx34f modelView;
    modelView.identity();
    J3DNativeShape::setPosition(0, modelView);
    J3DNativeState::setCurrentMtx(0);
    J3DNativeState::setChanCtrl(GX_COLOR0A0, GX_FALSE, GX_SRC_REG, GX_SRC_REG, 0, GX_DF_NONE, GX_AF_NONE);
    J3DNativeState::setChanCtrl(GX_COLOR1A1, GX_FALSE, GX_SRC_REG, GX_SRC_REG, 0, GX_DF_NONE, GX_AF_NONE);
    TVec2f screenPos;
    screenPos.x = 0.0f;
    screenPos.y = 0.0f;
    MR::calcScreenPosition(&screenPos, *MR::getPlayerCenterPos());
    screenPos.x /= MR::getScreenWidth();
    screenPos.y /= MR::getScreenHeight();
    TVec3f screenPosNorm;
    MR::calcNormalizedScreenPosition(&screenPosNorm, *MR::getPlayerCenterPos());
    TMtx34f textureMtx;
    textureMtx.identity();
    TPos3f translation;
    translation.identity();
    translation.setTrans(TVec3f(-0.5f, -0.5f, 0.0f));
    TPos3f rotation;
    rotation.identity();
    MR::makeMtxRotate(rotation, 0.0f, 0.0f, mWavePhase);
    MR::multMtx(textureMtx, translation, rotation);
    translation.setTrans(TVec3f(0.5f, 0.5f, 0.0f));
    MR::multMtx(textureMtx, textureMtx, translation);
    J3DNativeShape::setTexture(0, textureMtx, 3);
    mFilterTex->load(GX_TEXMAP0);
    mScreenTex->load(GX_TEXMAP1);
    sendFilterProgram(mFilterTex, mScreenTex);
    J3DNativeState::setTevColor(GX_TEVREG0, Color8(mWaterColor.r, mWaterColor.g, mWaterColor.b, (s32)(mWaterAlpha)));
    J3DNativeState::setClipMode(GX_CLIP_ENABLE);
}

void WaterCameraFilter::exeWater() {
    if (!MR::isCameraInWater()) {
        setNerve(GET_NERVE(WaterCameraFilter, WaterCameraFilterNrvWaterToAir));
    }
}

void WaterCameraFilter::exeAir() {
    if (MR::isCameraInWater()) {
        setNerve(GET_NERVE(WaterCameraFilter, WaterCameraFilterNrvAirToWater));
    }
}
