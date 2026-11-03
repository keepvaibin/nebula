#include "JSystem/JParticle/JPABaseShape.hpp"
#include "JSystem/JParticle/JPANativeColor.hpp"
#include <JSystem/J3DGraphBase/J3DNativeState.hpp>
#include <JSystem/J3DGraphBase/J3DNativeShape.hpp>
#include <JSystem/J3DGraphBase/J3DNativeDraw.hpp>
#include "Game/System/NebulaArena.hpp"
#include "JSystem/JMath/JMATrigonometric.hpp"
#include "JSystem/JParticle/JPAEmitter.hpp"
#include "JSystem/JParticle/JPAParticle.hpp"
#include "JSystem/JParticle/JPAResourceManager.hpp"
#include <nebula_math.h>
#include <string.h>

// The vertices of the particles being drawn: eye-space positions, the coordinates after the particle's texture matrix
// (coordinate 0: s, t; 2: their projection's q), the corners' own (coordinate 1), its colours (env in coordinate 3's place
// by way of colour 1). Everything a particle changes is here; only a new image or size ends the draw.
namespace {
    const u32 cBatchVertices = 16384;
    f32 sBatchPos[cBatchVertices * 3];
    f32 sBatchUv0[cBatchVertices * 2];
    f32 sBatchUv1[cBatchVertices * 2];
    f32 sBatchQ[cBatchVertices * 2];
    u32 sBatchPrm[cBatchVertices];
    u32 sBatchEnv[cBatchVertices];
    u32 sBatchCount = 0;
    u8 sBatchPrimitive = NEBULA_PRIMITIVE_QUADS;

    // The particle's matrix, and its coordinates': none, two rows, or a projection (three rows).
    Mtx sParticleMtx;
    enum { TexMtx_None, TexMtx_Rows, TexMtx_Projection };
    u32 sTexMtxMode = TexMtx_None;
    f32 sTexMtx[3][4];
    // The image in place 0 (a particle's animation changes it; repeats do not end the draw).
    s32 sBatchImage = -1;

    void setParticleMtx(const Mtx mtx) {
        memcpy(sParticleMtx, mtx, sizeof(Mtx));
    }

    void setTexMtx(const Mtx mtx, u32 mode) {
        memcpy(sTexMtx, mtx, sizeof(sTexMtx));
        sTexMtxMode = mode;
    }

    void addVertex(f32 x, f32 y, f32 z, f32 s, f32 t) {
        if (sBatchCount == cBatchVertices) {
            JPAParticleBatch::flush();
        }
        const u32 v = sBatchCount++;
        f32* pPos = &sBatchPos[v * 3];
        pPos[0] = sParticleMtx[0][0] * x + sParticleMtx[0][1] * y + sParticleMtx[0][2] * z + sParticleMtx[0][3];
        pPos[1] = sParticleMtx[1][0] * x + sParticleMtx[1][1] * y + sParticleMtx[1][2] * z + sParticleMtx[1][3];
        pPos[2] = sParticleMtx[2][0] * x + sParticleMtx[2][1] * y + sParticleMtx[2][2] * z + sParticleMtx[2][3];
        sBatchUv1[v * 2] = s;
        sBatchUv1[v * 2 + 1] = t;
        f32 u = s;
        f32 w = t;
        f32 q = 1.0f;
        if (sTexMtxMode == TexMtx_Rows) {
            u = sTexMtx[0][0] * s + sTexMtx[0][1] * t + sTexMtx[0][2] + sTexMtx[0][3];
            w = sTexMtx[1][0] * s + sTexMtx[1][1] * t + sTexMtx[1][2] + sTexMtx[1][3];
        } else if (sTexMtxMode == TexMtx_Projection) {
            u = sTexMtx[0][0] * x + sTexMtx[0][1] * y + sTexMtx[0][2] * z + sTexMtx[0][3];
            w = sTexMtx[1][0] * x + sTexMtx[1][1] * y + sTexMtx[1][2] * z + sTexMtx[1][3];
            q = sTexMtx[2][0] * x + sTexMtx[2][1] * y + sTexMtx[2][2] * z + sTexMtx[2][3];
        }
        sBatchUv0[v * 2] = u;
        sBatchUv0[v * 2 + 1] = w;
        sBatchQ[v * 2] = q;
        sBatchQ[v * 2 + 1] = 0.0f;
        sBatchPrm[v] = JPANativeColor::prmWord();
        sBatchEnv[v] = JPANativeColor::envWord();
    }
}  // namespace

namespace JPAParticleBatch {
    void primitive(u8 primitive) {
        if (sBatchPrimitive != primitive || primitive == NEBULA_PRIMITIVE_TRIANGLE_STRIP) {
            flush();
            sBatchPrimitive = primitive;
        }
    }

    void vertex(f32 x, f32 y, f32 z, f32 s, f32 t) {
        addVertex(x, y, z, s, t);
    }

    void setMatrix(const f32 (*pMtx)[4]) {
        memcpy(sParticleMtx, pMtx, sizeof(Mtx));
    }

    void setProjection(const f32 (*pMtx)[4]) {
        setTexMtx(pMtx, TexMtx_Projection);
    }

    void begin() {
        flush();
        sTexMtxMode = TexMtx_None;
        sBatchImage = -1;
    }

    void flush() {
        if (sBatchCount == 0) {
            return;
        }
        // The positions are in eye space already, the coordinates final.
        Mtx identity;
        NebulaMtxIdentity(identity);
        J3DNativeShape::setPosition(0, identity);
        J3DNativeShape::setCurrentIdentity();
        const u32* const pColors[2] = {sBatchPrm, sBatchEnv};
        const f32* const pTexCoords[8] = {sBatchUv0, sBatchUv1, sBatchQ, nullptr, nullptr, nullptr, nullptr, nullptr};
        J3DNativeDraw::drawArrays(sBatchPrimitive, NEBULA_SHAPE_COLOR0 | NEBULA_SHAPE_COLOR1 | NEBULA_SHAPE_TEXCOORD0 |
                                                       (NEBULA_SHAPE_TEXCOORD0 << 1) | (NEBULA_SHAPE_TEXCOORD0 << 2),
                                  sBatchCount, sBatchPos, pColors, pTexCoords);
        sBatchCount = 0;
    }
}  // namespace JPAParticleBatch

