#include "Game/Camera/CameraGround.hpp"
#include "Game/Camera/CameraParamChunk.hpp"
#include "Game/Camera/CameraCalc.hpp"
#include "Game/Camera/CameraLocalUtil.hpp"
#include "Game/Camera/CameraTargetObj.hpp"
#include "Game/Util/VectorUtil.hpp"
#include "Game/System/LumaCore.hpp"

CameraGround::CameraGround(const char* pName)
    : Camera(pName), mAngleA(), mAngleB(), mDist(1000.0f), mUp(0.0f, 1.0f, 0.0f), mLastWatchPoint(0.0f, 0.0f, 0.0f), mIsNotInitialized(true) {
    lumacore::setClass(this, lumacore::kClassId< CameraGround >);
}

void CameraGround::reset() {
    mIsNotInitialized = true;
}

CameraTargetObj* CameraGround::calc() {
    Vec3f watchPoint;
    CameraLocalUtil::makeGroundWatchPoint(&watchPoint, this, &mLastWatchPoint, &mIsNotInitialized);

    Vec3f pos;
    MR::polarToCross(watchPoint, &pos, mDist, mAngleA, mAngleB);
    CameraLocalUtil::setWatchPos(this, watchPoint);
    CameraLocalUtil::setPos(this, pos);
    CameraLocalUtil::setUpVec(this, mUp);
    CameraLocalUtil::setWatchUpVec(this, mUp);

    return CameraLocalUtil::getTarget(this);
}

void CameraGround::setParamFromChunk(const CameraParamChunk* pChunk) {
    setParam(Vec2f(pChunk->mGeneralParam->mAngleA, pChunk->mGeneralParam->mAngleB), pChunk->mGeneralParam->mDist, pChunk->mGeneralParam->mUp);
}
