#include "Game/Camera/CameraRaceFollow.hpp"
#include "Game/Camera/CameraParamChunk.hpp"
#include "Game/Camera/CameraLocalUtil.hpp"
#include "Game/Camera/CameraMan.hpp"
#include "Game/Camera/CameraTargetObj.hpp"
#include "Game/Util/MathUtil.hpp"
#include "Game/System/LumaCore.hpp"

CameraRaceFollow::CameraRaceFollow(const char* pName)
    : Camera(pName), mDistMin(500.0f), mDistMax(1200.0f), mAngleX(MR::toRadian(15.0f)), mIsForcePitch(), mRoundAngleSpeed() {
    lumacore::setClass(this, lumacore::kClassId< CameraRaceFollow >);
}

void CameraRaceFollow::reset() {
    CameraLocalUtil::setWatchPos(this, CameraLocalUtil::getWatchPos(mCameraMan));
    CameraLocalUtil::setPos(this, CameraLocalUtil::getPos(mCameraMan));
    Vec3f up;
    mCameraMan->mMatrix.getYDir(up);
    CameraLocalUtil::setUpVec(this, up);
}

CameraTargetObj* CameraRaceFollow::calc() {
    CameraTargetObj* target = CameraLocalUtil::getTarget(this);
    Vec3f camDirPrev = CameraLocalUtil::getPos(this) - CameraLocalUtil::getWatchPos(this);

    Vec3f watchPoint;
    CameraLocalUtil::makeWatchPoint(&watchPoint, this, target, 0.1f / 15.0f);
    CameraLocalUtil::setWatchPos(this, watchPoint);

    CameraLocalUtil::arrangeDistanceByPushAndPull(this, mDistMin, mDistMax);

    Vec3f offsetUp = -target->getGravityVector();

    Vec3f offsetDir = CameraLocalUtil::getPos(this) - CameraLocalUtil::getWatchPos(this);
    f32 camDist = offsetDir.length();
    MR::normalize(&offsetDir);

    f32 angle = offsetUp.angle(offsetDir);
    f32 angleTargetDiff = MR::pi() / 2.0f - mAngleX;

    if (mIsForcePitch || angle > angleTargetDiff) {
        Quat4f rotX;
        rotX.setRotate(offsetDir, offsetUp, (angle - angleTargetDiff) / angle);
        rotX.transform(offsetDir);
        CameraLocalUtil::setPos(this, offsetDir * camDist + CameraLocalUtil::getWatchPos(this));
    }

    Vec3f frontPrev = camDirPrev;
    Vec3f front = CameraLocalUtil::getPos(this) - CameraLocalUtil::getWatchPos(this);
    if (!MR::isNearZero(frontPrev) && !MR::isNearZero(front)) {
        MR::normalize(&frontPrev);
        MR::normalize(&front);
        Quat4f rotY;
        rotY.setRotate(frontPrev, front);

        Vec3f localUp = CameraLocalUtil::getUpVec(this);
        rotY.transform(localUp);
        CameraLocalUtil::setUpVec(this, localUp);
    }

    MR::normalize(&front);
    Vec3f side = CameraLocalUtil::getUpVec(this).cross(front);
    Vec3f up1 = front.cross(side);
    CameraLocalUtil::setUpVec(this, up1);

    MR::normalize(&front);

    Vec3f camUp = -target->getGravityVector();
    camUp.orthogonalize(front);

    Vec3f up = CameraLocalUtil::getUpVec(this);
    Quat4f rotUp;
    rotUp.setRotate(CameraLocalUtil::getUpVec(this), camUp, 0.05f);
    rotUp.transform(up);
    CameraLocalUtil::setUpVec(this, up);

    goRound();

    CameraLocalUtil::setWatchUpVec(this, CameraLocalUtil::getUpVec(this));

    return target;
}

void CameraRaceFollow::setParamFromChunk(const CameraParamChunk* pChunk) {
    CameraGeneralParam* general = pChunk->mGeneralParam;
    setParam(general->mWPoint.x, general->mWPoint.y, general->mAngleA * MR::pi() / 180.0f, general->mNum1 != 0, general->mWPoint.z);
}

void CameraRaceFollow::goRound() {
    Vec3f targetSide = CameraLocalUtil::getTarget(this)->getSideVec();
    Vec3f offset = CameraLocalUtil::getPos(this) - CameraLocalUtil::getWatchPos(this);

    if (MR::isNearZero(offset)) {
        return;
    }

    Vec3f side = offset.cross(CameraLocalUtil::getUpVec(this));
    if (MR::isNearZero(side)) {
        return;
    }
    MR::normalize(&side);

    Quat4f rotY;
    rotY.setRotate(side, targetSide, mRoundAngleSpeed);
    rotY.transform(offset);
    CameraLocalUtil::setPos(this, CameraLocalUtil::getWatchPos(this) + offset);
}
