#include "Game/Camera/CameraObjParallel.hpp"
#include "Game/Camera/CameraLocalUtil.hpp"
#include "Game/Camera/CameraParamChunk.hpp"
#include "Game/Camera/CameraTargetObj.hpp"
#include "Game/Util/Tunable.hpp"

NEBULA_TUNABLE_NAMED(f32, cNum_calc_f1, "calc.f1", 0.1f);
NEBULA_TUNABLE_NAMED(f32, cNum_calc_f2, "calc.f2", 15.0f);

CameraObjParallel::CameraObjParallel(const char* pName) : Camera(pName), mAngleX(), mAngleY(), mDist(3000.0f) {
}

CameraTargetObj* CameraObjParallel::calc() {
    CameraTargetObj* target = CameraLocalUtil::getTarget(this);

    Pos3f mtx;
    mtx.identity();

    mtx.setYDir(target->getUpVec());
    mtx.setZDir(target->getFrontVec());
    mtx.setXDir(target->getSideVec());

    Pos3f rotX;
    rotX.makeRotate(Vec3f(1.0f, 0.0f, 0.0f), mAngleX);

    Pos3f rotY;
    rotY.makeRotate(Vec3f(0.0f, 1.0f, 0.0f), mAngleY);

    mtx.concat(rotY);
    mtx.concat(rotX);

    Vec3f watchPoint;
    CameraLocalUtil::makeWatchPoint(&watchPoint, this, target, cNum_calc_f1 / cNum_calc_f2);
    CameraLocalUtil::setWatchPos(this, watchPoint);

    Vec3f offset;
    mtx.getZDir(offset);

    offset.set(watchPoint + offset * mDist);
    CameraLocalUtil::setPos(this, offset);

    Vec3f up;
    mtx.getYDir(up);
    CameraLocalUtil::setUpVec(this, up);

    CameraLocalUtil::setWatchUpVec(this, target->getUpVec());

    return target;
}

void CameraObjParallel::setParamFromChunk(const CameraParamChunk* pChunk) {
    CameraGeneralParam* general = pChunk->mGeneralParam;
    setParam(general->mDist, general->mAngleA, general->mAngleB);
}
