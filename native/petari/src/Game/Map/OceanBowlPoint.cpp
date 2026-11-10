#include "Game/Map/OceanBowlPoint.hpp"
#include "Game/Util/MathUtil.hpp"
#include "Game/Util/Tunable.hpp"

NEBULA_TUNABLE_NAMED(f32, cNum_reset_f1, "reset.f1", 255.0f);

NEBULA_TUNABLE(f32, sSinRate0, 0.003f);
NEBULA_TUNABLE(f32, sSinRate1, 0.003f);
NEBULA_TUNABLE(f32, sSinRate2, 0.003f);
NEBULA_TUNABLE(f32, sHeight0, 40.0f);
NEBULA_TUNABLE(f32, sHeight1, 30.0f);

OceanBowlPoint::OceanBowlPoint(const Vec3f& rPos) : mVertexPosition(rPos), mPosition(rPos) {
    mAlpha = 255;
    mWaveScale = 1.0f;
}

void OceanBowlPoint::updatePos(f32 phaseX, f32 phaseZ) {
    if (mWaveScale == 0.0f) {
        return;
    }
    f32 z = mPosition.z;
    f32 x = mPosition.x;
    f32 height = mWaveScale * calcHeightStatic(phaseX, phaseZ, x, z);
    mVertexPosition.set(Vec3f(0.0f, 1.0f, 0.0f));
    mVertexPosition *= height;
    mVertexPosition.add(mPosition);
}

// Two sine waves: one along x + z (30 high), one along z (40 high).
f32 OceanBowlPoint::calcHeightStatic(f32 phaseX, f32 phaseZ, f32 x, f32 z) {
    return (MR::sin((phaseX + (sSinRate1 * x)) + (sSinRate2 * z)) * sHeight1) + (MR::sin(phaseZ + (sSinRate0 * z)) * sHeight0);
}

void OceanBowlPoint::reset(const Vec3f& rPos, f32 waveScale) {
    mPosition.set(rPos);
    mVertexPosition.set(mPosition);
    mWaveScale = waveScale;
    mAlpha = (s32)(cNum_reset_f1 * waveScale);
}
