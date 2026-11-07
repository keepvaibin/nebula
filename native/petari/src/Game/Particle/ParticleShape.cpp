#include "Game/Particle/ParticleShape.hpp"
#include "Game/Particle/ParticleDraw.hpp"
#include "Game/Particle/EmitterCore.hpp"
#include "Game/Particle/ParticleCore.hpp"
#include "Game/Particle/ParticleResources.hpp"
#include "Game/Util/MathUtil.hpp"

// A particle of scale 1 as a point or line.
static const f32 cParticleSizePixels = 25.0f / 6.0f;

void ParticleSetPointSize(EmitterWork* work) {
    ParticleDraw::flush();
    ParticleMaterial::pointSize(cParticleSizePixels * work->mGlobalPtclScl.x);
}

void ParticleSetLineWidth(EmitterWork* work) {
    ParticleDraw::flush();
    ParticleMaterial::lineWidth(cParticleSizePixels * work->mGlobalPtclScl.x);
}

void ParticleSetPointSize(EmitterWork* work, ParticleCore* ptcl) {
    ParticleDraw::flush();
    ParticleMaterial::pointSize(ptcl->mParticleScaleX * cParticleSizePixels * work->mGlobalPtclScl.x);
}

void ParticleSetLineWidth(EmitterWork* work, ParticleCore* ptcl) {
    ParticleDraw::flush();
    ParticleMaterial::lineWidth(ptcl->mParticleScaleX * cParticleSizePixels * work->mGlobalPtclScl.x);
}

void ParticleRegistPrm(EmitterWork* work) {
    EmitterCore* emtr = work->mpEmtr;
    NebulaColor8 prm = emtr->mPrmClr;
    prm.r = COLOR_MULTI(prm.r, emtr->mGlobalPrmClr.r);
    prm.g = COLOR_MULTI(prm.g, emtr->mGlobalPrmClr.g);
    prm.b = COLOR_MULTI(prm.b, emtr->mGlobalPrmClr.b);
    prm.a = COLOR_MULTI(prm.a, emtr->mGlobalPrmClr.a);
    ParticleMaterial::prm(prm);
}

void ParticleRegistEnv(EmitterWork* work) {
    EmitterCore* emtr = work->mpEmtr;
    NebulaColor8 env = emtr->mEnvClr;
    env.r = COLOR_MULTI(env.r, emtr->mGlobalEnvClr.r);
    env.g = COLOR_MULTI(env.g, emtr->mGlobalEnvClr.g);
    env.b = COLOR_MULTI(env.b, emtr->mGlobalEnvClr.b);
    ParticleMaterial::env(env);
}

void ParticleRegistPrmEnv(EmitterWork* work) {
    EmitterCore* emtr = work->mpEmtr;
    NebulaColor8 prm = emtr->mPrmClr;
    NebulaColor8 env = emtr->mEnvClr;
    prm.r = COLOR_MULTI(prm.r, emtr->mGlobalPrmClr.r);
    prm.g = COLOR_MULTI(prm.g, emtr->mGlobalPrmClr.g);
    prm.b = COLOR_MULTI(prm.b, emtr->mGlobalPrmClr.b);
    prm.a = COLOR_MULTI(prm.a, emtr->mGlobalPrmClr.a);
    env.r = COLOR_MULTI(env.r, emtr->mGlobalEnvClr.r);
    env.g = COLOR_MULTI(env.g, emtr->mGlobalEnvClr.g);
    env.b = COLOR_MULTI(env.b, emtr->mGlobalEnvClr.b);
    ParticleMaterial::prmEnv(prm, env);
}

void ParticleRegistAlpha(EmitterWork* work, ParticleCore* ptcl) {
    EmitterCore* emtr = work->mpEmtr;
    NebulaColor8 prm = emtr->mPrmClr;
    prm.r = COLOR_MULTI(prm.r, emtr->mGlobalPrmClr.r);
    prm.g = COLOR_MULTI(prm.g, emtr->mGlobalPrmClr.g);
    prm.b = COLOR_MULTI(prm.b, emtr->mGlobalPrmClr.b);
    prm.a = COLOR_MULTI(prm.a, emtr->mGlobalPrmClr.a);
    prm.a = COLOR_MULTI(prm.a, ptcl->mPrmColorAlphaAnm);
    ParticleMaterial::prm(prm);
}

void ParticleRegistPrmAlpha(EmitterWork* work, ParticleCore* ptcl) {
    EmitterCore* emtr = work->mpEmtr;
    NebulaColor8 prm = ptcl->mPrmClr;
    prm.r = COLOR_MULTI(prm.r, emtr->mGlobalPrmClr.r);
    prm.g = COLOR_MULTI(prm.g, emtr->mGlobalPrmClr.g);
    prm.b = COLOR_MULTI(prm.b, emtr->mGlobalPrmClr.b);
    prm.a = COLOR_MULTI(prm.a, emtr->mGlobalPrmClr.a);
    prm.a = COLOR_MULTI(prm.a, ptcl->mPrmColorAlphaAnm);
    ParticleMaterial::prm(prm);
}

