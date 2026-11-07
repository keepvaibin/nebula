#include "Game/Effect/ParticleDrawExecutor.hpp"
#include "Game/Render/FrameList.hpp"
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
    void connectToSceneDrawAdaptor(NameObjAdaptor* pAdaptor, const MR::FunctorBase& rFunctor, int drawType) {
        pAdaptor->connectToDraw(rFunctor);
        MR::connectToScene(pAdaptor, -1, -1, -1, drawType);
        MR::registerPreDrawFunction(MR::Functor(&ParticleDrawExecutor::initDraw), drawType);
    }
};  // namespace

ParticleDrawExecutor::ParticleDrawExecutor(const EffectSystem* pSystem, bool createAdaptors)
    : mHost(pSystem), _4(), _8(), _C(), _10(), _14(), _18(), _1C(), _20(true), _21() {
    if (createAdaptors) {
        initDrawAdaptor();
    }
}

void ParticleDrawExecutor::initDraw() {
}

void ParticleDrawExecutor::draw3D() const {
    drawWithViewMtx3D(MR::getCameraViewMtx());
}

void ParticleDrawExecutor::draw2D() const {
    if (_20) {
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
}

void ParticleDrawExecutor::drawIndirect() const {
    if (_20) {
        ParticleCamera drawInfo(ModelSys::getViewMtx(), MR::getRenderFovy(), MR::getAspect());
        mHost->mEmitterManager->draw(&drawInfo, 2);
    }
}

void ParticleDrawExecutor::drawAfterIndirect() const {
    drawWithViewMtxAfterIndirect(MR::getCameraViewMtx());
}

void ParticleDrawExecutor::drawFor2DModel() const {
    if (_20) {
        ParticleCamera drawInfo(ModelSys::getViewMtx());
        mHost->mEmitterManager->draw(&drawInfo, 8);
    }
}

void ParticleDrawExecutor::drawForBloomEffect() const {
    drawWithViewMtxForBloomEffect(MR::getCameraViewMtx());
}

void ParticleDrawExecutor::drawAfterImageEffect() const {
    drawWithViewMtxAfterImageEffect(MR::getCameraViewMtx());
}

void ParticleDrawExecutor::drawWithViewMtx3D(const Pos3f& rViewMtx) const {
    if (_20) {
        ParticleCamera drawInfo(rViewMtx);
        mHost->mEmitterManager->draw(&drawInfo, 0);
        mHost->mEmitterManager->draw(&drawInfo, 1);
    }
}

void ParticleDrawExecutor::drawWithViewMtxAfterIndirect(const Pos3f& rViewMtx) const {
    if (_20) {
        ParticleCamera drawInfo(rViewMtx);
        mHost->mEmitterManager->draw(&drawInfo, 3);
    }
}

void ParticleDrawExecutor::drawWithViewMtxForBloomEffect(const Pos3f& rViewMtx) const {
    if (_20) {
        ParticleCamera drawInfo(rViewMtx);
        mHost->mEmitterManager->draw(&drawInfo, 4);
    }
}

void ParticleDrawExecutor::drawWithViewMtxAfterImageEffect(const Pos3f& rViewMtx) const {
    if (_20) {
        ParticleCamera drawInfo(rViewMtx);
        mHost->mEmitterManager->draw(&drawInfo, 5);
    }
}

void ParticleDrawExecutor::initDrawAdaptor() {
    _4 = new NameObjAdaptor("3Dパーティクル");
    ::connectToSceneDrawAdaptor(_4, MR::Functor(this, &ParticleDrawExecutor::draw3D), MR::DrawType_EffectDraw3D);

    _8 = new NameObjAdaptor("2Dパーティクル");
    ::connectToSceneDrawAdaptor(_8, MR::Functor(this, &ParticleDrawExecutor::draw2D), MR::DrawType_EffectDraw2D);

    _C = new NameObjAdaptor("インダイレクトパーティクル");
    ::connectToSceneDrawAdaptor(_C, MR::Functor(this, &ParticleDrawExecutor::drawIndirect), MR::DrawType_EffectDrawIndirect);

    _10 = new NameObjAdaptor("インダイレクト後パーティクル");
    ::connectToSceneDrawAdaptor(_10, MR::Functor(this, &ParticleDrawExecutor::drawAfterIndirect), MR::DrawType_EffectDrawAfterIndirect);

    _14 = new NameObjAdaptor("2Dモデル用パーティクル");
    ::connectToSceneDrawAdaptor(_14, MR::Functor(this, &ParticleDrawExecutor::drawFor2DModel), MR::DrawType_EffectDrawFor2DModel);

    _18 = new NameObjAdaptor("ブルーム用パーティクル");
    ::connectToSceneDrawAdaptor(_18, MR::Functor(this, &ParticleDrawExecutor::drawForBloomEffect), MR::DrawType_EffectDrawForBloomEffect);

    _1C = new NameObjAdaptor("イメージエフェクト後パーティクル");
    ::connectToSceneDrawAdaptor(_1C, MR::Functor(this, &ParticleDrawExecutor::drawAfterImageEffect), MR::DrawType_EffectDrawAfterImageEffect);
}
