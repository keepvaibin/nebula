#include "Game/MapObj/WhirlPool.hpp"
#include <JSystem/J3DGraphBase/J3DNativeState.hpp>
#include <JSystem/J3DGraphBase/J3DNativeShape.hpp>
#include <JSystem/J3DGraphBase/J3DNativeDraw.hpp>
#include "Game/Map/WaterAreaHolder.hpp"
#include "Game/Map/WhirlPoolAccelerator.hpp"
#include "Game/Scene/SceneFunction.hpp"
#include "Game/Util.hpp"
#include <JSystem/JUtility/JUTTexture.hpp>
#include <nebula_game_api.h>
#include <cstring>

extern "C" const NebulaHostServices* nebula_host(void);

namespace {
    // The whirlpool's material (nebula_game_api.h NebulaMaterialProgram); blended by its alpha where above zero, depth
    // tested and written, no culling.
    NebulaMaterialProgram sWhirlProgram;
    bool sWhirlRegistered = false;

    void setStage(NebulaMaterialTevStage& rStage, u8 coord, u8 map, u8 channel, u8 c0, u8 c1, u8 c2, u8 c3, u8 colorOp,
                  u8 colorScale, u8 colorClamp, u8 a0, u8 a1, u8 a2, u8 a3, u8 alphaScale) {
        rStage.tex_coord = coord;
        rStage.tex_map = map;
        rStage.channel = channel;
        rStage.color_in[0] = c0;
        rStage.color_in[1] = c1;
        rStage.color_in[2] = c2;
        rStage.color_in[3] = c3;
        rStage.color_op = colorOp;
        rStage.color_scale = colorScale;
        rStage.color_clamp = colorClamp;
        rStage.color_reg = GX_TEVPREV;
        rStage.alpha_in[0] = a0;
        rStage.alpha_in[1] = a1;
        rStage.alpha_in[2] = a2;
        rStage.alpha_in[3] = a3;
        rStage.alpha_op = GX_TEV_ADD;
        rStage.alpha_scale = alphaScale;
        rStage.alpha_clamp = GX_FALSE;
        rStage.alpha_reg = GX_TEVPREV;
    }

