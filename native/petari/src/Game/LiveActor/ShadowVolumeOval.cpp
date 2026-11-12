#include "Game/LiveActor/ShadowVolumeOval.hpp"
#include "Game/Render/TypedDraw.hpp"
#include "Game/LiveActor/LiveActor.hpp"
#include "Game/Util/CameraUtil.hpp"
#include "Game/Util/MathUtil.hpp"
#include "Game/Util/MtxUtil.hpp"
#include <nebula_geometry.hpp>
#include "Game/System/LumaCore.hpp"

ShadowVolumeOval::ShadowVolumeOval() : ShadowVolumeModel("影描画[ボリューム楕球]"), mSize(100.0f, 100.0f, 200.0f) {
    lumacore::setClass(this, lumacore::kClassId< ShadowVolumeOval >);
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

    Vec3f size(mSize / 100.0f);
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
