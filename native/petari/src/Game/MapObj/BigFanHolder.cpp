#include "Game/MapObj/BigFanHolder.hpp"
#include "Game/MapObj/BigFan.hpp"
#include "Game/Scene/SceneObjHolder.hpp"
#include "Game/Util.hpp"
#include "Game/System/LumaCore.hpp"

namespace {
    const s32 sMaxBigFan = 32;

    BigFanHolder* getBigFanHolder() {
        return MR::getSceneObj< BigFanHolder >(SceneObj_BigFanHolder);
    }
};  // namespace

BigFanHolder::BigFanHolder() : DeriveActorGroup("大型扇風機管理", ::sMaxBigFan) {
    lumacore::setClass(this, lumacore::kClassId< BigFanHolder >);
}

void BigFanHolder::calcWindInfo(Vec3f* pWindInfo, const Vec3f& rPos, f32* pWindSpeed) {
    s32 count = getObjNum();
    Vec3f totalWind;
    totalWind.zero();

    for (int i = 0; i < count; i++) {
        Vec3f windVec;
        windVec.zero();
        getMember(i)->calcWindInfo(&windVec, rPos);
        f32 speed = getMember(i)->mWindSpeed;
        totalWind.add(windVec * (speed / 100.0f));
    }

    f32 mag = totalWind.length();
    if (MR::isNearZero(mag)) {
        pWindInfo->zero();

        if (pWindSpeed != nullptr) {
            *pWindSpeed = 0.0f;
        }
    } else {
        pWindInfo->set(totalWind * (1.0f / mag));

        if (pWindSpeed != nullptr) {
            *pWindSpeed = mag;
        }
    }
}

void BigFanFunction::createBigFanHolder() {
    MR::createSceneObj(SceneObj_BigFanHolder);
}

void BigFanFunction::calcWindInfo(Vec3f* pWindInfo, const Vec3f& rPos, f32* pWindSpeed) {
    if (!MR::isExistSceneObj(SceneObj_BigFanHolder)) {
        pWindInfo->zero();

        if (pWindSpeed != nullptr) {
            *pWindSpeed = 0.0f;
        }
    } else {
        ::getBigFanHolder()->calcWindInfo(pWindInfo, rPos, pWindSpeed);
    }
}

void BigFanFunction::registerBigFan(BigFan* pFan) {
    ::getBigFanHolder()->registerActor(pFan);
}
