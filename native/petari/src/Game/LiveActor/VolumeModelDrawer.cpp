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

// A glow the model's shape adds: its full strength where the scene is behind all of it, its strength squared where a
// surface stands inside it, nothing where a surface is in front. Each pixel once, whatever the shape's layers.
void VolumeModelDrawer::draw() const {
    MR::loadProjectionMtx();
    loadModelDrawMtx();
    J3DPass::positionMatrix(0);
    J3DPass::writeAlpha(false);

    const u32 strength = mColor.a;
    const u8 squared = static_cast< u8 >(strength * strength / 255);
    NebulaColor8 color = mColor;
    color.a = squared;

    // Where a surface is in front, marked; what is not marked gets the squared part.
    MR::setShadowVolumeColor(color);
    J3DPass::writeColor(false);
    J3DPass::noBlend();
    J3DPass::depth(true, NEBULA_COMPARE_GEQUAL, false);
    J3DPass::cull(NEBULA_CULL_BACK);
    J3DPass::stencil(NEBULA_STENCIL_ALWAYS, NEBULA_STENCIL_ADD);
    MR::drawSimpleModel(mModelData);
    fill();

    // Where a surface stands inside too, marked; what is left gets the rest.
    color.a = static_cast< u8 >(strength - squared);
    MR::setShadowVolumeColor(color);
    J3DPass::writeColor(false);
    J3DPass::noBlend();
    J3DPass::depth(true, NEBULA_COMPARE_GEQUAL, false);
    J3DPass::cull(NEBULA_CULL_NONE);
    J3DPass::stencil(NEBULA_STENCIL_ALWAYS, NEBULA_STENCIL_ADD);
    MR::drawSimpleModel(mModelData);
    fill();

    J3DPass::stencil(NEBULA_STENCIL_OFF, NEBULA_STENCIL_KEEP);
    J3DPass::cull(NEBULA_CULL_NONE);
    J3DPass::noBlend();
}

// The colour added over the shape where the stencil is clear, each pixel once, and the stencil cleared.
void VolumeModelDrawer::fill() const {
    J3DPass::depth(false, NEBULA_COMPARE_ALWAYS, false);
    J3DPass::cull(NEBULA_CULL_NONE);
    J3DPass::writeColor(true);
    J3DPass::blend(NEBULA_FACTOR_SRC_ALPHA, NEBULA_FACTOR_ONE);
    J3DPass::stencil(NEBULA_STENCIL_IF_ZERO, NEBULA_STENCIL_ADD);
    MR::drawSimpleModel(mModelData);
    J3DPass::writeColor(false);
    J3DPass::noBlend();
    J3DPass::stencil(NEBULA_STENCIL_ALWAYS, NEBULA_STENCIL_ZERO);
    MR::drawSimpleModel(mModelData);
}

void VolumeModelDrawer::loadModelDrawMtx() const {
    Mtx drawMtx;
    MR::multMtx(drawMtx, mMtx, MR::getCameraViewMtx().mMtx);
    J3DNativeShape::setPosition(0, drawMtx);
}
