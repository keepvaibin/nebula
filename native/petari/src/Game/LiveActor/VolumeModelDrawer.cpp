#include "Game/LiveActor/VolumeModelDrawer.hpp"
#include "Game/Util/NativeShader.hpp"
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
}

VolumeModelDrawer::VolumeModelDrawer(const char* pName, const char* pFileName, MtxPtr mtx)
    : NameObj(pName), mMtx(), mModelData(), mColor(0xFF, 0xA9, 0, 0xFF) {
    MR::createSceneObj(SceneObj_VolumeModelDrawInit);
    mMtx = mtx;
    char buf[0x100];
    nebula_snprintf(buf, sizeof(buf), "%s.arc", pFileName);
    mModelData = (J3DModelData*)MR::createAndAddResourceHolder(buf)->mModelResTable->getRes(pFileName);
}

// A glow on whatever stands inside the model's shape: its back faces behind a surface mark the pixel, its front faces
// behind one unmark it, so what stays marked has the surface between them. The colour goes on at its alpha squared, each
// pixel once.
void VolumeModelDrawer::draw() const {
    MR::loadProjectionMtx();
    loadModelDrawMtx();

    const u32 strength = mColor.a;
    NebulaColor8 color = mColor;
    color.a = static_cast< u8 >(strength * strength / 255);
    NebulaNativeMaterial material;
    MR::initShadowVolumeMaterial(material, color);

    material.cull = NEBULA_CULL_NONE;
    NativeShader::setDepth(material, false, NEBULA_COMPARE_ALWAYS, false);
    NativeShader::setStencil(material, NEBULA_STENCIL_ALWAYS, NEBULA_STENCIL_ZERO);
    NativeShader::send(material);
    MR::drawSimpleModel(mModelData);

    NativeShader::setDepth(material, true, NEBULA_COMPARE_GEQUAL, false);
    material.cull = NEBULA_CULL_FRONT;
    NativeShader::setStencil(material, NEBULA_STENCIL_ALWAYS, NEBULA_STENCIL_ADD);
    NativeShader::send(material);
    MR::drawSimpleModel(mModelData);
    material.cull = NEBULA_CULL_BACK;
    NativeShader::setStencil(material, NEBULA_STENCIL_ALWAYS, NEBULA_STENCIL_ZERO);
    NativeShader::send(material);
    MR::drawSimpleModel(mModelData);

    material.cull = NEBULA_CULL_NONE;
    NativeShader::setDepth(material, false, NEBULA_COMPARE_ALWAYS, false);
    material.color_write = 1;
    NativeShader::setBlend(material, NEBULA_FACTOR_SRC_ALPHA, NEBULA_FACTOR_ONE);
    NativeShader::setStencil(material, NEBULA_STENCIL_IF_SET, NEBULA_STENCIL_ZERO);
    NativeShader::send(material);
    MR::drawSimpleModel(mModelData);
}

void VolumeModelDrawer::loadModelDrawMtx() const {
    Mtx drawMtx;
    MR::multMtx(drawMtx, mMtx, MR::getCameraViewMtx().mMtx);
    J3DNativeShape::setPosition(0, drawMtx);
}
