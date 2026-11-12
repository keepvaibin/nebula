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
    if (work->mpEmtr->mTick < static_cast< u32 >(bsp->getClrAnmMaxFrm())) {
        keyFrame = work->mpEmtr->mTick;
    } else {
        keyFrame = bsp->getClrAnmMaxFrm();
    }

    work->mClrKeyFrame = keyFrame;
}

void ParticleCalcClrIdxNormal(EmitterWork* work, ParticleCore* ptcl) {
    ParticleShape* const shape = work->mpRes->getBsp();
    s16 keyFrame;
    if (ptcl->mAge < shape->getClrAnmMaxFrm()) {
        keyFrame = ptcl->mAge;
    } else {
        keyFrame = shape->getClrAnmMaxFrm();
    }

    work->mClrKeyFrame = keyFrame;
}

void ParticleCalcClrIdxRepeat(EmitterWork* work) {
    ParticleShape* const shape = work->mpRes->getBsp();
    work->mClrKeyFrame = work->mpEmtr->mTick % (shape->getClrAnmMaxFrm() + 1);
}

void ParticleCalcClrIdxRepeat(EmitterWork* work, ParticleCore* ptcl) {
    ParticleShape* const shape = work->mpRes->getBsp();
    s32 age = ptcl->mAge;
    s32 tick = age + shape->getClrLoopOfst(ptcl->mAnmRandom);
    work->mClrKeyFrame = tick % (shape->getClrAnmMaxFrm() + 1);
}

void ParticleCalcClrIdxReverse(EmitterWork* work) {
    ParticleShape* const shape = work->mpRes->getBsp();
    int maxFrm = shape->getClrAnmMaxFrm();
    u32 tick = work->mpEmtr->mTick;
    // Odd passes run backward.
    u32 pass = maxFrm != 0 ? tick / maxFrm : 0;
    tick = maxFrm != 0 ? tick % maxFrm : tick;
    pass &= 1;
    work->mClrKeyFrame = tick + (pass) * (maxFrm - tick * 2);
}

void ParticleCalcClrIdxReverse(EmitterWork* work, ParticleCore* ptcl) {
    ParticleShape* const shape = work->mpRes->getBsp();
    s32 tick = ptcl->mAge + shape->getClrLoopOfst(ptcl->mAnmRandom);
    s32 maxFrm = shape->getClrAnmMaxFrm();
    s32 rem = maxFrm != 0 ? tick % maxFrm : tick;
    s32 div = maxFrm != 0 ? tick / maxFrm : tick < 0 ? -1 : 0;
    work->mClrKeyFrame = rem + (div & 1) * (maxFrm - rem * 2);
}

void ParticleCalcClrIdxMerge(EmitterWork* work) {
    work->mClrKeyFrame = 0;
}

void ParticleCalcClrIdxMerge(EmitterWork* work, ParticleCore* ptcl) {
    ParticleShape* const shape = work->mpRes->getBsp();
    s32 maxFrm = shape->getClrAnmMaxFrm() + 1;
    s32 tick = (s32)(ptcl->mTime * maxFrm) + shape->getClrLoopOfst(ptcl->mAnmRandom);
    work->mClrKeyFrame = tick % maxFrm;
}

void ParticleCalcClrIdxRandom(EmitterWork* work) {
    work->mClrKeyFrame = 0;
}

