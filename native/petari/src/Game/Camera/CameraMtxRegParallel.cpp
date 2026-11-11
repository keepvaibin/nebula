#include "Game/Camera/CameraMtxRegParallel.hpp"
#include "Game/Camera/CameraCalc.hpp"
#include "Game/Camera/CameraLocalUtil.hpp"
#include "Game/Camera/CameraParamChunk.hpp"
#include "Game/Camera/CameraTargetObj.hpp"
#include "Game/Util/Tunable.hpp"
#include "Game/System/LumaCore.hpp"

NEBULA_TUNABLE_NAMED(f32, cNum_calcIdealPose_f1, "calcIdealPose.f1", 300.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_calcIdealPose_f2, "calcIdealPose.f2", 300.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_calcIdealPose_f3, "calcIdealPose.f3", 0.1f);
NEBULA_TUNABLE_NAMED(f32, cNum_calcIdealPose_f4, "calcIdealPose.f4", 15.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_calcIdealPose_f5, "calcIdealPose.f5", 2.0f);

CameraMtxRegParallel::CameraMtxRegParallel(const char* pName)
    : Camera(pName), mString(), mAngleX(), mAngleY(), mDist(1000.0f), mWPoint(0.0f, 0.0f, 0.0f) {
    lumacore::setClass(this, lumacore::kClassId< CameraMtxRegParallel >);
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
    if (mDist < cNum_calcIdealPose_f1) {
        mDist = cNum_calcIdealPose_f2;
    }

    CameraTargetObj* target = CameraLocalUtil::getTarget(this);
    Vec3f watchPoint;
    CameraLocalUtil::makeWatchPoint(&watchPoint, this, target, cNum_calcIdealPose_f3 / cNum_calcIdealPose_f4);

    Pos3f mtx;
    mtx.set(CameraLocalUtil::getMtxReg(mString));

    Vec3f pos;
    MR::polarToCross(Vec3f(0.0f, 0.0f, 0.0f), &pos, mDist, mAngleX, mAngleY - MR::pi() / cNum_calcIdealPose_f5);

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
    CameraLocalUtil::setWatchUpVec(this, target->getUpVec());
}
