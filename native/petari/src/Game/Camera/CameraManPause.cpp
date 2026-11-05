#include "Game/Camera/CameraManPause.hpp"
#include "Game/Camera/CameraCalc.hpp"
#include "Game/Camera/CameraLocalUtil.hpp"
#include "Game/Camera/CameraTargetObj.hpp"
#include "Game/Util/PlayerInputUtil.hpp"
#include "Game/Util/VectorUtil.hpp"

namespace {
    static const f32 sDistance = 6000.0f;
};  // namespace

CameraManPause::CameraManPause(const char* pName)
    : CameraMan(pName), mDistBase(::sDistance), mDistOffset(), mAngle(0.0f, 0.0f), mBasePos(3230.0f, 13350.0f, 520.0f) {
    CameraLocalUtil::setFovy(this, 45.0f);
}

void CameraManPause::init(const JMapInfoIter& rIter) {
}

void CameraManPause::calc() {
    if (CameraLocalUtil::getTarget(this) == nullptr) {
        return;
    }

    Pos3f mtx;
    if (CameraLocalUtil::getTarget(this)->getMapBaseMtx() != nullptr) {
        mtx.set(*CameraLocalUtil::getTarget(this)->getMapBaseMtx());
    } else {
        mtx.identity();
    }

    Vec3f pos = CameraLocalUtil::getTarget(this)->getPosition();
    Vec3f watchUp, watchPos;
    mtx.getYDir(watchUp);
    mtx.getTrans(watchPos);

    mtx.setTrans(watchPos + watchUp * watchUp.dot(pos - watchPos));

    Pos3f inv = mtx;
    inv.invert(inv);
    inv.mult(pos, pos);

    f32 length, angleX, angleY;
    MR::crossToPolar(Vec3f(0.0f, 0.0f, 0.0f), pos, &length, &angleX, &angleY);

    if (MR::isHeldCamera(PLAYER_1)) {
        mDistOffset -= MR::getMoveStickY(PLAYER_1) * 200.0f;
    } else {
        mAngle.x += MR::getMoveStickY(PLAYER_1) * 0.1f;
        mAngle.y += MR::getMoveStickX(PLAYER_1) * 0.1f;
    }

    MR::clampBoth(&mDistOffset, -mDistBase * 0.7f, mDistBase * 4.5f);
    MR::clampBoth(&mAngle.x, -1.4959966f, 1.4959966f);  // TODO: result of a const calculation?

    if (MR::isHeldCamera(PLAYER_1) && MR::isPressedCrouch(PLAYER_1)) {
        mDistOffset = mDistBase * 4.5f;
        mAngle.zero();
    }

    Vec3f newPos;
    MR::polarToCross(Vec3f(0.0f, 0.0f, 0.0f), &newPos, mDistBase + mDistOffset + length, mAngle.x, mAngle.y + angleY);

    mtx.mult(newPos, newPos);

    Pos3f rotX;
    rotX.makeRotate(Vec3f(-1.0f, 0.0f, 0.0f), mAngle.x);

    Pos3f rotY;
    rotY.makeRotate(Vec3f(0.0f, 1.0f, 0.0f), mAngle.y + angleY);

    Pos3f rotMtx;
    rotMtx.concat(rotY, rotX);

    Vec3f up;
    rotMtx.getYDir(up);

    CameraLocalUtil::setWatchPos(this, watchPos);
    CameraLocalUtil::setWatchUpVec(this, watchUp);
    CameraLocalUtil::setPos(this, newPos);
    CameraLocalUtil::setUpVec(this, up);
}

void CameraManPause::notifyActivate() {
    mAngle.x = 0.0f;
    mAngle.y = 0.0f;
    mDistOffset = 0.0f;

    if (CameraLocalUtil::getTarget(this) == nullptr) {
        return;
    }

    f32 dist = (mBasePos - CameraLocalUtil::getTarget(this)->getPosition()).length();

    f32 focalDist = dist * 1.5f / (MR::tan(CameraLocalUtil::getFovy(this) * 0.5f));

    if (focalDist > mDistBase) {
        mDistOffset = focalDist - mDistBase;
    }
}

void CameraManPause::notifyDeactivate() {
}
