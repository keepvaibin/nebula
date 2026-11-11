#include "Game/LiveActor/ClippingJudge.hpp"
#include "Game/LiveActor/ClippingDirector.hpp"
#include "Game/Util.hpp"
#include "Game/Util/Tunable.hpp"
#include "Game/System/LumaCore.hpp"
#include "Game/System/NebulaHost.hpp"

NEBULA_TUNABLE_NAMED(f32, cNum_calcViewingVolumes_f1, "calcViewingVolumes.f1", 360.0f);

namespace {
    // Level 0's entry is unused: it ends at the camera's far Z.
    const f32 sFarClipDistance[ClippingJudge::cFarClipLevelNum] = {
        0.0f, 60000.0f, 50000.0f, 40000.0f, 30000.0f, 20000.0f, 10000.0f, 5000.0f,
    };
    // Objects closer than this in front of the camera are not drawn or run; the second while in first-person view.
    NEBULA_TUNABLE(f32, cNearClip, 500.0f);
    NEBULA_TUNABLE(f32, cNearClipSubjective, 100.0f);
    // While the host traces: how far outside the view a stopped actor stays drawn (30 m), so a tree's shadow does not
    // leave the ground in view as the tree leaves the picture. Drawn only: it still does not run.
    NEBULA_TUNABLE(f32, sTracedDrawMargin, 3000.0f);
};  // namespace

ClippingJudge::ClippingJudge(const char* pName) : NameObj(pName), mFrustums(), mTracedDrawMargin(0.0f) {
    lumacore::setClass(this, lumacore::kClassId< ClippingJudge >);
}

void ClippingJudge::movement() {
    // Once a frame: the host's answer is not free (it looks up the stage's row).
    const u32 tracing = NebulaHost::option(NEBULA_OPTION_RAY_TRACING);
    mTracedDrawMargin = (tracing & static_cast< u32 >(NEBULA_RAY_TRACING_ON)) != 0 && ::sTracedDrawMargin > 0.0f ? ::sTracedDrawMargin : 0.0f;
    calcViewingVolumes(MR::getFarZ());
}

bool ClippingJudge::isJudgedToClipFrustum(const Vec3f& rPos, f32 radius) const {
    return !mFrustums[0].mayIntersectBall3(rPos, radius);
}

bool ClippingJudge::isJudgedToClipFrustum(const Vec3f& rPos, f32 radius, s32 farClipLevel) const {
    return !mFrustums[farClipLevel].mayIntersectBall3(rPos, radius);
}

// Every level shares the four side planes and the near plane; only the far plane differs.
void ClippingJudge::calcViewingVolumes(f32 farZ) {
    f32 nearClip = ::cNearClip;
    if (MR::isSubjectiveCameraOnForObjClipping()) {
        nearClip = ::cNearClipSubjective;
    }
    if (!(nearClip > 1.0f)) {
        nearClip = 1.0f;
    }

    f32 aspect = MR::getAspect();
    f32 fovy = MR::getRenderFovy();
    Pos3f cameraMtx;
    cameraMtx.setPositionFromLookAt(MR::getCameraViewMtx());
    f32 height = nearClip * static_cast< f32 >(tan(fovy * (FloatUtil::PI() / cNum_calcViewingVolumes_f1)));
    f32 width = aspect * height;

    Vec3f front;
    cameraMtx.getZDir(front);
    Vec3f position;
    Vec3f bottomRight(width, -height, nearClip);
    Vec3f topRight(width, height, nearClip);
    Vec3f topLeft(-width, height, nearClip);
    Vec3f bottomLeft(-width, -height, nearClip);
    cameraMtx.getTrans(position);
    cameraMtx.mult(topRight, topRight);
    cameraMtx.mult(topLeft, topLeft);
    cameraMtx.mult(bottomLeft, bottomLeft);
    cameraMtx.mult(bottomRight, bottomRight);

    Hex3f& rCamera = mFrustums[0];
    rCamera.mPlanes[3].set(position, topLeft, topRight);
    rCamera.mPlanes[1].set(position, topRight, bottomRight);
    rCamera.mPlanes[2].set(position, bottomRight, bottomLeft);
    rCamera.mPlanes[0].set(position, bottomLeft, topLeft);

    Vec3f planePosition;
    planePosition.scaleAdd(nearClip, front, position);
    rCamera.mPlanes[4].set(front, planePosition);

    Vec3f back;
    back.negate(front);
    for (s32 level = 0; level < cFarClipLevelNum; level++) {
        Hex3f& rVolume = mFrustums[level];
        if (level != 0) {
            for (s32 plane = 0; plane < 5; plane++) {
                rVolume.mPlanes[plane] = rCamera.mPlanes[plane];
            }
        }

        planePosition.scaleAdd(level == 0 ? farZ : ::sFarClipDistance[level], front, position);
        rVolume.mPlanes[5].set(back, planePosition);
    }
}

namespace MR {
    ClippingJudge* getClippingJudge() {
        return getClippingDirector()->mJudge;
    }
};  // namespace MR
