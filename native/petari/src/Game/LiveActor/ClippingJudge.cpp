#include "Game/LiveActor/ClippingJudge.hpp"
#include "Game/LiveActor/ClippingDirector.hpp"
#include "Game/Util.hpp"

ClippingJudge::ClippingJudge(const char* pName) : NameObj(pName), mFrustum() {
    for (s32 i = 0; i < 8; i++) {
        mClipDistances[i] = -1.0f;
    }

    mClipDistances[0] = 0.0f;
    mClipDistances[1] = 60000.0f;
    mClipDistances[2] = 50000.0f;
    mClipDistances[3] = 40000.0f;
    mClipDistances[4] = 30000.0f;
    mClipDistances[5] = 20000.0f;
    mClipDistances[6] = 10000.0f;
    mClipDistances[7] = 5000.0f;
}

void ClippingJudge::init(const TableIter& rIter) {
}

void ClippingJudge::movement() {
    calcViewingVolume(&mFrustum, MR::getFarZ());

    for (u32 i = 1; i < 8; i++) {
        calcViewingVolume(&mClipFrustums[i], mClipDistances[i]);
    }
}

bool ClippingJudge::isJudgedToClipFrustum(const Vec3f& rVec, f32 a2) const {
    return !mFrustum.mayIntersectBall3(rVec, a2);
}

bool ClippingJudge::isJudgedToClipFrustum(const Vec3f& rVec, f32 a2, s32 index) const {
    return (!index) ? !mFrustum.mayIntersectBall3(rVec, a2) : !mClipFrustums[index].mayIntersectBall3(rVec, a2);
}

void ClippingJudge::calcViewingVolume(Hex3f* pVolume, f32 farClip) {
    f32 nearClip = 500.0f;
    if (MR::isSubjectiveCameraOnForObjClipping()) {
        nearClip = 100.0f;
    }

    f32 aspect = MR::getAspect();
    f32 fovy = MR::getRenderFovy();
    Pos3f cameraMtx;
    cameraMtx.setPositionFromLookAt(MR::getCameraViewMtx());
    f32 height = nearClip * static_cast< f32 >(tan(fovy * (FloatUtil::PI() / 360.0f)));
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

    pVolume->mPlanes[3].set(position, topLeft, topRight);
    pVolume->mPlanes[1].set(position, topRight, bottomRight);
    pVolume->mPlanes[2].set(position, bottomRight, bottomLeft);
    pVolume->mPlanes[0].set(position, bottomLeft, topLeft);

    Vec3f back;
    back.negate(front);
    Vec3f planePosition;
    planePosition.scaleAdd(farClip, front, position);
    pVolume->mPlanes[5].set(back, planePosition);
    planePosition.scaleAdd(nearClip, front, position);
    pVolume->mPlanes[4].set(front, planePosition);
}

namespace MR {
    ClippingJudge* getClippingJudge() {
        return getClippingDirector()->mJudge;
    }
};  // namespace MR

ClippingJudge::~ClippingJudge() {
}
