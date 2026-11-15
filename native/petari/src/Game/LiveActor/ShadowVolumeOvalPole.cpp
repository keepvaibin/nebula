#include "Game/LiveActor/ShadowVolumeOvalPole.hpp"
#include "Game/Render/TypedDraw.hpp"
#include "Game/LiveActor/LiveActor.hpp"
#include "Game/Util/CameraUtil.hpp"
#include "Game/Util/MathUtil.hpp"
#include "Game/Util/MtxUtil.hpp"
#include <NebulaGpu.h>
#include "Game/System/LumaCore.hpp"

namespace {
    const f32 sModelScale = 100.0f;
};  // namespace

ShadowVolumeOvalPole::~ShadowVolumeOvalPole() {
}

ShadowVolumeOvalPole::ShadowVolumeOvalPole() : ShadowVolumeModel("影描画[ボリューム楕円柱]") {
    lumacore::setClass(this, lumacore::kClassId< ShadowVolumeOvalPole >);
    mSize.x = 100.0f;
    mSize.y = 100.0f;
    mSize.z = 200.0f;
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
    if (size.x <= 0.01f) {
        size.x = 0.01f;
    }

    if (size.y <= 0.01f) {
        size.y = 0.01f;
    }

    if (size.z <= 0.01f) {
        size.z = 0.01f;
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
