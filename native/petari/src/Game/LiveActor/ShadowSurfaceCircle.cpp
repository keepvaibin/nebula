#include "Game/Util/CameraUtil.hpp"
#include "Game/LiveActor/ShadowSurfaceCircle.hpp"
#include "Game/LiveActor/LiveActor.hpp"
#include "Game/LiveActor/ShadowController.hpp"
#include "Game/Util/DirectDraw.hpp"
#include "Game/Util/MathUtil.hpp"
#include <nebula_geometry.hpp>
#include "Game/Util/Tunable.hpp"
#include "Game/System/LumaCore.hpp"

NEBULA_TUNABLE_NAMED(f32, cNum_draw_f1, "draw.f1", 6.2831855f);

namespace {
    // Rim segments of the fan.
    NEBULA_TUNABLE(u32, cSegments, 20);
};  // namespace

ShadowSurfaceCircle::~ShadowSurfaceCircle() {
}

ShadowSurfaceCircle::ShadowSurfaceCircle() : ShadowSurfaceDrawer("影描画[水面円]"), mRadius(100.0f) {
    lumacore::setClass(this, lumacore::kClassId< ShadowSurfaceCircle >);
}

void ShadowSurfaceCircle::setRadius(f32 radius) {
    mRadius = radius;
}

// The circle is a unit fan in its own space under one matrix (x the axis the fan starts on, y a quarter turn about the drop
// direction, both of the radius), so a step's matrices blend by their turn and scale: a circle laid out against another
// start axis (the axis follows the normal's x being zero or not) never shrinks through its own plane while it blends.
void ShadowSurfaceCircle::draw() const {
    ShadowController* controller = getController();
    if (!controller->isProjected() || !controller->isDraw()) {
        return;
    }

    beginDrawObject();
    f32 radius = mRadius;
    if (controller->isFollowHostScale()) {
        radius *= controller->getHost()->mScale.x;
    }

    Vec3f pos, normal;
    controller->getProjectionPos(&pos);
    controller->getProjectionNormal(&normal);

    // The fan turns about the drop direction (the normal reversed) from the axis the first rim point is on.
    Vec3f down(-normal);
    Vec3f axis;
    if (down.x != 0.0f) {
        axis.set< f32 >(down.y, -down.x, 0.0f);
    } else {
        axis.set< f32 >(0.0f, -down.z, down.y);
    }

    MR::normalizeOrZero(&axis);
    if (MR::normalizeOrZero(&down)) {
        return;
    }

    Vec3f quarter;
    quarter.cross(down, axis);
    axis.scale(radius);
    quarter.scale(radius);
    down.scale(radius);
    Pos3f circleMtx;
    circleMtx.setXYZDir(axis, quarter, down);
    circleMtx.setTrans(pos + normal);

    const f32 step = cNum_draw_f1 / static_cast< f32 >(cSegments);
    TypedDraw site(ShadowSurfaceDrawInit::material(), circleMtx.toMtxPtr(), MR::getCameraViewMtx());
    VertexPC* vtx = site.vertices< VertexPC >(NEBULA_PRIMITIVE_TRIANGLE_FAN, cSegments + 2);
    *vtx++ = {0.0f, 0.0f, 0.0f, Rgba8(0x80)};
    for (u32 i = 0; i <= cSegments; i++) {
        const f32 angle = step * static_cast< f32 >(i);
        *vtx++ = {MR::cos(angle), MR::sin(angle), 0.0f, Rgba8(0x80)};
    }
}
