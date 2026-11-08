#include "Game/Util/CameraUtil.hpp"
#include "Game/LiveActor/ShadowSurfaceCircle.hpp"
#include "Game/LiveActor/LiveActor.hpp"
#include "Game/LiveActor/ShadowController.hpp"
#include "Game/Util/DirectDraw.hpp"
#include <nebula_geometry.hpp>

ShadowSurfaceCircle::~ShadowSurfaceCircle() {
}

ShadowSurfaceCircle::ShadowSurfaceCircle() : ShadowSurfaceDrawer("影描画[水面円]"), mRadius(100.0f) {
}

void ShadowSurfaceCircle::setRadius(f32 radius) {
    mRadius = radius;
}

void ShadowSurfaceCircle::draw() const {
    ShadowController* controller = getController();
    if (!controller->isProjected() || !controller->isDraw()) {
        return;
    }

    f32 radius = mRadius;
    if (controller->isFollowHostScale()) {
        radius *= controller->getHost()->mScale.x;
    }

    Vec3f pos, normal;
    controller->getProjectionPos(&pos);
    controller->getProjectionNormal(&normal);
    TypedDraw site(ShadowSurfaceDrawInit::material(), nullptr, MR::getCameraViewMtx());
    TDDraw::drawFillCircle(site, pos + normal, -normal, radius, 128, 20);
}
