#include "Game/Map/WaterPoint.hpp"
#include "Game/Util.hpp"
#include "Game/Util/Tunable.hpp"

NEBULA_TUNABLE_NAMED(f32, cNum_initAfterPlacement_f1, "initAfterPlacement.f1", 2.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_initAfterPlacement_f2, "initAfterPlacement.f2", 2.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_initAfterPlacement_f3, "initAfterPlacement.f3", 255.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_initAfterPlacement_f4, "initAfterPlacement.f4", 255.0f);

namespace {
    NEBULA_TUNABLE(f32, sSinRate0, 0.003f);
    NEBULA_TUNABLE(f32, sSinRate1, 0.0003f);
    NEBULA_TUNABLE(f32, sSinRate2, 0.0025f);
    NEBULA_TUNABLE(f32, sMapCheckHeight, 400.0f);
    NEBULA_TUNABLE(f32, sMapWaveRateMin, 0.1f);
    NEBULA_TUNABLE(f32, sAlphaMin, 30.0f);
};  // namespace

WaterPoint::WaterPoint(const Vec3f& rPos, const Vec3f& rUpVec, f32 coordAcrossRail, f32 coordOnRail, f32 height)
    : mPosition(rPos), mOrigPos(rPos), mCoordAcrossRail(coordAcrossRail), mCoordOnRail(coordOnRail), mUpVec(rUpVec), mHeight(height),
      mAlpha(-1) {
}

void WaterPoint::initAfterPlacement() {
    if (mHeight != 0.0f) {
        Vec3f startCheckPos(mPosition.x, (::sMapCheckHeight/cNum_initAfterPlacement_f1 + mPosition.y), mPosition.z);
        Vec3f mapCheckPos(0.0f, 0.0f, 0.0f);

        if (MR::calcMapGround(startCheckPos, &mapCheckPos, ::sMapCheckHeight)) {
            f32 heightDiff = ((mapCheckPos.y - mPosition.y) / (::sMapCheckHeight / cNum_initAfterPlacement_f2));
            f32 waveRate = MR::clamp((::sMapWaveRateMin + MR::abs(heightDiff)), ::sMapWaveRateMin, 1.0f);
            mAlpha = (s32)(MR::clamp((::sAlphaMin + (cNum_initAfterPlacement_f3 * -heightDiff)), ::sAlphaMin, cNum_initAfterPlacement_f4));
            mHeight = MR::min(waveRate, mHeight);
        }
    }
}

void WaterPoint::updatePos(f32 waveTheta1, f32 waveTheta2, f32 waveHeight1, f32 waveHeight2, f32 heightRate) {
    f32 height = calcHeight(waveTheta1, waveTheta2, waveHeight1, waveHeight2, mCoordAcrossRail, mCoordOnRail);
    f32 offset = heightRate * height;
    mPosition.x = mUpVec.x * offset;
    mPosition.y = mUpVec.y * offset;
    mPosition.z = mUpVec.z * offset;
    mPosition.add(mOrigPos);
}

f32 WaterPoint::calcHeight(f32 waveTheta1, f32 waveTheta2, f32 waveHeight1, f32 waveHeight2, f32 coordAcrossRail, f32 coordOnRail) const {
    return mHeight * (waveHeight1 * MR::sin(waveTheta1 + ::sSinRate0 * coordAcrossRail + ::sSinRate1 * coordOnRail) +
                                 waveHeight2 * MR::sin(waveTheta2 + (::sSinRate2 * coordOnRail)));
}
