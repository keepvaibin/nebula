#include "Game/Camera/CamHeliEffector.hpp"
#include "Game/Camera/CameraLocalUtil.hpp"
#include "Game/Camera/CameraTargetObj.hpp"
#include "Game/Player/MarioActor.hpp"  // DO NOT REMOVE
#include "Game/Util/MathUtil.hpp"

CamHeliEffector::CamHeliEffector() : mStep(), mRate() {
}

void CamHeliEffector::update(CameraMan* pCamera) {
    if (!CameraLocalUtil::getTarget(pCamera)->isCameraStateOn(1)) {
        mStep = 0;
        if (mRate == 0.0f) {
            return;
        }

        f32 val = mRate;
        val *= 0.98f;
        mRate = val;
        if (val < 0.001f) {
            mRate = 0.0f;
        }
    } else {
        f32 val = mStep / 60.0f;
        if (val > 1.0f) {
            val = 1.0f;
        }

        val *= val;
        val *= val;
        mRate += (1.0f - mRate) * val;
        mStep++;
    }

    Vec3f up = CameraLocalUtil::getTarget(pCamera)->getUpVec();
    Vec3f diff = CameraLocalUtil::getPos(pCamera) - CameraLocalUtil::getWatchPos(pCamera);

    if (MR::isNearZero(diff)) {
        return;
    }

    Vec3f direction = diff;
    MR::normalize(&direction);

    f32 angle = direction.angle(up);
    if (angle < MR::pi() / 6.0f) {
        return;
    }

    Quat4f quat;
    const f32 rate = (angle - MR::pi() / 6.0f) / angle;
    quat.setRotate(direction, up, rate * mRate);
    quat.transform(diff);

    CameraLocalUtil::setPos(pCamera, diff + CameraLocalUtil::getWatchPos(pCamera));

    Vec3f newUp(CameraLocalUtil::getUpVec(pCamera));
    quat.transform(newUp);
    CameraLocalUtil::setUpVec(pCamera, newUp);
}
