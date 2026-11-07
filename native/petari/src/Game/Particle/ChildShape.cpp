#include "Game/Particle/ChildShape.hpp"
#include "JSystem/JParticle/JPANativeColor.hpp"
#include "Game/Render/FrameList.hpp"
#include "Game/Particle/EmitterCore.hpp"
#include "Game/Particle/ParticleCore.hpp"

void JPARegistChildPrmEnv(EmitterWork* work) {
    ChildShape* csp = work->mpRes->getCsp();
    EmitterCore* emtr = work->mpEmtr;
    NebulaColor8 prm, env;
    csp->getPrmClr(&prm);
    csp->getEnvClr(&env);
    prm.r = COLOR_MULTI(prm.r, emtr->mGlobalPrmClr.r);
    prm.g = COLOR_MULTI(prm.g, emtr->mGlobalPrmClr.g);
    prm.b = COLOR_MULTI(prm.b, emtr->mGlobalPrmClr.b);
    prm.a = COLOR_MULTI(prm.a, emtr->mGlobalPrmClr.a);
    env.r = COLOR_MULTI(env.r, emtr->mGlobalEnvClr.r);
    env.g = COLOR_MULTI(env.g, emtr->mGlobalEnvClr.g);
    env.b = COLOR_MULTI(env.b, emtr->mGlobalEnvClr.b);
    JPANativeColor::prmEnv(prm, env);
}

void JPACalcChildAlphaOut(EmitterWork* work, ParticleCore* ptcl) {
    f32 anm = (1.0f - ptcl->mTime) * 255.0f;
    ptcl->mPrmColorAlphaAnm = nebula_f32_to_u8(anm);
}

void JPACalcChildScaleOut(EmitterWork* work, ParticleCore* ptcl) {
    ptcl->mParticleScaleX = ptcl->mScaleOut * (1.0f - ptcl->mTime);
    ptcl->mParticleScaleY = ptcl->mAlphaWaveRandom * (1.0f - ptcl->mTime);
}
