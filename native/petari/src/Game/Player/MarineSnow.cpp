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
#include "Game/Util/Tunable.hpp"

NEBULA_TUNABLE_NAMED(f32, cNum_draw_f1, "draw.f1", 60.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_draw_f2, "draw.f2", 2.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_draw_f3, "draw.f3", 2.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_draw_f4, "draw.f4", 2.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_draw_f5, "draw.f5", 4.5f);
NEBULA_TUNABLE_NAMED(f32, cNum_draw_f6, "draw.f6", 10.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_draw_f7, "draw.f7", 10.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_draw_f8, "draw.f8", 3.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_draw_f9, "draw.f9", 5.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_draw_f10, "draw.f10", 8.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_draw_f11, "draw.f11", 12.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_draw_f12, "draw.f12", 242.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_draw_f13, "draw.f13", 255.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_draw_f14, "draw.f14", 3.0f);

namespace {
    // The cell's opacity is the draw's constant (flakes.constants[1]), not a vertex colour.
    void sendFlake(VertexPCT*& rVtx, const Vec3f& rPoint, f32 s, f32 t) {
        *rVtx++ = {rPoint.x, rPoint.y, rPoint.z, Rgba8(255), s, t};
    }

    // The draw object of the cell at world cell (x, y, z): the same cell is the same object from step to step, whichever
    // cells around the player are drawn and wherever the player's own cell is.
    u32 cellObject(s32 x, s32 y, s32 z) {
        u32 h = static_cast< u32 >(x) * 73856093u ^ static_cast< u32 >(y) * 19349663u ^ static_cast< u32 >(z) * 83492791u;
        h ^= h >> 15;
        h *= 0x2C1B3C6Du;
        h ^= h >> 12;
        return h | 0x80000000u;
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

    f32 fadeRate = static_cast< f32 >(mFadeStep) / cNum_draw_f1;

    s32 cellX = static_cast< s32 >((rPosition.x + mCellSize / cNum_draw_f2) / mCellSize);
    s32 originX = cellX * mCellSize;
    s32 cellY = static_cast< s32 >((rPosition.y + mCellSize / cNum_draw_f3) / mCellSize);
    s32 originY = cellY * mCellSize;
    s32 cellZ = static_cast< s32 >((rPosition.z + mCellSize / cNum_draw_f4) / mCellSize);
    s32 originZ = cellZ * mCellSize;

    SiteMaterial material;
    TDDraw::setupNative(&material, 0, 1, 0);
    material.setDepth(true, NEBULA_COMPARE_LEQUAL, false);
    material.mState.point_size = cNum_draw_f5;

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

    Vec3f diagonalUp = (billboardRight + billboardUp) * cNum_draw_f6;
    Vec3f diagonalDown = (billboardRight - billboardUp) * cNum_draw_f7;

    // Each cell draws as an object of its own (the caller's slot, a generation from the cell's place in the world), and
    // the caller's object and numbers are back when the snow is done: a cell left out, or the player crossing into
    // another cell, renumbers neither the other cells nor what the caller draws after.
    const u32 callerSlot = Frame::objectSlot();
    const u32 callerGeneration = Frame::objectGeneration();
    const u32 callerPart = Frame::nextPart();

    // runtime/src/render/materials/tex_vertex_alpha.hlsl: the flake texture, its alpha times the cell's opacity (the
    // constant alpha scale, so a generated frame blends the fade of a cell between two steps); blended by alpha, depth
    // tested. The vertices' alpha is 1.
    SiteMaterial flakes;
    TDDraw::setupNative(&flakes, 1, 1, 0);
    flakes.setShader(NEBULA_SHADER_TEX_VERTEX_ALPHA);
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
                if (cellDistance > cNum_draw_f8) {
                    particleStride = 2;
                }

                if (cellDistance > cNum_draw_f9) {
                    particleStride = 4;
                }

                if (cellDistance > cNum_draw_f10) {
                    particleStride = 8;
                }

                f32 opacity = 1.0f - cellDistance / cNum_draw_f11;
                if (opacity < 0.0f) {
                    opacity = 0.0f;
                }

                opacity *= opacity * opacity;
                u32 alpha = cNum_draw_f12 * opacity * fadeRate;

                if (alpha == 0) {
                    continue;
                }

                Frame::setObject(callerSlot, ::cellObject(cellX + i, cellY + j, cellZ + k));
                flakes.constants[1] = static_cast< f32 >(alpha) / cNum_draw_f13;
                TypedDraw site(flakes, MR::tmpMtxTrans(cellOrigin), MR::getCameraViewMtx());

                u16 lastParticle;
                u16 firstParticle;

                if (opacity > cNum_draw_f14) {
                    firstParticle = start;
                    lastParticle = end;
                } else {
                    firstParticle = 0;
                    lastParticle = pointCount;
                }

                for (u16 point = firstParticle; point < lastParticle; point += particleStride) {
                    // A flake keeps its number when the cell's stride (its distance from the player) changes.
                    site.setPart(point);
                    VertexPCT* vtx = site.vertices< VertexPCT >(NEBULA_PRIMITIVE_QUADS, 4);
                    {
                        sendFlake(vtx, mParticlePositions[point] - diagonalDown, 0.0f, 0.0f);

                        sendFlake(vtx, mParticlePositions[point] + diagonalUp, 1.0f, 0.0f);

                        sendFlake(vtx, mParticlePositions[point] + diagonalDown, 1.0f, 1.0f);

                        sendFlake(vtx, mParticlePositions[point] - diagonalUp, 0.0f, 1.0f);
                    }
                }
            }
        }
    }

    Frame::setObject(callerSlot, callerGeneration);
    Frame::setPart(callerPart);
}