void ParticleRegistPrmAlphaEnv(EmitterWork* work, ParticleCore* ptcl) {
    EmitterCore* emtr = work->mpEmtr;
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
    ParticleMaterial::prmEnv(prm, env);
}

void ParticleRegistAlphaEnv(EmitterWork* work, ParticleCore* ptcl) {
    EmitterCore* emtr = work->mpEmtr;
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
    ParticleMaterial::prmEnv(prm, env);
}

void ParticleRegistEnv(EmitterWork* work, ParticleCore* ptcl) {
    EmitterCore* emtr = work->mpEmtr;
    NebulaColor8 env = ptcl->mEnvClr;
    env.r = COLOR_MULTI(env.r, emtr->mGlobalEnvClr.r);
    env.g = COLOR_MULTI(env.g, emtr->mGlobalEnvClr.g);
    env.b = COLOR_MULTI(env.b, emtr->mGlobalEnvClr.b);
    ParticleMaterial::env(env);
}

void ParticleCalcClrIdxNormal(EmitterWork* work) {
    ParticleShape* bsp = work->mpRes->getBsp();
    s16 keyFrame;
    if (work->mpEmtr->mTick < bsp->getClrAnmMaxFrm()) {
        keyFrame = work->mpEmtr->mTick;
    } else {
        keyFrame = bsp->getClrAnmMaxFrm();
    }

    work->mClrKeyFrame = keyFrame;
}

void ParticleCalcClrIdxNormal(EmitterWork* work, ParticleCore* param_1) {
    ParticleShape* const shape = work->mpRes->getBsp();
    s16 keyFrame;
    if (param_1->mAge < shape->getClrAnmMaxFrm()) {
        keyFrame = param_1->mAge;
    } else {
        keyFrame = shape->getClrAnmMaxFrm();
    }

    work->mClrKeyFrame = keyFrame;
}

void ParticleCalcClrIdxRepeat(EmitterWork* work) {
    ParticleShape* const shape = work->mpRes->getBsp();
    work->mClrKeyFrame = work->mpEmtr->mTick % (shape->getClrAnmMaxFrm() + 1);
}

void ParticleCalcClrIdxRepeat(EmitterWork* work, ParticleCore* param_1) {
    ParticleShape* const shape = work->mpRes->getBsp();
    s32 age = param_1->mAge;
    s32 tick = age + shape->getClrLoopOfst(param_1->mAnmRandom);
    work->mClrKeyFrame = tick % (shape->getClrAnmMaxFrm() + 1);
}

void ParticleCalcClrIdxReverse(EmitterWork* work) {
    ParticleShape* const shape = work->mpRes->getBsp();
    int maxFrm = shape->getClrAnmMaxFrm();
    u32 tick = work->mpEmtr->mTick;
    u32 uVar1 = maxFrm != 0 ? tick / maxFrm : 0;
    tick = maxFrm != 0 ? tick % maxFrm : tick;
    uVar1 &= 1;
    work->mClrKeyFrame = tick + (uVar1) * (maxFrm - tick * 2);
}

void ParticleCalcClrIdxReverse(EmitterWork* work, ParticleCore* param_1) {
    ParticleShape* const shape = work->mpRes->getBsp();
    s32 tick = param_1->mAge + shape->getClrLoopOfst(param_1->mAnmRandom);
    s32 maxFrm = shape->getClrAnmMaxFrm();
    s32 rem = maxFrm != 0 ? tick % maxFrm : tick;
    s32 div = maxFrm != 0 ? tick / maxFrm : tick < 0 ? -1 : 0;
    work->mClrKeyFrame = rem + (div & 1) * (maxFrm - rem * 2);
}

void ParticleCalcClrIdxMerge(EmitterWork* work) {
    work->mClrKeyFrame = 0;
}

void ParticleCalcClrIdxMerge(EmitterWork* work, ParticleCore* param_1) {
    ParticleShape* const shape = work->mpRes->getBsp();
    s32 maxFrm = shape->getClrAnmMaxFrm() + 1;
    s32 tick = (s32)(param_1->mTime * maxFrm) + shape->getClrLoopOfst(param_1->mAnmRandom);
    work->mClrKeyFrame = tick % maxFrm;
}

void ParticleCalcClrIdxRandom(EmitterWork* work) {
    work->mClrKeyFrame = 0;
}