// A particle of scale 1 as a point or line.
static const f32 cParticleSizePixels = 25.0f / 6.0f;

void JPASetPointSize(JPAEmitterWorkData* work) {
    JPAParticleBatch::flush();
    J3DPass::pointSize(cParticleSizePixels * work->mGlobalPtclScl.x, true);
}

void JPASetLineWidth(JPAEmitterWorkData* work) {
    JPAParticleBatch::flush();
    J3DPass::lineWidth(cParticleSizePixels * work->mGlobalPtclScl.x, true);
}

void JPASetPointSize(JPAEmitterWorkData* work, JPABaseParticle* ptcl) {
    JPAParticleBatch::flush();
    J3DPass::pointSize(ptcl->mParticleScaleX * cParticleSizePixels * work->mGlobalPtclScl.x, true);
}

void JPASetLineWidth(JPAEmitterWorkData* work, JPABaseParticle* ptcl) {
    JPAParticleBatch::flush();
    J3DPass::lineWidth(ptcl->mParticleScaleX * cParticleSizePixels * work->mGlobalPtclScl.x, true);
}

void JPARegistPrm(JPAEmitterWorkData* work) {
    JPABaseEmitter* emtr = work->mpEmtr;
    NebulaColor8 prm = emtr->mPrmClr;
    prm.r = COLOR_MULTI(prm.r, emtr->mGlobalPrmClr.r);
    prm.g = COLOR_MULTI(prm.g, emtr->mGlobalPrmClr.g);
    prm.b = COLOR_MULTI(prm.b, emtr->mGlobalPrmClr.b);
    prm.a = COLOR_MULTI(prm.a, emtr->mGlobalPrmClr.a);
    JPANativeColor::prm(prm);
}

void JPARegistEnv(JPAEmitterWorkData* work) {
    JPABaseEmitter* emtr = work->mpEmtr;
    NebulaColor8 env = emtr->mEnvClr;
    env.r = COLOR_MULTI(env.r, emtr->mGlobalEnvClr.r);
    env.g = COLOR_MULTI(env.g, emtr->mGlobalEnvClr.g);
    env.b = COLOR_MULTI(env.b, emtr->mGlobalEnvClr.b);
    JPANativeColor::env(env);
}

void JPARegistPrmEnv(JPAEmitterWorkData* work) {
    JPABaseEmitter* emtr = work->mpEmtr;
    NebulaColor8 prm = emtr->mPrmClr;
    NebulaColor8 env = emtr->mEnvClr;
    prm.r = COLOR_MULTI(prm.r, emtr->mGlobalPrmClr.r);
    prm.g = COLOR_MULTI(prm.g, emtr->mGlobalPrmClr.g);
    prm.b = COLOR_MULTI(prm.b, emtr->mGlobalPrmClr.b);
    prm.a = COLOR_MULTI(prm.a, emtr->mGlobalPrmClr.a);
    env.r = COLOR_MULTI(env.r, emtr->mGlobalEnvClr.r);
    env.g = COLOR_MULTI(env.g, emtr->mGlobalEnvClr.g);
    env.b = COLOR_MULTI(env.b, emtr->mGlobalEnvClr.b);
    JPANativeColor::prmEnv(prm, env);
}

void JPARegistAlpha(JPAEmitterWorkData* work, JPABaseParticle* ptcl) {
    JPABaseEmitter* emtr = work->mpEmtr;
    NebulaColor8 prm = emtr->mPrmClr;
    prm.r = COLOR_MULTI(prm.r, emtr->mGlobalPrmClr.r);
    prm.g = COLOR_MULTI(prm.g, emtr->mGlobalPrmClr.g);
    prm.b = COLOR_MULTI(prm.b, emtr->mGlobalPrmClr.b);
    prm.a = COLOR_MULTI(prm.a, emtr->mGlobalPrmClr.a);
    prm.a = COLOR_MULTI(prm.a, ptcl->mPrmColorAlphaAnm);
    JPANativeColor::prm(prm);
}

void JPARegistPrmAlpha(JPAEmitterWorkData* work, JPABaseParticle* ptcl) {
    JPABaseEmitter* emtr = work->mpEmtr;
    NebulaColor8 prm = ptcl->mPrmClr;
    prm.r = COLOR_MULTI(prm.r, emtr->mGlobalPrmClr.r);
    prm.g = COLOR_MULTI(prm.g, emtr->mGlobalPrmClr.g);
    prm.b = COLOR_MULTI(prm.b, emtr->mGlobalPrmClr.b);
    prm.a = COLOR_MULTI(prm.a, emtr->mGlobalPrmClr.a);
    prm.a = COLOR_MULTI(prm.a, ptcl->mPrmColorAlphaAnm);
    JPANativeColor::prm(prm);
}

void JPARegistPrmAlphaEnv(JPAEmitterWorkData* work, JPABaseParticle* ptcl) {
    JPABaseEmitter* emtr = work->mpEmtr;
    NebulaColor8 prm = ptcl->mPrmClr;
    NebulaColor8 env = ptcl->mEnvClr;
    prm.r = COLOR_MULTI(prm.r, emtr->mGlobalPrmClr.r);
    prm.g = COLOR_MULTI(prm.g, emtr->mGlobalPrmClr.g);
    prm.b = COLOR_MULTI(prm.b, emtr->mGlobalPrmClr.b);
    prm.a = COLOR_MULTI(prm.a, emtr->mGlobalPrmClr.a);
    prm.a = COLOR_MULTI(prm.a, ptcl->mPrmColorAlphaAnm);
    env.r = COLOR_MULTI(env.r, emtr->mGlobalEnvClr.r);
    env.g = COLOR_MULTI(env.g, emtr->mGlobalEnvClr.g);
    env.b = COLOR_MULTI(env.b, emtr->mGlobalEnvClr.b);
    JPANativeColor::prmEnv(prm, env);
}

