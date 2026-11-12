#include "Game/Util/CameraUtil.hpp"
#include "Game/LiveActor/ShadowSurfaceOval.hpp"
#include "Game/LiveActor/LiveActor.hpp"
#include "Game/LiveActor/ShadowController.hpp"
#include "Game/LiveActor/ShadowSurfaceDrawer.hpp"
#include "Game/Util/Color.hpp"
#include "Game/Util/DirectDraw.hpp"
#include "Game/Util/DrawUtil.hpp"
#include "Game/Util/ModelUtil.hpp"
#include "Game/Util/MtxUtil.hpp"
#include <nebula_geometry.hpp>
#include "Game/System/LumaCore.hpp"

ShadowSurfaceOval::~ShadowSurfaceOval() {
}

ShadowSurfaceOval::ShadowSurfaceOval() : ShadowSurfaceDrawer("影描画[水面楕円影]"), mModelData(), mSize(100.0f, 100.0f, 200.0f), mColor(0, 0, 0, 64) {
    lumacore::setClass(this, lumacore::kClassId< ShadowSurfaceOval >);
    mModelData = MR::getModelData("ShadowVolumeSphere");
}

void ShadowSurfaceOval::setColor(Color8 color) {
    mColor.set(color);
}

void ShadowSurfaceOval::setAlpha(u8 alpha) {
    mColor.a = alpha;
}

void ShadowSurfaceOval::setSize(const Vec3f& pSize) {
    mSize = pSize;
}

void ShadowSurfaceOval::draw() const {
    MtxPtr baseMtx;
    ShadowController* controller = getController();
    baseMtx = controller->mHostMtx;

    if (!controller->isProjected() || !controller->isDraw()) {
        return;
    }

    beginDrawObject();
    Vec3f size(mSize / 100.0f);
    if (controller->isFollowHostScale()) {
        size *= controller->getHost()->mScale;
    }

    Vec3f pos, normal;
    controller->getProjectionPos(&pos);
    controller->getProjectionNormal(&normal);
    ShadowSurfaceDrawInit::sendColor(mColor);
    Pos3f mtx;
    mtx.setInline(baseMtx);
    MR::preScaleMtx(mtx, size);
    MR::flattenMtx(mtx, normal);
    mtx.setTrans(pos);
    TypedDraw site(ShadowSurfaceDrawInit::material(), mtx.toMtxPtr(), MR::getCameraViewMtx());
    site.drawModelFile(mModelData);
    MR::preScaleMtx(mtx, 0.9f);
    site.setWorld(mtx.toMtxPtr());
    site.drawModelFile(mModelData);
    MR::preScaleMtx(mtx.toMtxPtr(), 0.9f);
    site.setWorld(mtx.toMtxPtr());
    site.drawModelFile(mModelData);
    ShadowSurfaceDrawInit::sendColor(Color8(128));
}
