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
    nebula_snprintf(buf, sizeof(buf), "%s.arc", pFileName);
    mModelData = (J3DModelData*)MR::createAndAddResourceHolder(buf)->mModelResTable->getRes(pFileName);
}

void VolumeModelDrawer::draw() const {
    MR::loadProjectionMtx();
    loadModelDrawMtx();
    J3DPass::positionMatrix(0);
    J3DPass::writeColor(false);
    J3DPass::writeAlpha(true);
    J3DPass::constantAlpha(true, 0);
    J3DPass::cull(NEBULA_CULL_NONE);
    NebulaColor8 color_1 = mColor;
    MR::setShadowVolumeColor(color_1);
    J3DPass::depth(false, NEBULA_COMPARE_ALWAYS, false);
    J3DPass::blend(NEBULA_FACTOR_ONE, NEBULA_FACTOR_ZERO);
    MR::drawSimpleModel(mModelData);
    J3DPass::constantAlpha(false, 0);
    J3DPass::depth(true, NEBULA_COMPARE_GEQUAL, false);
    J3DPass::cull(NEBULA_CULL_FRONT);
    J3DPass::blend(NEBULA_FACTOR_SRC_ALPHA, NEBULA_FACTOR_ZERO);
    MR::drawSimpleModel(mModelData);
    J3DPass::cull(NEBULA_CULL_BACK);
    J3DPass::subtractBlend();
    MR::drawSimpleModel(mModelData);
    NebulaColor8 color_2 = mColor;
    MR::setShadowVolumeColor(color_2);
    J3DPass::cull(NEBULA_CULL_NONE);
    J3DPass::blend(NEBULA_FACTOR_DST_ALPHA, NEBULA_FACTOR_ONE);
    J3DPass::writeColor(true);
    J3DPass::constantAlpha(true, 0);
    J3DPass::depth(false, NEBULA_COMPARE_ALWAYS, false);
    MR::drawSimpleModel(mModelData);
}

void VolumeModelDrawer::loadModelDrawMtx() const {
    Mtx drawMtx;
    MR::multMtx(drawMtx, mMtx, MR::getCameraViewMtx().mMtx);
    J3DNativeShape::setPosition(0, drawMtx);
}