void JPARegistAlphaEnv(JPAEmitterWorkData* work, JPABaseParticle* ptcl) {
    JPABaseEmitter* emtr = work->mpEmtr;
    NebulaColor8 prm = emtr->mPrmClr;
    NebulaColor8 env = ptcl->mEnvClr;
    prm.r = COLOR_MULTI(prm.r, emtr->mGlobalPrmClr.r);
    prm.g = COLOR_MULTI(prm.g, emtr->mGlobalPrmClr.g);
    prm.b = COLOR_MULTI(prm.b, emtr->mGlobalPrmClr.b);
    prm.a = COLOR_MULTI(prm.a, emtr->mGlobalPrmClr.a);
    prm.a = COLOR_MULTI(prm.a, ptcl->mPrmColorAlphaAnm);
    env.r = COLOR_MULTI(env.r, emtr->mGlobalEnvClr.r);
    env.g = COLOR_MULTI(env.g, emtr->mGlobalEnvClr.g);
    env.b = COLOR_MULTI(env.b, emtr->mGlobalEnvClr.b);
    JPANativeColor::prmEnv(prm, env);
}

void JPARegistEnv(JPAEmitterWorkData* work, JPABaseParticle* ptcl) {
    JPABaseEmitter* emtr = work->mpEmtr;
    NebulaColor8 env = ptcl->mEnvClr;
    env.r = COLOR_MULTI(env.r, emtr->mGlobalEnvClr.r);
    env.g = COLOR_MULTI(env.g, emtr->mGlobalEnvClr.g);
    env.b = COLOR_MULTI(env.b, emtr->mGlobalEnvClr.b);
    JPANativeColor::env(env);
}

void JPACalcClrIdxNormal(JPAEmitterWorkData* work) {
    JPABaseShape* bsp = work->mpRes->getBsp();
    s16 keyFrame;
    if (work->mpEmtr->mTick < bsp->getClrAnmMaxFrm()) {
        keyFrame = work->mpEmtr->mTick;
    } else {
        keyFrame = bsp->getClrAnmMaxFrm();
    }

    work->mClrKeyFrame = keyFrame;
}

void JPACalcClrIdxNormal(JPAEmitterWorkData* work, JPABaseParticle* param_1) {
    JPABaseShape* const shape = work->mpRes->getBsp();
    s16 keyFrame;
    if (param_1->mAge < shape->getClrAnmMaxFrm()) {
        keyFrame = param_1->mAge;
    } else {
        keyFrame = shape->getClrAnmMaxFrm();
    }

    work->mClrKeyFrame = keyFrame;
}

void JPACalcClrIdxRepeat(JPAEmitterWorkData* work) {
    JPABaseShape* const shape = work->mpRes->getBsp();
    work->mClrKeyFrame = work->mpEmtr->mTick % (shape->getClrAnmMaxFrm() + 1);
}

void JPACalcClrIdxRepeat(JPAEmitterWorkData* work, JPABaseParticle* param_1) {
    JPABaseShape* const shape = work->mpRes->getBsp();
    s32 age = param_1->mAge;
    s32 tick = age + shape->getClrLoopOfst(param_1->mAnmRandom);
    work->mClrKeyFrame = tick % (shape->getClrAnmMaxFrm() + 1);
}

void JPACalcClrIdxReverse(JPAEmitterWorkData* work) {
    JPABaseShape* const shape = work->mpRes->getBsp();
    int maxFrm = shape->getClrAnmMaxFrm();
    u32 tick = work->mpEmtr->mTick;
    u32 uVar1 = maxFrm != 0 ? tick / maxFrm : 0;
    tick = maxFrm != 0 ? tick % maxFrm : tick;
    uVar1 &= 1;
    work->mClrKeyFrame = tick + (uVar1) * (maxFrm - tick * 2);
}

void JPACalcClrIdxReverse(JPAEmitterWorkData* work, JPABaseParticle* param_1) {
    JPABaseShape* const shape = work->mpRes->getBsp();
    s32 tick = param_1->mAge + shape->getClrLoopOfst(param_1->mAnmRandom);
    s32 maxFrm = shape->getClrAnmMaxFrm();
    s32 rem = maxFrm != 0 ? tick % maxFrm : tick;
    s32 div = maxFrm != 0 ? tick / maxFrm : tick < 0 ? -1 : 0;
    work->mClrKeyFrame = rem + (div & 1) * (maxFrm - rem * 2);
}

void JPACalcClrIdxMerge(JPAEmitterWorkData* work) {
    work->mClrKeyFrame = 0;
}

void JPACalcClrIdxMerge(JPAEmitterWorkData* work, JPABaseParticle* param_1) {
    JPABaseShape* const shape = work->mpRes->getBsp();
    s32 maxFrm = shape->getClrAnmMaxFrm() + 1;
    s32 tick = (s32)(param_1->mTime * maxFrm) + shape->getClrLoopOfst(param_1->mAnmRandom);
    work->mClrKeyFrame = tick % maxFrm;
}

void JPACalcClrIdxRandom(JPAEmitterWorkData* work) {
    work->mClrKeyFrame = 0;
}

void JPACalcClrIdxRandom(JPAEmitterWorkData* work, JPABaseParticle* param_1) {
    JPABaseShape* const shape = work->mpRes->getBsp();
    s32 tick = shape->getClrLoopOfst(param_1->mAnmRandom);
    s32 maxFrm = shape->getClrAnmMaxFrm() + 1;
    work->mClrKeyFrame = tick % maxFrm;
}

void JPACalcPrm(JPAEmitterWorkData* work) {
    work->mpRes->getBsp()->getPrmClr(work->mClrKeyFrame, &work->mpEmtr->mPrmClr);
}

void JPACalcPrm(JPAEmitterWorkData* work, JPABaseParticle* ptcl) {
    work->mpRes->getBsp()->getPrmClr(work->mClrKeyFrame, &ptcl->mPrmClr);
}

void JPACalcEnv(JPAEmitterWorkData* work) {
    work->mpRes->getBsp()->getEnvClr(work->mClrKeyFrame, &work->mpEmtr->mEnvClr);
}

void JPACalcEnv(JPAEmitterWorkData* work, JPABaseParticle* ptcl) {
    work->mpRes->getBsp()->getEnvClr(work->mClrKeyFrame, &ptcl->mEnvClr);
}

