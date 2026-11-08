#include "Game/Camera/CameraRotChecker.hpp"
#include "Game/Util/CameraUtil.hpp"
#include "Game/Util/MathUtil.hpp"

namespace {
    const f32 sThreshold = FloatUtil::PI() / 180.0f;
};  // namespace

CameraRotChecker::CameraRotChecker() : mIsRotatingHard(false) {
    mMtx.identity();
}

void CameraRotChecker::update() {
    Pos3f mtx;
    mtx.concat(MR::getCameraViewMtx(), mMtx);

    Vec3f axis;
    mIsRotatingHard = MR::abs(mtx.getRotate(axis)) > ::sThreshold;

    mMtx.set(MR::getCameraInvViewMtx());
}
