#include "Game/LiveActor/VolumeModelDrawer.hpp"
#include <JSystem/J3DGraphBase/J3DNativeState.hpp>
#include <JSystem/J3DGraphBase/J3DNativeShape.hpp>
#include "Game/Scene/SceneFunction.hpp"
#include "Game/Scene/SceneObjHolder.hpp"
#include "Game/System/ResourceHolder.hpp"
#include "Game/Util/CameraUtil.hpp"
#include "Game/Util/DrawUtil.hpp"
#include "Game/Util/MtxUtil.hpp"
#include "Game/Util/ObjUtil.hpp"
#include <cstdio>

VolumeModelDrawInit::VolumeModelDrawInit() : NameObj("\203\173\203\212\203\205\201\133\203\200\203\202\203\146\203\213\225\140\211\346\217\211\212\372\211\273" /* ボリュームモデル描画初期化 */) {
    MR::registerPreDrawFunction(MR::Functor(&MR::setupShadowVolumeDraw), MR::DrawType_VolumeModel);
}

VolumeModelDrawer::VolumeModelDrawer(const char* pName, const char* pFileName, MtxPtr mtx)
    : NameObj(pName), mMtx(), mModelData(), mColor(0xFF, 0xA9, 0, 0xFF) {
    MR::createSceneObj(SceneObj_VolumeModelDrawInit);
    mMtx = mtx;
    char buf[0x100];
    snprintf(buf, sizeof(buf), "%s.arc", pFileName);
    mModelData = (J3DModelData*)MR::createAndAddResourceHolder(buf)->mModelResTable->getRes(pFileName);
}

void VolumeModelDrawer::draw() const {
    MR::loadProjectionMtx();
    loadModelDrawMtx();
    J3DNativeState::setCurrentMtx(0);
    J3DNativeState::setColorUpdate(0);
    J3DNativeState::setAlphaUpdate(1);
    J3DNativeState::setDstAlpha(1, 0);
    J3DNativeState::setCullMode(GX_CULL_NONE);
    GXColor color_1 = mColor;
    J3DNativeState::setTevColor(GX_TEVREG0, color_1);
    J3DNativeState::setZMode(0, GX_ALWAYS, 0);
    J3DNativeState::setBlendMode(GX_BM_BLEND, GX_BL_ONE, GX_BL_ZERO, GX_LO_COPY);
    MR::drawSimpleModel(mModelData);
    J3DNativeState::setDstAlpha(0, 0);
    J3DNativeState::setZMode(1, GX_GEQUAL, 0);
    J3DNativeState::setCullMode(GX_CULL_FRONT);
    J3DNativeState::setBlendMode(GX_BM_BLEND, GX_BL_SRCALPHA, GX_BL_ZERO, GX_LO_COPY);
    MR::drawSimpleModel(mModelData);
    J3DNativeState::setCullMode(GX_CULL_BACK);
    J3DNativeState::setBlendMode(GX_BM_SUBTRACT, GX_BL_ZERO, GX_BL_ZERO, GX_LO_COPY);
    MR::drawSimpleModel(mModelData);
    GXColor color_2 = mColor;
    J3DNativeState::setTevColor(GX_TEVREG0, color_2);
    J3DNativeState::setCullMode(GX_CULL_NONE);
    J3DNativeState::setBlendMode(GX_BM_BLEND, GX_BL_DSTALPHA, GX_BL_ONE, GX_LO_COPY);
    J3DNativeState::setColorUpdate(1);
    J3DNativeState::setDstAlpha(1, 0);
    J3DNativeState::setZMode(0, GX_ALWAYS, 0);
    MR::drawSimpleModel(mModelData);
}

void VolumeModelDrawer::loadModelDrawMtx() const {
    Mtx drawMtx;
    MR::multMtx(drawMtx, mMtx, MR::getCameraViewMtx().mMtx);
    J3DNativeShape::sendPosition(drawMtx, 0);
}
