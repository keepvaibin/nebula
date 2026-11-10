#include "Game/Camera/CameraDead.hpp"
#include "Game/Camera/CameraParamChunk.hpp"
#include "Game/Camera/Camera.hpp"
#include "Game/Camera/CameraLocalUtil.hpp"
#include "Game/Camera/CameraTargetObj.hpp"
#include "Game/Util/MathUtil.hpp"
#include "Game/Util/Tunable.hpp"

NEBULA_TUNABLE_NAMED(f32, cNum_calc_f1, "calc.f1", 180.0f);

CameraDead::CameraDead(const char* pName) : Camera(pName), mDist(0.5f), mDeadTime(), mCameraType(), mDeadFrame() {
}

void CameraDead::reset() {
    mDeadFrame = 0;
    mFovy = CameraLocalUtil::getFovy(mCameraMan);
    CameraLocalUtil::setWatchPos(this, CameraLocalUtil::getWatchPos(mCameraMan));
    CameraLocalUtil::setPos(this, CameraLocalUtil::getPos(mCameraMan));
    Vec3f up = CameraLocalUtil::getUpVec(mCameraMan);
    Vec3f front = CameraLocalUtil::getWatchPos(this) - CameraLocalUtil::getPos(this);
    if (!MR::isNearZero(front)) {
        MR::normalize(&front);
        Quat4f rot;
        rot.setRotate(front, -CameraLocalUtil::getRoll(mCameraMan));
        rot.transform(up);
    }
    CameraLocalUtil::setUpVec(this, up);
    CameraLocalUtil::setWatchUpVec(this, CameraLocalUtil::getWatchUpVec(mCameraMan));
}

CameraTargetObj* CameraDead::calc() {
    if (mDeadFrame <= mDeadTime) {
        // The fourth power of the dead time passed: the watch point and the zoom ease in.
        f32 t = static_cast< f32 >(mDeadFrame) / mDeadTime;
        t *= t;
        t *= t;

        Vec3f watchPoint;
        CameraLocalUtil::makeWatchPointImm(&watchPoint, this, CameraLocalUtil::getTarget(this));

        if (mCameraType != CameraType_FixedPos) {
            watchPoint = CameraLocalUtil::getWatchPos(this) + (watchPoint - CameraLocalUtil::getWatchPos(this)) * t;
        }
        CameraLocalUtil::setWatchPos(this, watchPoint);

        f32 angle = MR::atan2(CameraLocalUtil::getTarget(this)->getRadius() / mDist,
                              (CameraLocalUtil::getPos(this) - CameraLocalUtil::getTarget(this)->getPosition()).length());

        if (angle > mFovy) {
            angle = mFovy;
        }

        angle *= cNum_calc_f1 / MR::pi();

        if (mDeadTime != 0) {
            mFovy += t * (angle - mFovy);
        } else {
            mFovy = angle;
        }
    }

    CameraLocalUtil::setFovy(this, mFovy);
    mDeadFrame++;

    return CameraLocalUtil::getTarget(this);
}

void CameraDead::setParamFromChunk(const CameraParamChunk* pChunk) {
    CameraGeneralParam* general = pChunk->mGeneralParam;
    setParam(general->mDist, general->mNum1, general->mNum2);
}
