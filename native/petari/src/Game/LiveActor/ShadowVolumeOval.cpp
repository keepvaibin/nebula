#include "Game/LiveActor/ShadowVolumeOval.hpp"
#include "Game/Render/TypedDraw.hpp"
#include "Game/LiveActor/LiveActor.hpp"
#include "Game/Util/CameraUtil.hpp"
#include "Game/Util/MathUtil.hpp"
#include "Game/Util/MtxUtil.hpp"
#include <nebula_geometry.hpp>
#include "Game/Util/Tunable.hpp"

NEBULA_TUNABLE_NAMED(f32, cNum_loadModelDrawMtx_f1, "loadModelDrawMtx.f1", 100.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_loadModelDrawMtx_f2, "loadModelDrawMtx.f2", 0.01f);
NEBULA_TUNABLE_NAMED(f32, cNum_loadModelDrawMtx_f3, "loadModelDrawMtx.f3", 0.01f);
NEBULA_TUNABLE_NAMED(f32, cNum_loadModelDrawMtx_f4, "loadModelDrawMtx.f4", 0.01f);
NEBULA_TUNABLE_NAMED(f32, cNum_loadModelDrawMtx_f5, "loadModelDrawMtx.f5", 0.01f);
NEBULA_TUNABLE_NAMED(f32, cNum_loadModelDrawMtx_f6, "loadModelDrawMtx.f6", 0.01f);
NEBULA_TUNABLE_NAMED(f32, cNum_loadModelDrawMtx_f7, "loadModelDrawMtx.f7", 0.01f);

ShadowVolumeOval::ShadowVolumeOval() : ShadowVolumeModel("影描画[ボリューム楕球]"), mSize(100.0f, 100.0f, 200.0f) {
    initVolumeModel("ShadowVolumeSphere");
}

bool ShadowVolumeOval::isDraw() const {
    ShadowController* controller = getController();

    return controller->isProjected() && controller->isDraw();
}

// The oval stands where the drop meets the ground: that point jumping at a ledge starts a new object.
void ShadowVolumeOval::calcDrawAnchor(Vec3f* pOut) const {
    getController()->getProjectionPos(pOut);
}

void ShadowVolumeOval::loadModelDrawMtx(TypedDraw& rDraw) const {
    MtxPtr dropMtx;
    ShadowController* controller = getController();
    dropMtx = controller->mHostMtx;
    Vec3f direction;
    controller->getDropDir(&direction);

    Vec3f size(mSize / cNum_loadModelDrawMtx_f1);
    if (size.x <= cNum_loadModelDrawMtx_f2) {
        size.x = cNum_loadModelDrawMtx_f3;
    }

    if (size.y <= cNum_loadModelDrawMtx_f4) {
        size.y = cNum_loadModelDrawMtx_f5;
    }

    if (size.z <= cNum_loadModelDrawMtx_f6) {
        size.z = cNum_loadModelDrawMtx_f7;
    }

    if (controller->isFollowHostScale()) {
        size *= controller->getHost()->mScale;
    }

    Pos3f scaledMtx;
    scaledMtx.set(dropMtx);
    scaledMtx.setTrans(Vec3f(0.0f, 0.0f, 0.0f));
    MR::preScaleMtx(scaledMtx, size);

    Pos3f inverseMtx;
    inverseMtx.invert(scaledMtx);

    Vec3f localDirection;
    inverseMtx.mult(direction, localDirection);

    Pos3f mtx;
    mtx.identity();
    MR::makeMtxUpNoSupport(&mtx, localDirection);
    mtx.concat(scaledMtx, mtx);

    Vec3f position;
    controller->getProjectionPos(&position);
    mtx.setTrans(position);

    Vec3f side;
    Vec3f up;
    Vec3f front;
    mtx.getXDir(side);
    mtx.getYDir(up);
    mtx.getZDir(front);

    if (!MR::normalizeOrZero(&up)) {
        side.orthogonalize(up);
        front.orthogonalize(up);
    }

    mtx.setXDir(side);
    mtx.setZDir(front);
    rDraw.setWorld(mtx);
}

void ShadowVolumeOval::setSize(const Vec3f& rSize) {
    mSize = rSize;
}
