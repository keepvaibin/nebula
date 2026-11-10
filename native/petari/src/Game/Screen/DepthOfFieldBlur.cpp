#include "Game/Render/FrameList.hpp"
#include "Game/Screen/DepthOfFieldBlur.hpp"
#include "Game/Render/TypedDraw.hpp"
#include "Game/Render/Material.hpp"
#include "Game/Render/FrameList.hpp"
#include "Game/Render/FrameList.hpp"
#include "Game/Render/TypedDraw.hpp"
#include "Game/Render/TypedDraw.hpp"
#include "Game/Screen/ImageEffectLocalUtil.hpp"
#include "Game/Screen/ImageEffectResource.hpp"
#include "Game/Screen/ImageEffectSystemHolder.hpp"
#include "Game/Util/CameraUtil.hpp"
#include "Game/Util/DrawUtil.hpp"
#include "Game/Util/ScreenUtil.hpp"
#include "Game/Render/GameTexture.hpp"
#include "Game/System/NebulaFrameClock.hpp"
#include <nebula_game_api.h>
#include <cstring>
#include "Game/Util/Tunable.hpp"
#include "Game/System/LumaCore.hpp"

NEBULA_TUNABLE_NAMED(f32, cNum_finalMaterial_f1, "finalMaterial.f1", 0.5f);
NEBULA_TUNABLE_NAMED(f32, cNum_finalMaterial_f2, "finalMaterial.f2", 2.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_finalMaterial_f3, "finalMaterial.f3", 0.5f);
NEBULA_TUNABLE_NAMED(f32, cNum_finalMaterial_f4, "finalMaterial.f4", 255.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_drawZAlphaTex_f1, "drawZAlphaTex.f1", 2.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_drawZAlphaTex_f2, "drawZAlphaTex.f2", 128.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_drawZAlphaTex_f3, "drawZAlphaTex.f3", 0.5f);
NEBULA_TUNABLE_NAMED(f32, cNum_drawZAlphaTex_f4, "drawZAlphaTex.f4", 0.5f);
NEBULA_TUNABLE_NAMED(f32, cNum_drawZAlphaTex_f5, "drawZAlphaTex.f5", 128.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_drawZAlphaTex_f6, "drawZAlphaTex.f6", 2.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_drawZAlphaTex_f7, "drawZAlphaTex.f7", 128.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_drawZAlphaTex_f8, "drawZAlphaTex.f8", 0.5f);
NEBULA_TUNABLE_NAMED(f32, cNum_drawZAlphaTex_f9, "drawZAlphaTex.f9", 0.5f);
NEBULA_TUNABLE_NAMED(f32, cNum_drawZAlphaTex_f10, "drawZAlphaTex.f10", 2.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_createBlurTexture_f1, "createBlurTexture.f1", 0.005f);
NEBULA_TUNABLE_NAMED(f32, cNum_drawFinal_f1, "drawFinal.f1", 255.0f);

extern "C" const NebulaHostServices* nebula_host(void);

namespace {
    // static const s32 sFirstBlurCount = _;
    // static const f32 sFirstBlurRadius = _;
    // static const f32 sBlendRate = _;
    NEBULA_TUNABLE(f32, sViewDistance, 10000.0f);
    NEBULA_TUNABLE(s8, sScaleExp, 1);

    // The depth-to-alpha pass (runtime/src/render/materials/vertex_color.hlsl): the ramp's vertex colours, no blending,
    // depth or culling.
    SiteMaterial zAlphaMaterial() {
        SiteMaterial material;
        material.init(NEBULA_SHADER_VERTEX_COLOR, SiteMaterial::BLEND_OPAQUE, NEBULA_COMPARE_ALWAYS, false,
                           NEBULA_CULL_NONE);
        material.constants[0] = -1.0f;
        return material;
    }

    // The final pass (runtime/src/render/materials/dof_final.hlsl): the blurred picture (texture 0) over the frame at the
    // alpha the focus ramp (texture 1) gives at the depth (texture 2) times `alpha`; blended by that alpha, no depth or
    // culling.
    SiteMaterial finalMaterial(GameTexture* pTexture0, GameTexture* pTexture1, GameTexture* pTexture2, u8 alpha) {
        SiteMaterial material;
        material.init(NEBULA_SHADER_DOF_FINAL, SiteMaterial::BLEND_ALPHA, NEBULA_COMPARE_ALWAYS, false,
                           NEBULA_CULL_NONE);
        f32 scale = cNum_finalMaterial_f1;
        for (s32 e = ::sScaleExp; e > 0; e--) {
            scale *= cNum_finalMaterial_f2;
        }
        for (s32 e = ::sScaleExp; e < 0; e++) {
            scale *= cNum_finalMaterial_f3;
        }
        material.constants[0] = alpha / cNum_finalMaterial_f4;
        material.constants[1] = scale;
        material.constants[2] = pTexture1->getWidth();
        material.constants[3] = pTexture1->getHeight();
        material.setTexture(0, pTexture0);
        material.setTexture(1, pTexture1);
        material.setTexture(2, pTexture2);
        return material;
    }
};  // namespace

