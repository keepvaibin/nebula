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
    J3DNativeState::setColorUpdate(GPU_FALSE);
    J3DNativeState::setDstAlpha(GPU_FALSE, 0);
    J3DNativeState::setCullMode(GPU_CULL_FRONT);
    J3DNativeState::setBlendMode(GPU_BM_BLEND, GPU_BL_ONE, GPU_BL_ONE, GPU_LO_COPY);
    mShape->drawVolumeShape(mBaseMtx, mScale);
    J3DNativeState::setCullMode(GPU_CULL_BACK);
    J3DNativeState::setBlendMode(GPU_BM_SUBTRACT, GPU_BL_ZERO, GPU_BL_ZERO, GPU_LO_COPY);
    mShape->drawVolumeShape(mBaseMtx, mScale);
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
