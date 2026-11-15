#include "Game/Camera/CameraSubjective.hpp"
#include "Game/Camera/CameraLocalUtil.hpp"
#include "Game/Camera/CameraTargetObj.hpp"
#include "Game/Util/PlayerInputUtil.hpp"
#include "Game/Util/MathUtil.hpp"
#include "Game/System/LumaCore.hpp"

namespace {
    const f32 sDistance = 200.0f;
    const f32 sHeight = 120.0f;
    const f32 sDivW = 10.0f;
    const f32 sDivH = 10.0f;
    const f32 sRotXMax = 80.0f;
    const f32 sRotYMax = 90.0f;
    const f32 sPadStickPlay = 0.3f;
    const f32 sRotSpeedEff = 3.0f;

    // A stick value outside the play, rescaled to -1..1.
    f32 removeStickPlay(f32 value) {
        f32 stick = 0.0f;
        if (value < -::sPadStickPlay) {
            stick = (value + ::sPadStickPlay) / (1.0f - ::sPadStickPlay);
        } else if (value > ::sPadStickPlay) {
            stick = (value - ::sPadStickPlay) / (1.0f - ::sPadStickPlay);
        }

        if (stick > 1.0f) {
            stick = 1.0f;
        } else if (stick < -1.0f) {
            stick = -1.0f;
        }

        return stick;
    }
};  // namespace

CameraSubjective::CameraSubjective(const char* pName) : Camera(pName), mAngleVel(0.0f, 0.0f), mAngle(0.0f, 0.0f) {
    lumacore::setClass(this, lumacore::kClassId< CameraSubjective >);
    mMtx.identity();
}

void CameraSubjective::reset() {
    mAngle.zero();

    CameraLocalUtil::setPos(this, CameraLocalUtil::getTarget(this)->getPosition() + CameraLocalUtil::getTarget(this)->getUpVec() * ::sHeight);

    mMtx.setXDir(CameraLocalUtil::getTarget(this)->getSideVec());
    mMtx.setYDir(CameraLocalUtil::getTarget(this)->getUpVec());
    mMtx.setZDir(CameraLocalUtil::getTarget(this)->getFrontVec());
    mMtx.setTrans(Vec3f(0.0f, 0.0f, 0.0f));

    mAngleVel.zero();
}

CameraTargetObj* CameraSubjective::calc() {
    Vec2f accel(0.0f, 0.0f);

    Vec2f stick(::removeStickPlay(MR::getMoveStickX(PLAYER_1)), ::removeStickPlay(MR::getMoveStickY(PLAYER_1)));
    stick.y *= -1.0f;

    accel.x -= stick.x;
    accel.y -= stick.y;

    mAngle.x = MR::clamp(mAngle.x + mAngleVel.x * ::sRotSpeedEff, -45.0f, ::sRotYMax);
    mAngle.y = MR::clamp(mAngle.y + mAngleVel.y * ::sRotSpeedEff, -::sRotXMax, ::sRotXMax);

    mAngleVel.x *= 0.9f;
    mAngleVel.y *= 0.9f;

    mAngleVel.x = MR::clamp(mAngleVel.x + accel.y / ::sDivH, -1.0f, 2.0f);
    mAngleVel.y = MR::clamp(mAngleVel.y + accel.x / ::sDivW, -1.0f, 2.0f);

    Pos3f rotX;
    rotX.makeRotate(Vec3f(1.0f, 0.0f, 0.0f), mAngle.x * MR::pi() / 180.0f);
    Pos3f rotY;
    rotY.makeRotate(Vec3f(0.0f, 1.0f, 0.0f), mAngle.y * MR::pi() / 180.0f);

    Pos3f rot;
    rot.concat(mMtx, rotY);
    rot.concat(rotX);

    Vec3f front;
    rot.getZDir(front);
    front.scale(-::sDistance);
    Vec3f up;
    rot.getYDir(up);

    CameraLocalUtil::setWatchPos(this, CameraLocalUtil::getPos(this) - front);
    CameraLocalUtil::setUpVec(this, up);
    CameraLocalUtil::setWatchUpVec(this, CameraLocalUtil::getTarget(this)->getUpVec());

    return CameraLocalUtil::getTarget(this);
}
