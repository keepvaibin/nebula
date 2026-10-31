#include "Game/Map/OceanRingPipeInside.hpp"
#include <JSystem/J3DGraphBase/J3DNativeState.hpp>
#include <JSystem/J3DGraphBase/J3DNativeShape.hpp>
#include "Game/Map/OceanRingPipe.hpp"
#include "Game/Scene/SceneFunction.hpp"
#include "Game/Util/CameraUtil.hpp"
#include "Game/Util/LiveActorUtil.hpp"
#include "Game/Util/MathUtil.hpp"
#include "Game/Util/ObjUtil.hpp"
#include "Game/Util/SchedulerUtil.hpp"
#include <JSystem/JKernel/JKRHeap.hpp>
#include <JSystem/JUtility/JUTTexture.hpp>
#include <nebula_game_api.h>
#include <cstring>

extern "C" const NebulaHostServices* nebula_host(void);

namespace {
    // The pipe's inside (nebula_game_api.h NebulaMaterialProgram): the pipe texture (coordinate 0, texture matrix 0)
    // warped by itself (coordinate 1, texture matrix 1) as a signed indirect offset, register 0 to register 1 by its
    // colour and its alpha doubled, both scaled by four; blended by alpha, depth tested and not written, back faces
    // culled, fog off, no channels.
    NebulaMaterialProgram sInsideProgram;
    bool sInsideRegistered = false;

    void sendInsideProgram(JUTTexture* pTexture, f32 indirectScale) {
        const NebulaHostServices* pHost = nebula_host();
        if (!sInsideRegistered) {
            NebulaMaterialProgram& rProgram = sInsideProgram;
            memset(&rProgram, 0, sizeof(rProgram));
            NebulaMaterialTevStage& rStage = rProgram.tev[0];
            rStage.tex_coord = GX_TEXCOORD0;
            rStage.tex_map = GX_TEXMAP0;
            rStage.channel = GX_COLOR_NULL;
            rStage.color_in[0] = GX_CC_C0;
            rStage.color_in[1] = GX_CC_C1;
            rStage.color_in[2] = GX_CC_TEXC;
            rStage.color_in[3] = GX_CC_ZERO;
            rStage.alpha_in[0] = GX_CA_ZERO;
            rStage.alpha_in[1] = GX_CA_TEXA;
            rStage.alpha_in[2] = GX_CA_TEXA;
            rStage.alpha_in[3] = GX_CA_ZERO;
            rStage.color_op = rStage.alpha_op = GX_TEV_ADD;
            rStage.color_scale = rStage.alpha_scale = GX_CS_SCALE_4;
            rStage.color_clamp = rStage.alpha_clamp = GX_TRUE;
            rStage.color_reg = rStage.alpha_reg = GX_TEVPREV;
            rStage.ind_stage = GX_INDTEXSTAGE0;
            rStage.ind_format = GX_ITF_8;
            rStage.ind_bias = GX_ITB_STU;
            rStage.ind_matrix = GX_ITM_0;
            rStage.ind_wrap_s = rStage.ind_wrap_t = GX_ITW_OFF;
            rStage.ind_alpha = GX_ITBA_OFF;
            rProgram.ind_order[0].coord = GX_TEXCOORD1;
            rProgram.ind_order[0].map = GX_TEXMAP0;
            rProgram.ind_order[0].scale_s = rProgram.ind_order[0].scale_t = GX_ITS_1;
            for (u32 t = 0; t < 2; t++) {
                rProgram.tex_gen[t].type = GX_TG_MTX2x4;
                rProgram.tex_gen[t].source = static_cast< u8 >(GX_TG_TEX0 + t);
                rProgram.tex_gen[t].matrix = static_cast< u8 >(GX_TEXMTX0 + t * 3);
                rProgram.tex_gen[t].post_matrix = GX_PTIDENTITY;
            }
            rProgram.tev_stages = 1;
            rProgram.tex_gens = 2;
            rProgram.ind_stages = 1;
            rProgram.channels = 0;
            rProgram.cull = GX_CULL_BACK;
            rProgram.z_compare_location = 1;
            rProgram.loads = NEBULA_MATERIAL_LOADS_TEV | NEBULA_MATERIAL_LOADS_INDIRECT | NEBULA_MATERIAL_LOADS_FOG;
            rProgram.fog.type = GX_FOG_NONE;
            rProgram.alpha_compare.comp0 = rProgram.alpha_compare.comp1 = GX_ALWAYS;
            rProgram.alpha_compare.op = GX_AOP_OR;
            rProgram.blend.type = GX_BM_BLEND;
            rProgram.blend.source = GX_BL_SRCALPHA;
            rProgram.blend.destination = GX_BL_INVSRCALPHA;
            rProgram.blend.logic_op = GX_LO_COPY;
            rProgram.z_mode.enable = GX_TRUE;
            rProgram.z_mode.func = GX_LEQUAL;
            rProgram.z_mode.update = GX_FALSE;
            pHost->material_program(pHost->user, reinterpret_cast< uintptr_t >(&sInsideProgram), &rProgram);
            sInsideRegistered = true;
        }
        NebulaMaterialValues values;
        memset(&values, 0, sizeof(values));
        values.ind_matrix[0].m[0][0] = indirectScale;
        values.ind_matrix[0].m[1][1] = indirectScale;
        values.fog.near_z = MR::getNearZ();
        values.fog.far_z = MR::getFarZ();
        memset(values.fog.color, 0xFF, sizeof(values.fog.color));
        for (u32 c = 0; c < 2; c++) {
            values.tex_coord_scale[c].scale_s = pTexture->getWidth();
            values.tex_coord_scale[c].scale_t = pTexture->getHeight();
        }
        J3DNativeShape::flushState();
        pHost->material(pHost->user, reinterpret_cast< uintptr_t >(&sInsideProgram), &values);
        static const u8 kPlaces[8] = {GX_TEXMTX0, GX_TEXMTX1, GX_IDENTITY, GX_IDENTITY,
                                      GX_IDENTITY, GX_IDENTITY, GX_IDENTITY, GX_IDENTITY};
        J3DNativeShape::setCurrent(GX_PNMTX0, kPlaces);
    }
}  // namespace

