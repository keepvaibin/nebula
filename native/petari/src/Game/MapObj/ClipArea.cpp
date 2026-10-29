#include "Game/MapObj/ClipArea.hpp"
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

    J3DNativeState::setTevColor(GX_TEVREG0, ::sShapeColor);
    J3DNativeState::setColorUpdate(GX_FALSE);
    J3DNativeState::setDstAlpha(GX_FALSE, 0);
    J3DNativeState::setCullMode(GX_CULL_FRONT);
    J3DNativeState::setBlendMode(GX_BM_BLEND, GX_BL_ONE, GX_BL_ONE, GX_LO_COPY);
    mShape->drawVolumeShape(mBaseMtx, mScale);
    J3DNativeState::setCullMode(GX_CULL_BACK);
    J3DNativeState::setBlendMode(GX_BM_SUBTRACT, GX_BL_ZERO, GX_BL_ZERO, GX_LO_COPY);
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
