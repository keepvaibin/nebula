#include "Game/MapObj/AstroDomeCameraController.hpp"
#include "Game/LiveActor/HitSensor.hpp"
#include "Game/LiveActor/Nerve.hpp"
#include "Game/Map/SphereSelector.hpp"
#include "Game/Util/ActorCameraUtil.hpp"
#include "Game/Util/CameraUtil.hpp"
#include "Game/Util/DemoUtil.hpp"
#include "Game/Util/LiveActorUtil.hpp"
#include "Game/Util/MathUtil.hpp"
#include "Game/Util/ObjUtil.hpp"
#include "Game/Util/Tunable.hpp"

namespace {
    NEBULA_TUNABLE_RECORD(Vec, cDefaultUp, {0.0f, 1.0f, 0.0f});
    NEBULA_TUNABLE_RECORD(Vec, cZoomInPos, {0.0f, 3000.0f, -8500.0f});
    NEBULA_TUNABLE(f32, cFovy, 60.0f);
    NEBULA_TUNABLE(f32, cZoomOutPosZ, -22000.0f);
    NEBULA_TUNABLE(f32, cZoomInTargetOffsetY, 200.0f);
};  // namespace

namespace NrvAstroDomeCameraController {
    NEW_NERVE(AstroDomeCameraControllerNrvGalaxySelectStart, AstroDomeCameraController, GalaxySelectStart);
    NEW_NERVE(AstroDomeCameraControllerNrvGalaxySelect, AstroDomeCameraController, GalaxySelect);
    NEW_NERVE(AstroDomeCameraControllerNrvGalaxyConfirmStart, AstroDomeCameraController, GalaxyConfirmStart);
    NEW_NERVE(AstroDomeCameraControllerNrvGalaxyConfirm, AstroDomeCameraController, GalaxyConfirm);
    NEW_NERVE(AstroDomeCameraControllerNrvGalaxyConfirmCancel, AstroDomeCameraController, GalaxyConfirmCancel);
};  // namespace NrvAstroDomeCameraController

AstroDomeCameraController::AstroDomeCameraController(const char* pName) : LiveActor(pName), mStartPos(gZeroVec), mEaseRate(), mZeroPos(gZeroVec) {
}

void AstroDomeCameraController::init(const TableIter& /*rIter*/) {
    MR::connectToSceneMapObjMovement(this);

    MR::invalidateClipping(this);

    MR::initActorCameraProgrammable(this);
    initNerve(GET_NERVE(AstroDomeCameraController, AstroDomeCameraControllerNrvGalaxySelectStart));

    SphereSelectorFunction::registerTarget(this);
    MR::registerDemoSimpleCastAll(this);

    makeActorDead();
}

void AstroDomeCameraController::appear() {
    LiveActor::appear();

    mStartPos.set(MR::getCamPos());
    mZoomPos.reset(mStartPos);
    mTargetPos.reset(mZeroPos);
    mUpPos.reset(::cDefaultUp);

    MR::startActorCameraProgrammable(this, SphereSelectorFunction::getSelectStartFrame());

    setNerve(GET_NERVE(AstroDomeCameraController, AstroDomeCameraControllerNrvGalaxySelectStart));
}

void AstroDomeCameraController::kill() {
    MR::endActorCameraProgrammable(this, -1, true);
    LiveActor::kill();
}

void AstroDomeCameraController::control() {
    mZoomPos.mTarget.lerp(mZoomPos.mStart, mZoomPos.mEnd, mEaseRate);
    mTargetPos.mTarget.lerp(mTargetPos.mStart, mTargetPos.mEnd, mEaseRate);
    mUpPos.mTarget.lerp(mUpPos.mStart, mUpPos.mEnd, mEaseRate);

    MR::setProgrammableCameraParam(this, mTargetPos.mTarget, mZoomPos.mTarget, mUpPos.mTarget);
    MR::setProgrammableCameraParamFovy(this, ::cFovy);
}

bool AstroDomeCameraController::receiveOtherMsg(u32 msg, HitSensor* /*pSender*/, HitSensor* /*pReceiver*/) {
    if (SphereSelectorFunction::trySyncAppearMsgSelectStart(this, msg)) {
        return true;
    }

    if (SphereSelectorFunction::isMsgConfirmStart(msg)) {
        setNerve(GET_NERVE(AstroDomeCameraController, AstroDomeCameraControllerNrvGalaxyConfirmStart));
        return true;
    }

    if (SphereSelectorFunction::isMsgConfirmCancel(msg)) {
        setNerve(GET_NERVE(AstroDomeCameraController, AstroDomeCameraControllerNrvGalaxyConfirmCancel));
        return true;
    }

    return false;
}