namespace {
    const f32 sTexRateU0 = 0.08f;
    const f32 sTexRateV0 = 0.3f;
    const f32 sTexRateU1 = 0.04f;
    const f32 sTexRateV1 = 0.3f;
    const f32 sTexSpeedU0 = -0.006f;
    const f32 sTexSpeedU1 = -0.006f;
    const f32 sIndirectScale = 0.3f;
    const f32 sTexSpeedV0 = 0.0f;
    const f32 sTexSpeedV1 = 0.0f;

    static GXColor sTevReg0 = {0x00, 0x1C, 0x00, 0xFF};
    static GXColor sTevReg1 = {0x78, 0xA0, 0xF6, 0xFF};
};  // namespace

OceanRingPipeInside::OceanRingPipeInside(const OceanRingPipe* pPipe)
    : LiveActor("\203\111\201\133\203\126\203\203\203\223\203\212\203\223\203\117\202\314\223\340\221\244" /* オーシャンリングの内側 */), mRingPipe(pPipe), mTexU0(), mTexV0(), mTexU1(), mTexV1(), mWaterPipeInsideTex() {
}

void OceanRingPipeInside::init(const JMapInfoIter& rIter) {
    MR::connectToScene(this, MR::MovementType_None, MR::CalcAnimType_None, MR::DrawBufferType_None, MR::DrawType_OceanRingPipeInside);
    initDisplayList();
    MR::invalidateClipping(this);
    mWaterPipeInsideTex = new JUTTexture(MR::loadTexFromArc("OceanRing.arc", "WaterPipeInside.bti"), 0);
    makeActorAppeared();
}