void ParticleCalcClrIdxRandom(EmitterWork* work, ParticleCore* param_1) {
    ParticleShape* const shape = work->mpRes->getBsp();
    s32 tick = shape->getClrLoopOfst(param_1->mAnmRandom);
    s32 maxFrm = shape->getClrAnmMaxFrm() + 1;
    work->mClrKeyFrame = tick % maxFrm;
}

void ParticleCalcPrm(EmitterWork* work) {
    work->mpRes->getBsp()->getPrmClr(work->mClrKeyFrame, &work->mpEmtr->mPrmClr);
}

void ParticleCalcPrm(EmitterWork* work, ParticleCore* ptcl) {
    work->mpRes->getBsp()->getPrmClr(work->mClrKeyFrame, &ptcl->mPrmClr);
}

void ParticleCalcEnv(EmitterWork* work) {
    work->mpRes->getBsp()->getEnvClr(work->mClrKeyFrame, &work->mpEmtr->mEnvClr);
}

void ParticleCalcEnv(EmitterWork* work, ParticleCore* ptcl) {
    work->mpRes->getBsp()->getEnvClr(work->mClrKeyFrame, &ptcl->mEnvClr);
}

void ParticleCalcColorCopy(EmitterWork* work, ParticleCore* ptcl) {
    EmitterCore* emtr = work->mpEmtr;
    ptcl->mPrmClr = emtr->mPrmClr;
    ptcl->mEnvClr = emtr->mEnvClr;
}

void ParticleGenTexCrdMtxIdt(EmitterWork* param_0) {
    ParticleDraw::clearCoord();
}

void ParticleGenTexCrdMtxAnm(EmitterWork* param_0) {
}

void ParticleGenTexCrdMtxPrj(EmitterWork* param_0) {
}

void ParticleGenCalcTexCrdMtxAnm(EmitterWork* work) {
    ParticleShape* const shape = work->mpRes->getBsp();
    f32 dVar16 = work->mpEmtr->mTick;
    f32 dVar15 = 0.5f * (1.0f + shape->getTilingS());
    f32 dVar14 = 0.5f * (1.0f + shape->getTilingT());
    f32 dVar11 = (dVar16 * shape->getIncTransX()) + shape->getInitTransX();
    f32 dVar10 = (dVar16 * shape->getIncTransY()) + shape->getInitTransY();
    f32 dVar13 = (dVar16 * shape->getIncScaleX()) + shape->getInitScaleX();
    f32 dVar12 = (dVar16 * shape->getIncScaleY()) + shape->getInitScaleY();
    s32 local_c0 = (dVar16 * shape->getIncRot()) + shape->getInitRot();
    f32 dVar8 = Angle::sinShort(local_c0);
    f32 dVar9 = Angle::cosShort(local_c0);
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
    ParticleDraw::setCoordRows(local_108);
}

void ParticleLoadCalcTexCrdMtxAnm(EmitterWork* work, ParticleCore* param_1) {
    ParticleShape* const shape = work->mpRes->getBsp();
    f32 dVar16 = param_1->mAge;
    f32 dVar15 = 0.5f * (1.0f + shape->getTilingS());
    f32 dVar14 = 0.5f * (1.0f + shape->getTilingT());
    f32 dVar11 = (dVar16 * shape->getIncTransX()) + shape->getInitTransX();
    f32 dVar10 = (dVar16 * shape->getIncTransY()) + shape->getInitTransY();
    f32 dVar13 = (dVar16 * shape->getIncScaleX()) + shape->getInitScaleX();
    f32 dVar12 = (dVar16 * shape->getIncScaleY()) + shape->getInitScaleY();
    s32 local_c0 = (dVar16 * shape->getIncRot()) + shape->getInitRot();
    f32 dVar8 = Angle::sinShort(local_c0);
    f32 dVar9 = Angle::cosShort(local_c0);
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
    ParticleDraw::setCoordRows(local_108);
}

void ParticleLoadTex(EmitterWork* work) {
    ParticleDraw::image(work, work->mpRes->getTexIdx(work->mpRes->getBsp()->getTexIdx()));
}

void ParticleLoadTexAnm(EmitterWork* work) {
    ParticleDraw::image(work, work->mpRes->getTexIdx(work->mpEmtr->mTexAnmIdx));
}

void ParticleLoadTexAnm(EmitterWork* work, ParticleCore* ptcl) {
    ParticleDraw::image(work, work->mpRes->getTexIdx(ptcl->mTexAnmIdx));
}

void ParticleCalcTexIdxNormal(EmitterWork* work) {
    ParticleShape* const shape = work->mpRes->getBsp();
    u8 tick = shape->getTexAnmKeyNum() - 1 < work->mpEmtr->mTick ? shape->getTexAnmKeyNum() - 1 : work->mpEmtr->mTick;
    work->mpEmtr->mTexAnmIdx = shape->getTexIdx(tick);
}