void AstroDomeCameraController::calcZoomOutPos(Vec3f* pDst) const {
    SphereSelectorFunction::calcOffsetPos(pDst, gZeroVec, Vec3f(0.0f, 0.0f, ::cZoomOutPosZ), -mStartPos, Vec3f(0.0f, 1.0f, 0.0f));
}

void AstroDomeCameraController::calcZoomInPos(Vec3f* pDst, const Vec3f& rUp) const {
    Vec3f zoomOutPos;
    calcZoomOutPos(&zoomOutPos);

    Vec3f x;
    x.sub(SphereSelectorFunction::getSelectedActorTrans(), zoomOutPos);
    SphereSelectorFunction::calcOffsetPos(pDst, SphereSelectorFunction::getSelectedActorTrans(), ::cZoomInPos, x, rUp);
}

void AstroDomeCameraController::calcZoomInTarget(Vec3f* pDst, const Vec3f& rUp) const {
    Vec3f offset;
    offset.scale(::cZoomInTargetOffsetY, rUp);
    pDst->add(SphereSelectorFunction::getSelectedActorTrans(), offset);
}

void AstroDomeCameraController::calcZoomInUp(Vec3f* pDst) const {
    Pos3f rotateMtx;
    SphereSelectorFunction::calcHandledRotateMtx(Vec3f(0.0f, 0.0f, 0.0f), &rotateMtx);
    rotateMtx.getYDir(*pDst);
}

void AstroDomeCameraController::exeGalaxySelectStart() {
    s32 startFrame = SphereSelectorFunction::getSelectStartFrame();

    if (MR::isFirstStep(this)) {
        Vec3f zoomOutPos;
        calcZoomOutPos(&zoomOutPos);

        mZoomPos.setEnd(zoomOutPos);
        mTargetPos.setEnd(mZeroPos);
    }

    mEaseRate = MR::getEaseInOutValue(MR::calcNerveRate(this, startFrame), 0.0f, 1.0f, 1.0f);
    MR::setNerveAtStep(this, GET_NERVE(AstroDomeCameraController, AstroDomeCameraControllerNrvGalaxySelect), startFrame);
}

void AstroDomeCameraController::exeGalaxySelect() {
    if (MR::isFirstStep(this)) {
        mEaseRate = 1.0f;
    }

    Vec3f zoomOutPos;
    calcZoomOutPos(&zoomOutPos);

    mZoomPos.reset(zoomOutPos);
    mTargetPos.reset(mZeroPos);
}

void AstroDomeCameraController::exeGalaxyConfirmStart() {
    s32 frame = SphereSelectorFunction::getConfirmStartCancelFrame();

    Vec3f vecUp;
    calcZoomInUp(&vecUp);

    Vec3f vecTarget;
    calcZoomInTarget(&vecTarget, vecUp);

    if (MR::isFirstStep(this)) {
        mTargetPos.setEnd(vecTarget);

        Vec3f zoomInPos;
        calcZoomInPos(&zoomInPos, vecUp);

        mZoomPos.setEnd(zoomInPos);
        mUpPos.setEnd(vecUp);
    }

    mEaseRate = MR::calcNerveEaseOutRate(this, frame);
    mTargetPos.mEnd.set(vecTarget);

    MR::setNerveAtStep(this, GET_NERVE(AstroDomeCameraController, AstroDomeCameraControllerNrvGalaxyConfirm), frame);
}

void AstroDomeCameraController::exeGalaxyConfirm() {
    Vec3f vecUp;
    calcZoomInUp(&vecUp);

    if (MR::isFirstStep(this)) {
        mEaseRate = 1.0f;
    }

    Vec3f zoomInPos;
    calcZoomInPos(&zoomInPos, vecUp);
    mZoomPos.reset(zoomInPos);

    Vec3f zoomInTarget;
    calcZoomInTarget(&zoomInTarget, vecUp);
    mTargetPos.reset(zoomInTarget);
}

void AstroDomeCameraController::exeGalaxyConfirmCancel() {
    s32 frame = SphereSelectorFunction::getConfirmStartCancelFrame();

    if (MR::isFirstStep(this)) {
        Vec3f zoomOutPos;
        calcZoomOutPos(&zoomOutPos);

        mZoomPos.setEnd(zoomOutPos);
        mTargetPos.setEnd(mZeroPos);
        mUpPos.setEnd(::cDefaultUp);
    }

    mEaseRate = MR::calcNerveEaseInRate(this, frame);
    MR::setNerveAtStep(this, GET_NERVE(AstroDomeCameraController, AstroDomeCameraControllerNrvGalaxySelect), frame);
}
