#include "Game/Camera/CameraSpiral.hpp"
#include "Game/Camera/CamTranslatorSpiral.hpp"
#include "Game/Camera/CameraLocalUtil.hpp"
#include "Game/Camera/CameraTargetObj.hpp"
#include "Game/Util/MathUtil.hpp"

CameraSpiral::CameraSpiral(const char* pName)
    : Camera(pName), mEndTime(60), mTimer(), mStartTime(), mDistStart(1000.0f), mDistEnd(1000.0f), mAngleStart(), mAngleEnd() {
}

void CameraSpiral::reset() {
    mTimer = 0;
}


CameraTargetObj* CameraSpiral::calc() {
    Vec3f watchPoint;
    CameraLocalUtil::makeWatchPoint(&watchPoint, this, CameraLocalUtil::getTarget(this), 0.1f / 15.0f);

    const s32 timer = mTimer;
    f32 easeTime = timer < mStartTime ? 0.0f : timer - mStartTime;

    f32 rate = 0.0f;
    switch (mEaseType) {
    case EaseType_Linear: {
        f32 t = easeTime / getInterval();
        rate = t;
        break;
    }

    case EaseType_InOut: {
        f32 t = easeTime / getInterval();
        f32 t3 = t * t * t;
        f32 t4 = t3 * t;
        f32 t5 = t4 * t;
        rate = t5 * 6.0f - t4 * 15.0f + t3 * 10.0f;
        break;
    }
    }

    f32 invrate = 1.0f - rate;
    f32 offsetY = mOffsetYEnd * rate + mOffsetYStart * invrate;
    f32 angle = mAngleEnd * rate + mAngleStart * invrate;
    f32 dist = mDistEnd * rate + mDistStart * invrate;

    Vec3f spiralDir(MR::sin(angle), 0.0f, MR::cos(angle));
    Vec3f offset = spiralDir * dist + Vec3f(0.0f, offsetY, 0.0f);

    Pos3f mtx;
    mtx.identity();
    const Vec3f& rSide = CameraLocalUtil::getTarget(this)->getSideVec();
    Pos3f* const pMtx = &mtx;
    pMtx->setXDir(rSide);
    pMtx->setYDir(CameraLocalUtil::getTarget(this)->getUpVec());
    pMtx->setZDir(CameraLocalUtil::getTarget(this)->getFrontVec());

    pMtx->mult33(offset);
    pMtx->mult33(spiralDir);

    Vec3f pos;
    pos.add(watchPoint, offset);
    MR::normalize(&offset);

    Quat4f rot;
    rot.setRotate(spiralDir, offset);

    Vec3f up;
    rot.transform(CameraLocalUtil::getTarget(this)->getUpVec(), up);

    CameraLocalUtil::setPos(this, pos);
    CameraLocalUtil::setWatchPos(this, watchPoint);
    CameraLocalUtil::setUpVec(this, up);
    CameraLocalUtil::setWatchUpVec(this, CameraLocalUtil::getTarget(this)->getUpVec());

    if ((mTimer += 1) > mEndTime) {
        mTimer = mEndTime;
    }

    return CameraLocalUtil::getTarget(this);
}

CamTranslatorBase* CameraSpiral::createTranslator() {
    return new CamTranslatorSpiral(this);
}
