#include "Game/LiveActor/ShadowVolumeCylinder.hpp"
#include "Game/Render/TypedDraw.hpp"
#include "Game/LiveActor/LiveActor.hpp"
#include "Game/Util/CameraUtil.hpp"
#include "Game/Util/MtxUtil.hpp"
#include <NebulaGpu.h>
#include "Game/Util/Tunable.hpp"

NEBULA_TUNABLE_NAMED(f32, cNum_ShadowVolumeCylinder_f1, "ShadowVolumeCylinder.f1", 100.0f);

namespace {
    NEBULA_TUNABLE(f32, sModelScale, 100.0f);
};  // namespace

ShadowVolumeCylinder::~ShadowVolumeCylinder() {
}

ShadowVolumeCylinder::ShadowVolumeCylinder() : ShadowVolumeModel("影描画[ボリューム円柱]") {
    mRadius = cNum_ShadowVolumeCylinder_f1;
    initVolumeModel("ShadowVolumeCylinder");
}

void ShadowVolumeCylinder::setRadius(f32 radius) {
    mRadius = radius;
}

void ShadowVolumeCylinder::loadModelDrawMtx(TypedDraw& rDraw) const {
    ShadowController* controller = getController();
    Vec3f position;
    calcBaseDropPosition(&position);
    Vec3f direction;
    controller->getDropDir(&direction);
    Vec3f up(-direction);
    Pos3f mtx;
    MR::makeMtxUpNoSupportPos(&mtx, up, position);

    f32 radius = mRadius / ::sModelScale;
    if (controller->isFollowHostScale()) {
        radius *= controller->getHost()->mScale.x;
    }

    Vec3f scale(radius, calcBaseDropLength() / ::sModelScale, radius);
    MR::preScaleMtx(mtx, scale);
    rDraw.setWorld(mtx);
}
