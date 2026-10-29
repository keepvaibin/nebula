#include "Game/Effect/ParticleDrawExecutor.hpp"
#include <JSystem/J3DGraphBase/J3DNativeState.hpp>
#include <JSystem/J3DGraphBase/J3DNativeView.hpp>
#include "Game/Effect/EffectSystem.hpp"
#include "Game/NameObj/NameObjAdaptor.hpp"
#include "Game/Scene/SceneFunction.hpp"
#include "Game/Util/CameraUtil.hpp"
#include "Game/Util/Color.hpp"
#include "Game/Util/ObjUtil.hpp"
#include "Game/Util/ScreenUtil.hpp"
#include "Game/Util/SystemUtil.hpp"
#include <JSystem/J3DGraphBase/J3DSys.hpp>
#include <JSystem/JParticle/JPAEmitterManager.hpp>
#include <JSystem/JUtility/JUTVideo.hpp>

namespace {
    void connectToSceneDrawAdaptor(NameObjAdaptor* pAdaptor, const MR::FunctorBase& rFunctor, int drawType) NO_INLINE {
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
    J3DNativeState::setFog(GX_FOG_NONE, 0.0f, 0.0f, 0.0f, 0.0f, Color8(0, 0, 0, 0));
}

void ParticleDrawExecutor::draw3D() const {
    drawWithViewMtx3D(MR::getCameraViewMtx());
}

void ParticleDrawExecutor::draw2D() const {
    if (_20) {
        TPos3f cameraMtx;
        cameraMtx.identity();
        JPADrawInfo drawInfo(cameraMtx.toMtxPtr());

        f32 width;
        f32 height = static_cast< s32 >(JUTVideo::getManager()->getEfbHeight());
        width = MR::isScreen16Per9() ? MR::getScreenWidth() : 608.0f;
        width *= 0.5f;
        height *= 0.5f;

        Mtx44 projection;
        C_MTXOrtho(projection, height, -height, -width, width, -1000.0f, 1000.0f);
        J3DNativeView::setProjection(projection, GX_ORTHOGRAPHIC);
        J3DNativeState::setCullMode(GX_CULL_NONE);
        J3DNativeState::setZMode(GX_FALSE, GX_NEVER, GX_FALSE);
        C_MTXLightOrtho(drawInfo.mPrjMtx, height, -height, -width, width, 0.5f, 0.5f, 0.5f, 0.5f);
        mHost->mEmitterManager->draw(&drawInfo, 6);
        J3DNativeState::setClipMode(GX_CLIP_ENABLE);
        mHost->mEmitterManager->draw(&drawInfo, 7);
        J3DNativeState::setClipMode(GX_CLIP_ENABLE);
    }
}

void ParticleDrawExecutor::drawIndirect() const {
    if (_20) {
        JPADrawInfo drawInfo(j3dSys.getViewMtx(), MR::getRenderFovy(), MR::getAspect());
        mHost->mEmitterManager->draw(&drawInfo, 2);
        J3DNativeState::setClipMode(GX_CLIP_ENABLE);
    }
}

void ParticleDrawExecutor::drawAfterIndirect() const {
    drawWithViewMtxAfterIndirect(MR::getCameraViewMtx());
}

void ParticleDrawExecutor::drawFor2DModel() const {
    if (_20) {
        JPADrawInfo drawInfo(j3dSys.getViewMtx());
        mHost->mEmitterManager->draw(&drawInfo, 8);
        J3DNativeState::setClipMode(GX_CLIP_ENABLE);
    }
}

void ParticleDrawExecutor::drawForBloomEffect() const {
    drawWithViewMtxForBloomEffect(MR::getCameraViewMtx());
}

void ParticleDrawExecutor::drawAfterImageEffect() const {
    drawWithViewMtxAfterImageEffect(MR::getCameraViewMtx());
}

void ParticleDrawExecutor::drawWithViewMtx3D(const TPos3f& rViewMtx) const {
    if (_20) {
        JPADrawInfo drawInfo(rViewMtx);
        mHost->mEmitterManager->draw(&drawInfo, 0);
        J3DNativeState::setClipMode(GX_CLIP_ENABLE);
        mHost->mEmitterManager->draw(&drawInfo, 1);
        J3DNativeState::setClipMode(GX_CLIP_ENABLE);
    }
}

void ParticleDrawExecutor::drawWithViewMtxAfterIndirect(const TPos3f& rViewMtx) const {
    if (_20) {
        JPADrawInfo drawInfo(rViewMtx);
        mHost->mEmitterManager->draw(&drawInfo, 3);
        J3DNativeState::setClipMode(GX_CLIP_ENABLE);
    }
}

void ParticleDrawExecutor::drawWithViewMtxForBloomEffect(const TPos3f& rViewMtx) const {
    if (_20) {
        JPADrawInfo drawInfo(rViewMtx);
        mHost->mEmitterManager->draw(&drawInfo, 4);
        J3DNativeState::setClipMode(GX_CLIP_ENABLE);
    }
}

void ParticleDrawExecutor::drawWithViewMtxAfterImageEffect(const TPos3f& rViewMtx) const {
    if (_20) {
        JPADrawInfo drawInfo(rViewMtx);
        mHost->mEmitterManager->draw(&drawInfo, 5);
        J3DNativeState::setClipMode(GX_CLIP_ENABLE);
    }
}

void ParticleDrawExecutor::initDrawAdaptor() {
    _4 = new NameObjAdaptor("3D\203\160\201\133\203\145\203\102\203\116\203\213" /* 3Dパーティクル */);
    ::connectToSceneDrawAdaptor(_4, MR::Functor(this, &ParticleDrawExecutor::draw3D), MR::DrawType_EffectDraw3D);

    _8 = new NameObjAdaptor("2D\203\160\201\133\203\145\203\102\203\116\203\213" /* 2Dパーティクル */);
    ::connectToSceneDrawAdaptor(_8, MR::Functor(this, &ParticleDrawExecutor::draw2D), MR::DrawType_EffectDraw2D);

    _C = new NameObjAdaptor("\203\103\203\223\203\137\203\103\203\214\203\116\203\147\203\160\201\133\203\145\203\102\203\116\203\213" /* インダイレクトパーティクル */);
    ::connectToSceneDrawAdaptor(_C, MR::Functor(this, &ParticleDrawExecutor::drawIndirect), MR::DrawType_EffectDrawIndirect);

    _10 = new NameObjAdaptor("\203\103\203\223\203\137\203\103\203\214\203\116\203\147\214\343\203\160\201\133\203\145\203\102\203\116\203\213" /* インダイレクト後パーティクル */);
    ::connectToSceneDrawAdaptor(_10, MR::Functor(this, &ParticleDrawExecutor::drawAfterIndirect), MR::DrawType_EffectDrawAfterIndirect);

    _14 = new NameObjAdaptor("2D\203\202\203\146\203\213\227\160\203\160\201\133\203\145\203\102\203\116\203\213" /* 2Dモデル用パーティクル */);
    ::connectToSceneDrawAdaptor(_14, MR::Functor(this, &ParticleDrawExecutor::drawFor2DModel), MR::DrawType_EffectDrawFor2DModel);

    _18 = new NameObjAdaptor("\203\165\203\213\201\133\203\200\227\160\203\160\201\133\203\145\203\102\203\116\203\213" /* ブルーム用パーティクル */);
    ::connectToSceneDrawAdaptor(_18, MR::Functor(this, &ParticleDrawExecutor::drawForBloomEffect), MR::DrawType_EffectDrawForBloomEffect);

    _1C = new NameObjAdaptor("\203\103\203\201\201\133\203\127\203\107\203\164\203\106\203\116\203\147\214\343\203\160\201\133\203\145\203\102\203\116\203\213" /* イメージエフェクト後パーティクル */);
    ::connectToSceneDrawAdaptor(_1C, MR::Functor(this, &ParticleDrawExecutor::drawAfterImageEffect), MR::DrawType_EffectDrawAfterImageEffect);
}
