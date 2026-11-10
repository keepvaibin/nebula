#include "Game/Camera/CameraTalk.hpp"
#include "Game/Camera/CameraParamChunk.hpp"
#include "Game/Camera/CameraLocalUtil.hpp"
#include "Game/Camera/CameraTargetObj.hpp"
#include "Game/Util/MathUtil.hpp"
#include "Game/Util/Tunable.hpp"
#include "Game/System/LumaCore.hpp"

NEBULA_TUNABLE_NAMED(f32, cNum_calc_f1, "calc.f1", 2.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_calc_f2, "calc.f2", 2.0f);

namespace {
    NEBULA_TUNABLE(f32, sRate, 0.5f);
    NEBULA_TUNABLE(f32, sAngleX, 0.12f);
    NEBULA_TUNABLE(f32, sAngleY, 0.7f);
};  // namespace

CameraTalk::CameraTalk(const char* pName)
    : Camera(pName), mPos(0.0f, 0.0f, 0.0f), mUp(0.0f, 1.0f, 0.0f), mHeight(120.0f), mDist(450.0f) {
    lumacore::setClass(this, lumacore::kClassId< CameraTalk >);
}

void CameraTalk::reset() {
    CameraLocalUtil::setPos(this, CameraLocalUtil::getPos(mCameraMan));
    calc();
}

CameraTargetObj* CameraTalk::calc() {
    Quat4f rotUp;
    rotUp.setRotate(mUp, -CameraLocalUtil::getTarget(this)->getGravityVector(), ::sRate);

    Vec3f up = mUp;
    rotUp.transform(up);

    Vec3f pos = CameraLocalUtil::getTarget(this)->getPosition() * (1.0f - ::sRate) + mPos * ::sRate;
    Vec3f front = CameraLocalUtil::getTarget(this)->getPosition() - mPos;
    if (!MR::isNearZero(front)) {
        MR::normalize(&front);
    } else {
        front.set< f32 >(0.0f, 0.0f, 1.0f);
    }

    Vec3f side = up.cross(front);
    if (MR::isNearZero(side)) {
        return nullptr;
    }
    MR::normalize(&side);
    front.cross(side, up);

    Vec3f posDiff = CameraLocalUtil::getPos(this) - CameraLocalUtil::getTarget(this)->getPosition();

    Vec3f orientCross = posDiff.cross(front);
    f32 orientation = orientCross.dot(up);

    Pos3f transMtx;
    transMtx.makeTrans(0.0f, 0.0f, mDist);

    Pos3f rotX;
    rotX.makeRotate(Vec3f(-1.0f, 0.0f, 0.0f), ::sAngleX);

    Pos3f rotY;
    if (front.dot(posDiff) >= 0.0f) {
        if (orientation >= 0.0f) {
            rotY.makeRotate(Vec3f(0.0f, 1.0f, 0.0f), -::sAngleY);
        } else {
            rotY.makeRotate(Vec3f(0.0f, 1.0f, 0.0f), ::sAngleY);
        }

    } else {
        if (orientation >= 0.0f) {
            rotY.makeRotate(Vec3f(0.0f, 1.0f, 0.0f), -MR::pi() / cNum_calc_f1);
        } else {
            rotY.makeRotate(Vec3f(0.0f, 1.0f, 0.0f), MR::pi() / cNum_calc_f2);
        }
    }

    Pos3f mtx;
    mtx.identity();
    mtx.setXDir(side);
    mtx.setYDir(up);
    mtx.setZDir(front);
    mtx.setTrans(pos);

    mtx.concat(mtx, rotY);
    mtx.concat(mtx, rotX);
    mtx.concat(mtx, transMtx);

    Vec3f basePos;
    mtx.getTrans(basePos);

    CameraLocalUtil::setWatchPos(this, pos + up * mHeight);
    CameraLocalUtil::setPos(this, basePos + up * mHeight);
    CameraLocalUtil::setUpVec(this, up);
    CameraLocalUtil::setWatchUpVec(this, CameraLocalUtil::getTarget(this)->getUpVec());

    return CameraLocalUtil::getTarget(this);
}

void CameraTalk::setParam(const Vec3f& pos, const Vec3f& up, f32 height, f32 dist) {
    mPos.set(pos);
    mUp.set(up);
    mHeight = height;
    mDist = dist;
}

void CameraTalk::setParamFromChunk(const CameraParamChunk* pChunk) {
    CameraGeneralParam* general = pChunk->mGeneralParam;
    setParam(general->mWPoint, general->mUp, general->mAxis.x, general->mAxis.y);
}
