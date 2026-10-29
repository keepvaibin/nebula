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
    J3DNativeShape::sendPosition(MR::getCameraViewMtx(), 0);
    J3DNativeShape::sendNormal(MR::getCameraViewMtx(), 0);
    J3DNativeState::setCurrentMtx(0);
    J3DNativeState::setNumChans(0);
    J3DNativeState::setNumTexGens(2);
    J3DNativeState::setTexCoordGen2(GX_TEXCOORD0, GX_TG_MTX2x4, GX_TG_TEX0, 0x1E, 0, 0x7D);
    J3DNativeState::setTexCoordGen2(GX_TEXCOORD1, GX_TG_MTX2x4, GX_TG_TEX1, 0x21, 0, 0x7D);

    TMtx34f mtx;
    mtx.identity();
    mtx.mMtx[0][2] = mTexU0;
    mtx.mMtx[1][2] = mTexV0;
    J3DNativeShape::sendTexture(mtx.toMtxPtr(), 0x1E, GX_MTX2x4);
    mtx.mMtx[0][2] = mTexU1;
    mtx.mMtx[1][2] = mTexV1;
    J3DNativeShape::sendTexture(mtx.toMtxPtr(), 0x21, GX_MTX2x4);

    mWaterPipeInsideTex->load(GX_TEXMAP0);
    J3DNativeState::setNumIndStages(1);
    J3DNativeState::setIndTexOrder(GX_INDTEXSTAGE0, GX_TEXCOORD1, GX_TEXMAP0);
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
    J3DNativeState::setTevColor(GX_TEVREG1, ::sTevReg1);
    J3DNativeState::setNumTevStages(1);
    J3DNativeState::setTevOrder(GX_TEVSTAGE0, GX_TEXCOORD0, GX_TEXMAP0, GX_COLOR_NULL);
    J3DNativeState::setTevColorIn(GX_TEVSTAGE0, GX_CC_C0, GX_CC_C1, GX_CC_TEXC, GX_CC_ZERO);
    J3DNativeState::setTevColorOp(GX_TEVSTAGE0, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_4, GX_TRUE, GX_TEVPREV);
    J3DNativeState::setTevAlphaIn(GX_TEVSTAGE0, GX_CA_ZERO, GX_CA_TEXA, GX_CA_TEXA, GX_CA_ZERO);
    J3DNativeState::setTevAlphaOp(GX_TEVSTAGE0, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_4, GX_TRUE, GX_TEVPREV);

    J3DNativeState::setBlendMode(GX_BM_BLEND, GX_BL_SRCALPHA, GX_BL_INVSRCALPHA, GX_LO_COPY);
    J3DNativeState::setAlphaCompare(GX_ALWAYS, 0, GX_AOP_OR, GX_ALWAYS, 0);
    J3DNativeState::setZMode(1, GX_LEQUAL, 0);
    J3DNativeState::setZCompLoc(1);
    J3DNativeState::setCullMode(GX_CULL_BACK);
    J3DNativeState::setClipMode(GX_CLIP_ENABLE);
    J3DNativeState::setFog(GX_FOG_NONE, 0.0f, 0.0f, MR::getNearZ(), MR::getFarZ(), sFogCol);
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
