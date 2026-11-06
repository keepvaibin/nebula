#include "Game/LiveActor/ShadowVolumeOval.hpp"
#include "Game/Render/TypedDraw.hpp"
#include "Game/LiveActor/LiveActor.hpp"
#include "Game/Util/CameraUtil.hpp"
#include "Game/Util/MathUtil.hpp"
#include "Game/Util/MtxUtil.hpp"
#include <nebula_geometry.hpp>

ShadowVolumeOval::ShadowVolumeOval() : ShadowVolumeModel("\211\145\225\140\211\346[\203\173\203\212\203\205\201\133\203\200\221\310\213\205]" /* 影描画[ボリューム楕球] */), mSize(100.0f, 100.0f, 200.0f) {
    initVolumeModel("ShadowVolumeSphere");
}

bool ShadowVolumeOval::isDraw() const {
    ShadowController* controller = getController();

    return controller->isProjected() && controller->isDraw();
}

void ShadowVolumeOval::loadModelDrawMtx(TypedDraw& rDraw) const {
    MtxPtr dropMtx;
    ShadowController* controller = getController();
    dropMtx = controller->_18;
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