void JPACalcColorCopy(JPAEmitterWorkData* work, JPABaseParticle* ptcl) {
    JPABaseEmitter* emtr = work->mpEmtr;
    ptcl->mPrmClr = emtr->mPrmClr;
    ptcl->mEnvClr = emtr->mEnvClr;
}

void JPAGenTexCrdMtxIdt(JPAEmitterWorkData* param_0) {
    sTexMtxMode = TexMtx_None;
}

void JPAGenTexCrdMtxAnm(JPAEmitterWorkData* param_0) {
}

void JPAGenTexCrdMtxPrj(JPAEmitterWorkData* param_0) {
}

void JPAGenCalcTexCrdMtxAnm(JPAEmitterWorkData* work) {
    JPABaseShape* const shape = work->mpRes->getBsp();
    f32 dVar16 = work->mpEmtr->mTick;
    f32 dVar15 = 0.5f * (1.0f + shape->getTilingS());
    f32 dVar14 = 0.5f * (1.0f + shape->getTilingT());
    f32 dVar11 = (dVar16 * shape->getIncTransX()) + shape->getInitTransX();
    f32 dVar10 = (dVar16 * shape->getIncTransY()) + shape->getInitTransY();
    f32 dVar13 = (dVar16 * shape->getIncScaleX()) + shape->getInitScaleX();
    f32 dVar12 = (dVar16 * shape->getIncScaleY()) + shape->getInitScaleY();
    s32 local_c0 = (dVar16 * shape->getIncRot()) + shape->getInitRot();
    f32 dVar8 = JMASSin(local_c0);
    f32 dVar9 = JMASCos(local_c0);
    Mtx local_108;
    local_108[0][0] = dVar13 * dVar9;
    local_108[0][1] = -dVar13 * dVar8;
    local_108[0][2] = 0.0f;
    local_108[0][3] = (dVar15 + (dVar13 * ((dVar8 * (dVar14 + dVar10)) - (dVar9 * (dVar15 + dVar11)))));
    local_108[1][0] = dVar12 * dVar8;
    local_108[1][1] = dVar12 * dVar9;
    local_108[1][2] = 0.0f;
    local_108[1][3] = (dVar14 + (-dVar12 * ((dVar8 * (dVar15 + dVar11)) + (dVar9 * (dVar14 + dVar10)))));
    local_108[2][0] = 0.0f;
    local_108[2][1] = 0.0f;
    local_108[2][2] = 1.0f;
    local_108[2][3] = 0.0f;
    setTexMtx(local_108, TexMtx_Rows);
}

void JPALoadCalcTexCrdMtxAnm(JPAEmitterWorkData* work, JPABaseParticle* param_1) {
    JPABaseShape* const shape = work->mpRes->getBsp();
    f32 dVar16 = param_1->mAge;
    f32 dVar15 = 0.5f * (1.0f + shape->getTilingS());
    f32 dVar14 = 0.5f * (1.0f + shape->getTilingT());
    f32 dVar11 = (dVar16 * shape->getIncTransX()) + shape->getInitTransX();
    f32 dVar10 = (dVar16 * shape->getIncTransY()) + shape->getInitTransY();
    f32 dVar13 = (dVar16 * shape->getIncScaleX()) + shape->getInitScaleX();
    f32 dVar12 = (dVar16 * shape->getIncScaleY()) + shape->getInitScaleY();
    s32 local_c0 = (dVar16 * shape->getIncRot()) + shape->getInitRot();
    f32 dVar8 = JMASSin(local_c0);
    f32 dVar9 = JMASCos(local_c0);
    Mtx local_108;
    local_108[0][0] = dVar13 * dVar9;
    local_108[0][1] = -dVar13 * dVar8;
    local_108[0][2] = 0.0f;
    local_108[0][3] = (dVar15 + (dVar13 * ((dVar8 * (dVar14 + dVar10)) - (dVar9 * (dVar15 + dVar11)))));
    local_108[1][0] = dVar12 * dVar8;
    local_108[1][1] = dVar12 * dVar9;
    local_108[1][2] = 0.0f;
    local_108[1][3] = (dVar14 + (-dVar12 * ((dVar8 * (dVar15 + dVar11)) + (dVar9 * (dVar14 + dVar10)))));
    local_108[2][0] = 0.0f;
    local_108[2][1] = 0.0f;
    local_108[2][2] = 1.0f;
    local_108[2][3] = 0.0f;
    setTexMtx(local_108, TexMtx_Rows);
}

namespace {
    void loadImage(JPAEmitterWorkData* work, s32 image) {
        if (image != sBatchImage) {
            JPAParticleBatch::flush();
            sBatchImage = image;
            work->mpResMgr->use(image, 0);
        }
    }
}  // namespace

void JPALoadTex(JPAEmitterWorkData* work) {
    loadImage(work, work->mpRes->getTexIdx(work->mpRes->getBsp()->getTexIdx()));
}

void JPALoadTexAnm(JPAEmitterWorkData* work) {
    loadImage(work, work->mpRes->getTexIdx(work->mpEmtr->mTexAnmIdx));
}

void JPALoadTexAnm(JPAEmitterWorkData* work, JPABaseParticle* ptcl) {
    loadImage(work, work->mpRes->getTexIdx(ptcl->mTexAnmIdx));
}

void JPACalcTexIdxNormal(JPAEmitterWorkData* work) {
    JPABaseShape* const shape = work->mpRes->getBsp();
    u8 tick = shape->getTexAnmKeyNum() - 1 < work->mpEmtr->mTick ? shape->getTexAnmKeyNum() - 1 : work->mpEmtr->mTick;
    work->mpEmtr->mTexAnmIdx = shape->getTexIdx(tick);
}

void JPACalcTexIdxNormal(JPAEmitterWorkData* work, JPABaseParticle* param_1) {
    JPABaseShape* const shape = work->mpRes->getBsp();
    u8 tick = shape->getTexAnmKeyNum() - 1 < param_1->mAge ? shape->getTexAnmKeyNum() - 1 : param_1->mAge;
    param_1->mTexAnmIdx = shape->getTexIdx(tick);
}

