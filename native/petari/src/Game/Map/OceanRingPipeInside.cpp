#include <nebula_game_api.h>
#include "Game/Map/OceanRingPipeInside.hpp"
#include "Game/Render/TypedDraw.hpp"
#include "Game/Render/FrameList.hpp"
#include "Game/Map/OceanRingPipe.hpp"
#include "Game/Scene/SceneFunction.hpp"
#include "Game/Util/CameraUtil.hpp"
#include "Game/Util/LiveActorUtil.hpp"
#include "Game/Util/MathUtil.hpp"
#include "Game/Util/ObjUtil.hpp"
#include "Game/System/NebulaArena.hpp"
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

    static NebulaColor8 sStageColor0 = {0x00, 0x1C, 0x00, 0xFF};
    static NebulaColor8 sStageColor1 = {0x78, 0xA0, 0xF6, 0xFF};
};  // namespace

OceanRingPipeInside::OceanRingPipeInside(const OceanRingPipe* pPipe)
    : LiveActor("オーシャンリングの内側"), mRingPipe(pPipe), mTexU0(), mTexV0(), mTexU1(), mTexV1(), mWaterPipeInsideTex() {
}

void OceanRingPipeInside::init(const TableIter& rIter) {
    MR::connectToScene(this, MR::MovementType_None, MR::CalcAnimType_None, MR::DrawBufferType_None, MR::DrawType_OceanRingPipeInside);
    initMesh();
    MR::invalidateClipping(this);
    mWaterPipeInsideTex = new JUTTexture(MR::loadTexFromArc("OceanRing", "WaterPipeInside.bti"));
    makeActorAppeared();
}

void OceanRingPipeInside::movement() {
    mTexU0 = MR::repeat(mTexU0 + ::sTexSpeedU0, 0.0f, 1.0f);
    mTexV0 = MR::repeat(mTexV0 + ::sTexSpeedV0, 0.0f, 1.0f);
    mTexU1 = MR::repeat(mTexU1 + ::sTexSpeedU1, 0.0f, 1.0f);
    mTexV1 = MR::repeat(mTexV1 + ::sTexSpeedV1, 0.0f, 1.0f);
}

void OceanRingPipeInside::initMesh() {
    mMesh.init(2 * mRingPipe->mNumSegments * (mRingPipe->mNumSides - 1));
    sendGD();
    mMesh.send();
}

void OceanRingPipeInside::draw() const {
    if (!MR::isValidDraw(mRingPipe) || !mRingPipe->mIsCameraInside) {
        return;
    }

    const SiteMaterial material = loadMaterial();
    TypedDraw site(material, nullptr, MR::getCameraViewMtx());
    mMesh.draw(site);
}

SiteMaterial OceanRingPipeInside::loadMaterial() const {

    // runtime/src/render/materials/ocean_ring_pipe_inside.hlsl: the texture warped by itself (sIndirectScale texels a
    // step), sStageColor0 toward sStageColor1 by its colour, its alpha squared, both times four; blended by alpha, depth tested
    // and not written, back faces culled, no fog.
    SiteMaterial material;
    material.init(NEBULA_SHADER_OCEAN_RING_PIPE_INSIDE, SiteMaterial::BLEND_ALPHA, NEBULA_COMPARE_LEQUAL,
                       false, NEBULA_CULL_BACK);
    Mtx34f mtx;
    mtx.identity();
    mtx.mMtx[0][2] = mTexU0;
    mtx.mMtx[1][2] = mTexV0;
    material.setMatrix(3, 0, mtx.toMtxPtr());
    mtx.mMtx[0][2] = mTexU1;
    mtx.mMtx[1][2] = mTexV1;
    material.setMatrix(3, 1, mtx.toMtxPtr());
    material.setColor(0, ::sStageColor0.r, ::sStageColor0.g, ::sStageColor0.b, ::sStageColor0.a);
    material.setColor(4, ::sStageColor1.r, ::sStageColor1.g, ::sStageColor1.b, ::sStageColor1.a);
    material.constants[8] = ::sIndirectScale;
    material.constants[10] = static_cast< f32 >(mWaterPipeInsideTex->getWidth());
    material.constants[11] = static_cast< f32 >(mWaterPipeInsideTex->getHeight());
    material.setTexture(0, mWaterPipeInsideTex);
    return material;
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

    for (s32 i = 0; i < mRingPipe->mNumSides - 1; i++) {
        f32 f24 = 0.0f;
        f32 f23 = 0.0f;
        vv = mRingPipe->mNumSegments;
        VertexPT2* vtx = mMesh.strip((u16)(vv << 1));
        for (s32 j = 0; j < mRingPipe->mNumSegments; j++) {
            v = j * mRingPipe->mNumSides;
            const Vec3f& rNear = mRingPipe->mPositions[i + v];
            const Vec3f& rFar = mRingPipe->mPositions[i + (v + 1)];
            *vtx++ = {rNear.x, rNear.y, rNear.z, f24, f26, f23, f25};
            *vtx++ = {rFar.x, rFar.y, rFar.z, f24, f28, f23, f27};

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
