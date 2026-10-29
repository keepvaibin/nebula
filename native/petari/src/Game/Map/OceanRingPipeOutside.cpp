#include "Game/Map/OceanRingPipeOutside.hpp"
#include <JSystem/J3DGraphBase/J3DNativeState.hpp>
#include <JSystem/J3DGraphBase/J3DNativeShape.hpp>
#include "Game/Map/OceanRingPipe.hpp"
#include "Game/Scene/SceneFunction.hpp"
#include "Game/Util/CameraUtil.hpp"
#include "Game/Util/LiveActorUtil.hpp"
#include "Game/Util/MathUtil.hpp"
#include "Game/Util/MtxUtil.hpp"
#include "Game/Util/ObjUtil.hpp"
#include "Game/Util/SchedulerUtil.hpp"
#include <JSystem/JKernel/JKRHeap.hpp>
#include <JSystem/JUtility/JUTTexture.hpp>

namespace {
    const f32 sTexRateU = 0.08f;
    const f32 sTexRateV = 1.0f;
    const f32 sTexSpeedU = -0.004f;
    const f32 sIndirectScale = 0.15f;
    const f32 sEnvMapScale = 0.6f;

    static GXColor sTevReg0 = {0x14, 0x65, 0xFF, 0xB9};
};  // namespace

OceanRingPipeOutside::OceanRingPipeOutside(const OceanRingPipe* pPipe)
    : LiveActor("\203\111\201\133\203\126\203\203\203\223\203\212\203\223\203\117\202\314\212\117\221\244" /* オーシャンリングの外側 */), mRingPipe(pPipe), mTexU(0.0f), mWaterPipeIndirectTex(nullptr), mWaterPipeHighLightTex(nullptr) {
}

void OceanRingPipeOutside::init(const JMapInfoIter& rIter) {
    MR::connectToScene(this, MR::MovementType_None, MR::CalcAnimType_None, MR::DrawBufferType_None, MR::DrawType_OceanRingPipeOutside);
    initDisplayList();
    MR::invalidateClipping(this);
    mWaterPipeIndirectTex = new JUTTexture(MR::loadTexFromArc("OceanRing.arc", "WaterPipeIndirect.bti"), 0);
    mWaterPipeHighLightTex = new JUTTexture(MR::loadTexFromArc("OceanRing.arc", "WaterPipeHighLight.bti"), 0);
    makeActorAppeared();
}

void OceanRingPipeOutside::movement() {
    mTexU = MR::repeat(mTexU + ::sTexSpeedU, 0.0f, 1.0f);
}

void OceanRingPipeOutside::initDisplayList() {
    mMesh.init(NEBULA_SHAPE_NORMAL | NEBULA_SHAPE_TEXCOORD0, 2 * mRingPipe->_98 * (mRingPipe->_9C - 1));
    mMesh.useArray(NativeMesh::SLOT_POSITION, mRingPipe->_A0);
    mMesh.useArray(NativeMesh::SLOT_NORMAL, mRingPipe->_A4);
    sendGD();
    mMesh.send();
}

void OceanRingPipeOutside::draw() const {
    if (!MR::isValidDraw(mRingPipe))
        return;

    loadMaterial();
    mMesh.draw();
    J3DNativeState::setTevDirect(GX_TEVSTAGE0);
}

const GXColor sFogCol = {0xFF, 0xFF, 0xFF, 0xFF};

