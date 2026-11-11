#include "Game/Camera/CameraBlackHole.hpp"
#include "Game/Camera/CameraParamChunk.hpp"
#include "Game/Camera/CameraLocalUtil.hpp"
#include "Game/Util/MathUtil.hpp"
#include <nebula_geometry.hpp>
#include "Game/Util/Tunable.hpp"
#include "Game/System/LumaCore.hpp"

NEBULA_TUNABLE_NAMED(f32, cNum_reset_f1, "reset.f1", 0.7f);
NEBULA_TUNABLE_NAMED(f32, cNum_calc_f1, "calc.f1", 0.002f);

CameraBlackHole::CameraBlackHole(const char* pName) : Camera(pName), mFovy(1.0f), mRoll(), mWPoint(0.0f, 0.0f, 0.0f), mBasePos(0.0f, 0.0f, 0.0f) {
    lumacore::setClass(this, lumacore::kClassId< CameraBlackHole >);
}

void CameraBlackHole::reset() {
    mFovy = CameraLocalUtil::getFovy(mCameraMan);
    mRoll = CameraLocalUtil::getRoll(mCameraMan);
    Vec3f camWatchDir = mBasePos - mWPoint;
    Vec3f watchDir = CameraLocalUtil::getPos(mCameraMan) - CameraLocalUtil::getWatchPos(mCameraMan);
    if (MR::isNearZero(camWatchDir) || MR::isNearZero(watchDir)) {
        CameraLocalUtil::setWatchPos(this, mWPoint);
        CameraLocalUtil::setPos(this, CameraLocalUtil::getPos(mCameraMan));
        CameraLocalUtil::setWatchUpVec(this, CameraLocalUtil::getWatchUpVec(mCameraMan));
        CameraLocalUtil::setUpVec(this, CameraLocalUtil::getUpVec(mCameraMan));
    } else {
        f32 camWatchDist = camWatchDir.length();
        f32 targetDist = (CameraLocalUtil::getPos(mCameraMan) - mWPoint).length();
        f32 targetLength = camWatchDist + (targetDist - camWatchDist) * cNum_reset_f1;
        Vec3f up = CameraLocalUtil::getUpVec(mCameraMan);
        Vec3f watchUp = CameraLocalUtil::getWatchUpVec(mCameraMan);
        MR::normalize(&camWatchDir);
        MR::normalize(&watchDir);
        Quat4f rot;
        rot.setRotate(watchDir, camWatchDir);
        rot.transform(up);
        CameraLocalUtil::setUpVec(this, up);
        rot.transform(watchUp);
        CameraLocalUtil::setWatchUpVec(this, watchUp);
        camWatchDir.setLength(targetLength);
        CameraLocalUtil::setWatchPos(this, mWPoint);
        CameraLocalUtil::setPos(this, camWatchDir + mWPoint);
    }
}

CameraTargetObj* CameraBlackHole::calc() {
    CameraLocalUtil::setFovy(this, mFovy);
    CameraLocalUtil::setRoll(this, mRoll);
    mRoll += cNum_calc_f1;
    return nullptr;
}

void CameraBlackHole::setParamFromChunk(const CameraParamChunk* pChunk) {
    CameraGeneralParam* general = pChunk->mGeneralParam;
    setParam(general->mWPoint, general->mAxis);
}
