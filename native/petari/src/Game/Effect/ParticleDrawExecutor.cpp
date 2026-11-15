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
    width *= 0.5f;
    height *= 0.5f;

    Mtx44 projection;
    NebulaMtxOrtho(projection, height, -height, -width, width, -1000.0f, 1000.0f);
    Frame::setOrthographic(projection);
    NebulaMtxLightOrtho(drawInfo.mPrjMtx, height, -height, -width, width, 0.5f, 0.5f, 0.5f, 0.5f);
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
