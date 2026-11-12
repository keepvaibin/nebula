#include "Game/LiveActor/ShadowVolumeCylinder.hpp"
#include "Game/Render/TypedDraw.hpp"
#include "Game/LiveActor/LiveActor.hpp"
#include "Game/Util/CameraUtil.hpp"
#include "Game/Util/MtxUtil.hpp"
#include <NebulaGpu.h>
#include "Game/System/LumaCore.hpp"

namespace {
    const f32 sModelScale = 100.0f;
};  // namespace

ShadowVolumeCylinder::~ShadowVolumeCylinder() {
}

ShadowVolumeCylinder::ShadowVolumeCylinder() : ShadowVolumeModel("影描画[ボリューム円柱]") {
    lumacore::setClass(this, lumacore::kClassId< ShadowVolumeCylinder >);
    mRadius = 100.0f;
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