void JPACalcTexIdxRepeat(JPAEmitterWorkData* work) {
    JPABaseShape* const shape = work->mpRes->getBsp();
    work->mpEmtr->mTexAnmIdx = shape->getTexIdx(shape->getTexAnmKeyNum() != 0 ? work->mpEmtr->mTick % shape->getTexAnmKeyNum() : work->mpEmtr->mTick);
}

void JPACalcTexIdxRepeat(JPAEmitterWorkData* work, JPABaseParticle* param_1) {
    JPABaseShape* const shape = work->mpRes->getBsp();
    const int frame = (int)shape->getTexLoopOfst(param_1->mAnmRandom) + param_1->mAge;
    u8 texIdx = shape->getTexAnmKeyNum() != 0 ? frame % shape->getTexAnmKeyNum() : frame;
    param_1->mTexAnmIdx = shape->getTexIdx(texIdx);
}

void JPACalcTexIdxReverse(JPAEmitterWorkData* work) {
    JPABaseShape* const shape = work->mpRes->getBsp();
    int tick = work->mpEmtr->mTick;
    int keyNum = (int)shape->getTexAnmKeyNum() - 1;
    int div = keyNum != 0 ? tick / keyNum : tick < 0 ? -1 : 0;
    int rem = keyNum != 0 ? tick % keyNum : tick;
    work->mpEmtr->mTexAnmIdx = shape->getTexIdx(rem + (div & 1) * (keyNum - rem * 2));
}

void JPACalcTexIdxReverse(JPAEmitterWorkData* work, JPABaseParticle* param_1) {
    JPABaseShape* const shape = work->mpRes->getBsp();
    s32 tick = shape->getTexLoopOfst(param_1->mAnmRandom) + param_1->mAge;
    int keyNum = (int)shape->getTexAnmKeyNum() - 1;
    int div = keyNum != 0 ? tick / keyNum : tick < 0 ? -1 : 0;
    int rem = keyNum != 0 ? tick % keyNum : tick;
    u8 texIdx = rem + (div & 1) * (keyNum - rem * 2);
    param_1->mTexAnmIdx = work->mpRes->getBsp()->getTexIdx(texIdx);
}

void JPACalcTexIdxMerge(JPAEmitterWorkData* work) {
    work->mpEmtr->mTexAnmIdx = work->mpRes->getBsp()->getTexIdx();
}

void JPACalcTexIdxMerge(JPAEmitterWorkData* work, JPABaseParticle* param_1) {
    JPABaseShape* const shape = work->mpRes->getBsp();
    s32 maxFrm = shape->getTexAnmKeyNum();
    s32 tick = (s32)(maxFrm * param_1->mTime) + shape->getTexLoopOfst(param_1->mAnmRandom);
    u8 texIdx = maxFrm != 0 ? tick % maxFrm : tick;
    param_1->mTexAnmIdx = work->mpRes->getBsp()->getTexIdx(texIdx);
}

void JPACalcTexIdxRandom(JPAEmitterWorkData* work) {
    work->mpEmtr->mTexAnmIdx = work->mpRes->getBsp()->getTexIdx();
}

void JPACalcTexIdxRandom(JPAEmitterWorkData* work, JPABaseParticle* param_1) {
    JPABaseShape* const shape = work->mpRes->getBsp();
    const int frame = shape->getTexLoopOfst(param_1->mAnmRandom);
    u8 texIdx = shape->getTexAnmKeyNum() != 0 ? frame % shape->getTexAnmKeyNum() : frame;
    param_1->mTexAnmIdx = shape->getTexIdx(texIdx);
}

void JPALoadPosMtxCam(JPAEmitterWorkData* work) {
    setParticleMtx(work->mPosCamMtx);
}

static void noLoadPrj(JPAEmitterWorkData const* work, const Mtx srt) {
    /* empty function */
}

void loadPrj(JPAEmitterWorkData const* work, const Mtx srt) {
    Mtx mtx;
    NebulaMtxConcat(work->mPrjMtx, srt, mtx);
    setTexMtx(mtx, TexMtx_Projection);
}

static void loadPrjAnm(JPAEmitterWorkData const* work, const Mtx srt) {
    f32 dVar16 = work->mpEmtr->getAge();
    JPABaseShape* const shape = work->mpRes->getBsp();
    f32 dVar15 = 0.5f * (1.0f + shape->getTilingS());
    f32 dVar14 = 0.5f * (1.0f + shape->getTilingT());
    f32 dVar11 = (dVar16 * shape->getIncTransX()) + shape->getInitTransX();
    f32 dVar10 = (dVar16 * shape->getIncTransY()) + shape->getInitTransY();
    f32 dVar13 = (dVar16 * shape->getIncScaleX()) + shape->getInitScaleX();
    f32 dVar12 = (dVar16 * shape->getIncScaleY()) + shape->getInitScaleY();
    s32 local_c0 = (dVar16 * shape->getIncRot()) + shape->getInitRot();
    f32 dVar8 = JMASSin(local_c0);
    f32 dVar9 = JMASCos(local_c0);
    Mtx local_108;
    local_108[0][0] = dVar13 * dVar9;
    local_108[0][1] = -dVar13 * dVar8;
    local_108[0][2] = (dVar15 + (dVar13 * ((dVar8 * (dVar14 + dVar10)) - (dVar9 * (dVar15 + dVar11)))));
    local_108[0][3] = 0.0f;
    local_108[1][0] = dVar12 * dVar8;
    local_108[1][1] = dVar12 * dVar9;
    local_108[1][2] = (dVar14 + (-dVar12 * ((dVar8 * (dVar15 + dVar11)) + (dVar9 * (dVar14 + dVar10)))));
    local_108[1][3] = 0.0f;
    local_108[2][0] = 0.0f;
    local_108[2][1] = 0.0f;
    local_108[2][2] = 1.0f;
    local_108[2][3] = 0.0f;
    NebulaMtxConcat(local_108, work->mPrjMtx, local_108);
    NebulaMtxConcat(local_108, srt, local_108);
    setTexMtx(local_108, TexMtx_Projection);
}

