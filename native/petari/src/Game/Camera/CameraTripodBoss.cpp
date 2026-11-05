#include "Game/Camera/CameraTripodBoss.hpp"
#include "Game/Camera/CamTranslatorTripodBoss.hpp"
#include "Game/Camera/CameraLocalUtil.hpp"

CameraTripodBoss::~CameraTripodBoss() {
}

CameraTripodBoss::CameraTripodBoss(const char* pName) : CameraTower(pName), mAngleY() {
}

CamTranslatorBase* CameraTripodBoss::createTranslator() {
    return new CamTranslatorTripodBoss(this);
}

void CameraTripodBoss::arrangeRound() {
    Vec3f offset = CameraLocalUtil::getPos(this) - CameraLocalUtil::getWatchPos(this);

    Quat4f rot;
    rot.setRotate(Vec3f(0.0f, 1.0f, 0.0f), mAngleY);
    rot.transform(offset);

    CameraLocalUtil::setPos(this, CameraLocalUtil::getWatchPos(this) + offset);
    CameraTower::arrangeRound();
}
