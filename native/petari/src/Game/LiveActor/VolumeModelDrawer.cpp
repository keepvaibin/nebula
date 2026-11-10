#include "Game/LiveActor/VolumeModelDrawer.hpp"
#include "Game/Render/TypedDraw.hpp"
#include "Game/Render/FrameList.hpp"
#include "Game/Scene/SceneFunction.hpp"
#include "Game/Scene/SceneObjHolder.hpp"
#include "Game/System/ResourceHolder.hpp"
#include "Game/Util/CameraUtil.hpp"
#include "Game/Util/DrawUtil.hpp"
#include "Game/Util/MtxUtil.hpp"
#include "Game/Util/ObjUtil.hpp"
#include "Game/Util/StringUtil.hpp"
#include "Game/System/LumaCore.hpp"

VolumeModelDrawInit::VolumeModelDrawInit() : NameObj("ボリュームモデル描画初期化") {
    lumacore::setClass(this, lumacore::kClassId< VolumeModelDrawInit >);
}

VolumeModelDrawer::VolumeModelDrawer(const char* pName, const char* pFileName, MtxPtr mtx)
    : NameObj(pName), mMtx(), mModelData(), mColor(0xFF, 0xA9, 0, 0xFF) {
    lumacore::setClass(this, lumacore::kClassId< VolumeModelDrawer >);
    MR::createSceneObj(SceneObj_VolumeModelDrawInit);
    mMtx = mtx;
    mModelData = (ModelData*)MR::createAndAddResourceHolder(pFileName)->mModelResTable->getRes(pFileName);
}

// A glow on whatever stands inside the model's shape: its back faces behind a surface mark the pixel, its front faces
// behind one unmark it, so what stays marked has the surface between them. The colour goes on at its alpha squared, each
// pixel once.
void VolumeModelDrawer::draw() const {
    MR::loadProjectionMtx();

    const u32 strength = mColor.a;
    NebulaColor8 color = mColor;
    color.a = static_cast< u8 >(strength * strength / 255);
    SiteMaterial material;
    MR::initShadowVolumeMaterial(material, color);

    material.mState.cull = NEBULA_CULL_NONE;
    material.setDepth(false, NEBULA_COMPARE_ALWAYS, false);
    material.setStencil(NEBULA_STENCIL_ALWAYS, NEBULA_STENCIL_ZERO);
    TypedDraw site(material, mMtx, MR::getCameraViewMtx());
    site.drawModelFile(mModelData);

    material.setDepth(true, NEBULA_COMPARE_GEQUAL, false);
    material.mState.cull = NEBULA_CULL_FRONT;
    material.setStencil(NEBULA_STENCIL_ALWAYS, NEBULA_STENCIL_ADD);
    site.setMaterial(material);
    site.drawModelFile(mModelData);
    material.mState.cull = NEBULA_CULL_BACK;
    material.setStencil(NEBULA_STENCIL_ALWAYS, NEBULA_STENCIL_ZERO);
    site.setMaterial(material);
    site.drawModelFile(mModelData);

    material.mState.cull = NEBULA_CULL_NONE;
    material.setDepth(false, NEBULA_COMPARE_ALWAYS, false);
    material.mState.color_write = 1;
    material.setBlend(NEBULA_FACTOR_SRC_ALPHA, NEBULA_FACTOR_ONE);
    material.setStencil(NEBULA_STENCIL_IF_SET, NEBULA_STENCIL_ZERO);
    site.setMaterial(material);
    site.drawModelFile(mModelData);
}
