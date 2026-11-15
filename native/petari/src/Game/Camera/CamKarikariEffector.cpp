#include "Game/Camera/CamKarikariEffector.hpp"
#include "Game/Camera/CameraLocalUtil.hpp"
#include "Game/Camera/CameraMan.hpp"
#include "Game/Enemy/KarikariDirector.hpp"
#include "Game/Util/MathUtil.hpp"
#include "Game/Util/PlayerUtil.hpp"
#include <nebula_geometry.hpp>

namespace {
    const f32 sKarikariViewRate = 0.8f;
    const s32 sKarikariCounterMax = 30;
    const f32 sPlayerRadius = 75.0f;

    // The view angle (radians) whose tangent is 1/0.3 of the player's radius seen from dist.
    f32 calcFovy(f32 dist) {
        f32 fovAngle = MR::asin(::sPlayerRadius / dist);

        return MR::atan2((dist * MR::tan(fovAngle)) / 0.3f, dist);
    }
};  // namespace

CamKarikariEffector::CamKarikariEffector() : mCounter(0) {
}

void CamKarikariEffector::update(CameraMan* pCameraMan) {
    if (MR::isPlayerDead()) {
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
    Vec3f playerFocusPos = *MR::getPlayerPos() + playerUp * ::sPlayerRadius;

    Vec3f diffPlayerPos = playerFocusPos - CameraLocalUtil::getPos(pCameraMan);
    Vec3f toPlayerPos(diffPlayerPos);
    MR::normalize(&toPlayerPos);

    f32 rotRatio = MR::clamp((1.0f - MR::cos(mCounter * MR::pi() / ::sKarikariCounterMax)) * 0.5f, 0.0f, 1.0f);
    Quat4f rot;
    rot.setRotate(toWatchPos, toPlayerPos, rotRatio);
    rot.transform(diffWatchPos);

    CameraLocalUtil::setWatchPos(pCameraMan, diffWatchPos + CameraLocalUtil::getPos(pCameraMan));

    Vec3f camUp(CameraLocalUtil::getUpVec(pCameraMan));
    rot.transform(camUp);
    CameraLocalUtil::setUpVec(pCameraMan, camUp);

    f32 fovy = ::calcFovy(diffPlayerPos.length());

    if (fovy < CameraLocalUtil::getFovy(pCameraMan) * MR::pi() / 180.0f) {
        CameraLocalUtil::setFovy(pCameraMan, (fovy * 180.0f * t) / MR::pi() + (1.0f - t) * CameraLocalUtil::getFovy(pCameraMan));
    }
}
