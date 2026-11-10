#include "Game/Map/OceanSpherePoint.hpp"
#include "Game/Util/MathUtil.hpp"
#include "Game/Util/Tunable.hpp"

NEBULA_TUNABLE_NAMED(f32, cNum_calcHeight_f1, "calcHeight.f1", 0.8f);
NEBULA_TUNABLE_NAMED(f32, cNum_calcHeight_f2, "calcHeight.f2", 10.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_calcHeight_f3, "calcHeight.f3", 5.0f);

OceanSpherePoint::OceanSpherePoint(const Vec3f* pCenter, const Vec3f& rNormal, f32 wave1Pos, f32 wave2Pos, const Vec2f& rTexCoord)
    : mPos(*pCenter), mCenter(pCenter), mNormal(rNormal), mWave1Pos(wave1Pos), mWave2Pos(wave2Pos), mTexCoord(rTexCoord) {
}

void OceanSpherePoint::updatePos(f32 radius, f32 wave1Time, f32 wave2Time) {
    const f32 height = calcHeight(wave1Time, wave2Time, mWave1Pos, mWave2Pos);
    mPos.set(mNormal);
    mPos.scale(height + radius);
    mPos.add(*mCenter);
}

f32 OceanSpherePoint::calcHeight(f32 wave1Time, f32 wave2Time, f32 wave1Pos, f32 wave2Pos) const {
    return MR::sin(wave1Time + (cNum_calcHeight_f1 * wave1Pos)) * cNum_calcHeight_f2 + MR::sin(wave2Time + wave2Pos) * cNum_calcHeight_f3;
}
