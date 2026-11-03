#include "Game/Map/OceanRingPipeInside.hpp"
#include "Game/Util/NativeShader.hpp"
#include <JSystem/J3DGraphBase/J3DNativeState.hpp>
#include <JSystem/J3DGraphBase/J3DNativeShape.hpp>
#include "Game/Map/OceanRingPipe.hpp"
#include "Game/Scene/SceneFunction.hpp"
#include "Game/Util/CameraUtil.hpp"
#include "Game/Util/LiveActorUtil.hpp"
#include "Game/Util/MathUtil.hpp"
#include "Game/Util/ObjUtil.hpp"
#include "Game/System/NebulaArena.hpp"
#include <JSystem/JUtility/JUTTexture.hpp>
#include <nebula_game_api.h>
#include <cstring>

extern "C" const NebulaHostServices* nebula_host(void);

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

    static NebulaColor8 sTevReg0 = {0x00, 0x1C, 0x00, 0xFF};
    static NebulaColor8 sTevReg1 = {0x78, 0xA0, 0xF6, 0xFF};
};  // namespace

OceanRingPipeInside::OceanRingPipeInside(const OceanRingPipe* pPipe)
    : LiveActor("\203\111\201\133\203\126\203\203\203\223\203\212\203\223\203\117\202\314\223\340\221\244" /* オーシャンリングの内側 */), mRingPipe(pPipe), mTexU0(), mTexV0(), mTexU1(), mTexV1(), mWaterPipeInsideTex() {
}

void OceanRingPipeInside::init(const JMapInfoIter& rIter) {
    MR::connectToScene(this, MR::MovementType_None, MR::CalcAnimType_None, MR::DrawBufferType_None, MR::DrawType_OceanRingPipeInside);
    initMesh();
    MR::invalidateClipping(this);
    mWaterPipeInsideTex = new JUTTexture(MR::loadTexFromArc("OceanRing.arc", "WaterPipeInside.bti"));
    makeActorAppeared();
}

void OceanRingPipeInside::movement() {
    mTexU0 = MR::repeat(mTexU0 + ::sTexSpeedU0, 0.0f, 1.0f);
    mTexV0 = MR::repeat(mTexV0 + ::sTexSpeedV0, 0.0f, 1.0f);
    mTexU1 = MR::repeat(mTexU1 + ::sTexSpeedU1, 0.0f, 1.0f);
    mTexV1 = MR::repeat(mTexV1 + ::sTexSpeedV1, 0.0f, 1.0f);
}

void OceanRingPipeInside::initMesh() {
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

void OceanRingPipeInside::loadMaterial() const {
    J3DNativeShape::setPosition(0, MR::getCameraViewMtx());
    J3DNativeShape::setNormal(0, MR::getCameraViewMtx());
    J3DPass::positionMatrix(0);

    TMtx34f mtx;
    mtx.identity();
    mtx.mMtx[0][2] = mTexU0;
    mtx.mMtx[1][2] = mTexV0;
    J3DNativeShape::setTexture(0, mtx.toMtxPtr(), 2);
    mtx.mMtx[0][2] = mTexU1;
    mtx.mMtx[1][2] = mTexV1;
    J3DNativeShape::setTexture(1, mtx.toMtxPtr(), 2);

    // runtime/src/render/materials/ocean_ring_pipe_inside.hlsl: the texture warped by itself (sIndirectScale texels a
    // step), sTevReg0 toward sTevReg1 by its colour, its alpha squared, both times four; blended by alpha, depth tested
    // and not written, back faces culled, no fog.
    NebulaNativeMaterial material;
    NativeShader::init(material, NEBULA_SHADER_OCEAN_RING_PIPE_INSIDE, NativeShader::BLEND_ALPHA, NEBULA_COMPARE_LEQUAL,
                       false, NEBULA_CULL_BACK);
    NativeShader::setColor(material, 0, ::sTevReg0.r, ::sTevReg0.g, ::sTevReg0.b, ::sTevReg0.a);
    NativeShader::setColor(material, 4, ::sTevReg1.r, ::sTevReg1.g, ::sTevReg1.b, ::sTevReg1.a);
    material.constants[8] = ::sIndirectScale;
    material.constants[10] = static_cast< f32 >(mWaterPipeInsideTex->getWidth());
    material.constants[11] = static_cast< f32 >(mWaterPipeInsideTex->getHeight());
    NativeShader::setTexture(material, 0, mWaterPipeInsideTex);
    NativeShader::send(material);
    static const u8 kPlaces[8] = {0, 1, J3DNativeShape::kIdentityTexture, J3DNativeShape::kIdentityTexture,
                                  J3DNativeShape::kIdentityTexture, J3DNativeShape::kIdentityTexture, J3DNativeShape::kIdentityTexture, J3DNativeShape::kIdentityTexture};
    J3DNativeShape::setCurrent(0, kPlaces);
}

void OceanRingPipeInside::sendGD() {
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
        mMesh.begin(NEBULA_PRIMITIVE_TRIANGLE_STRIP, (u16)(vv << 1));
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
