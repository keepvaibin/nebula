#include "Game/Player/MarineSnow.hpp"
#include "Game/Render/TypedDraw.hpp"
#include "Game/Render/FrameList.hpp"
#include "Game/Render/TypedDraw.hpp"
#include "Game/Render/TypedDraw.hpp"
#include "Game/Util/CameraUtil.hpp"
#include "Game/Util/DirectDraw.hpp"
#include "Game/Util/MathUtil.hpp"
#include "Game/Util/MtxUtil.hpp"
#include "Game/Util/ObjUtil.hpp"
#include "Game/System/NebulaArena.hpp"
#include "Game/Render/GameTexture.hpp"

namespace {
    void sendFlake(VertexPCT*& rVtx, const Vec3f& rPoint, u32 alpha, f32 s, f32 t) {
        *rVtx++ = {rPoint.x, rPoint.y, rPoint.z, Rgba8(alpha), s, t};
    }
}  // namespace

MarineSnow::MarineSnow() : mParticleCount(16), mParticlePositions(new Vec3f[mParticleCount]), mCellSize(1000.0f) {
    for (s32 idx = 0; idx < mParticleCount; idx++) {
        Vec3f randVec;
        randVec.set(MR::getRandom() * mCellSize, MR::getRandom() * mCellSize, MR::getRandom() * mCellSize);
        mParticlePositions[idx] = randVec;
    }

    mFadeStep = 0;

    mTexture = new GameTexture(MR::loadTexFromArc("MarineSnow"));
}

void MarineSnow::view() {
    if (mFadeStep < 60) {
        mFadeStep++;
    }
}

void MarineSnow::clear() {
    if (mFadeStep != 0) {
        mFadeStep--;
    }
}

void MarineSnow::draw(const Vec3f& rPosition, const Vec3f& rUp, f32 surfaceDistance) const {
    if (mFadeStep == 0) {
        return;
    }

    f32 fadeRate = static_cast< f32 >(mFadeStep) / 60.0f;

    s32 cellX = static_cast< s32 >((rPosition.x + mCellSize / 2.0f) / mCellSize);
    s32 originX = cellX * mCellSize;
    s32 cellY = static_cast< s32 >((rPosition.y + mCellSize / 2.0f) / mCellSize);
    s32 originY = cellY * mCellSize;
    s32 cellZ = static_cast< s32 >((rPosition.z + mCellSize / 2.0f) / mCellSize);
    s32 originZ = cellZ * mCellSize;

    SiteMaterial material;
    TDDraw::setupNative(&material, 0, 1, 0);
    material.setDepth(true, NEBULA_COMPARE_LEQUAL, false);
    material.mState.point_size = 4.5f;

    static u8 phase = 0;
    phase = (phase + 1) & 3;
    u32 pointCount;
    u32 start = (mParticleCount / 2) * phase;
    u32 end = start + mParticleCount / 2;
    pointCount = mParticleCount;

    Vec3f cameraForward = -MR::getCamZdir();
    Vec3f cameraUp = MR::getCamYdir();

    Vec3f billboardRight;
    billboardRight.cross(cameraForward, cameraUp);
    MR::normalizeOrZero(&billboardRight);

    Vec3f billboardUp;
    billboardUp.cross(billboardRight, cameraForward);
    MR::normalizeOrZero(&billboardUp);

    Vec3f diagonalUp = (billboardRight + billboardUp) * 10.0f;
    Vec3f diagonalDown = (billboardRight - billboardUp) * 10.0f;

    // runtime/src/render/materials/tex_vertex_alpha.hlsl: the flake texture, its alpha times the cell's opacity (the
    // vertices' alpha); blended by alpha, depth tested.
    SiteMaterial flakes;
    TDDraw::setupNative(&flakes, 1, 1, 0);
    flakes.mShader = NEBULA_SHADER_TEX_VERTEX_ALPHA;
    flakes.constants[0] = 1.0f;
    flakes.setDepth(true, NEBULA_COMPARE_LEQUAL, false);
    flakes.setTexture(0, mTexture);

    for (s32 i = -4; i < 4; i++) {
        for (s32 j = -4; j < 4; j++) {
            for (s32 k = -4; k < 4; k++) {
                Vec3f cellOrigin;
                cellOrigin.x = originX + i * mCellSize;
                cellOrigin.y = originY + j * mCellSize;
                cellOrigin.z = originZ + k * mCellSize;
                Vec3f surfaceOffset = cellOrigin + rUp * mCellSize - rPosition;

                if (surfaceOffset.dot(rUp) > surfaceDistance) {
                    continue;
                }

                f32 cellDistance = (i >= 0 ? i : -i) + (j >= 0 ? j : -j) + (k >= 0 ? k : -k);

                s32 particleStride = 1;
                if (cellDistance > 3.0f) {
                    particleStride = 2;
                }

                if (cellDistance > 5.0f) {
                    particleStride = 4;
                }

                if (cellDistance > 8.0f) {
                    particleStride = 8;
                }

                f32 opacity = 1.0f - cellDistance / 12.0f;
                if (opacity < 0.0f) {
                    opacity = 0.0f;
                }

                opacity *= opacity * opacity;
                u32 alpha = 242.0f * opacity * fadeRate;

                if (alpha == 0) {
                    continue;
                }

                TypedDraw site(flakes, MR::tmpMtxTrans(cellOrigin), MR::getCameraViewMtx());

                u16 lastParticle;
                u16 firstParticle;

                if (opacity > 3.0f) {
                    firstParticle = start;
                    lastParticle = end;
                } else {
                    firstParticle = 0;
                    lastParticle = pointCount;
                }

                for (u16 point = firstParticle; point < lastParticle; point += particleStride) {
                    VertexPCT* vtx = site.vertices< VertexPCT >(NEBULA_PRIMITIVE_QUADS, 4);
                    {
                        sendFlake(vtx, mParticlePositions[point] - diagonalDown, alpha, 0.0f, 0.0f);

                        sendFlake(vtx, mParticlePositions[point] + diagonalUp, alpha, 1.0f, 0.0f);

                        sendFlake(vtx, mParticlePositions[point] + diagonalDown, alpha, 1.0f, 1.0f);

                        sendFlake(vtx, mParticlePositions[point] - diagonalUp, alpha, 0.0f, 1.0f);
                    }
                }
            }
        }
    }
}