void ParticleCalcClrIdxRandom(EmitterWork* work, ParticleCore* ptcl) {
    ParticleShape* const shape = work->mpRes->getBsp();
    s32 tick = shape->getClrLoopOfst(ptcl->mAnmRandom);
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

void ParticleGenTexCrdMtxIdt(EmitterWork*) {
    ParticleDraw::clearCoord();
}

void ParticleGenTexCrdMtxAnm(EmitterWork*) {
}

void ParticleGenTexCrdMtxPrj(EmitterWork*) {
}

namespace {
    // The shape's texture-coordinate animation at `frame`: scale and rotation about the tile centre, then translation.
    void setTexCoordAnimation(const ParticleShape* pShape, f32 frame) {
        f32 centerS = 0.5f * (1.0f + pShape->getTilingS());
        f32 centerT = 0.5f * (1.0f + pShape->getTilingT());
        f32 transS = (frame * pShape->getIncTransX()) + pShape->getInitTransX();
        f32 transT = (frame * pShape->getIncTransY()) + pShape->getInitTransY();
        f32 scaleS = (frame * pShape->getIncScaleX()) + pShape->getInitScaleX();
        f32 scaleT = (frame * pShape->getIncScaleY()) + pShape->getInitScaleY();
        s32 angle = (frame * pShape->getIncRot()) + pShape->getInitRot();
        f32 sine = Angle::sinShort(angle);
        f32 cosine = Angle::cosShort(angle);
        Mtx mtx;
        mtx[0][0] = scaleS * cosine;
        mtx[0][1] = -scaleS * sine;
        mtx[0][2] = 0.0f;
        mtx[0][3] = (centerS + (scaleS * ((sine * (centerT + transT)) - (cosine * (centerS + transS)))));
        mtx[1][0] = scaleT * sine;
        mtx[1][1] = scaleT * cosine;
        mtx[1][2] = 0.0f;
        mtx[1][3] = (centerT + (-scaleT * ((sine * (centerS + transS)) + (cosine * (centerT + transT)))));
        mtx[2][0] = 0.0f;
        mtx[2][1] = 0.0f;
        mtx[2][2] = 1.0f;
        mtx[2][3] = 0.0f;
        ParticleDraw::setCoordRows(mtx);
    }
}  // namespace

void ParticleGenCalcTexCrdMtxAnm(EmitterWork* work) {
    setTexCoordAnimation(work->mpRes->getBsp(), work->mpEmtr->mTick);
}

void ParticleLoadCalcTexCrdMtxAnm(EmitterWork* work, ParticleCore* ptcl) {
    setTexCoordAnimation(work->mpRes->getBsp(), ptcl->mAge);
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

void ParticleCalcTexIdxNormal(EmitterWork* work, ParticleCore* ptcl) {
    ParticleShape* const shape = work->mpRes->getBsp();
    u8 tick = shape->getTexAnmKeyNum() - 1 < ptcl->mAge ? shape->getTexAnmKeyNum() - 1 : ptcl->mAge;
    ptcl->mTexAnmIdx = shape->getTexIdx(tick);
}

void ParticleCalcTexIdxRepeat(EmitterWork* work) {
    ParticleShape* const shape = work->mpRes->getBsp();
    work->mpEmtr->mTexAnmIdx = shape->getTexIdx(shape->getTexAnmKeyNum() != 0 ? work->mpEmtr->mTick % shape->getTexAnmKeyNum() : work->mpEmtr->mTick);
}

void ParticleCalcTexIdxRepeat(EmitterWork* work, ParticleCore* ptcl) {
    ParticleShape* const shape = work->mpRes->getBsp();
    const int frame = (int)shape->getTexLoopOfst(ptcl->mAnmRandom) + ptcl->mAge;
    u8 texIdx = shape->getTexAnmKeyNum() != 0 ? frame % shape->getTexAnmKeyNum() : frame;
    ptcl->mTexAnmIdx = shape->getTexIdx(texIdx);
}

void ParticleCalcTexIdxReverse(EmitterWork* work) {
    ParticleShape* const shape = work->mpRes->getBsp();
    int tick = work->mpEmtr->mTick;
    int keyNum = (int)shape->getTexAnmKeyNum() - 1;
    int div = keyNum != 0 ? tick / keyNum : tick < 0 ? -1 : 0;
    int rem = keyNum != 0 ? tick % keyNum : tick;
    work->mpEmtr->mTexAnmIdx = shape->getTexIdx(rem + (div & 1) * (keyNum - rem * 2));
}

void ParticleCalcTexIdxReverse(EmitterWork* work, ParticleCore* ptcl) {
    ParticleShape* const shape = work->mpRes->getBsp();
    s32 tick = shape->getTexLoopOfst(ptcl->mAnmRandom) + ptcl->mAge;
    int keyNum = (int)shape->getTexAnmKeyNum() - 1;
    int div = keyNum != 0 ? tick / keyNum : tick < 0 ? -1 : 0;
    int rem = keyNum != 0 ? tick % keyNum : tick;
    u8 texIdx = rem + (div & 1) * (keyNum - rem * 2);
    ptcl->mTexAnmIdx = work->mpRes->getBsp()->getTexIdx(texIdx);
}

void ParticleCalcTexIdxMerge(EmitterWork* work) {
    work->mpEmtr->mTexAnmIdx = work->mpRes->getBsp()->getTexIdx();
}

void ParticleCalcTexIdxMerge(EmitterWork* work, ParticleCore* ptcl) {
    ParticleShape* const shape = work->mpRes->getBsp();
    s32 maxFrm = shape->getTexAnmKeyNum();
    s32 tick = (s32)(maxFrm * ptcl->mTime) + shape->getTexLoopOfst(ptcl->mAnmRandom);
    u8 texIdx = maxFrm != 0 ? tick % maxFrm : tick;
    ptcl->mTexAnmIdx = work->mpRes->getBsp()->getTexIdx(texIdx);
}

void ParticleCalcTexIdxRandom(EmitterWork* work) {
    work->mpEmtr->mTexAnmIdx = work->mpRes->getBsp()->getTexIdx();
}

void ParticleCalcTexIdxRandom(EmitterWork* work, ParticleCore* ptcl) {
    ParticleShape* const shape = work->mpRes->getBsp();
    const int frame = shape->getTexLoopOfst(ptcl->mAnmRandom);
    u8 texIdx = shape->getTexAnmKeyNum() != 0 ? frame % shape->getTexAnmKeyNum() : frame;
    ptcl->mTexAnmIdx = shape->getTexIdx(texIdx);
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
void ParticleShape::setPass(EmitterWork* /*work*/) const {
}