DepthOfFieldBlur::DepthOfFieldBlur(const char* pName)
    : ImageEffectBase(pName), mFrameKey(), mFrameDepthKey(), mHalfTexture(), mQuarterTexture(), mDepthRampTexture(), mIntensity(1.0f), mBlurMaxDist(248), mBlurMinDist(242) {
    lumacore::setClass(this, lumacore::kClassId< DepthOfFieldBlur >);
    MR::connectToSceneImageEffect(this);
    MR::createImageEffectSystemHolder();
    MR::getImageEffectResource()->createDOFTexture();
    mFrameKey = ImageEffectLocalUtil::createFrameKey();
    mFrameDepthKey = ImageEffectLocalUtil::createFrameKey();
    mHalfTexture = MR::getImageEffectResource()->mHalfTexture;
    mQuarterTexture = MR::getImageEffectResource()->mQuarterTexture;
    mDepthRampTexture = GameTexture::createPixelTarget(128, 2);
    mDepthRampTexture->setSampling(NEBULA_WRAP_CLAMP, NEBULA_WRAP_CLAMP, NEBULA_FILTER_LINEAR, NEBULA_FILTER_LINEAR);
    mQuarter = GameTexture::createTarget(mQuarterTexture->shareAcross(), mQuarterTexture->shareDown());
    mQuarter->setSampling(NEBULA_WRAP_CLAMP, NEBULA_WRAP_CLAMP, NEBULA_FILTER_LINEAR, NEBULA_FILTER_LINEAR);
    mBlurScratch = GameTexture::createTarget(mQuarterTexture->shareAcross(), mQuarterTexture->shareDown());
    mBlurScratch->setSampling(NEBULA_WRAP_CLAMP, NEBULA_WRAP_CLAMP, NEBULA_FILTER_LINEAR, NEBULA_FILTER_LINEAR);
}

void DepthOfFieldBlur::draw() const {
    if (!isSomething()) {
        return;
    }

    Frame::targetBeginPixels(mDepthRampTexture->image(), mDepthRampTexture->getWidth(), mDepthRampTexture->getHeight(), 0);
    drawZAlphaTex();
    Frame::targetEnd(mDepthRampTexture->image(), NEBULA_TARGET_RED);
    ImageEffectLocalUtil::setupDrawTexture();
    createBlurTexture();
    ImageEffectLocalUtil::drawFrameDepth(mHalfTexture, mFrameDepthKey);
    drawFinal();
    MR::loadProjectionMtx();
    MR::loadViewMtx();
}

void DepthOfFieldBlur::drawZAlphaTex() const {
    Mtx44 projectionMtx;
    NebulaMtxOrtho(projectionMtx, 0.0f, cNum_drawZAlphaTex_f1, 0.0f, cNum_drawZAlphaTex_f2, -::sViewDistance, ::sViewDistance);
    Frame::setOrthographic(projectionMtx);
    MR::loadViewMtxFor2DModel();

    const SiteMaterial material = zAlphaMaterial();
    TypedDraw site = TypedDraw::eyeSpace(material, cIdentityMtx);

    // The ramp's stops along the whole texture.
    VertexPC* vtx = site.vertices< VertexPC >(NEBULA_PRIMITIVE_TRIANGLE_STRIP, 8);
    {
        u8 blurMinDist;
        u8 blurMaxDist = 248;

        // The distances come from an area's arguments (a mod's): held to a byte, not wrapped.
        if (mBlurMaxDist >= 0) {
            blurMaxDist = mBlurMaxDist > 255 ? 255 : mBlurMaxDist;
        }

        blurMinDist = 242;

        if (mBlurMinDist >= 0) {
            blurMinDist = mBlurMinDist > 255 ? 255 : mBlurMinDist;
        }

        if (blurMaxDist >= blurMinDist) {
            const u8 c[4] = {0, 0, 255, 255};
            const f32 at[4] = {0.0f, blurMinDist * cNum_drawZAlphaTex_f3, blurMaxDist * cNum_drawZAlphaTex_f4, cNum_drawZAlphaTex_f5};
            for (u32 i = 0; i < 4; i++) {
                *vtx++ = {at[i], 0.0f, 0.0f, Rgba8(c[i], c[i], c[i], 0xFF)};
                *vtx++ = {at[i], cNum_drawZAlphaTex_f6, 0.0f, Rgba8(c[i], c[i], c[i], 0xFF)};
            }
        } else {
            const u8 c[4] = {0, 0, 255, 255};
            const f32 at[4] = {cNum_drawZAlphaTex_f7, blurMinDist * cNum_drawZAlphaTex_f8, blurMaxDist * cNum_drawZAlphaTex_f9, 0.0f};
            for (u32 i = 0; i < 4; i++) {
                *vtx++ = {at[i], 0.0f, 0.0f, Rgba8(c[i], c[i], c[i], 0xFF)};
                *vtx++ = {at[i], cNum_drawZAlphaTex_f10, 0.0f, Rgba8(c[i], c[i], c[i], 0xFF)};
            }
        }
    }
}

void DepthOfFieldBlur::createBlurTexture() const {
    Frame::targetBeginOverFrame(mQuarter->image(), mQuarter->shareAcross(), mQuarter->shareDown(), 0, mFrameKey->image(), NEBULA_TARGET_RGB,
                                    nullptr);
    const NebulaColor8 none = {0, 0, 0, 0};
    ImageEffectLocalUtil::setMode(ImageEffectLocalUtil::Mode_QuarterBox, none);
    ImageEffectLocalUtil::drawTexture(mFrameKey, 255, ImageEffectLocalUtil::TexDrawType_0);
    ImageEffectLocalUtil::setupDrawTexture();
    Frame::targetEnd(mQuarter->image(), NEBULA_TARGET_RGB);
    ImageEffectLocalUtil::blurTexture(mQuarter, mBlurScratch, mQuarterTexture, mIntensity * getFadeLevel() * cNum_createBlurTexture_f1, NEBULA_TARGET_RGB);
}

void DepthOfFieldBlur::drawFinal() const {
    ImageEffectLocalUtil::drawQuad(finalMaterial(mQuarterTexture, mDepthRampTexture, mHalfTexture, static_cast< u8 >((s32)(mIntensity * getFadeLevel() * cNum_drawFinal_f1))));
    ImageEffectLocalUtil::setupDrawTexture();
}
