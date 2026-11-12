#include "Game/Camera/CameraTripodPlanet.hpp"
#include "Game/Camera/CameraLocalUtil.hpp"
#include "Game/Camera/CameraParamChunk.hpp"
#include "Game/Camera/CameraTargetObj.hpp"
#include "Game/Util/MathUtil.hpp"
#include "Game/System/LumaCore.hpp"

CameraTripodPlanet::CameraTripodPlanet(const char* pName)
    : Camera(pName), mAxis(0.0f, 1.0f, 0.0f), mWPoint(0.0f, 0.0f, 0.0f), mAngleX(), mAngleY(), mDist(1000.0f) {
    lumacore::setClass(this, lumacore::kClassId< CameraTripodPlanet >);
}

CameraTargetObj* CameraTripodPlanet::calc() {
    CameraTargetObj* target = CameraLocalUtil::getTarget(this);

    Vec3f watchPos;
    CameraLocalUtil::makeWatchPoint(&watchPos, this, target, 0.1f / 15.0f);

    Pos3f mtx = mZoneMatrix;
    Pos3f inv = mtx;
    inv.invert(inv);

    Vec3f localWatchPos = watchPos;
    inv.mult(localWatchPos, localWatchPos);

    Vec3f localPos = target->getPosition();
    inv.mult(localPos, localPos);

    Vec3f rotUp = localPos - mWPoint;
    if (MR::isNearZero(rotUp)) {
        return nullptr;
    }
    MR::normalize(&rotUp);

    Vec3f rotFront = mAxis;
    MR::normalize(&rotFront);
    rotFront.negate();

    Vec3f rotSide = rotUp.cross(rotFront);
    MR::normalize(&rotSide);

    rotFront.cross(rotSide, rotUp);

    Pos3f baseMtx;
    baseMtx.setXDir(rotSide);
    baseMtx.setYDir(rotUp);
    baseMtx.setZDir(rotFront);
    baseMtx.setTrans(localWatchPos);

    Pos3f rotX;
    rotX.makeRotate(Vec3f(1.0f, 0.0f, 0.0f), -mAngleX);

    Pos3f rotY;
    rotY.makeRotate(Vec3f(0.0f, 1.0f, 0.0f), mAngleY);

    Pos3f transMtx;
    transMtx.makeTrans(0.0f, 0.0f, mDist);

    Pos3f dstMtx;
    dstMtx.concat(rotX, transMtx);
    dstMtx.concat(rotY, dstMtx);
    dstMtx.concat(baseMtx, dstMtx);

    Vec3f pos;
    dstMtx.getTrans(pos);
    Vec3f up;
    dstMtx.getYDir(up);

    mtx.mult(pos, pos);
    mtx.mult33(up, up);

    CameraLocalUtil::setWatchPos(this, watchPos);
    CameraLocalUtil::setPos(this, pos);
    CameraLocalUtil::setUpVec(this, up);
    CameraLocalUtil::setWatchUpVec(this, target->getUpVec());

    return target;
}

void CameraTripodPlanet::setParamFromChunk(const CameraParamChunk* pChunk) {
    CameraGeneralParam* general = pChunk->mGeneralParam;
    setParam(general->mWPoint, CameraLocalUtil::getNormalizedOr(general->mAxis, Vec3f(0.0f, 1.0f, 0.0f)), Vec2f(general->mAngleA, general->mAngleB), general->mDist);
}
