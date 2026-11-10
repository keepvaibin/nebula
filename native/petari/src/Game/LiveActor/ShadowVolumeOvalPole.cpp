#include "Game/LiveActor/ShadowVolumeOvalPole.hpp"
#include "Game/Render/TypedDraw.hpp"
#include "Game/LiveActor/LiveActor.hpp"
#include "Game/Util/CameraUtil.hpp"
#include "Game/Util/MathUtil.hpp"
#include "Game/Util/MtxUtil.hpp"
#include <NebulaGpu.h>
#include "Game/Util/Tunable.hpp"

NEBULA_TUNABLE_NAMED(f32, cNum_ShadowVolumeOvalPole_f1, "ShadowVolumeOvalPole.f1", 100.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_ShadowVolumeOvalPole_f2, "ShadowVolumeOvalPole.f2", 100.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_ShadowVolumeOvalPole_f3, "ShadowVolumeOvalPole.f3", 200.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_loadModelDrawMtx_f1, "loadModelDrawMtx.f1", 0.01f);
NEBULA_TUNABLE_NAMED(f32, cNum_loadModelDrawMtx_f2, "loadModelDrawMtx.f2", 0.01f);
NEBULA_TUNABLE_NAMED(f32, cNum_loadModelDrawMtx_f3, "loadModelDrawMtx.f3", 0.01f);
NEBULA_TUNABLE_NAMED(f32, cNum_loadModelDrawMtx_f4, "loadModelDrawMtx.f4", 0.01f);
NEBULA_TUNABLE_NAMED(f32, cNum_loadModelDrawMtx_f5, "loadModelDrawMtx.f5", 0.01f);
NEBULA_TUNABLE_NAMED(f32, cNum_loadModelDrawMtx_f6, "loadModelDrawMtx.f6", 0.01f);

namespace {
    NEBULA_TUNABLE(f32, sModelScale, 100.0f);
};  // namespace

ShadowVolumeOvalPole::~ShadowVolumeOvalPole() {
}

ShadowVolumeOvalPole::ShadowVolumeOvalPole() : ShadowVolumeModel("影描画[ボリューム楕円柱]") {
    mSize.x = cNum_ShadowVolumeOvalPole_f1;
    mSize.y = cNum_ShadowVolumeOvalPole_f2;
    mSize.z = cNum_ShadowVolumeOvalPole_f3;
    initVolumeModel("ShadowVolumeCylinder");
}

void ShadowVolumeOvalPole::loadModelDrawMtx(TypedDraw& rDraw) const {
    ShadowController* controller = getController();
    MtxPtr dropMtx = controller->mHostMtx;
    Pos3f scaledMtx;
    Pos3f inverseMtx;
    Pos3f mtx;
    Vec3f position;
    calcBaseDropPosition(&position);
    Vec3f direction;
    controller->getDropDir(&direction);
    direction.negate();
    f32 modelScale = ::sModelScale;
    Vec3f size(mSize / modelScale);
    if (size.x <= cNum_loadModelDrawMtx_f1) {
        size.x = cNum_loadModelDrawMtx_f2;
    }

    if (size.y <= cNum_loadModelDrawMtx_f3) {
        size.y = cNum_loadModelDrawMtx_f4;
    }

    if (size.z <= cNum_loadModelDrawMtx_f5) {
        size.z = cNum_loadModelDrawMtx_f6;
    }

    if (controller->isFollowHostScale()) {
        size *= controller->getHost()->mScale;
    }

    scaledMtx.set(dropMtx);
    scaledMtx.setTrans(Vec3f(0.0f, 0.0f, 0.0f));
    MR::preScaleMtx(scaledMtx, size);
    inverseMtx.invert(scaledMtx);
    Vec3f localDirection;
    inverseMtx.mult(direction, localDirection);
    mtx.identity();
    MR::makeMtxUpNoSupport(&mtx, localDirection);
    mtx.concat(scaledMtx, mtx);
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

    up.scale(calcBaseDropLength() / ::sModelScale);
    mtx.setXDir(side);
    mtx.setYDir(up);
    mtx.setZDir(front);
    rDraw.setWorld(mtx);
}

void ShadowVolumeOvalPole::setSize(const Vec3f& rSize) {
    mSize = rSize;
}
