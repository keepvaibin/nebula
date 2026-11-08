#include "Game/Camera/CameraMtxRegParallel.hpp"
#include "Game/Camera/CameraParamChunk.hpp"
#include "Game/Camera/CameraCalc.hpp"
#include "Game/Camera/CameraLocalUtil.hpp"
#include "Game/Camera/CameraTargetObj.hpp"

CameraMtxRegParallel::~CameraMtxRegParallel() {
}

CameraMtxRegParallel::CameraMtxRegParallel(const char* pName)
    : Camera(pName), mString(), mAngleX(), mAngleY(), mDist(1000.0f), mWPoint(0.0f, 0.0f, 0.0f) {
}

void CameraMtxRegParallel::reset() {
    calcIdealPose();
}

CameraTargetObj* CameraMtxRegParallel::calc() {
    calcIdealPose();
    return CameraLocalUtil::getTarget(this);
}

void CameraMtxRegParallel::setParamFromChunk(const CameraParamChunk* pChunk) {
    CameraGeneralParam* general = pChunk->mGeneralParam;
    setParam(general->mString.getCharPtr(), general->mDist, Vec2f(general->mAngleB, general->mAngleA), general->mWPoint);
}

void CameraMtxRegParallel::calcIdealPose() {
    if (mDist < 300.0f) {
        mDist = 300.0f;
    }

    Vec3f watchPoint;
    CameraLocalUtil::makeWatchPoint(&watchPoint, this, CameraLocalUtil::getTarget(this), 0.1f / 15.0f);

    Pos3f mtx;
    mtx.set(CameraLocalUtil::getMtxReg(mString));

    Vec3f pos;
    MR::polarToCross(Vec3f(0.0f, 0.0f, 0.0f), &pos, mDist, mAngleX, mAngleY - MR::pi() / 2.0f);

    mtx.mult33(pos);
    pos.add(watchPoint);

    Vec3f up;
    mtx.getYDir(up);

    Vec3f wPoint = mWPoint;
    mtx.mult33(wPoint);

    watchPoint.add(wPoint);
    CameraLocalUtil::setWatchPos(this, watchPoint);
    CameraLocalUtil::setPos(this, pos);
    CameraLocalUtil::setUpVec(this, up);
    CameraLocalUtil::setWatchUpVec(this, CameraLocalUtil::getTarget(this)->getUpVec());
}
