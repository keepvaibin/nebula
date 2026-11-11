#include "Game/Camera/CameraTripodBoss.hpp"
#include "Game/Camera/CameraLocalUtil.hpp"
#include "Game/Camera/CameraParamChunk.hpp"
#include "Game/System/LumaCore.hpp"

CameraTripodBoss::CameraTripodBoss(const char* pName) : CameraTower(pName), mAngleY() {
    lumacore::setClass(this, lumacore::kClassId< CameraTripodBoss >);
}

void CameraTripodBoss::setParamFromChunk(const CameraParamChunk* pChunk) {
    CameraGeneralParam* general = pChunk->mGeneralParam;
    setParam(general->mWPoint, general->mAxis, general->mDist, Vec2f(general->mUp.x, general->mUp.y));
    mAngleY = general->mUp.z;
}

void CameraTripodBoss::arrangeRound() {
    Vec3f offset = CameraLocalUtil::getPos(this) - CameraLocalUtil::getWatchPos(this);

    Quat4f rot;
    rot.setRotate(Vec3f(0.0f, 1.0f, 0.0f), mAngleY);
    rot.transform(offset);

    CameraLocalUtil::setPos(this, CameraLocalUtil::getWatchPos(this) + offset);
    CameraTower::arrangeRound();
}
