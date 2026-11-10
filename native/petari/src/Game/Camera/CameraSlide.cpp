#include "Game/Camera/CameraSlide.hpp"
#include "Game/Camera/CameraLocalUtil.hpp"
#include "Game/Camera/CameraParamChunk.hpp"
#include "Game/Camera/CameraTargetObj.hpp"
#include "Game/Util/MathUtil.hpp"
#include "Game/Util/Tunable.hpp"
#include "Game/System/LumaCore.hpp"

NEBULA_TUNABLE_NAMED(f32, cNum_calc_f1, "calc.f1", 180.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_calc_f2, "calc.f2", 0.1f);
NEBULA_TUNABLE_NAMED(f32, cNum_calc_f3, "calc.f3", 15.0f);

CameraSlide::CameraSlide(const char* pName)
    : Camera(pName), mAxis(1.0f, 0.0f, 0.0f), mUp(0.0f, 1.0f, 0.0f), mWPoint(0.0f, 0.0f, 0.0f), mAngleA(0.0f), mDist(1000.0f) {
    lumacore::setClass(this, lumacore::kClassId< CameraSlide >);
}

CameraTargetObj* CameraSlide::calc() {
    Vec3f front = mAxis.cross(mUp);
    if (MR::isNearZero(front)) {
        return nullptr;
    }
    MR::normalize(&front);

    Vec3f up = front.cross(mAxis);
    MR::normalize(&up);

    Quat4f rot;
    rot.setRotate(front, mAngleA * MR::pi() / cNum_calc_f1);

    Vec3f side = mAxis;
    rot.transform(side);

    const Pos3f& mtx = mZoneMatrix;

    mtx.mult33(front);
    mtx.mult33(up);
    mtx.mult33(side);

    Vec3f watchPoint = mWPoint;
    mtx.mult(watchPoint, watchPoint);

    CameraTargetObj* target = CameraLocalUtil::getTarget(this);
    Vec3f camZ = target->getPosition() - watchPoint;
    Vec3f watchPos = side * side.dot(camZ) + watchPoint;

    Vec3f watchOffset;
    CameraLocalUtil::makeWatchOffset(&watchOffset, this, target, cNum_calc_f2 / cNum_calc_f3);
    watchPos.add(watchOffset);

    Vec3f pos = watchPos + front * mDist;
    CameraLocalUtil::setWatchPos(this, watchPos);
    CameraLocalUtil::setPos(this, pos);
    CameraLocalUtil::setUpVec(this, up);
    CameraLocalUtil::setWatchUpVec(this, target->getUpVec());

    return target;
}

void CameraSlide::setParamFromChunk(const CameraParamChunk* pChunk) {
    CameraGeneralParam* general = pChunk->mGeneralParam;
    Vec3f axis = general->mAxis;
    if (MR::isNearZero(axis)) {
        axis.set< f32 >(1.0f, 0.0f, 0.0f);
    }
    MR::normalize(&axis);
    Vec3f up = general->mUp;
    if (MR::isNearZero(axis) || MR::isSameDirection(axis, up)) {
        up.set< f32 >(0.0f, 1.0f, 0.0f);
        Quat4f rot;
        rot.setRotate(Vec3f(1.0f, 0.0f, 0.0f), axis);
        rot.transform(up);
    }
    MR::normalize(&up);
    setParam(axis, up, general->mWPoint, general->mAngleA, general->mDist);
}