namespace {
    // The quad arrays JPAResource set for the particles being drawn: positions 3 signed bytes, texture coordinates 2.
    const u8* sQuadPositions = nullptr;
    const u8* sQuadTexCoords = nullptr;

    void addQuadVertex(u32 position, u32 texCoord) {
        const s8* pPos = reinterpret_cast< const s8* >(sQuadPositions) + 3 * position;
        const s8* pCrd = reinterpret_cast< const s8* >(sQuadTexCoords) + 2 * texCoord;
        addVertex(pPos[0], pPos[1], pPos[2], pCrd[0], pCrd[1]);
    }
}  // namespace

void JPASetQuadArrays(const u8* pPositions, const u8* pTexCoords) {
    sQuadPositions = pPositions;
    sQuadTexCoords = pTexCoords;
}

void JPADrawQuad(u32 type) {
    // Positions 0-3 with texture coordinates 0-3; the crossed type adds positions 0x48-0x4B with the same coordinates.
    if (sBatchPrimitive != NEBULA_PRIMITIVE_QUADS) {
        JPAParticleBatch::flush();
        sBatchPrimitive = NEBULA_PRIMITIVE_QUADS;
    }
    if (sBatchCount + 8 > cBatchVertices) {
        JPAParticleBatch::flush();
    }
    for (u32 i = 0; i < 4; i++) {
        addQuadVertex(i, i);
    }
    if (type != 0) {
        for (u32 i = 0; i < 4; i++) {
            addQuadVertex(0x48 + i, i);
        }
    }
}

typedef void (*projectionFunc)(JPAEmitterWorkData const*, const Mtx);

static projectionFunc p_prj[3] = {
    noLoadPrj,
    loadPrj,
    loadPrjAnm,
};

void JPADrawBillboard(JPAEmitterWorkData* work, JPABaseParticle* param_1) {
    if (param_1->checkStatus(8) == 0) {
        JGeometry::TVec3< f32 > local_48;
        NebulaMtxMultVec(work->mPosCamMtx, &param_1->mPosition, &local_48);
        Mtx local_38;
        local_38[0][0] = work->mGlobalPtclScl.x * param_1->mParticleScaleX;
        local_38[0][3] = local_48.x;
        local_38[1][1] = work->mGlobalPtclScl.y * param_1->mParticleScaleY;
        local_38[1][3] = local_48.y;
        local_38[2][2] = 1.0f;
        local_38[2][3] = local_48.z;
        local_38[2][1] = 0.0f;
        local_38[2][0] = 0.0f;
        local_38[1][2] = 0.0f;
        local_38[1][0] = 0.0f;
        local_38[0][2] = 0.0f;
        local_38[0][1] = 0.0f;
        setParticleMtx(local_38);
        p_prj[work->mPrjType](work, local_38);
        JPADrawQuad(0);
    }
}

void JPADrawRotBillboard(JPAEmitterWorkData* work, JPABaseParticle* param_1) {
    if (param_1->checkStatus(8) == 0) {
        JGeometry::TVec3< f32 > local_48;
        NebulaMtxMultVec(work->mPosCamMtx, &param_1->mPosition, &local_48);
        f32 sinRot = JMASSin(param_1->mRotateAngle);
        f32 cosRot = JMath::sSinCosTable.cosShort(param_1->mRotateAngle);
        f32 particleX = work->mGlobalPtclScl.x * param_1->mParticleScaleX;
        f32 particleY = work->mGlobalPtclScl.y * param_1->mParticleScaleY;

        Mtx local_38;
        local_38[0][0] = cosRot * particleX;
        local_38[0][1] = -sinRot * particleY;
        local_38[0][3] = local_48.x;
        local_38[1][0] = sinRot * particleX;
        local_38[1][1] = cosRot * particleY;
        local_38[1][3] = local_48.y;
        local_38[2][2] = 1.0f;
        local_38[2][3] = local_48.z;
        local_38[2][1] = 0.0f;
        local_38[2][0] = 0.0f;
        local_38[1][2] = 0.0f;
        local_38[0][2] = 0.0f;
        setParticleMtx(local_38);
        p_prj[work->mPrjType](work, local_38);
        JPADrawQuad(0);
    }
}

void dirTypeVel(JPAEmitterWorkData const* work, JPABaseParticle const* param_1, JGeometry::TVec3< f32 >* param_2) {
    param_1->getVelVec(*param_2);
}

void dirTypePos(JPAEmitterWorkData const* work, JPABaseParticle const* param_1, JGeometry::TVec3< f32 >* param_2) {
    param_1->getLocalPosition(*param_2);
}

void dirTypePosInv(JPAEmitterWorkData const* work, JPABaseParticle const* param_1, JGeometry::TVec3< f32 >* param_2) {
    param_1->getLocalPosition(*param_2);
    param_2->negate();
}

void dirTypeEmtrDir(JPAEmitterWorkData const* work, JPABaseParticle const* param_1, JGeometry::TVec3< f32 >* param_2) {
    param_2->set(work->mGlobalEmtrDir);
}

void dirTypePrevPtcl(JPAEmitterWorkData const* work, JPABaseParticle const* param_1, JGeometry::TVec3< f32 >* param_2) {
    JGeometry::TVec3< f32 > aTStack_24;
    param_1->getGlobalPosition(aTStack_24);
    JPANode< JPABaseParticle >* end = work->mpAlivePtcl->getEnd();
    JPANode< JPABaseParticle >* prev = work->mpCurNode->getPrev();

    if (end != prev) {
        JPABaseParticle* particle = work->mpCurNode->getPrev()->getObject();
        particle->getGlobalPosition(*param_2);
    } else {
        work->mpEmtr->calcEmitterGlobalPosition(param_2);
    }

    param_2->sub(aTStack_24);
}

static void rotTypeY(f32 param_0, f32 param_1, Mtx& param_2) {
    param_2[0][0] = param_1;
    param_2[0][1] = 0.0f;
    param_2[0][2] = -param_0;
    param_2[0][3] = 0.0f;
    param_2[1][0] = 0.0f;
    param_2[1][1] = 1.0f;
    param_2[1][2] = 0.0f;
    param_2[1][3] = 0.0f;
    param_2[2][0] = param_0;
    param_2[2][1] = 0.0f;
    param_2[2][2] = param_1;
    param_2[2][3] = 0.0f;
}

