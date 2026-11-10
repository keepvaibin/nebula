#include "Game/Camera/CameraContext.hpp"
#include "Game/System/NebulaMods.hpp"
#include "Game/Util/MathUtil.hpp"
#include "Game/Util/ScreenUtil.hpp"
#include "Game/Util/SystemUtil.hpp"

#include <cmath>
#include "Game/Util/Tunable.hpp"
#include "Game/System/LumaCore.hpp"

NEBULA_TUNABLE_NAMED(f32, cNum_initParams_f1, "initParams.f1", 3000.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_initParams_f2, "initParams.f2", 45.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_initParams_f3, "initParams.f3", 100.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_initParams_f4, "initParams.f4", 800000.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_getRenderFovy_f1, "getRenderFovy.f1", 4.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_getRenderFovy_f2, "getRenderFovy.f2", 3.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_getRenderFovy_f3, "getRenderFovy.f3", 0.5f);
NEBULA_TUNABLE_NAMED(f32, cNum_getRenderFovy_f4, "getRenderFovy.f4", 180.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_getRenderFovy_f5, "getRenderFovy.f5", 4.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_getRenderFovy_f6, "getRenderFovy.f6", 3.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_getRenderFovy_f7, "getRenderFovy.f7", 2.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_getRenderFovy_f8, "getRenderFovy.f8", 180.0f);

CameraContext::CameraContext() : NameObj("カメラコンテキスト") {
    lumacore::setClass(this, lumacore::kClassId< CameraContext >);
    initParams();
}

void CameraContext::initParams() {
    mView.setPositionFromLookAt(Vec3f(0.0f, 0.0f, cNum_initParams_f1), Vec3f(0.0f, 1.0f, 0.0f), Vec3f(0.0f, 0.0f, 0.0f));

    mViewInv.set(mView);
    mViewInv.invert(mViewInv);

    mFovy = cNum_initParams_f2;
    mNearZ = cNum_initParams_f3;
    mFarZ = cNum_initParams_f4;

    mShakeOffset.zero();

    updateProjectionMtx();
}

const Pos3f& CameraContext::getViewMtx() const {
    return mView;
}

const Pos3f& CameraContext::getInvViewMtx() const {
    return mViewInv;
}

f32 CameraContext::getAspect() const {
    return MR::getScreenAspect();
}

// The vertical field of view the picture is drawn with: the camera's, and below 4:3 (the native build's wide shapes) widened
// so the view keeps 4:3's width and grows taller (Vert+). What projects as the picture does (particles, clipping,
// blur, the pointer) uses it; the cameras' own framing keeps mFovy.
f32 CameraContext::getRenderFovy() const {
    if (MR::isScreen16Per9() && MR::getDisplay().wide) {
        const f32 aspect = MR::getDisplay().content_aspect;
        if (aspect < cNum_getRenderFovy_f1 / cNum_getRenderFovy_f2 - 1e-4f) {
            const f32 halfTan = tanf(mFovy * (cNum_getRenderFovy_f3 * PI / cNum_getRenderFovy_f4)) * (cNum_getRenderFovy_f5 / cNum_getRenderFovy_f6) / aspect;
            return cNum_getRenderFovy_f7 * atanf(halfTan) * (cNum_getRenderFovy_f8 / PI);
        }
    }
    return mFovy;
}

void CameraContext::setViewMtx(const Pos3f& rMtx) {
    mView.set(rMtx);
    mViewInv.invert(rMtx);
}

void CameraContext::setNearZ(f32 near) {
    mNearZ = near;
    updateProjectionMtx();
}

void CameraContext::setFovy(f32 fovy) {
    mFovy = fovy;
    updateProjectionMtx();
}

void CameraContext::setShakeOffset(f32 x, f32 y) {
    mShakeOffset.set(x, y);
    updateProjectionMtx();
}

void CameraContext::updateProjectionMtx() {
    mProjection.makePerspective(getRenderFovy(), getAspect(), NebulaMods::cameraNear(mNearZ), mFarZ);

    Proj3f trans;
    trans.makeTrans(mShakeOffset.x, mShakeOffset.y);

    mProjection.concat(trans, mProjection);
}
