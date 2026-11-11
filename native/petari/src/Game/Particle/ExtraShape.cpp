#include "Game/Particle/ExtraShape.hpp"
#include <nebula_geometry.hpp>
#include "Game/Particle/EmitterCore.hpp"
#include "Game/Particle/ParticleCore.hpp"
#include "Game/Util/Tunable.hpp"

NEBULA_TUNABLE_NAMED(f32, cNum_ParticleCalcScaleAnmReverseX_f1, "ParticleCalcScaleAnmReverseX.f1", 2.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_ParticleCalcScaleAnmReverseY_f1, "ParticleCalcScaleAnmReverseY.f1", 2.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_ParticleCalcAlphaAnm_f1, "ParticleCalcAlphaAnm.f1", 255.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_ParticleCalcAlphaAnm_f2, "ParticleCalcAlphaAnm.f2", 255.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_ParticleCalcAlphaAnm_f3, "ParticleCalcAlphaAnm.f3", 255.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_ParticleCalcAlphaFlickAnm_f1, "ParticleCalcAlphaFlickAnm.f1", 16384.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_ParticleCalcAlphaFlickAnm_f2, "ParticleCalcAlphaFlickAnm.f2", 0.5f);
NEBULA_TUNABLE_NAMED(f32, cNum_ParticleCalcAlphaFlickAnm_f3, "ParticleCalcAlphaFlickAnm.f3", 255.0f);

void ParticleCalcScaleX(EmitterWork* work, ParticleCore* ptcl) {
    ExtraShape* esp = work->mpRes->getEsp();
    if (work->mScaleAnm < esp->getScaleInTiming()) {
        ptcl->mParticleScaleX = ptcl->mScaleOut * (esp->getScaleIncRateX() * work->mScaleAnm + esp->getScaleInValueX());
    } else if (work->mScaleAnm > esp->getScaleOutTiming()) {
        ptcl->mParticleScaleX = ptcl->mScaleOut * (esp->getScaleDecRateX() * (work->mScaleAnm - esp->getScaleOutTiming()) + 1.0f);
    } else {
        ptcl->mParticleScaleX = ptcl->mScaleOut;
    }
}

void ParticleCalcScaleY(EmitterWork* work, ParticleCore* ptcl) {
    ExtraShape* esp = work->mpRes->getEsp();
    if (work->mScaleAnm < esp->getScaleInTiming()) {
        ptcl->mParticleScaleY = ptcl->mScaleOut * (esp->getScaleIncRateY() * work->mScaleAnm + esp->getScaleInValueY());
    } else if (work->mScaleAnm > esp->getScaleOutTiming()) {
        ptcl->mParticleScaleY = ptcl->mScaleOut * (esp->getScaleDecRateY() * (work->mScaleAnm - esp->getScaleOutTiming()) + 1.0f);
    } else {
        ptcl->mParticleScaleY = ptcl->mScaleOut;
    }
}

void ParticleCalcScaleCopy(EmitterWork* /*work*/, ParticleCore* ptcl) {
    ptcl->mParticleScaleY = ptcl->mParticleScaleX;
}

void ParticleCalcScaleAnmNormal(EmitterWork* work, ParticleCore* ptcl) {
    work->mScaleAnm = ptcl->mTime;
}

void ParticleCalcScaleAnmRepeatX(EmitterWork* work, ParticleCore* ptcl) {
    ExtraShape* esp = work->mpRes->getEsp();
    s16 cycle = esp->getScaleAnmCycleX();
    work->mScaleAnm = (cycle != 0 ? ptcl->mAge % cycle : ptcl->mAge) / static_cast< f32 >(cycle);
}

void ParticleCalcScaleAnmRepeatY(EmitterWork* work, ParticleCore* ptcl) {
    ExtraShape* esp = work->mpRes->getEsp();
    s16 cycle = esp->getScaleAnmCycleY();
    work->mScaleAnm = (cycle != 0 ? ptcl->mAge % cycle : ptcl->mAge) / static_cast< f32 >(cycle);
}

void ParticleCalcScaleAnmReverseX(EmitterWork* work, ParticleCore* ptcl) {
    ExtraShape* esp = work->mpRes->getEsp();
    const s16 period = esp->getScaleAnmCycleX();
    f32 cycle = (period != 0 ? ptcl->mAge / period : ptcl->mAge < 0 ? -1 : 0) & 1;
    f32 base = (period != 0 ? ptcl->mAge % period : ptcl->mAge) / (f32)period;
    work->mScaleAnm = base + (cycle * (1.0f - base * cNum_ParticleCalcScaleAnmReverseX_f1));
}

void ParticleCalcScaleAnmReverseY(EmitterWork* work, ParticleCore* ptcl) {
    ExtraShape* esp = work->mpRes->getEsp();
    const s16 period = esp->getScaleAnmCycleY();
    f32 cycle = (period != 0 ? ptcl->mAge / period : ptcl->mAge < 0 ? -1 : 0) & 1;
    f32 base = (period != 0 ? ptcl->mAge % period : ptcl->mAge) / (f32)period;
    work->mScaleAnm = base + (cycle * (1.0f - base * cNum_ParticleCalcScaleAnmReverseY_f1));
}

void ParticleCalcAlphaAnm(EmitterWork* work, ParticleCore* ptcl) {
    ExtraShape* esp = work->mpRes->getEsp();
    f32 alpha = 0.0f;
    if (ptcl->mTime < esp->getAlphaInTiming()) {
        alpha = cNum_ParticleCalcAlphaAnm_f1 * (esp->getAlphaInValue() + esp->getAlphaIncRate() * ptcl->mTime);
    } else if (ptcl->mTime > esp->getAlphaOutTiming()) {
        alpha = cNum_ParticleCalcAlphaAnm_f2 * ((ptcl->mTime - esp->getAlphaOutTiming()) * esp->getAlphaDecRate() + esp->getAlphaBaseValue());
    } else {
        alpha = cNum_ParticleCalcAlphaAnm_f3 * esp->getAlphaBaseValue();
    }
    ptcl->mPrmColorAlphaAnm = nebula_f32_to_u8(alpha);
}

void ParticleCalcAlphaFlickAnm(EmitterWork* work, ParticleCore* ptcl) {
    ExtraShape* esp = work->mpRes->getEsp();
    f32 alpha = 0.0f;
    if (ptcl->mTime < esp->getAlphaInTiming()) {
        alpha = (esp->getAlphaInValue() + esp->getAlphaIncRate() * ptcl->mTime);
    } else if (ptcl->mTime > esp->getAlphaOutTiming()) {
        alpha = ((ptcl->mTime - esp->getAlphaOutTiming()) * esp->getAlphaDecRate() + esp->getAlphaBaseValue());
    } else {
        alpha = esp->getAlphaBaseValue();
    }
    s32 theta = ptcl->mAlphaWaveRandom * ptcl->mAge * cNum_ParticleCalcAlphaFlickAnm_f1 * (1.0f - esp->getAlphaFreq());
    f32 wave = Angle::sinShort(theta);
    alpha *= (1.0f + esp->getAlphaAmp() * (wave - 1.0f) * cNum_ParticleCalcAlphaFlickAnm_f2) * cNum_ParticleCalcAlphaFlickAnm_f3;
    ptcl->mPrmColorAlphaAnm = nebula_f32_to_u8(alpha);
}


