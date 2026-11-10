#include "Game/Effect/ParticleDrawExecutor.hpp"
#include "Game/Render/FrameList.hpp"
#include "Game/Effect/EffectSystem.hpp"
#include "Game/NameObj/NameObjAdaptor.hpp"
#include "Game/Scene/SceneFunction.hpp"
#include "Game/Util/CameraUtil.hpp"
#include "Game/Util/Color.hpp"
#include "Game/Util/ObjUtil.hpp"
#include "Game/Util/ScreenUtil.hpp"
#include "Game/Util/SystemUtil.hpp"
#include "Game/Render/Model.hpp"
#include "Game/Particle/EmitterManager.hpp"
#include "Game/System/NebulaFrameClock.hpp"
#include "Game/Util/Tunable.hpp"

NEBULA_TUNABLE_NAMED(f32, cNum_draw2D_f1, "draw2D.f1", 0.5f);
NEBULA_TUNABLE_NAMED(f32, cNum_draw2D_f2, "draw2D.f2", 0.5f);
NEBULA_TUNABLE_NAMED(f32, cNum_draw2D_f3, "draw2D.f3", 1000.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_draw2D_f4, "draw2D.f4", 1000.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_draw2D_f5, "draw2D.f5", 0.5f);
NEBULA_TUNABLE_NAMED(f32, cNum_draw2D_f6, "draw2D.f6", 0.5f);
NEBULA_TUNABLE_NAMED(f32, cNum_draw2D_f7, "draw2D.f7", 0.5f);
NEBULA_TUNABLE_NAMED(f32, cNum_draw2D_f8, "draw2D.f8", 0.5f);

namespace {
    // The scene owns the adaptor.
    void connectPass(const char* pName, const MR::FunctorBase& rDraw, int drawType) {
        NameObjAdaptor* pAdaptor = new NameObjAdaptor(pName);
        pAdaptor->connectToDraw(rDraw);
        MR::connectToScene(pAdaptor, -1, -1, -1, drawType);
    }
};  // namespace

ParticleDrawExecutor::ParticleDrawExecutor(const EffectSystem* pSystem, bool connect) : mHost(pSystem) {
    if (connect) {
        connectToScene();
    }
}

void ParticleDrawExecutor::draw3D() const {
    drawWithViewMtx3D(MR::getCameraViewMtx());
}

void ParticleDrawExecutor::draw2D() const {
    Pos3f cameraMtx;
    cameraMtx.identity();
    ParticleCamera drawInfo(cameraMtx.toMtxPtr());

    f32 width;
    f32 height = static_cast< s32 >(NebulaFrameClock::cScreenLines);
    width = MR::isScreen16Per9() ? MR::getScreenWidth() : NebulaFrameClock::cLayoutWidth;
    width *= cNum_draw2D_f1;
    height *= cNum_draw2D_f2;

    Mtx44 projection;
    NebulaMtxOrtho(projection, height, -height, -width, width, -cNum_draw2D_f3, cNum_draw2D_f4);
    Frame::setOrthographic(projection);
    NebulaMtxLightOrtho(drawInfo.mPrjMtx, height, -height, -width, width, cNum_draw2D_f5, cNum_draw2D_f6, cNum_draw2D_f7, cNum_draw2D_f8);
    mHost->mEmitterManager->draw(&drawInfo, 6);
    mHost->mEmitterManager->draw(&drawInfo, 7);
}

void ParticleDrawExecutor::drawIndirect() const {
    ParticleCamera drawInfo(ModelSys::getViewMtx(), MR::getRenderFovy(), MR::getAspect());
    mHost->mEmitterManager->draw(&drawInfo, 2);
}

void ParticleDrawExecutor::drawAfterIndirect() const {
    drawWithViewMtxAfterIndirect(MR::getCameraViewMtx());
}

void ParticleDrawExecutor::drawFor2DModel() const {
    ParticleCamera drawInfo(ModelSys::getViewMtx());
    mHost->mEmitterManager->draw(&drawInfo, 8);
}

void ParticleDrawExecutor::drawForBloomEffect() const {
    drawWithViewMtxForBloomEffect(MR::getCameraViewMtx());
}

void ParticleDrawExecutor::drawAfterImageEffect() const {
    drawWithViewMtxAfterImageEffect(MR::getCameraViewMtx());
}

void ParticleDrawExecutor::drawWithViewMtx3D(const Pos3f& rViewMtx) const {
    ParticleCamera drawInfo(rViewMtx);
    mHost->mEmitterManager->draw(&drawInfo, 0);
    mHost->mEmitterManager->draw(&drawInfo, 1);
}

void ParticleDrawExecutor::drawWithViewMtxAfterIndirect(const Pos3f& rViewMtx) const {
    ParticleCamera drawInfo(rViewMtx);
    mHost->mEmitterManager->draw(&drawInfo, 3);
}

void ParticleDrawExecutor::drawWithViewMtxForBloomEffect(const Pos3f& rViewMtx) const {
    ParticleCamera drawInfo(rViewMtx);
    mHost->mEmitterManager->draw(&drawInfo, 4);
}

void ParticleDrawExecutor::drawWithViewMtxAfterImageEffect(const Pos3f& rViewMtx) const {
    ParticleCamera drawInfo(rViewMtx);
    mHost->mEmitterManager->draw(&drawInfo, 5);
}

void ParticleDrawExecutor::connectToScene() {
    ::connectPass("3Dパーティクル", MR::Functor(this, &ParticleDrawExecutor::draw3D), MR::DrawType_EffectDraw3D);
    ::connectPass("2Dパーティクル", MR::Functor(this, &ParticleDrawExecutor::draw2D), MR::DrawType_EffectDraw2D);
    ::connectPass("インダイレクトパーティクル", MR::Functor(this, &ParticleDrawExecutor::drawIndirect), MR::DrawType_EffectDrawIndirect);
    ::connectPass("インダイレクト後パーティクル", MR::Functor(this, &ParticleDrawExecutor::drawAfterIndirect),
                  MR::DrawType_EffectDrawAfterIndirect);
    ::connectPass("2Dモデル用パーティクル", MR::Functor(this, &ParticleDrawExecutor::drawFor2DModel), MR::DrawType_EffectDrawFor2DModel);
    ::connectPass("ブルーム用パーティクル", MR::Functor(this, &ParticleDrawExecutor::drawForBloomEffect), MR::DrawType_EffectDrawForBloomEffect);
    ::connectPass("イメージエフェクト後パーティクル", MR::Functor(this, &ParticleDrawExecutor::drawAfterImageEffect),
                  MR::DrawType_EffectDrawAfterImageEffect);
}
