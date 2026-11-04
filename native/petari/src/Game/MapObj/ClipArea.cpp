#include "Game/MapObj/ClipArea.hpp"
#include "Game/Util/DrawUtil.hpp"
#include <JSystem/J3DGraphBase/J3DNativeState.hpp>
#include "Game/MapObj/ClipAreaHolder.hpp"
#include "Game/MapObj/ClipAreaShape.hpp"
#include "Game/Scene/SceneFunction.hpp"
#include "Game/Util.hpp"

namespace {
    static const Color8 sShapeColor(0, 0, 0, 4);
    static const Color8 sDebugShapeColor(255, 0, 0, 128);
};  // namespace

ClipArea::ClipArea(const char* pName) : LiveActor(pName), mShape() {
    mBaseMtx.identity();
    MR::createClipAreaHolder();
    MR::addClipArea(this);
}

void ClipArea::init(const JMapInfoIter& rIter) {
    initBaseMatrix(rIter);
    MR::connectToScene(this, MR::MovementType_None, MR::CalcAnimType_None, MR::DrawBufferType_None, MR::DrawType_ClipArea);
}

void ClipArea::initBaseMatrix(const JMapInfoIter& rIter) {
    MR::initDefaultPos(this, rIter);
    MR::makeMtxTR(mBaseMtx, this);
}

void ClipArea::draw() const {
    if (!MR::isActiveClipArea()) {
        return;
    }

    MR::setShadowVolumeColor(::sShapeColor);
    J3DPass::writeColor(false);
    J3DPass::writeAlpha(false);
    J3DPass::noBlend();
    J3DPass::stencil(NEBULA_STENCIL_ALWAYS, NEBULA_STENCIL_ADD);
    J3DPass::cull(NEBULA_CULL_FRONT);
    mShape->drawVolumeShape(mBaseMtx, mScale);
    J3DPass::stencil(NEBULA_STENCIL_ALWAYS, NEBULA_STENCIL_TAKE);
    J3DPass::cull(NEBULA_CULL_BACK);
    mShape->drawVolumeShape(mBaseMtx, mScale);
    J3DPass::stencil(NEBULA_STENCIL_OFF, NEBULA_STENCIL_KEEP);
    J3DPass::writeColor(true);
}

void ClipArea::setShape(ClipAreaShape* pShape) {
    mShape = pShape;
}

bool ClipArea::isInArea(const TVec3f& rParam1, f32 param2) const {
    return mShape->isInArea(rParam1, param2, mBaseMtx, mScale);
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
