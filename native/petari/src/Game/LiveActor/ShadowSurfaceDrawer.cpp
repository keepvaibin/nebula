#include "Game/LiveActor/ShadowSurfaceDrawer.hpp"
#include "Game/Util/NativeShader.hpp"
#include <JSystem/J3DGraphBase/J3DNativeState.hpp>
#include "Game/Scene/SceneFunction.hpp"
#include "Game/Scene/SceneObjHolder.hpp"
#include "Game/Util/Color.hpp"
#include "Game/Util/DirectDraw.hpp"
#include "Game/Util/ObjUtil.hpp"

ShadowSurfaceDrawInit::ShadowSurfaceDrawInit(const char* pName) : NameObj(pName) {
    MR::registerPreDrawFunction(MR::Functor(&ShadowSurfaceDrawInit::initDraw), MR::DrawType_ShadowSurface);
}

void ShadowSurfaceDrawInit::sendColor(const NebulaColor8& rColor) {
    NebulaNativeMaterial material;
    NativeShader::init(material, NEBULA_SHADER_FLAT_COLOR, NativeShader::BLEND_ALPHA, NEBULA_COMPARE_LEQUAL, false,
                       NEBULA_CULL_BACK);
    material.color_write = 1;
    material.alpha_write = 1;
    NativeShader::setColor(material, 0, rColor.r, rColor.g, rColor.b, rColor.a);
    NativeShader::send(material);
}

void ShadowSurfaceDrawInit::initDraw() {
    NebulaNativeMaterial view;
    TDDraw::setupNative(&view, 0, 1, 1);
    sendColor(Color8(128));
}

ShadowSurfaceDrawer::ShadowSurfaceDrawer(const char* pName) : ShadowDrawer(pName) {
    MR::createSceneObj(SceneObj_ShadowSurfaceDrawInit);
    MR::connectToScene(this, MR::MovementType_None, MR::CalcAnimType_None, MR::DrawBufferType_None, MR::DrawType_ShadowSurface);
}

ShadowSurfaceDrawInit::~ShadowSurfaceDrawInit() {
}
