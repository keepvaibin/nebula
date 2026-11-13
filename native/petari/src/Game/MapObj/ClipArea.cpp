#include "Game/MapObj/ClipArea.hpp"
#include "Game/Render/TypedDraw.hpp"
#include "Game/Util/DrawUtil.hpp"
#include "Game/Render/FrameList.hpp"
#include "Game/MapObj/ClipAreaHolder.hpp"
#include "Game/MapObj/ClipAreaShape.hpp"
#include "Game/Scene/SceneFunction.hpp"
#include "Game/Util.hpp"
#include "Game/System/LumaCore.hpp"

namespace {
    static const Color8 sShapeColor(0, 0, 0, 4);
};  // namespace

ClipArea::ClipArea(const char* pName) : LiveActor(pName), mShape() {
    lumacore::setClass(this, lumacore::kClassId< ClipArea >);
    mBaseMtx.identity();
    MR::createClipAreaHolder();
    MR::addClipArea(this);
}

void ClipArea::init(const TableIter& rIter) {
    initBaseMatrix(rIter);
    MR::connectToScene(this, MR::MovementType_None, MR::CalcAnimType_None, MR::DrawBufferType_None, MR::DrawType_ClipArea);
}

void ClipArea::initBaseMatrix(const TableIter& rIter) {
    MR::initDefaultPos(this, rIter);
    MR::makeMtxTR(mBaseMtx, this);
}

void ClipArea::draw() const {
    if (!MR::isActiveClipArea()) {
        return;
    }

    SiteMaterial material;
    MR::initShadowVolumeMaterial(material, ::sShapeColor);
    material.setStencil(NEBULA_STENCIL_ALWAYS, NEBULA_STENCIL_ADD);
    material.mState.cull = NEBULA_CULL_FRONT;
    TypedDraw site(material, nullptr, MR::getCameraViewMtx());
    mShape->drawVolumeShape(site, mBaseMtx, mScale);
    material.setStencil(NEBULA_STENCIL_ALWAYS, NEBULA_STENCIL_TAKE);
    material.mState.cull = NEBULA_CULL_BACK;
    site.setMaterial(material);
    mShape->drawVolumeShape(site, mBaseMtx, mScale);
    // The volume, for the ray tracer: the clipped map parts stand where FallOutFieldDraw's rule shows them alone.
    NebulaFrameClipVolume volume;
    if (mShape->frameVolume(&volume, mBaseMtx, mScale)) {
        Frame::clipVolume(volume);
    }
}

void ClipArea::setShape(ClipAreaShape* pShape) {
    mShape = pShape;
}

bool ClipArea::isInArea(const Vec3f& rPos, f32 radius) const {
    return mShape->isInArea(rPos, radius, mBaseMtx, mScale);
}

namespace MR {
    void setClipSphereArea(ClipArea* pClipArea) {
        pClipArea->setShape(new ClipAreaShapeSphere());
    }

    void setClipCenterBoxArea(ClipArea* pClipArea) {
        pClipArea->setShape(new ClipAreaShapeBox(0));
    }

    void setClipBottomBoxArea(ClipArea* pClipArea) {
        pClipArea->setShape(new ClipAreaShapeBox(1));
    }

    void setClipTopConeArea(ClipArea* pClipArea) {
        pClipArea->setShape(new ClipAreaShapeCone(0));
    }

    void setClipBottomConeArea(ClipArea* pClipArea) {
        pClipArea->setShape(new ClipAreaShapeCone(1));
    }
};  // namespace MR