static void rotTypeX(f32 param_0, f32 param_1, Mtx& param_2) {
    param_2[0][0] = 1.0f;
    param_2[0][1] = 0.0f;
    param_2[0][2] = 0.0f;
    param_2[0][3] = 0.0f;
    param_2[1][0] = 0.0f;
    param_2[1][1] = param_1;
    param_2[1][2] = -param_0;
    param_2[1][3] = 0.0f;
    param_2[2][0] = 0.0f;
    param_2[2][1] = param_0;
    param_2[2][2] = param_1;
    param_2[2][3] = 0.0f;
}

static void rotTypeZ(f32 param_0, f32 param_1, Mtx& param_2) {
    param_2[0][0] = param_1;
    param_2[0][1] = -param_0;
    param_2[0][2] = 0.0f;
    param_2[0][3] = 0.0f;
    param_2[1][0] = param_0;
    param_2[1][1] = param_1;
    param_2[1][2] = 0.0f;
    param_2[1][3] = 0.0f;
    param_2[2][0] = 0.0f;
    param_2[2][1] = 0.0f;
    param_2[2][2] = 1.0f;
    param_2[2][3] = 0.0f;
}

static void rotTypeXYZ(f32 sin, f32 cos, Mtx& rMtx) {
    f32 third = 0.333333f * (1.0f - cos);
    f32 scaledSin = 0.57735f * sin;
    f32 plus = third + scaledSin;
    rMtx[0][0] = (third + cos);
    rMtx[0][1] = (third - scaledSin);
    rMtx[0][2] = plus;
    rMtx[0][3] = 0.0f;
    rMtx[1][0] = plus;
    rMtx[1][1] = (third + cos);
    rMtx[1][2] = (third - scaledSin);
    rMtx[1][3] = 0.0f;
    rMtx[2][0] = (third - scaledSin);
    rMtx[2][1] = plus;
    rMtx[2][2] = (third + cos);
    rMtx[2][3] = 0.0f;
}

static void basePlaneTypeXY(MtxPtr param_0, f32 param_1, f32 param_2) {
    param_0[0][0] *= param_1;
    param_0[1][0] *= param_1;
    param_0[2][0] *= param_1;
    param_0[0][1] *= param_2;
    param_0[1][1] *= param_2;
    param_0[2][1] *= param_2;
}

static void basePlaneTypeXZ(MtxPtr param_0, f32 param_1, f32 param_2) {
    param_0[0][0] *= param_1;
    param_0[1][0] *= param_1;
    param_0[2][0] *= param_1;
    param_0[0][2] *= param_2;
    param_0[1][2] *= param_2;
    param_0[2][2] *= param_2;
}

static void basePlaneTypeX(MtxPtr param_0, f32 param_1, f32 param_2) {
    param_0[0][0] *= param_1;
    param_0[1][0] *= param_1;
    param_0[2][0] *= param_1;
    param_0[0][1] *= param_2;
    param_0[1][1] *= param_2;
    param_0[2][1] *= param_2;
    param_0[0][2] *= param_1;
    param_0[1][2] *= param_1;
    param_0[2][2] *= param_1;
}

typedef void (*dirTypeFunc)(JPAEmitterWorkData const*, JPABaseParticle const*, JGeometry::TVec3< f32 >*);

typedef void (*rotTypeFunc)(f32, f32, Mtx&);
static rotTypeFunc p_rot[5] = {
    rotTypeY, rotTypeX, rotTypeZ, rotTypeXYZ, rotTypeY,
};

typedef void (*planeFunc)(MtxPtr, f32, f32);

static planeFunc p_plane[3] = {
    basePlaneTypeXY,
    basePlaneTypeXZ,
    basePlaneTypeX,
};

void JPADrawRotation(JPAEmitterWorkData* param_0, JPABaseParticle* param_1) {
    if (param_1->checkStatus(8) == 0) {
        f32 sinRot = JMASSin(param_1->mRotateAngle);
        f32 cosRot = JMath::sSinCosTable.cosShort(param_1->mRotateAngle);
        f32 particleX = param_0->mGlobalPtclScl.x * param_1->mParticleScaleX;
        f32 particleY = param_0->mGlobalPtclScl.y * param_1->mParticleScaleY;
        Mtx auStack_88;
        p_rot[param_0->mRotType](sinRot, cosRot, auStack_88);
        p_plane[param_0->mPlaneType](auStack_88, particleX, particleY);
        auStack_88[0][3] = param_1->mPosition.x;
        auStack_88[1][3] = param_1->mPosition.y;
        auStack_88[2][3] = param_1->mPosition.z;
        NebulaMtxConcat(param_0->mPosCamMtx, auStack_88, auStack_88);
        setParticleMtx(auStack_88);
        p_prj[param_0->mPrjType](param_0, auStack_88);
        JPADrawQuad(param_0->mListType);
    }
}

void JPADrawPoint(JPAEmitterWorkData* work, JPABaseParticle* ptcl) {
    if (!!(ptcl->mStatus & JPAPtclStts_Invisible)) {
        return;
    }

    if (sBatchPrimitive != NEBULA_PRIMITIVE_POINTS) {
        JPAParticleBatch::flush();
        sBatchPrimitive = NEBULA_PRIMITIVE_POINTS;
    }
    addVertex(ptcl->mPosition.x, ptcl->mPosition.y, ptcl->mPosition.z, 0.0f, 0.0f);
}


JPANode< JPABaseParticle >* getNext(JPANode< JPABaseParticle >* param_0) {
    return param_0->getNext();
}

JPANode< JPABaseParticle >* getPrev(JPANode< JPABaseParticle >* param_0) {
    return param_0->getPrev();
}

typedef JPANode< JPABaseParticle >* (*getNodeFunc)(JPANode< JPABaseParticle >*);



void JPADrawEmitterCallBackB(JPAEmitterWorkData* work) {
    if (work->mpEmtr->mpEmtrCallBack == nullptr) {
        return;
    }

    work->mpEmtr->mpEmtrCallBack->draw(work->mpEmtr);
}

