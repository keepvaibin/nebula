#include "Game/Map/OceanRingPipeOutside.hpp"
#include "Game/Render/TypedDraw.hpp"
#include "Game/Render/Material.hpp"
#include "Game/Render/FrameList.hpp"
#include "Game/Map/OceanRingPipe.hpp"
#include "Game/Scene/SceneFunction.hpp"
#include "Game/Util/CameraUtil.hpp"
#include "Game/Util/LiveActorUtil.hpp"
#include "Game/Util/MathUtil.hpp"
#include "Game/Util/MtxUtil.hpp"
#include "Game/Util/ObjUtil.hpp"
#include "Game/System/NebulaArena.hpp"
#include "Game/Render/GameTexture.hpp"
#include "Game/Util/Tunable.hpp"
#include "Game/System/LumaCore.hpp"

namespace {
    NEBULA_TUNABLE(f32, sTexRateU, 0.08f);
    NEBULA_TUNABLE(f32, sTexRateV, 1.0f);
    NEBULA_TUNABLE(f32, sTexSpeedU, -0.004f);
    NEBULA_TUNABLE(f32, sIndirectScale, 0.15f);
    NEBULA_TUNABLE(f32, sEnvMapScale, 0.6f);

    static NebulaColor8 sStageColor0 = {0x14, 0x65, 0xFF, 0xB9};
};  // namespace

OceanRingPipeOutside::OceanRingPipeOutside(const OceanRingPipe* pPipe)
    : LiveActor("オーシャンリングの外側"), mRingPipe(pPipe), mTexU(0.0f), mWaterPipeIndirectTex(nullptr), mWaterPipeHighLightTex(nullptr) {
    lumacore::setClass(this, lumacore::kClassId< OceanRingPipeOutside >);
}

void OceanRingPipeOutside::init(const TableIter& /*rIter*/) {
    MR::connectToScene(this, MR::MovementType_None, MR::CalcAnimType_None, MR::DrawBufferType_None, MR::DrawType_OceanRingPipeOutside);
    initMesh();
    MR::invalidateClipping(this);
    mWaterPipeIndirectTex = new GameTexture(MR::loadTexFromArc("OceanRing", "WaterPipeIndirect.bti"));
    mWaterPipeHighLightTex = new GameTexture(MR::loadTexFromArc("OceanRing", "WaterPipeHighLight.bti"));
    makeActorAppeared();
}

void OceanRingPipeOutside::movement() {
    mTexU = MR::repeat(mTexU + ::sTexSpeedU, 0.0f, 1.0f);
}

void OceanRingPipeOutside::initMesh() {
    mMesh.init(2 * mRingPipe->mNumSegments * (mRingPipe->mNumSides - 1));
    sendGD();
    mMesh.send();
}

void OceanRingPipeOutside::draw() const {
    if (!MR::isValidDraw(mRingPipe))
        return;

    const SiteMaterial material = loadMaterial();
    TypedDraw site(material, nullptr, MR::getCameraViewMtx());
    mMesh.draw(site);
}


SiteMaterial OceanRingPipeOutside::loadMaterial() const {
    
    Pos3f mtx;
    mtx.identity();
    mtx.mMtx[0][2] = mTexU;
    mtx.mMtx[1][2] = 0.0f;

    Pos3f envMtx;
    envMtx.identity();
    envMtx.set(MR::getCameraViewMtx());
    envMtx.zeroTrans();

    Pos3f scaleMtx;
    scaleMtx.identity();
    scaleMtx.scale(::sEnvMapScale);

    static Mtx texRemap = {{0.5f, 0.0f, 0.0f, 0.5f}, {0.0f, -0.5f, 0.0f, 0.5f}, {0.0f, 0.0f, 1.0f, 0.0f}};

    MR::multMtx(envMtx, scaleMtx, envMtx);
    MR::multMtx(envMtx, texRemap, envMtx);

    // runtime/src/render/materials/ocean_ring_pipe_outside.hlsl: the highlight texture (1) by the normal, warped by
    // the indirect texture (0) at sIndirectScale texels a step, over sStageColor0 with light 2's rim; opaque, depth tested
    // and written, back faces culled (or none). The alpha channel's material and ambient alphas are the drawing
    // state's (the site never sets them).
    SiteMaterial material;
    material.init(NEBULA_SHADER_OCEAN_RING_PIPE_OUTSIDE, SiteMaterial::BLEND_OPAQUE, NEBULA_COMPARE_LEQUAL,
                       true, mRingPipe->mIsCameraInside ? NEBULA_CULL_BACK : NEBULA_CULL_NONE);
    material.setMatrix(3, 0, mtx.toMtxPtr());
    material.setMatrix(3, 1, envMtx);
    material.setColor(0, ::sStageColor0.r, ::sStageColor0.g, ::sStageColor0.b, ::sStageColor0.a);
    material.constants[4] = ::sIndirectScale;
    material.constants[6] = static_cast< f32 >(mWaterPipeHighLightTex->getWidth());
    material.constants[7] = static_cast< f32 >(mWaterPipeHighLightTex->getHeight());
    material.constants[8] = 1.0f;
    material.constants[9] = MaterialAmbient::get(0)[3];
    material.setTexture(0, mWaterPipeIndirectTex);
    material.setTexture(1, mWaterPipeHighLightTex);
    return material;
}

void OceanRingPipeOutside::sendGD() {
    // One strip per side band, along the segments; U steps per segment, V per side. Sums run step by step (the order
    // fixes their rounding).
    const auto vertex = [this](u16 at, f32 s, f32 t) {
        const Vec3f& rPlace = mRingPipe->mPositions[at];
        const Vec3f& rNormal = mRingPipe->mNormals[at];
        return VertexPNT{rPlace.x, rPlace.y, rPlace.z, rNormal.x, rNormal.y, rNormal.z, s, t};
    };
    f32 farV = 1.0f;
    f32 nearV = 0.0f;

    for (s32 i = 0; i < mRingPipe->mNumSides - 1; i++) {
        f32 u = 0.0f;
        VertexPNT* vtx = mMesh.strip((u16)(mRingPipe->mNumSegments << 1));
        for (s32 j = 0; j < mRingPipe->mNumSegments; j++) {
            const s32 segStart = j * mRingPipe->mNumSides;
            *vtx++ = vertex(i + segStart + 1, u, farV);
            *vtx++ = vertex(i + segStart, u, nearV);

            u += ::sTexRateU;
        }
        nearV = farV;
        farV += ::sTexRateV;
    }
}