void ParticleCalcTexIdxNormal(EmitterWork* work, ParticleCore* param_1) {
    ParticleShape* const shape = work->mpRes->getBsp();
    u8 tick = shape->getTexAnmKeyNum() - 1 < param_1->mAge ? shape->getTexAnmKeyNum() - 1 : param_1->mAge;
    param_1->mTexAnmIdx = shape->getTexIdx(tick);
}

void ParticleCalcTexIdxRepeat(EmitterWork* work) {
    ParticleShape* const shape = work->mpRes->getBsp();
    work->mpEmtr->mTexAnmIdx = shape->getTexIdx(shape->getTexAnmKeyNum() != 0 ? work->mpEmtr->mTick % shape->getTexAnmKeyNum() : work->mpEmtr->mTick);
}

void ParticleCalcTexIdxRepeat(EmitterWork* work, ParticleCore* param_1) {
    ParticleShape* const shape = work->mpRes->getBsp();
    const int frame = (int)shape->getTexLoopOfst(param_1->mAnmRandom) + param_1->mAge;
    u8 texIdx = shape->getTexAnmKeyNum() != 0 ? frame % shape->getTexAnmKeyNum() : frame;
    param_1->mTexAnmIdx = shape->getTexIdx(texIdx);
}

void ParticleCalcTexIdxReverse(EmitterWork* work) {
    ParticleShape* const shape = work->mpRes->getBsp();
    int tick = work->mpEmtr->mTick;
    int keyNum = (int)shape->getTexAnmKeyNum() - 1;
    int div = keyNum != 0 ? tick / keyNum : tick < 0 ? -1 : 0;
    int rem = keyNum != 0 ? tick % keyNum : tick;
    work->mpEmtr->mTexAnmIdx = shape->getTexIdx(rem + (div & 1) * (keyNum - rem * 2));
}

void ParticleCalcTexIdxReverse(EmitterWork* work, ParticleCore* param_1) {
    ParticleShape* const shape = work->mpRes->getBsp();
    s32 tick = shape->getTexLoopOfst(param_1->mAnmRandom) + param_1->mAge;
    int keyNum = (int)shape->getTexAnmKeyNum() - 1;
    int div = keyNum != 0 ? tick / keyNum : tick < 0 ? -1 : 0;
    int rem = keyNum != 0 ? tick % keyNum : tick;
    u8 texIdx = rem + (div & 1) * (keyNum - rem * 2);
    param_1->mTexAnmIdx = work->mpRes->getBsp()->getTexIdx(texIdx);
}

void ParticleCalcTexIdxMerge(EmitterWork* work) {
    work->mpEmtr->mTexAnmIdx = work->mpRes->getBsp()->getTexIdx();
}

void ParticleCalcTexIdxMerge(EmitterWork* work, ParticleCore* param_1) {
    ParticleShape* const shape = work->mpRes->getBsp();
    s32 maxFrm = shape->getTexAnmKeyNum();
    s32 tick = (s32)(maxFrm * param_1->mTime) + shape->getTexLoopOfst(param_1->mAnmRandom);
    u8 texIdx = maxFrm != 0 ? tick % maxFrm : tick;
    param_1->mTexAnmIdx = work->mpRes->getBsp()->getTexIdx(texIdx);
}

void ParticleCalcTexIdxRandom(EmitterWork* work) {
    work->mpEmtr->mTexAnmIdx = work->mpRes->getBsp()->getTexIdx();
}

void ParticleCalcTexIdxRandom(EmitterWork* work, ParticleCore* param_1) {
    ParticleShape* const shape = work->mpRes->getBsp();
    const int frame = shape->getTexLoopOfst(param_1->mAnmRandom);
    u8 texIdx = shape->getTexAnmKeyNum() != 0 ? frame % shape->getTexAnmKeyNum() : frame;
    param_1->mTexAnmIdx = shape->getTexIdx(texIdx);
}

void ParticleLoadPosMtxCam(EmitterWork* work) {
    ParticleDraw::setMatrix(work->mPosCamMtx);
}

void ParticleDrawEmitterCallBackB(EmitterWork* work) {
    if (work->mpEmtr->mpEmtrCallBack == nullptr) {
        return;
    }

    work->mpEmtr->mpEmtrCallBack->draw(work->mpEmtr);
}

void ParticleDrawParticleCallBack(EmitterWork* work, ParticleCore* ptcl) {
    if (work->mpEmtr->mpPtclCallBack == nullptr) {
        return;
    }

    work->mpEmtr->mpPtclCallBack->draw(work->mpEmtr, ptcl);
}

// The shape's blending and depth (the particle material keeps them) and its alpha test's references (the register
// file the particle material reads); its colour and alpha modes and compares go into the particle material
// (ParticleResource::sendMaterial).
void ParticleShape::setPass(EmitterWork* work) const {
}
