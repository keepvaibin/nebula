#include "Game/Camera/CameraSpiral.hpp"
#include "Game/Camera/CameraParamChunk.hpp"
#include "Game/Camera/CameraLocalUtil.hpp"
#include "Game/Camera/CameraTargetObj.hpp"
#include "Game/Util/MathUtil.hpp"
#include "Game/Util/Tunable.hpp"
#include "Game/System/LumaCore.hpp"

NEBULA_TUNABLE_NAMED(f32, cNum_calc_f1, "calc.f1", 0.1f);
NEBULA_TUNABLE_NAMED(f32, cNum_calc_f2, "calc.f2", 15.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_calc_f3, "calc.f3", 6.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_calc_f4, "calc.f4", 15.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_calc_f5, "calc.f5", 10.0f);

CameraSpiral::CameraSpiral(const char* pName)
    : Camera(pName), mEndTime(60), mTimer(), mStartTime(), mDistStart(1000.0f), mDistEnd(1000.0f), mAngleStart(), mAngleEnd() {
    lumacore::setClass(this, lumacore::kClassId< CameraSpiral >);
}

void CameraSpiral::reset() {
    mTimer = 0;
}

CameraTargetObj* CameraSpiral::calc() {
    Vec3f watchPoint;
    CameraLocalUtil::makeWatchPoint(&watchPoint, this, CameraLocalUtil::getTarget(this), cNum_calc_f1 / cNum_calc_f2);

    f32 easeTime = mTimer < mStartTime ? 0.0f : mTimer - mStartTime;

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
        rate = t5 * cNum_calc_f3 - t4 * cNum_calc_f4 + t3 * cNum_calc_f5;
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
    mtx.setXDir(CameraLocalUtil::getTarget(this)->getSideVec());
    mtx.setYDir(CameraLocalUtil::getTarget(this)->getUpVec());
    mtx.setZDir(CameraLocalUtil::getTarget(this)->getFrontVec());

    mtx.mult33(offset);
    mtx.mult33(spiralDir);

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

    mTimer++;
    if (mTimer > mEndTime) {
        mTimer = mEndTime;
    }

    return CameraLocalUtil::getTarget(this);
}

void CameraSpiral::setParamFromChunk(const CameraParamChunk* pChunk) {
    CameraGeneralParam* general = pChunk->mGeneralParam;
    s32 startTime = static_cast< s16 >(static_cast< u32 >(general->mNum1) >> 16);
    s32 endTime = static_cast< s16 >(static_cast< u32 >(general->mNum1) & 0xFFFF);
    setParam(general->mNum2, startTime, endTime, general->mWPoint.y, general->mAxis.y, general->mWPoint.z, general->mAxis.z, general->mWPoint.x, general->mAxis.x);
}
