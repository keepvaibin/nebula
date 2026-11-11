#include "Game/LiveActor/ShadowVolumeSphere.hpp"
#include "Game/Render/TypedDraw.hpp"
#include "Game/LiveActor/LiveActor.hpp"
#include "Game/Util/CameraUtil.hpp"
#include <NebulaGpu.h>
#include "Game/Util/Tunable.hpp"
#include "Game/System/LumaCore.hpp"

namespace {
    NEBULA_TUNABLE(f32, sModelScale, 100.0f);
};  // namespace

ShadowVolumeSphere::~ShadowVolumeSphere() {
}

ShadowVolumeSphere::ShadowVolumeSphere() : ShadowVolumeModel("影描画[ボリューム球]"), mRadius(100.0f) {
    lumacore::setClass(this, lumacore::kClassId< ShadowVolumeSphere >);
    initVolumeModel("ShadowVolumeSphere");
}

void ShadowVolumeSphere::setRadius(f32 radius) {
    mRadius = radius;
}

bool ShadowVolumeSphere::isDraw() const {
    ShadowController* controller = getController();

    return controller->isProjected() && controller->isDraw();
}

// The sphere stands where the drop meets the ground: that point jumping at a ledge starts a new object.
void ShadowVolumeSphere::calcDrawAnchor(Vec3f* pOut) const {
    getController()->getProjectionPos(pOut);
}

void ShadowVolumeSphere::loadModelDrawMtx(TypedDraw& rDraw) const {
    ShadowController* controller = getController();
    f32 radius = mRadius / ::sModelScale;
    if (controller->isFollowHostScale()) {
        radius *= controller->getHost()->mScale.x;
    }

    Pos3f mtx;
    mtx.identity();
    mtx.mMtx[0][0] = radius;
    mtx.mMtx[0][1] = 0.0f;
    mtx.mMtx[0][2] = 0.0f;
    mtx.mMtx[1][0] = 0.0f;
    mtx.mMtx[1][1] = radius;
    mtx.mMtx[1][2] = 0.0f;
    mtx.mMtx[2][0] = 0.0f;
    mtx.mMtx[2][1] = 0.0f;
    mtx.mMtx[2][2] = radius;
    Vec3f position;
    controller->getProjectionPos(&position);
    mtx.setTrans(position);

    rDraw.setWorld(mtx);
}
