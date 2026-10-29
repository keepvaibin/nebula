#include "Game/LiveActor/ShadowVolumeCylinder.hpp"
#include <JSystem/J3DGraphBase/J3DNativeShape.hpp>
#include "Game/LiveActor/LiveActor.hpp"
#include "Game/Util/CameraUtil.hpp"
#include "Game/Util/MtxUtil.hpp"
#include <revolution/gx.h>

namespace {
    const f32 sModelScale = 100.0f;
};  // namespace

ShadowVolumeCylinder::~ShadowVolumeCylinder() {
}

ShadowVolumeCylinder::ShadowVolumeCylinder() : ShadowVolumeModel("\211\145\225\140\211\346[\203\173\203\212\203\205\201\133\203\200\211\176\222\214]" /* 影描画[ボリューム円柱] */) {
    mRadius = 100.0f;
    initVolumeModel("ShadowVolumeCylinder");
}

void ShadowVolumeCylinder::setRadius(f32 radius) {
    mRadius = radius;
}

void ShadowVolumeCylinder::loadModelDrawMtx() const {
    ShadowController* controller = getController();
    TVec3f position;
    calcBaseDropPosition(&position);
    TVec3f direction;
    controller->getDropDir(&direction);
    TVec3f up(-direction);
    TPos3f mtx;
    MR::makeMtxUpNoSupportPos(&mtx, up, position);

    f32 radius = mRadius / ::sModelScale;
    if (controller->isFollowHostScale()) {
        radius *= controller->getHost()->mScale.x;
    }

    TVec3f scale(radius, calcBaseDropLength() / ::sModelScale, radius);
    MR::preScaleMtx(mtx, scale);
    PSMTXConcat(MR::getCameraViewMtx(), mtx, mtx);
    J3DNativeShape::sendPosition(mtx, 0);
}
