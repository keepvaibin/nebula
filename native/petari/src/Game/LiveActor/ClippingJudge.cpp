#include "Game/LiveActor/ClippingJudge.hpp"
#include "Game/LiveActor/ClippingDirector.hpp"
#include "Game/Util.hpp"
#ifdef TARGET_PC
#include "Game/Map/LightDataHolder.hpp"
#include "Game/Map/LightDirector.hpp"
#include "Game/Scene/SceneObjHolder.hpp"
#include <nebula_game_api.h>
#include <revolution/os.h>

namespace {
    // The traced sun's direction of travel (from the sun; else the stage key light's), when ray-traced shadows want
    // clipped casters.
    TVec3f sShadowDirection;
    bool sShadowCasters;
    // Ray-traced light wants out-of-view light sources whose light reaches the view (ClippingActorInfo::isWantedForLight).
    bool sLightSources;
};  // namespace
#endif

void ClippingJudge_FORCE_MATCH_SDATA2() {
    0.0f;
}

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

void ClippingJudge::init(const JMapInfoIter& rIter) {
}

void ClippingJudge::movement() {
    calcViewingVolume(&mFrustum, MR::getFarZ());

    for (u32 i = 1; i < 8; i++) {
        calcViewingVolume(&mClipFrustums[i], mClipDistances[i]);
    }
#ifdef TARGET_PC
    updateShadowCasters();
#endif
}

#ifdef TARGET_PC
void ClippingJudge::updateShadowCasters() {
    sShadowCasters = false;
    sLightSources = __NebulaOption(NEBULA_OPTION_RT_SHADOW_CASTERS) != 0;
    if (!sLightSources) {
        return;
    }
    // The sun the ray tracer lights the scene from (the game's visible sun, the stage table's, else the key light).
    f32 sun[3];
    if (__NebulaTracedSun(sun) != 0) {
        sShadowDirection.set(-sun[0], -sun[1], -sun[2]);
        sShadowCasters = true;
        return;
    }
    if (!MR::isExistSceneObj(SceneObj_LightDirector)) {
        return;
    }
    const LightDirector* director = MR::getSceneObj< LightDirector >(SceneObj_LightDirector);
    if (director->mDefaultAreaLight == nullptr) {
        return;
    }
    // Towards the key light as the ray tracer takes it: a world light's position from the origin, a camera light's
    // from the eye, turned into the world.
    const LightInfo& key = director->mDefaultAreaLight->mPlanetLight.mInfo0;
    TVec3f toward(key.mPos.x, key.mPos.y, key.mPos.z);
    if (key.mIsFollowCamera) {
        TPos3f camera;
        camera.setPositionFromLookAt(MR::getCameraViewMtx());
        camera.mult33(toward);
    }
    const f32 length = __builtin_sqrtf(toward.dot(toward));
    if (!(length > 1e-3f)) {
        return;
    }
    sShadowDirection.set(-toward.x / length, -toward.y / length, -toward.z / length);
    sShadowCasters = true;
}

bool ClippingJudge::isLightSourcesWanted() const {
    return sLightSources;
}

bool ClippingJudge::isLightInView(const TVec3f& rCenter, f32 reach) const {
    // Its light reaches past the actor's own draw distance: against the whole view.
    return sLightSources && reach > 0.0f && mFrustum.mayIntersectBall3(rCenter, reach);
}

bool ClippingJudge::isShadowCasterInView(const TVec3f& rCenter, f32 radius, s32 index) const {
    if (!sShadowCasters) {
        return false;
    }
    const THex3f& frustum = index == 0 ? mFrustum : mClipFrustums[index];
    const f32 reach = index == 0 ? MR::getFarZ() : mClipDistances[index];
    TVec3f end;
    end.scaleAdd(reach, sShadowDirection, rCenter);
    // The swept ball (a capsule) lies outside the frustum when both ends lie outside one of its planes.
    for (s32 i = 0; i < 6; i++) {
        const JGeometry::TPartition3< f32 >& plane = frustum.mPlanes[i];
        if (plane.mNormal.dot(rCenter) - plane.mDot < -radius && plane.mNormal.dot(end) - plane.mDot < -radius) {
            return false;
        }
    }
    return true;
}
#endif

bool ClippingJudge::isJudgedToClipFrustum(const TVec3f& rVec, f32 a2) const {
    return !mFrustum.mayIntersectBall3(rVec, a2);
}

bool ClippingJudge::isJudgedToClipFrustum(const TVec3f& rVec, f32 a2, s32 index) const {
    return (!index) ? !mFrustum.mayIntersectBall3(rVec, a2) : !mClipFrustums[index].mayIntersectBall3(rVec, a2);
}

void ClippingJudge::calcViewingVolume(THex3f* pVolume, f32 farClip) {
    f32 nearClip = 500.0f;
    if (MR::isSubjectiveCameraOnForObjClipping()) {
        nearClip = 100.0f;
    }

    f32 aspect = MR::getAspect();
    f32 fovy = MR::getRenderFovy();
    TPos3f cameraMtx;
    cameraMtx.setPositionFromLookAt(MR::getCameraViewMtx());
    f32 height = nearClip * static_cast< f32 >(tan(fovy * (JGeometry::TUtil< f32 >::PI() / 360.0f)));
    f32 width = aspect * height;

    TVec3f front;
    cameraMtx.getZDir(front);
    TVec3f position;
    TVec3f bottomRight(width, -height, nearClip);
    TVec3f topRight(width, height, nearClip);
    TVec3f topLeft(-width, height, nearClip);
    TVec3f bottomLeft(-width, -height, nearClip);
    cameraMtx.getTrans(position);
    cameraMtx.mult(topRight, topRight);
    cameraMtx.mult(topLeft, topLeft);
    cameraMtx.mult(bottomLeft, bottomLeft);
    cameraMtx.mult(bottomRight, bottomRight);

    pVolume->mPlanes[3].set(position, topLeft, topRight);
    pVolume->mPlanes[1].set(position, topRight, bottomRight);
    pVolume->mPlanes[2].set(position, bottomRight, bottomLeft);
    pVolume->mPlanes[0].set(position, bottomLeft, topLeft);

    TVec3f back;
    back.negate(front);
    TVec3f planePosition;
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