void OceanRingPipeOutside::loadMaterial() const {
    J3DNativeShape::sendPosition(MR::getCameraViewMtx(), 0);
    J3DNativeShape::sendNormal(MR::getCameraViewMtx(), 0);
    J3DNativeState::setCurrentMtx(0);
    J3DNativeState::setNumChans(1);
    J3DNativeState::setChanCtrl(GX_COLOR0, GX_FALSE, GX_SRC_REG, GX_SRC_REG, 0, GX_DF_NONE, GX_AF_NONE);
    J3DNativeState::setChanCtrl(GX_ALPHA0, GX_TRUE, GX_SRC_REG, GX_SRC_REG, 4, GX_DF_CLAMP, GX_AF_NONE);
    J3DNativeState::setNumTexGens(2);
    J3DNativeState::setTexCoordGen2(GX_TEXCOORD0, GX_TG_MTX2x4, GX_TG_TEX0, 0x1E, 0, 0x7D);
    J3DNativeState::setTexCoordGen2(GX_TEXCOORD1, GX_TG_MTX2x4, GX_TG_NRM, 0x21, 0, 0x7D);

    TPos3f mtx;
    mtx.identity();
    mtx.mMtx[0][2] = mTexU;
    mtx.mMtx[1][2] = 0.0f;
    J3DNativeShape::sendTexture(mtx.toMtxPtr(), 0x1E, GX_MTX2x4);

    TPos3f mtx2;
    mtx2.identity();
    mtx2.set(MR::getCameraViewMtx());
    mtx2.zeroTrans();

    TPos3f scaleMtx;
    scaleMtx.identity();
    scaleMtx.scale(::sEnvMapScale);

    static Mtx qMtx2 = {0.5f, 0.0f, 0.0f, 0.5f, 0.0f, -0.5f, 0.0f, 0.5f, 0.0f, 0.0f, 1.0f, 0.0f};

    MR::multMtx(mtx2, scaleMtx, mtx2);
    MR::multMtx(mtx2, qMtx2, mtx2);
    J3DNativeShape::sendTexture(mtx2, 0x21, GX_MTX2x4);

    mWaterPipeIndirectTex->load(GX_TEXMAP0);
    mWaterPipeHighLightTex->load(GX_TEXMAP1);
    J3DNativeState::setNumIndStages(1);
    J3DNativeState::setIndTexOrder(GX_INDTEXSTAGE0, GX_TEXCOORD0, GX_TEXMAP0);
    J3DNativeState::setTevIndWarp(GX_TEVSTAGE0, GX_INDTEXSTAGE0, 1, 0, GX_ITM_0);
    Mtx23 new_mtx;
    new_mtx[0][0] = ::sIndirectScale;
    new_mtx[0][1] = 0.0f;
    new_mtx[0][2] = 0.0f;
    new_mtx[1][0] = 0.0f;
    new_mtx[1][1] = ::sIndirectScale;
    new_mtx[1][2] = 0.0f;
    J3DNativeState::setIndTexMtx(GX_ITM_0, new_mtx, 0);

    J3DNativeState::setTevColor(GX_TEVREG0, ::sTevReg0);

    J3DNativeState::setNumTevStages(2);
    J3DNativeState::setTevOrder(GX_TEVSTAGE0, GX_TEXCOORD1, GX_TEXMAP1, GX_COLOR0A0);
    J3DNativeState::setTevColorIn(GX_TEVSTAGE0, GX_CC_TEXC, GX_CC_ZERO, GX_CC_ZERO, GX_CC_C0);
    J3DNativeState::setTevColorOp(GX_TEVSTAGE0, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1, GX_TRUE, GX_TEVPREV);
    J3DNativeState::setTevAlphaIn(GX_TEVSTAGE0, GX_CA_A0, GX_CA_ZERO, GX_CA_RASA, GX_CA_ZERO);
    J3DNativeState::setTevAlphaOp(GX_TEVSTAGE0, GX_TEV_ADD, GX_TB_SUBHALF, GX_CS_SCALE_2, GX_TRUE, GX_TEVPREV);

    J3DNativeState::setTevOrder(GX_TEVSTAGE1, GX_TEXCOORD_NULL, GX_TEXMAP_NULL, GX_COLOR0A0);
    J3DNativeState::setTevColorIn(GX_TEVSTAGE1, GX_CC_CPREV, GX_CC_ZERO, GX_CC_ZERO, GX_CC_APREV);
    J3DNativeState::setTevColorOp(GX_TEVSTAGE1, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1, GX_TRUE, GX_TEVPREV);
    J3DNativeState::setTevAlphaIn(GX_TEVSTAGE1, GX_CA_KONST, GX_CA_ZERO, GX_CA_ZERO, GX_CA_ZERO);
    J3DNativeState::setTevAlphaOp(GX_TEVSTAGE1, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1, GX_FALSE, GX_TEVPREV);

    J3DNativeState::setBlendMode(GX_BM_NONE, GX_BL_ONE, GX_BL_ZERO, GX_LO_NOOP);
    J3DNativeState::setAlphaCompare(GX_ALWAYS, 0, GX_AOP_OR, GX_ALWAYS, 0);
    J3DNativeState::setZMode(1, GX_LEQUAL, 1);
    J3DNativeState::setZCompLoc(1);
    if (mRingPipe->_90) {
        J3DNativeState::setCullMode(GX_CULL_BACK);
    } else {
        J3DNativeState::setCullMode(GX_CULL_NONE);
    }

    J3DNativeState::setClipMode(GX_CLIP_ENABLE);
    J3DNativeState::setFog(GX_FOG_NONE, 0.0f, 0.0f, MR::getNearZ(), MR::getFarZ(), sFogCol);
}

void OceanRingPipeOutside::sendGD() {
    u8 something = 0x98;
    s32 vv;
    s32 v;
    f32 f29;
    f32 f28;
    f29 = 1.0f;
    f28 = 0.0f;

    for (s32 i = 0; i < mRingPipe->_9C - 1; i++) {
        f32 f27 = 0.0f;
        vv = mRingPipe->_98;
        mMesh.begin(static_cast< GXPrimitive >(something), (u16)(vv << 1));
        for (s32 j = 0; j < mRingPipe->_98; j++) {
            v = j * mRingPipe->_9C;
            u16 cl2 = i + v + 1;
            u16 cl = i + v;
            mMesh.index(NativeMesh::SLOT_POSITION, cl2);
            mMesh.index(NativeMesh::SLOT_NORMAL, cl2);
            mMesh.texCoord(0, f27, f29);
            mMesh.corner();

            mMesh.index(NativeMesh::SLOT_POSITION, cl);
            mMesh.index(NativeMesh::SLOT_NORMAL, cl);
            mMesh.texCoord(0, f27, f28);
            mMesh.corner();

            f27 += ::sTexRateU;
        }
        f28 = f29;
        f29 += ::sTexRateV;
    }
}

OceanRingPipeOutside::~OceanRingPipeOutside() {
}
