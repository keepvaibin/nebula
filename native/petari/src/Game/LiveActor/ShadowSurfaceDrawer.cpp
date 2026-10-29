#include "Game/LiveActor/ShadowSurfaceDrawer.hpp"
#include <JSystem/J3DGraphBase/J3DNativeState.hpp>
#include "Game/Scene/SceneFunction.hpp"
#include "Game/Scene/SceneObjHolder.hpp"
#include "Game/Util/Color.hpp"
#include "Game/Util/DirectDraw.hpp"
#include "Game/Util/ObjUtil.hpp"

ShadowSurfaceDrawInit::ShadowSurfaceDrawInit(const char* pName) : NameObj(pName) {
    MR::registerPreDrawFunction(MR::Functor(&ShadowSurfaceDrawInit::initDraw), MR::DrawType_ShadowSurface);
}

void ShadowSurfaceDrawInit::initDraw() {
    TDDraw::setup(0, 1, 1);
    J3DNativeState::setChanCtrl(GX_COLOR0A0, 0, GX_SRC_REG, GX_SRC_REG, 0, GX_DF_NONE, GX_AF_NONE);
    J3DNativeState::setChanCtrl(GX_COLOR1A1, 0, GX_SRC_REG, GX_SRC_REG, 0, GX_DF_NONE, GX_AF_NONE);
    J3DNativeState::setChanMatColor(GX_COLOR0A0, Color8(128));
    J3DNativeState::setZMode(1, GX_LEQUAL, 0);
    J3DNativeState::setCullMode(GX_CULL_BACK);
    J3DNativeState::setColorUpdate(1);
    J3DNativeState::setAlphaUpdate(1);
    J3DNativeState::setDstAlpha(1, 0);
}

ShadowSurfaceDrawer::ShadowSurfaceDrawer(const char* pName) : ShadowDrawer(pName) {
    MR::createSceneObj(SceneObj_ShadowSurfaceDrawInit);
    MR::connectToScene(this, MR::MovementType_None, MR::CalcAnimType_None, MR::DrawBufferType_None, MR::DrawType_ShadowSurface);
}

ShadowSurfaceDrawInit::~ShadowSurfaceDrawInit() {
}
