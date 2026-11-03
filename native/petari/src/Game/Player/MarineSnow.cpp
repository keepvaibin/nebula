#include "Game/Player/MarineSnow.hpp"
#include "Game/Util/NativeShader.hpp"
#include <JSystem/J3DGraphBase/J3DNativeState.hpp>
#include <JSystem/J3DGraphBase/J3DNativeShape.hpp>
#include <JSystem/J3DGraphBase/J3DNativeDraw.hpp>
#include "Game/Util/CameraUtil.hpp"
#include "Game/Util/DirectDraw.hpp"
#include "Game/Util/MathUtil.hpp"
#include "Game/Util/MtxUtil.hpp"
#include "Game/Util/ObjUtil.hpp"
#include "Game/System/NebulaArena.hpp"
#include <JSystem/JUtility/JUTTexture.hpp>

MarineSnow::MarineSnow() : mParticleCount(16), mParticlePositions(new (32) TVec3f[mParticleCount]), mCellSize(1000.0f) {
    for (s32 idx = 0; idx < mParticleCount; idx++) {
        TVec3f randVec;
        randVec.set(MR::getRandom() * mCellSize, MR::getRandom() * mCellSize, MR::getRandom() * mCellSize);
        mParticlePositions[idx] = randVec;
    }

    mFadeStep = 0;

    mTexture = new JUTTexture(MR::loadTexFromArc("MarineSnow"));
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

void MarineSnow::draw(const TVec3f& rPosition, const TVec3f& rUp, f32 surfaceDistance) const {
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

    NebulaNativeMaterial material;
    TDDraw::setupNative(&material, 0, 1, 0);
    NativeShader::setDepth(material, true, NEBULA_COMPARE_LEQUAL, false);
    NativeShader::send(material);
    J3DPass::pointSize(4.5f);

    static u8 phase = 0;
    phase = (phase + 1) & 3;
    u32 pointCount;
    u32 start = (mParticleCount / 2) * phase;
    u32 end = start + mParticleCount / 2;
    pointCount = mParticleCount;

    TVec3f cameraForward = -MR::getCamZdir();
    TVec3f cameraUp = MR::getCamYdir();

    TVec3f billboardRight;
    billboardRight.cross(cameraForward, cameraUp);
    MR::normalizeOrZero(&billboardRight);

    TVec3f billboardUp;
    billboardUp.cross(billboardRight, cameraForward);
    MR::normalizeOrZero(&billboardUp);

    TVec3f diagonalUp = (billboardRight + billboardUp) * 10.0f;
    TVec3f diagonalDown = (billboardRight - billboardUp) * 10.0f;

    // runtime/src/render/materials/tex_vertex_alpha.hlsl: the flake texture, its alpha times the cell's opacity (the
    // vertices' alpha); blended by alpha, depth tested.
    NebulaNativeMaterial flakes;
    TDDraw::setupNative(&flakes, 1, 1, 0);
    flakes.shader = NEBULA_SHADER_TEX_VERTEX_ALPHA;
    flakes.constants[0] = 1.0f;
    NativeShader::setDepth(flakes, true, NEBULA_COMPARE_LEQUAL, false);
    NativeShader::setTexture(flakes, 0, mTexture);
    NativeShader::send(flakes);

    for (s32 i = -4; i < 4; i++) {
        for (s32 j = -4; j < 4; j++) {
            for (s32 k = -4; k < 4; k++) {
                TVec3f cellOrigin;
                cellOrigin.x = originX + i * mCellSize;
                cellOrigin.y = originY + j * mCellSize;
                cellOrigin.z = originZ + k * mCellSize;
                TVec3f surfaceOffset = cellOrigin + rUp * mCellSize - rPosition;

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


                TMtx34f mtx;
                MtxPtr pTranslationMtx = MR::tmpMtxTrans(cellOrigin);
                MR::multMtx(mtx, pTranslationMtx, MR::getCameraViewMtx());
                J3DNativeShape::setPosition(0, mtx);

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
                    J3DNativeDraw::begin(NEBULA_PRIMITIVE_QUADS, 4, NEBULA_SHAPE_COLOR0 | NEBULA_SHAPE_TEXCOORD0);
                    {
                        TDDraw::sendPoint(mParticlePositions[point] - diagonalDown, alpha);
                        J3DNativeDraw::texCoord(0.0f, 0.0f);

                        TDDraw::sendPoint(mParticlePositions[point] + diagonalUp, alpha);
                        J3DNativeDraw::texCoord(1.0f, 0.0f);

                        TDDraw::sendPoint(mParticlePositions[point] + diagonalDown, alpha);
                        J3DNativeDraw::texCoord(1.0f, 1.0f);

                        TDDraw::sendPoint(mParticlePositions[point] - diagonalUp, alpha);
                        J3DNativeDraw::texCoord(0.0f, 1.0f);
                    }
                }
            }
        }
    }
}