void JPADrawParticleCallBack(JPAEmitterWorkData* work, JPABaseParticle* ptcl) {
    if (work->mpEmtr->mpPtclCallBack == nullptr) {
        return;
    }

    work->mpEmtr->mpPtclCallBack->draw(work->mpEmtr, ptcl);
}

static void makeColorTable(NebulaColor8** o_color_table, JPAClrAnmKeyData const* i_data, u8 param_2, s16 i_size, NebulaArena* i_heap) {
    NebulaColor8* color_table = (NebulaColor8*)NebulaArena::alloc((i_size + 1) * 4, 4, i_heap);
    f32 next_r, next_g, next_b, next_a;
    f32 r_step, g_step, b_step, a_step, base_step;
    r_step = g_step = b_step = a_step = 0.0f;

    f32 r = i_data[0].color.r;
    f32 g = i_data[0].color.g;
    f32 b = i_data[0].color.b;
    f32 a = i_data[0].color.a;

    int j = 0;
    for (s16 i = 0; i < i_size + 1; i++) {
        if (i == i_data[j].index) {
            color_table[i] = i_data[j].color;
            r = i_data[j].color.r;
            g = i_data[j].color.g;
            b = i_data[j].color.b;
            a = i_data[j].color.a;
            j++;

            if (j < param_2) {
                next_r = i_data[j].color.r;
                next_g = i_data[j].color.g;
                next_b = i_data[j].color.b;
                next_a = i_data[j].color.a;
                base_step = 1.0f / (i_data[j].index - i_data[j - 1].index);
                r_step = base_step * (next_r - r);
                g_step = base_step * (next_g - g);
                b_step = base_step * (next_b - b);
                a_step = base_step * (next_a - a);
            } else {
                r_step = g_step = b_step = a_step = 0.0f;
            }
        } else {
            r += r_step;
            color_table[i].r = (s32)(r);
            g += g_step;
            color_table[i].g = (s32)(g);
            b += b_step;
            color_table[i].b = (s32)(b);
            a += a_step;
            color_table[i].a = (s32)(a);
        }
    }

    *o_color_table = color_table;
}

JPABaseShape::JPABaseShape(u8 const* pData, NebulaArena* pHeap) {
    mpData = (const JPABaseShapeData*)pData;

    if (isTexCrdAnm()) {
        mpTexCrdMtxAnmTbl = (const void*)(pData + sizeof(JPABaseShapeData));
    } else {
        mpTexCrdMtxAnmTbl = nullptr;
    }

    if (isTexAnm()) {
        u32 offs = sizeof(JPABaseShapeData);
        if (isTexCrdAnm()) {
            offs = sizeof(JPABaseShapeData) + 0x28;
        }

        mpTexIdxAnimTbl = (const u8*)(pData + offs);
    } else {
        mpTexIdxAnimTbl = nullptr;
    }

    if (isPrmAnm()) {
        makeColorTable(&mpPrmClrAnmTbl, (JPAClrAnmKeyData*)(pData + mpData->mClrPrmAnmOffset), mpData->mClrPrmKeyNum, mpData->mClrAnmFrmMax, pHeap);
    } else {
        mpPrmClrAnmTbl = nullptr;
    }

    if (isEnvAnm()) {
        makeColorTable(&mpEnvClrAnmTbl, (JPAClrAnmKeyData*)(pData + mpData->mClrEnvAnmOffset), mpData->mClrEnvKeyNum, mpData->mClrAnmFrmMax, pHeap);
    } else {
        mpEnvClrAnmTbl = nullptr;
    }
}

// The numbers particle shapes store, as the host's. A colour factor names the other side's colour.
const u8 JPABaseShape::st_bm[4] = {NEBULA_BLEND_REPLACE, NEBULA_BLEND_ADD, NEBULA_BLEND_LOGIC, NEBULA_BLEND_REPLACE};

u8 JPABaseShape::blendFactor(u32 code, bool source) {
    switch (code) {
    case 0:
        return NEBULA_FACTOR_ZERO;
    case 1:
        return NEBULA_FACTOR_ONE;
    case 2:
    case 4:
        return source ? NEBULA_FACTOR_DST_COLOR : NEBULA_FACTOR_SRC_COLOR;
    case 3:
    case 5:
        return source ? NEBULA_FACTOR_INV_DST_COLOR : NEBULA_FACTOR_INV_SRC_COLOR;
    case 6:
        return NEBULA_FACTOR_SRC_ALPHA;
    case 7:
        return NEBULA_FACTOR_INV_SRC_ALPHA;
    case 8:
        return NEBULA_FACTOR_DST_ALPHA;
    case 9:
        return NEBULA_FACTOR_INV_DST_ALPHA;
    default:
        return NEBULA_FACTOR_ZERO;
    }
}

// NEBULA_BLEND_LOGIC's order: clear, and, reverse and, copy, inverted and, no-op, xor, or, nor, equivalent, invert,
// reverse or, inverted copy, inverted or, nand, set.
const u8 JPABaseShape::st_lo[16] = {0, 15, 3, 12, 5, 10, 1, 14, 7, 8, 6, 9, 2, 4, 11, 13};

const u8 JPABaseShape::st_c[8] = {
    NEBULA_COMPARE_NEVER,  NEBULA_COMPARE_LESS,   NEBULA_COMPARE_LEQUAL,  NEBULA_COMPARE_EQUAL,
    NEBULA_COMPARE_NEQUAL, NEBULA_COMPARE_GEQUAL, NEBULA_COMPARE_GREATER, NEBULA_COMPARE_ALWAYS,
};

// The shape's blending and depth (the particle material keeps them) and its alpha test's references (the register
// file the particle material reads); its colour and alpha modes and compares go into the particle material
// (JPAResource::sendMaterial).
void JPABaseShape::setPass(JPAEmitterWorkData* work) const {
    J3DPass::blendState(getBlendMode(), getBlendSrc(), getBlendDst(), getLogicOp());
    J3DPass::depth(getZEnable(), getZCmp(), getZUpd());
    J3DPass::alphaCompare(getAlphaCmp0(), getAlphaRef0(), getAlphaOp(), getAlphaCmp1(), getAlphaRef1());
}