void OceanRingPipeInside::movement() {
    mTexU0 = MR::repeat(mTexU0 + ::sTexSpeedU0, 0.0f, 1.0f);
    mTexV0 = MR::repeat(mTexV0 + ::sTexSpeedV0, 0.0f, 1.0f);
    mTexU1 = MR::repeat(mTexU1 + ::sTexSpeedU1, 0.0f, 1.0f);
    mTexV1 = MR::repeat(mTexV1 + ::sTexSpeedV1, 0.0f, 1.0f);
}

void OceanRingPipeInside::initDisplayList() {
    mMesh.init(NEBULA_SHAPE_TEXCOORD0 | NEBULA_SHAPE_TEXCOORD0 << 1, 2 * mRingPipe->_98 * (mRingPipe->_9C - 1));
    mMesh.useArray(NativeMesh::SLOT_POSITION, mRingPipe->_A0);
    sendGD();
    mMesh.send();
}

void OceanRingPipeInside::draw() const {
    if (!MR::isValidDraw(mRingPipe) || !mRingPipe->_90) {
        return;
    }

    loadMaterial();
    mMesh.draw();
}

const GXColor sFogCol = {0xFF, 0xFF, 0xFF, 0xFF};
void OceanRingPipeInside::loadMaterial() const {
    J3DNativeShape::setPosition(0, MR::getCameraViewMtx());
    J3DNativeShape::setNormal(0, MR::getCameraViewMtx());
    J3DNativeState::setCurrentMtx(0);

    TMtx34f mtx;
    mtx.identity();
    mtx.mMtx[0][2] = mTexU0;
    mtx.mMtx[1][2] = mTexV0;
    J3DNativeShape::setTexture(0, mtx.toMtxPtr(), 2);
    mtx.mMtx[0][2] = mTexU1;
    mtx.mMtx[1][2] = mTexV1;
    J3DNativeShape::setTexture(1, mtx.toMtxPtr(), 2);

    mWaterPipeInsideTex->load(GX_TEXMAP0);
    sendInsideProgram(mWaterPipeInsideTex, ::sIndirectScale);
    J3DNativeState::setTevColor(GX_TEVREG0, ::sTevReg0);
    J3DNativeState::setTevColor(GX_TEVREG1, ::sTevReg1);
    J3DNativeState::setClipMode(GX_CLIP_ENABLE);
}

void OceanRingPipeInside::sendGD() {
    u8 something = 0x98;
    s32 vv;
    s32 v;
    f32 f30;
    f32 f29;
    f32 f28;
    f32 f27;
    f32 f26;
    f32 f25;
    f29 = ::sTexRateU0;
    f30 = ::sTexRateU1;
    f26 = 0.0f;
    f25 = 0.0f;
    f28 = ::sTexRateV0;
    f27 = ::sTexRateV1;

    for (s32 i = 0; i < mRingPipe->_9C - 1; i++) {
        f32 f24 = 0.0f;
        f32 f23 = 0.0f;
        vv = mRingPipe->_98;
        mMesh.begin(static_cast< GXPrimitive >(something), (u16)(vv << 1));
        for (s32 j = 0; j < mRingPipe->_98; j++) {
            v = j * mRingPipe->_9C;
            mMesh.index(NativeMesh::SLOT_POSITION, i + v);
            mMesh.texCoord(0, f24, f26);
            mMesh.texCoord(1, f23, f25);
            mMesh.corner();

            mMesh.index(NativeMesh::SLOT_POSITION, i + (v + 1));
            mMesh.texCoord(0, f24, f28);
            mMesh.texCoord(1, f23, f27);
            mMesh.corner();

            f24 += f29;
            f23 += f30;
        }

        f26 = f28;
        f25 = f27;
        f28 += ::sTexRateV0;
        f27 += ::sTexRateV1;
    }
}

OceanRingPipeInside::~OceanRingPipeInside() {
}
