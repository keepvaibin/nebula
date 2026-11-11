#include "Game/Util/CameraUtil.hpp"
#include "Game/LiveActor/ShadowSurfaceDrawer.hpp"
#include "Game/LiveActor/ShadowController.hpp"
#include "Game/Render/TypedDraw.hpp"
#include "Game/Render/FrameList.hpp"
#include "Game/Scene/SceneFunction.hpp"
#include "Game/Scene/SceneObjHolder.hpp"
#include "Game/Util/Color.hpp"
#include "Game/Util/DirectDraw.hpp"
#include "Game/Util/ObjUtil.hpp"
#include "Game/System/LumaCore.hpp"

ShadowSurfaceDrawInit::ShadowSurfaceDrawInit(const char* pName) : NameObj(pName) {
    lumacore::setClass(this, lumacore::kClassId< ShadowSurfaceDrawInit >);
    MR::registerPreDrawFunction(MR::Functor(&ShadowSurfaceDrawInit::initDraw), MR::DrawType_ShadowSurface);
}

namespace {
    SiteMaterial sSurface;
}

void ShadowSurfaceDrawInit::sendColor(const Color8& rColor) {
    sSurface.init(NEBULA_SHADER_FLAT_COLOR, SiteMaterial::BLEND_ALPHA, NEBULA_COMPARE_LEQUAL, false, NEBULA_CULL_BACK);
    sSurface.mState.color_write = 1;
    sSurface.mState.alpha_write = 1;
    sSurface.setColor(0, rColor.r, rColor.g, rColor.b, rColor.a);
}

const SiteMaterial& ShadowSurfaceDrawInit::material() {
    return sSurface;
}

void ShadowSurfaceDrawInit::initDraw() {
    MR::loadProjectionMtx();
    sendColor(Color8(0x80u));
}

ShadowSurfaceDrawer::ShadowSurfaceDrawer(const char* pName) : ShadowDrawer(pName) {
    lumacore::setClass(this, lumacore::kClassId< ShadowSurfaceDrawer >);
    MR::createSceneObj(SceneObj_ShadowSurfaceDrawInit);
    MR::connectToScene(this, MR::MovementType_None, MR::CalcAnimType_None, MR::DrawBufferType_None, MR::DrawType_ShadowSurface);
}

void ShadowSurfaceDrawer::calcDrawAnchor(Vec3f* pOut) const {
    getController()->getProjectionPos(pOut);
}

ShadowSurfaceDrawInit::~ShadowSurfaceDrawInit() {
}