    void sendWhirlProgram(JUTTexture* pSwirl, JUTTexture* pScreen, JUTTexture* pIndirect) {
        const NebulaHostServices* pHost = nebula_host();
        if (!sWhirlRegistered) {
            NebulaMaterialProgram& rProgram = sWhirlProgram;
            memset(&rProgram, 0, sizeof(rProgram));
            setStage(rProgram.tev[0], GX_TEXCOORD0, GX_TEXMAP0, GX_COLOR_NULL, GX_CC_TEXC, GX_CC_ZERO, GX_CC_ZERO, GX_CC_ZERO,
                     GX_TEV_ADD, GX_CS_SCALE_1, GX_FALSE, GX_CA_ZERO, GX_CA_ZERO, GX_CA_ZERO, GX_CA_ZERO, GX_CS_SCALE_1);
            setStage(rProgram.tev[1], GX_TEXCOORD1, GX_TEXMAP0, GX_COLOR_NULL, GX_CC_ZERO, GX_CC_TEXC, GX_CC_CPREV, GX_CC_ZERO,
                     GX_TEV_ADD, GX_CS_DIVIDE_2, GX_FALSE, GX_CA_ZERO, GX_CA_ZERO, GX_CA_ZERO, GX_CA_ZERO, GX_CS_SCALE_2);
            setStage(rProgram.tev[2], GX_TEXCOORD_NULL, GX_TEXMAP_NULL, GX_COLOR0A0, GX_CC_CPREV, GX_CC_A0, GX_CC_C0,
                     GX_CC_CPREV, GX_TEV_COMP_R8_EQ, GX_CS_SCALE_1, GX_FALSE, GX_CA_RASA, GX_CA_ZERO, GX_CA_ZERO, GX_CA_ZERO,
                     GX_CS_SCALE_1);
            setStage(rProgram.tev[3], GX_TEXCOORD3, GX_TEXMAP1, GX_COLOR_NULL, GX_CC_ZERO, GX_CC_TEXC, GX_CC_C1, GX_CC_CPREV,
                     GX_TEV_ADD, GX_CS_SCALE_1, GX_TRUE, GX_CA_APREV, GX_CA_ZERO, GX_CA_ZERO, GX_CA_ZERO, GX_CS_SCALE_1);
            NebulaMaterialTevStage& rWarp = rProgram.tev[3];
            rWarp.ind_stage = GX_INDTEXSTAGE0;
            rWarp.ind_format = GX_ITF_8;
            rWarp.ind_bias = GX_ITB_STU;
            rWarp.ind_matrix = GX_ITM_0;
            rWarp.ind_wrap_s = rWarp.ind_wrap_t = GX_ITW_OFF;
            rWarp.ind_alpha = GX_ITBA_OFF;
            rProgram.ind_order[0].coord = GX_TEXCOORD2;
            rProgram.ind_order[0].map = GX_TEXMAP2;
            rProgram.ind_order[0].scale_s = rProgram.ind_order[0].scale_t = GX_ITS_1;
            for (u32 t = 0; t < 4; t++) {
                rProgram.tex_gen[t].type = GX_TG_MTX3x4;
                rProgram.tex_gen[t].source = t < 3 ? static_cast< u8 >(GX_TG_TEX0 + t) : GX_TG_POS;
                rProgram.tex_gen[t].matrix = t < 3 ? GX_IDENTITY : GX_TEXMTX0;
                rProgram.tex_gen[t].post_matrix = GX_PTIDENTITY;
            }
            rProgram.tev_stages = 4;
            rProgram.tex_gens = 4;
            rProgram.ind_stages = 1;
            rProgram.channels = 1;
            rProgram.cull = GX_CULL_NONE;
            rProgram.z_compare_location = 1;
            rProgram.loads = NEBULA_MATERIAL_LOADS_TEV | NEBULA_MATERIAL_LOADS_INDIRECT;
            rProgram.alpha_compare.comp0 = rProgram.alpha_compare.comp1 = GX_GREATER;
            rProgram.alpha_compare.op = GX_AOP_OR;
            rProgram.blend.type = GX_BM_BLEND;
            rProgram.blend.source = GX_BL_SRCALPHA;
            rProgram.blend.destination = GX_BL_INVSRCALPHA;
            rProgram.blend.logic_op = GX_LO_NOOP;
            rProgram.z_mode.enable = GX_TRUE;
            rProgram.z_mode.func = GX_LEQUAL;
            rProgram.z_mode.update = GX_TRUE;
            pHost->material_program(pHost->user, reinterpret_cast< uintptr_t >(&sWhirlProgram), &rProgram);
            sWhirlRegistered = true;
        }
        NebulaMaterialValues values;
        memset(&values, 0, sizeof(values));
        values.ind_matrix[0].m[0][0] = 0.2f;
        values.ind_matrix[0].m[1][1] = 0.2f;
        JUTTexture* coordTextures[4] = {pSwirl, pSwirl, pIndirect, pScreen};
        for (u32 c = 0; c < 4; c++) {
            values.tex_coord_scale[c].scale_s = coordTextures[c]->getWidth();
            values.tex_coord_scale[c].scale_t = coordTextures[c]->getHeight();
        }
        J3DNativeShape::flushState();
        pHost->material(pHost->user, reinterpret_cast< uintptr_t >(&sWhirlProgram), &values);
        static const u8 kPlaces[8] = {GX_IDENTITY, GX_IDENTITY, GX_IDENTITY, GX_TEXMTX0,
                                      GX_IDENTITY, GX_IDENTITY, GX_IDENTITY, GX_IDENTITY};
        J3DNativeShape::setCurrent(GX_PNMTX0, kPlaces);
    }
}  // namespace

GXColor sWhirlPoolTevReg1 = {0x28, 0x28, 0x28, 0x14};
GXColor sWhirlPoolTevReg2 = {0x55, 0x96, 0xBE, 0xFF};

WhirlPool::WhirlPool(const char* pName)
    : LiveActor(pName), mRadius(100.0f), mHeight(100.0f), mAxis(0.0f, 1.0f, 0.0f), mPointCount(), mPoints(), mRotation(), mTexOffsetU0(),
      mTexOffsetV0(), mTexOffsetU1(), mTexOffsetV1(), mTexOffsetU2(), mTexOffsetV2(), mTexture(), mIndirectTexture(),
      mClippingCenter(0.0f, 0.0f, 0.0f) {
}

