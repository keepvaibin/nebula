#include "Game/Screen/ScreenBlurEffect.hpp"
#include "Game/Render/FrameList.hpp"
#include "Game/Render/FrameList.hpp"
#include "Game/Render/TypedDraw.hpp"
#include "Game/Render/TypedDraw.hpp"
#include "Game/Screen/ImageEffectLocalUtil.hpp"
#include "Game/Screen/ImageEffectResource.hpp"
#include "Game/Screen/ImageEffectSystemHolder.hpp"
#include "Game/Util/CameraUtil.hpp"
#include "Game/Util/Color.hpp"
#include "Game/Util/DrawUtil.hpp"
#include "Game/Util/MathUtil.hpp"
#include "Game/Render/GameTexture.hpp"
#include "Game/Render/TypedDraw.hpp"
#include <nebula_game_api.h>
#include "Game/Util/Tunable.hpp"

NEBULA_TUNABLE_NAMED(f32, cNum_blurMaterial_f1, "blurMaterial.f1", 255.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_blurMaterial_f2, "blurMaterial.f2", 255.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_drawBlur_f1, "drawBlur.f1", 220.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_drawBlur_f2, "drawBlur.f2", 0.5f);

namespace {
    // runtime/src/render/materials/screen_blur.hlsl: the blurred picture (texture 0) at the effect's alpha where the
    // depth capture (texture 1) lies beyond 240 (of 255); depth tested, not written; blended by its alpha.
    SiteMaterial blurMaterial(GameTexture* pPicture, GameTexture* pDepth, u8 alpha) {
        SiteMaterial material;
        material.init(NEBULA_SHADER_SCREEN_BLUR, SiteMaterial::BLEND_ALPHA, NEBULA_COMPARE_LEQUAL, false,
                           NEBULA_CULL_NONE);
        material.constants[0] = alpha / cNum_blurMaterial_f1;
        material.constants[1] = 240 / cNum_blurMaterial_f2;
        material.setTexture(0, pPicture);
        material.setTexture(1, pDepth);
        
        return material;
    }
}  // namespace
#include <nebula_geometry.hpp>
#include "Game/Render/GameTexture.hpp"
#include "Game/Util/Tunable.hpp"

namespace {
    NEBULA_TUNABLE(f32, sScale, 10000.0f);
};  // namespace

ScreenBlurEffect::ScreenBlurEffect(const char* pName) : ImageEffectBase(pName), mFullTexture(), mHalfTexture(), mScale(::sScale), mSkipDraw(true), mIntensity(1.0f) {
    MR::connectToSceneImageEffect(this);
    MR::createImageEffectSystemHolder();
    MR::getImageEffectResource()->createBlurTexture();

    mFullTexture = MR::getImageEffectResource()->mFullTexture;
    mHalfTexture = MR::getImageEffectResource()->mHalfTexture;
    mFrameDepthKey = ImageEffectLocalUtil::createFrameKey();
}

void ScreenBlurEffect::calcAnimSub() {
    mSkipDraw = false;
}

void ScreenBlurEffect::notifyTurnOn() {
    mSkipDraw = true;
}

void ScreenBlurEffect::draw() const {
    if (isSomething()) {
        drawBlur();
        captureBlur();
    }
}

void ScreenBlurEffect::drawBlur() const {
    if (mSkipDraw) {
        return;
    }

    Proj3f projectionMtx;
    projectionMtx.makePerspective(MR::getRenderFovy(), MR::getAspect(), MR::getRenderNearZ(), MR::getFarZ());
    Frame::setPerspective(projectionMtx);

    const SiteMaterial material = blurMaterial(mFullTexture, mHalfTexture, static_cast< u8 >(static_cast< s32 >(getFadeLevel() * cNum_drawBlur_f1 * mIntensity)));
    TypedDraw site = TypedDraw::eyeSpace(material, cIdentityMtx);

    f32 y = MR::tanDegree(MR::getRenderFovy() * cNum_drawBlur_f2) * mScale;
    f32 x = MR::getAspect() * y;

    VertexPT* vtx = site.vertices< VertexPT >(NEBULA_PRIMITIVE_TRIANGLE_STRIP, 4);
    {
        f32 z = -mScale;

        *vtx++ = {-x, -y, z, 0.0f, 1.0f};

        *vtx++ = {-x, y, z, 0.0f, 0.0f};

        *vtx++ = {x, -y, z, 1.0f, 1.0f};

        *vtx++ = {x, y, z, 1.0f, 0.0f};
    }
}

void ScreenBlurEffect::captureBlur() const {
    mFullTexture->copyFrame();
    ImageEffectLocalUtil::drawFrameDepth(mHalfTexture, mFrameDepthKey);
    MR::loadProjectionMtx();
    MR::loadViewMtx();
}
