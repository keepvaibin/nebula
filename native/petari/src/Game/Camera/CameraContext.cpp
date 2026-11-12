#include "Game/Camera/CameraContext.hpp"
#include "Game/System/NebulaMods.hpp"
#include "Game/Util/MathUtil.hpp"
#include "Game/Util/ScreenUtil.hpp"
#include "Game/Util/SystemUtil.hpp"

#include <cmath>
#include "Game/System/LumaCore.hpp"

CameraContext::CameraContext() : NameObj("カメラコンテキスト") {
    lumacore::setClass(this, lumacore::kClassId< CameraContext >);
    initParams();
}

void CameraContext::initParams() {
    mView.setPositionFromLookAt(Vec3f(0.0f, 0.0f, 3000.0f), Vec3f(0.0f, 1.0f, 0.0f), Vec3f(0.0f, 0.0f, 0.0f));

    mViewInv.set(mView);
    mViewInv.invert(mViewInv);

    mFovy = 45.0f;
    mNearZ = 100.0f;
    mFarZ = 800000.0f;

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
        if (aspect < 4.0f / 3.0f - 1e-4f) {
            const f32 halfTan = tanf(mFovy * (0.5f * PI / 180.0f)) * (4.0f / 3.0f) / aspect;
            return 2.0f * atanf(halfTan) * (180.0f / PI);
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
