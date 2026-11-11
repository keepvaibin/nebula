#include "Game/Map/FileSelectCameraController.hpp"
#include "Game/LiveActor/Nerve.hpp"
#include "Game/Util/ActorCameraUtil.hpp"
#include "Game/Util/LiveActorUtil.hpp"
#include "Game/Util/ObjUtil.hpp"
#include "Game/System/NebulaFrameClock.hpp"
#include "Game/Util/Tunable.hpp"
#include "Game/System/LumaCore.hpp"

NEBULA_TUNABLE_NAMED(f32, cNum_exeTitle_f1, "exeTitle.f1", 15000.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeTitle_f2, "exeTitle.f2", 15000.0f);

namespace {
    // const Vec cTitleTargetPos = {};
    // const Vec cTitlePos = {};
    NEBULA_TUNABLE_RECORD(Vec, cFarTarget, {0.0f, 800.0f, 0.0f});
    NEBULA_TUNABLE_RECORD(Vec, cFarPoint, {0.0f, 0.0f, 15000.0f});
    NEBULA_TUNABLE_RECORD(Vec, cNearTargetOffset, {0.0f, 1100.0f, 0.0f});
    NEBULA_TUNABLE_RECORD(Vec, cNearPointOffset, {0.0f, 0.0f, 4800.0f});
    NEBULA_TUNABLE(f32, cTitleFovy, 60.0f);
    NEBULA_TUNABLE(f32, cFarFovy, 40.0f);
    NEBULA_TUNABLE(f32, cNearFovy, 50.0f);
};  // namespace

namespace {
    NEW_NERVE(FileSelectCameraControllerNrvTitle, FileSelectCameraController, Title);
    NEW_NERVE(FileSelectCameraControllerNrvMoveToFarPoint, FileSelectCameraController, MoveToFarPoint);
    NEW_NERVE(FileSelectCameraControllerNrvFarPoint, FileSelectCameraController, FarPoint);
    NEW_NERVE(FileSelectCameraControllerNrvMoveToNearPoint, FileSelectCameraController, MoveToNearPoint);
    NEW_NERVE(FileSelectCameraControllerNrvNearPoint, FileSelectCameraController, NearPoint);
};  // namespace

FileSelectCameraController::FileSelectCameraController(const char* pName)
    : LiveActor(pName), mWPoint(0.0f, 0.0f, 0.0f), mPrevWPoint(0.0f, 0.0f, 0.0f), mFovy(::cTitleFovy), mPrevFovy(::cTitleFovy),
      mUpVec(0.0f, 1.0f, 0.0f) {
    lumacore::setClass(this, lumacore::kClassId< FileSelectCameraController >);
}

void FileSelectCameraController::init(const TableIter& /*rIter*/) {
    MR::connectToSceneMapObjMovement(this);
    MR::invalidateClipping(this);
    MR::initActorCameraProgrammable(this);
    initNerve(GET_NERVE_ANON(FileSelectCameraControllerNrvTitle));
    makeActorDead();
}

void FileSelectCameraController::appear() {
    LiveActor::appear();
    setNerve(GET_NERVE_ANON(FileSelectCameraControllerNrvTitle));
    MR::startActorCameraProgrammable(this);
}

void FileSelectCameraController::kill() {
    LiveActor::kill();
    MR::endActorCameraProgrammable(this, -1, true);
}

void FileSelectCameraController::goToFarPoint() {
    setNerve(GET_NERVE_ANON(FileSelectCameraControllerNrvMoveToFarPoint));
}

void FileSelectCameraController::goToNearPoint(const Vec3f& rPoint) {
    mNearTarget.set(rPoint + Vec3f(::cNearTargetOffset));

    setNerve(GET_NERVE_ANON(FileSelectCameraControllerNrvMoveToNearPoint));
}

bool FileSelectCameraController::isAtFarPoint() const {
    return isNerve(GET_NERVE_ANON(FileSelectCameraControllerNrvFarPoint));
}

bool FileSelectCameraController::isAtNearPoint() const {
    return isNerve(GET_NERVE_ANON(FileSelectCameraControllerNrvNearPoint));
}

bool FileSelectCameraController::isToOrAtFarPoint() const {
    return isNerve(GET_NERVE_ANON(FileSelectCameraControllerNrvMoveToFarPoint)) || isNerve(GET_NERVE_ANON(FileSelectCameraControllerNrvFarPoint));
}

bool FileSelectCameraController::isToOrAtNearPoint() const {
    return isNerve(GET_NERVE_ANON(FileSelectCameraControllerNrvMoveToNearPoint)) || isNerve(GET_NERVE_ANON(FileSelectCameraControllerNrvNearPoint));
}

void FileSelectCameraController::exeTitle() {
    if (MR::isFirstStep(this)) {
        mWPoint.set< f32 >(::cFarTarget.x, ::cFarTarget.y + cNum_exeTitle_f1, ::cFarTarget.z);
        mPosition.set< f32 >(::cFarPoint.x, ::cFarTarget.y + cNum_exeTitle_f2, ::cFarPoint.z);
        mUpVec.set(0.0f, 1.0f, 0.0f);
    }
}

void FileSelectCameraController::exeMoveToFarPoint() {
    if (MR::isFirstStep(this)) {
        mUpVec.set(0.0f, 1.0f, 0.0f);
    }

    f32 squaredTime = NebulaFrameClock::secondsOfSteps(getNerveStep());

    squaredTime *= squaredTime;
    mWPoint += (Vec3f(::cFarTarget) - mWPoint) * squaredTime;
    mFovy += (::cFarFovy - mFovy) * squaredTime;
    mPosition.add((Vec3f(::cFarPoint) - mPosition) * squaredTime);

    MR::setNerveAtStep(this, GET_NERVE_ANON(FileSelectCameraControllerNrvFarPoint), NebulaFrameClock::stepsOfSeconds(1));
}

void FileSelectCameraController::exeFarPoint() {
    mWPoint = ::cFarTarget;
    mFovy = ::cFarFovy;
    mPosition.set(Vec3f(::cFarPoint));
}

void FileSelectCameraController::exeMoveToNearPoint() {
    Vec3f nearPoint = mNearTarget + ::cNearPointOffset;
    f32 squaredTime = NebulaFrameClock::secondsOfSteps(getNerveStep());

    squaredTime *= squaredTime;
    mWPoint += (mNearTarget - mWPoint) * squaredTime;
    mFovy += (::cNearFovy - mFovy) * squaredTime;
    mPosition += (nearPoint - mPosition) * squaredTime;

    MR::setNerveAtStep(this, GET_NERVE_ANON(FileSelectCameraControllerNrvNearPoint), NebulaFrameClock::stepsOfSeconds(1));
}

void FileSelectCameraController::exeNearPoint() {
    Vec3f nearPoint = mNearTarget + ::cNearPointOffset;

    mFovy = ::cNearFovy;
    mPosition.set(nearPoint);
}

void FileSelectCameraController::control() {
    MR::setProgrammableCameraParam(this, mPrevWPoint, mPosition, mUpVec);
    MR::setProgrammableCameraParamFovy(this, mPrevFovy);

    mPrevWPoint = mWPoint;
    mPrevFovy = mFovy;
}