void WhirlPool::init(const JMapInfoIter& rIter) {
    MR::connectToScene(this, MR::MovementType_MapObj, -1, -1, MR::DrawType_WhirlPoolAccelerator);
    mPosition.set(*MR::getPlayerPos());
    mPosition.y = 1500.0f;
    MR::initDefaultPos(this, rIter);
    mRadius = 100.0f * mScale.x;
    mHeight = 100.0f * mScale.y;
    MR::calcActorAxisY(&mAxis, this);
    initPoints();
    mTexture = new JUTTexture(MR::loadTexFromArc("WaterWave.arc", "Water.bti"), 0);
    mIndirectTexture = new JUTTexture(MR::loadTexFromArc("WaterWave.arc", "WaterIndirect.bti"), 0);
    initSound(4, false);
    mClippingCenter.set(mPosition);
    mClippingCenter.y -= mRadius / 2.0f;
    MR::setClippingTypeSphere(this, mRadius, &mClippingCenter);
    WaterAreaFunction::entryWhirlPool(this);
    makeActorAppeared();
}

void WhirlPool::movement() {
    mRotation = MR::repeat(2.0f + mRotation, 0.0f, 360.0f);
    mTexOffsetU0 = MR::repeat(0.001f + mTexOffsetU0, 0.0f, 1.0f);
    mTexOffsetV0 = MR::repeat(-0.001f + mTexOffsetV0, 0.0f, 1.0f);
    mTexOffsetU1 = MR::repeat(mTexOffsetU1 - 0.001f, 0.0f, 1.0f);
    mTexOffsetV1 = MR::repeat(-0.001f + mTexOffsetV1, 0.0f, 1.0f);
    mTexOffsetU2 = MR::repeat(0.001f + mTexOffsetU2, 0.0f, 1.0f);
    mTexOffsetV2 = MR::repeat(-0.01f + mTexOffsetV2, 0.0f, 1.0f);
    MR::startLevelSound(this, "SE_AT_LV_WHIRL_POOL");
}

void WhirlPool::initPoints() {
    mPointCount = static_cast< s32 >(mHeight / 50.0f) + 1;
    mPoints = new WhirlPoolPoint*[mPointCount];
    TPos3f rotation;
    rotation.identity();
    MR::makeMtxRotate(rotation, 0.0f, 10.0f, 0.0f);
    TVec3f normal(0.0f, 0.0f, 1.0f);

    f32 angle = 0.0f;

    for (s32 i = 0; i < mPointCount; i++) {
        TVec3f position(mAxis);
        f32 pointOffset = i;
        position.scale(-50.0f * pointOffset);
        position.add(mPosition);

        f32 rate = MR::getEaseInValue(static_cast< f32 >(mPointCount - i) / mPointCount, 0.0f, 1.0f, 1.0f);
        rate = MR::getEaseInValue(rate, 0.0f, 1.0f, 1.0f);
        f32 radius = 100.0f + rate * (mRadius - 100.0f);
        mPoints[i] = new WhirlPoolPoint(position, mAxis, normal, radius, angle / 360.0f, 0xFF);
        rotation.mult(normal, normal);
        angle = MR::repeat(10.0f + angle, 0.0f, 360.0f);
    }
}

void WhirlPool::draw() const {
    loadMaterial();
    f32 u0 = mTexOffsetU0;
    f32 nextU0 = 1.0f / 6.0f + u0;
    f32 u1 = mTexOffsetU1;
    f32 nextU1 = 1.0f / 6.0f + u1;

    drawPlane(0.5f, 0.70710677f, -0.5f, 0.70710677f, u0, nextU0, u1, nextU1);
    u0 = nextU0;
    nextU0 += 1.0f / 6.0f;
    u1 = nextU1;
    nextU1 += 1.0f / 6.0f;

    drawPlane(-0.5f, 0.70710677f, -1.0f, 0.0f, u0, nextU0, u1, nextU1);
    u0 = nextU0;
    nextU0 += 1.0f / 6.0f;
    u1 = nextU1;
    nextU1 += 1.0f / 6.0f;

    drawPlane(-1.0f, 0.0f, -0.5f, -0.70710677f, u0, nextU0, u1, nextU1);
    u0 = nextU0;
    nextU0 += 1.0f / 6.0f;
    u1 = nextU1;
    nextU1 += 1.0f / 6.0f;

    drawPlane(-0.5f, -0.70710677f, 0.5f, -0.70710677f, u0, nextU0, u1, nextU1);
    u0 = nextU0;
    nextU0 += 1.0f / 6.0f;
    u1 = nextU1;
    nextU1 += 1.0f / 6.0f;

    drawPlane(0.5f, -0.70710677f, 1.0f, 0.0f, u0, nextU0, u1, nextU1);
    u0 = nextU0;
    nextU0 += 1.0f / 6.0f;
    u1 = nextU1;
    nextU1 += 1.0f / 6.0f;

    drawPlane(1.0f, 0.0f, 0.5f, 0.70710677f, u0, nextU0, u1, nextU1);
}

