#include "Game/Camera/CamKarikariEffector.hpp"
#include "Game/Camera/CameraLocalUtil.hpp"
#include "Game/Camera/CameraMan.hpp"
#include "Game/Enemy/KarikariDirector.hpp"
#include "Game/Util/MathUtil.hpp"
#include "Game/Util/PlayerUtil.hpp"
#include <nebula_geometry.hpp>

namespace {
    static const f32 sKarikariViewRate = 0.8f;
    static const s32 sKarikariCounterMax = 30;
    static const f32 sPlayerRadius = 75.0f;
};  // namespace

CamKarikariEffector::CamKarikariEffector() : mCounter(0) {
}

namespace {
    inline Vec3f calcPlayerFocusPos(const Vec3f& rOffset) {
        const Vec3f* pOffset = &rOffset;

        return *MR::getPlayerPos() + *pOffset;
    }

    inline f32 calcFovy(f32 dist) {
        f32 fovAngle = MR::asin(75.0f / dist);

        return MR::atan2((dist * MR::tan(fovAngle)) / 0.3f, dist);
    }
}

void CamKarikariEffector::update(CameraMan* pCameraMan) {
    if (MR::isPlayerDead()) {
        CameraLocalUtil::setFovy(pCameraMan, CameraLocalUtil::getFovy(pCameraMan));
        return;
    }

    if (MR::getKarikariClingNum() == 0) {
        if (mCounter > 0) {
            mCounter--;
        }
    } else {
        if (mCounter < ::sKarikariCounterMax) {
            mCounter++;
        }
    }

    f32 t;

    if (MR::getClingNumMax() == 0) {
        t = ::sKarikariViewRate;
    } else {
        t = static_cast< f32 >(MR::getKarikariClingNum()) / MR::getClingNumMax();
    }

    if (mCounter <= 0) {
        return;
    }

    t = 1.0f - t;
    t *= t;
    t = 1.0f - t;

    Vec3f diffWatchPos = CameraLocalUtil::getWatchPos(pCameraMan) - CameraLocalUtil::getPos(pCameraMan);
    Vec3f toWatchPos(diffWatchPos);
    MR::normalize(&toWatchPos);

    Vec3f playerUp;
    MR::getPlayerUpVec(&playerUp);
    Vec3f playerFocusPos = calcPlayerFocusPos(playerUp * ::sPlayerRadius);

    Vec3f diffPlayerPos = playerFocusPos - CameraLocalUtil::getPos(pCameraMan);
    Vec3f toPlayerPos(diffPlayerPos);
    MR::normalize(&toPlayerPos);

    f32 rotRatio = MR::clamp((1.0f - MR::cos(mCounter * MR::pi() / ::sKarikariCounterMax)) * 0.5f, 0.0f, 1.0f);
    Quat4f rot;
    rot.setRotate(toWatchPos, toPlayerPos, rotRatio);
    rot.transform(diffWatchPos);

    CameraLocalUtil::setPos(pCameraMan, CameraLocalUtil::getPos(pCameraMan));
    CameraLocalUtil::setWatchPos(pCameraMan, diffWatchPos + CameraLocalUtil::getPos(pCameraMan));

    Vec3f camUp(CameraLocalUtil::getUpVec(pCameraMan));
    rot.transform(camUp);
    CameraLocalUtil::setUpVec(pCameraMan, camUp);

    f32 fovy = calcFovy(diffPlayerPos.length());

    if (fovy < CameraLocalUtil::getFovy(pCameraMan) * MR::pi() / 180.0f) {
        CameraLocalUtil::setFovy(pCameraMan, (fovy * 180.0f * t) / MR::pi() + (1.0f - t) * CameraLocalUtil::getFovy(pCameraMan));
    }
}
