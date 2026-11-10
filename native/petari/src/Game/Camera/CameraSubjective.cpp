#include "Game/Camera/CameraSubjective.hpp"
#include "Game/Camera/CameraLocalUtil.hpp"
#include "Game/Camera/CameraTargetObj.hpp"
#include "Game/Util/PlayerInputUtil.hpp"
#include "Game/Util/MathUtil.hpp"
#include "Game/Util/Tunable.hpp"
#include "Game/System/LumaCore.hpp"

NEBULA_TUNABLE_NAMED(f32, cNum_calc_f1, "calc.f1", 45.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_calc_f2, "calc.f2", 0.9f);
NEBULA_TUNABLE_NAMED(f32, cNum_calc_f3, "calc.f3", 0.9f);
NEBULA_TUNABLE_NAMED(f32, cNum_calc_f4, "calc.f4", 2.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_calc_f5, "calc.f5", 2.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_calc_f6, "calc.f6", 180.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_calc_f7, "calc.f7", 180.0f);

namespace {
    NEBULA_TUNABLE(f32, sDistance, 200.0f);
    NEBULA_TUNABLE(f32, sHeight, 120.0f);
    NEBULA_TUNABLE(f32, sDivW, 10.0f);
    NEBULA_TUNABLE(f32, sDivH, 10.0f);
    NEBULA_TUNABLE(f32, sRotXMax, 80.0f);
    NEBULA_TUNABLE(f32, sRotYMax, 90.0f);
    NEBULA_TUNABLE(f32, sPadStickPlay, 0.3f);
    NEBULA_TUNABLE(f32, sRotSpeedEff, 3.0f);

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

    mAngle.x = MR::clamp(mAngle.x + mAngleVel.x * ::sRotSpeedEff, -cNum_calc_f1, ::sRotYMax);
    mAngle.y = MR::clamp(mAngle.y + mAngleVel.y * ::sRotSpeedEff, -::sRotXMax, ::sRotXMax);

    mAngleVel.x *= cNum_calc_f2;
    mAngleVel.y *= cNum_calc_f3;

    mAngleVel.x = MR::clamp(mAngleVel.x + accel.y / ::sDivH, -1.0f, cNum_calc_f4);
    mAngleVel.y = MR::clamp(mAngleVel.y + accel.x / ::sDivW, -1.0f, cNum_calc_f5);

    Pos3f rotX;
    rotX.makeRotate(Vec3f(1.0f, 0.0f, 0.0f), mAngle.x * MR::pi() / cNum_calc_f6);
    Pos3f rotY;
    rotY.makeRotate(Vec3f(0.0f, 1.0f, 0.0f), mAngle.y * MR::pi() / cNum_calc_f7);

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
