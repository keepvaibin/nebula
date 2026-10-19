#include "Game/Camera/CameraContext.hpp"
#include "Game/Util/MathUtil.hpp"
#include "Game/Util/ScreenUtil.hpp"
#include "Game/Util/SystemUtil.hpp"

#include <cmath>

CameraContext::~CameraContext() {
}

CameraContext::CameraContext() : NameObj("\203\112\203\201\203\211\203\122\203\223\203\145\203\114\203\130\203\147" /* カメラコンテキスト */) {
    initParams();
}

void CameraContext::initParams() {
    mView.setPositionFromLookAt(TVec3f(0.0f, 0.0f, 3000.0f), TVec3f(0.0f, 1.0f, 0.0f), TVec3f(0.0f, 0.0f, 0.0f));

    mViewInv.set(mView);
    mViewInv.invert(mViewInv);

    mFovy = 45.0f;
    const f32 height = MR::getScreenHeight();
    const f32 width = MR::getScreenWidth();
    mAspectRatio = width / height;
    mNearZ = 100.0f;
    mFarZ = 800000.0f;

    mShakeOffset.zero();

    updateProjectionMtx();
}

const TPos3f& CameraContext::getViewMtx() const {
    return mView;
}

const TPos3f& CameraContext::getInvViewMtx() const {
    return mViewInv;
}

f32 CameraContext::getAspect() const {
    if (MR::isScreen16Per9()) {
#ifdef TARGET_PC
        if (MR::getDisplay().wide) {
            return MR::getDisplay().content_aspect;
        }
#endif
        return 16.0f / 9.0f;
    } else {
        return static_cast< f32 >(MR::getScreenWidth()) / MR::getScreenHeight();
    }
}

// The vertical field of view the picture is drawn with: the camera's, and below 4:3 (TARGET_PC's wide shapes) widened
// so the view keeps 4:3's width and grows taller (Vert+). What projects as the picture does (particles, clipping,
// blur, the pointer) uses it; the cameras' own framing keeps mFovy.
f32 CameraContext::getRenderFovy() const {
#ifdef TARGET_PC
    if (MR::isScreen16Per9() && MR::getDisplay().wide) {
        const f32 aspect = MR::getDisplay().content_aspect;
        if (aspect < 4.0f / 3.0f - 1e-4f) {
            const f32 halfTan = tanf(mFovy * (0.5f * PI / 180.0f)) * (4.0f / 3.0f) / aspect;
            return 2.0f * atanf(halfTan) * (180.0f / PI);
        }
    }
#endif
    return mFovy;
}

void CameraContext::setViewMtx(const TPos3f& rMtx, bool b1, bool b2, const TVec3f& rVec) {
    mView.set(rMtx);
    mViewInv.invert(rMtx);
}

void CameraContext::setNearZ(f32 near) {
    mNearZ = near;
    updateProjectionMtx();
}

void CameraContext::setFarZ(f32 far) {
    mFarZ = far;
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
    mProjection.makePerspective(getRenderFovy(), getAspect(), mNearZ, mFarZ);

    TProj3f trans;
    trans.makeTrans(mShakeOffset.x, mShakeOffset.y);

    mProjection.concat(trans, mProjection);
}
