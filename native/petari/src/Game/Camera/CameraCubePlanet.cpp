#include "Game/Camera/CameraCubePlanet.hpp"
#include "Game/Camera/CameraParamChunk.hpp"
#include "Game/Camera/CameraLocalUtil.hpp"
#include "Game/Camera/CameraTargetObj.hpp"
#include "Game/Util/MathUtil.hpp"
#include <nebula_geometry.hpp>
#include "Game/System/LumaCore.hpp"

CameraCubePlanet::CameraCubePlanet(const char* pName)
    : Camera(pName), mDist(3000.0f), mAngleX(MR::pi() / 6.0f), mAngleY(0.35f), mUp(0.0f, 1.0f, 0.0f) {
    lumacore::setClass(this, lumacore::kClassId< CameraCubePlanet >);
}

void CameraCubePlanet::reset() {
    mUp.set(-CameraLocalUtil::getTarget(this)->getGravityVector());
}

CameraTargetObj* CameraCubePlanet::calc() {
    Vec3f pos = CameraLocalUtil::getPos(mCameraMan);
    Vec3f up = mUp;

    Quat4f upRot;
    upRot.setRotate(mUp, -CameraLocalUtil::getTarget(this)->getGravityVector(), CameraLocalUtil::isForceCameraChange() ? 1.0f : mAngleY);
    upRot.transform(up);
    mUp.set(up);

    Vec3f watchPoint;
    CameraLocalUtil::makeWatchPointImm(&watchPoint, this, CameraLocalUtil::getTarget(this));
    CameraLocalUtil::setWatchPos(this, watchPoint);
    CameraLocalUtil::setWatchUpVec(this, up);

    Vec3f toWatch = watchPoint - pos;
    Vec3f front = toWatch - up * up.dot(toWatch);
    if (MR::isNearZero(front)) {
        return nullptr;
    }
    MR::normalize(&front);

    Vec3f side = up.cross(front);
    Quat4f rot;
    rot.setRotate(side, mAngleX);
    rot.transform(up);
    rot.transform(front);
    front.setLength(mDist);
    CameraLocalUtil::setPos(this, watchPoint - front);
    CameraLocalUtil::setUpVec(this, up);
    return CameraLocalUtil::getTarget(this);
}

void CameraCubePlanet::setParamFromChunk(const CameraParamChunk* pChunk) {
    CameraGeneralParam* general = pChunk->mGeneralParam;
    setParam(general->mDist, general->mAngleA, general->mAngleB);
}