void WhirlPool::drawPlane(f32 x0, f32 y0, f32 x1, f32 y1, f32 u0, f32 u1, f32 u2, f32 u3) const {
    f32 v0 = mTexOffsetV0;
    f32 v1 = mTexOffsetV1;
    f32 v2 = mTexOffsetV2;

    TVec3f offset(0.0f, 0.0f, 50.0f);
    TPos3f rotation;
    rotation.identity();
    MR::makeMtxRotate(rotation, 0.0f, mRotation, 0.0f);
    rotation.mult(offset, offset);
    MR::makeMtxRotate(rotation, 0.0f, 25.0f, 0.0f);
    J3DNativeDraw::begin(GX_TRIANGLESTRIP, mPointCount * 2, NEBULA_SHAPE_COLOR0 | NEBULA_SHAPE_TEXCOORD0 | NEBULA_SHAPE_TEXCOORD0 << 1 | NEBULA_SHAPE_TEXCOORD0 << 2);
    const WhirlPoolPoint* pPoint;

    for (s32 i = 0; i < mPointCount; i++) {
        pPoint = mPoints[i];
        TVec3f side(pPoint->mSide);
        TVec3f normal(pPoint->mNormal);
        side.scale(pPoint->mRadius);
        normal.scale(pPoint->mRadius);
        TVec3f position(pPoint->mPosition);
        position.add(offset);
        rotation.mult(offset, offset);

        J3DNativeDraw::position(position.x + side.x * x0 + normal.x * y0, position.y + side.y * x0 + normal.y * y0, position.z + side.z * x0 + normal.z * y0);
        J3DNativeDraw::color(0xFF, 0xFF, 0xFF, mPoints[i]->mAlpha);
        J3DNativeDraw::texCoord(u0 + pPoint->mTexCoord, v0);
        J3DNativeDraw::texCoord(u2 + pPoint->mTexCoord, v1);
        J3DNativeDraw::texCoord(u0 + pPoint->mTexCoord, v2);

        J3DNativeDraw::position(position.x + side.x * x1 + normal.x * y1, position.y + side.y * x1 + normal.y * y1, position.z + side.z * x1 + normal.z * y1);
        J3DNativeDraw::color(0xFF, 0xFF, 0xFF, mPoints[i]->mAlpha);
        J3DNativeDraw::texCoord(u1 + pPoint->mTexCoord, v0);
        v0 += 0.01f;
        J3DNativeDraw::texCoord(u3 + pPoint->mTexCoord, v1);
        v1 += 0.01f;
        J3DNativeDraw::texCoord(u1 + pPoint->mTexCoord, v2);
        v2 += 0.01f;
    }
}

void WhirlPool::loadMaterial() const {
    J3DNativeShape::setPosition(0, MR::getCameraViewMtx());
    J3DNativeState::setCurrentMtx(GX_PNMTX0);
    J3DNativeState::setChanCtrl(GX_COLOR0A0, GX_DISABLE, GX_SRC_VTX, GX_SRC_VTX, GX_LIGHT_NULL, GX_DF_NONE, GX_AF_NONE);
    J3DNativeState::setChanCtrl(GX_COLOR1A1, GX_DISABLE, GX_SRC_REG, GX_SRC_REG, GX_LIGHT_NULL, GX_DF_NONE, GX_AF_NONE);
    MR::loadTexProjectionMtx(GX_TEXMTX0);
    mTexture->load(GX_TEXMAP0);
    JUTTexture screenTexture(MR::getScreenResTIMG(), 0);
    screenTexture.load(GX_TEXMAP1);
    mIndirectTexture->load(GX_TEXMAP2);
    sendWhirlProgram(mTexture, &screenTexture, mIndirectTexture);
    J3DNativeState::setTevColor(GX_TEVREG0, sWhirlPoolTevReg1);
    J3DNativeState::setTevColor(GX_TEVREG1, sWhirlPoolTevReg2);
    J3DNativeState::setClipMode(GX_CLIP_ENABLE);
}

WhirlPool::~WhirlPool() {
}
