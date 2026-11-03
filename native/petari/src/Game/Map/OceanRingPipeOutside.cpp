#include "Game/Map/OceanRingPipeOutside.hpp"
#include "Game/Util/NativeShader.hpp"
#include "JSystem/J3DGraphBase/J3DNativeMaterial.hpp"
#include <JSystem/J3DGraphBase/J3DNativeState.hpp>
#include <JSystem/J3DGraphBase/J3DNativeShape.hpp>
#include "Game/Map/OceanRingPipe.hpp"
#include "Game/Scene/SceneFunction.hpp"
#include "Game/Util/CameraUtil.hpp"
#include "Game/Util/LiveActorUtil.hpp"
#include "Game/Util/MathUtil.hpp"
#include "Game/Util/MtxUtil.hpp"
#include "Game/Util/ObjUtil.hpp"
#include "Game/System/NebulaArena.hpp"
#include <JSystem/JUtility/JUTTexture.hpp>

namespace {
    const f32 sTexRateU = 0.08f;
    const f32 sTexRateV = 1.0f;
    const f32 sTexSpeedU = -0.004f;
    const f32 sIndirectScale = 0.15f;
    const f32 sEnvMapScale = 0.6f;

    static NebulaColor8 sTevReg0 = {0x14, 0x65, 0xFF, 0xB9};
};  // namespace

OceanRingPipeOutside::OceanRingPipeOutside(const OceanRingPipe* pPipe)
    : LiveActor("\203\111\201\133\203\126\203\203\203\223\203\212\203\223\203\117\202\314\212\117\221\244" /* オーシャンリングの外側 */), mRingPipe(pPipe), mTexU(0.0f), mWaterPipeIndirectTex(nullptr), mWaterPipeHighLightTex(nullptr) {
}

void OceanRingPipeOutside::init(const JMapInfoIter& rIter) {
    MR::connectToScene(this, MR::MovementType_None, MR::CalcAnimType_None, MR::DrawBufferType_None, MR::DrawType_OceanRingPipeOutside);
    initMesh();
    MR::invalidateClipping(this);
    mWaterPipeIndirectTex = new JUTTexture(MR::loadTexFromArc("OceanRing.arc", "WaterPipeIndirect.bti"));
    mWaterPipeHighLightTex = new JUTTexture(MR::loadTexFromArc("OceanRing.arc", "WaterPipeHighLight.bti"));
    makeActorAppeared();
}

void OceanRingPipeOutside::movement() {
    mTexU = MR::repeat(mTexU + ::sTexSpeedU, 0.0f, 1.0f);
}

void OceanRingPipeOutside::initMesh() {
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
}


void OceanRingPipeOutside::loadMaterial() const {
    J3DNativeShape::setPosition(0, MR::getCameraViewMtx());
    J3DNativeShape::setNormal(0, MR::getCameraViewMtx());
    TPos3f mtx;
    mtx.identity();
    mtx.mMtx[0][2] = mTexU;
    mtx.mMtx[1][2] = 0.0f;
    J3DNativeShape::setTexture(0, mtx.toMtxPtr(), 2);

    TPos3f mtx2;
    mtx2.identity();
    mtx2.set(MR::getCameraViewMtx());
    mtx2.zeroTrans();

    TPos3f scaleMtx;
    scaleMtx.identity();
    scaleMtx.scale(::sEnvMapScale);

    static Mtx qMtx2 = {{0.5f, 0.0f, 0.0f, 0.5f}, {0.0f, -0.5f, 0.0f, 0.5f}, {0.0f, 0.0f, 1.0f, 0.0f}};

    MR::multMtx(mtx2, scaleMtx, mtx2);
    MR::multMtx(mtx2, qMtx2, mtx2);
    J3DNativeShape::setTexture(1, mtx2, 2);

    // runtime/src/render/materials/ocean_ring_pipe_outside.hlsl: the highlight texture (1) by the normal, warped by
    // the indirect texture (0) at sIndirectScale texels a step, over sTevReg0 with light 2's rim; opaque, depth tested
    // and written, back faces culled (or none). The alpha channel's material and ambient alphas are the drawing
    // state's (the site never sets them).
    NebulaNativeMaterial material;
    NativeShader::init(material, NEBULA_SHADER_OCEAN_RING_PIPE_OUTSIDE, NativeShader::BLEND_OPAQUE, NEBULA_COMPARE_LEQUAL,
                       true, mRingPipe->_90 ? NEBULA_CULL_BACK : NEBULA_CULL_NONE);
    NativeShader::setColor(material, 0, ::sTevReg0.r, ::sTevReg0.g, ::sTevReg0.b, ::sTevReg0.a);
    material.constants[4] = ::sIndirectScale;
    material.constants[6] = static_cast< f32 >(mWaterPipeHighLightTex->getWidth());
    material.constants[7] = static_cast< f32 >(mWaterPipeHighLightTex->getHeight());
    const f32* pRegisters = J3DNativeMaterial::registers();
    material.constants[8] = pRegisters[35];
    material.constants[9] = pRegisters[43];
    NativeShader::setTexture(material, 0, mWaterPipeIndirectTex);
    NativeShader::setTexture(material, 1, mWaterPipeHighLightTex);
    NativeShader::send(material);
    static const u8 kPlaces[8] = {0, 1, J3DNativeShape::kIdentityTexture, J3DNativeShape::kIdentityTexture,
                                  J3DNativeShape::kIdentityTexture, J3DNativeShape::kIdentityTexture, J3DNativeShape::kIdentityTexture, J3DNativeShape::kIdentityTexture};
    J3DNativeShape::setCurrent(0, kPlaces);
}

void OceanRingPipeOutside::sendGD() {
    s32 vv;
    s32 v;
    f32 f29;
    f32 f28;
    f29 = 1.0f;
    f28 = 0.0f;

    for (s32 i = 0; i < mRingPipe->_9C - 1; i++) {
        f32 f27 = 0.0f;
        vv = mRingPipe->_98;
        mMesh.begin(NEBULA_PRIMITIVE_TRIANGLE_STRIP, (u16)(vv << 1));
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
