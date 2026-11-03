#include <JSystem/J3DGraphBase/J3DNativeTarget.hpp>
#include "Game/Screen/DepthOfFieldBlur.hpp"
#include "Game/Util/NativeShader.hpp"
#include <JSystem/J3DGraphBase/J3DNativeMaterial.hpp>
#include <JSystem/J3DGraphBase/J3DNativeState.hpp>
#include <JSystem/J3DGraphBase/J3DNativeView.hpp>
#include <JSystem/J3DGraphBase/J3DNativeShape.hpp>
#include <JSystem/J3DGraphBase/J3DNativeDraw.hpp>
#include "Game/Screen/ImageEffectLocalUtil.hpp"
#include "Game/Screen/ImageEffectResource.hpp"
#include "Game/Screen/ImageEffectSystemHolder.hpp"
#include "Game/Util/CameraUtil.hpp"
#include "Game/Util/DrawUtil.hpp"
#include "Game/Util/ScreenUtil.hpp"
#include <JSystem/JUtility/JUTTexture.hpp>
#include "Game/System/NebulaFrameClock.hpp"
#include <nebula_game_api.h>
#include <cstring>

extern "C" const NebulaHostServices* nebula_host(void);

namespace {
    // static const s32 sFirstBlurCount = _;
    // static const f32 sFirstBlurRadius = _;
    // static const f32 sBlendRate = _;
    static const f32 sViewDistance = 10000.0f;
    static const s8 sScaleExp = 1;

    // The depth-to-alpha pass (runtime/src/render/materials/vertex_color.hlsl): the ramp's vertex colours, no blending,
    // depth or culling.
    void sendZAlphaMaterial() {
        NebulaNativeMaterial material;
        NativeShader::init(material, NEBULA_SHADER_VERTEX_COLOR, NativeShader::BLEND_OPAQUE, NEBULA_COMPARE_ALWAYS, false,
                           NEBULA_CULL_NONE);
        material.constants[4] = -1.0f;
        NativeShader::send(material);
    }

    // The final pass (runtime/src/render/materials/dof_final.hlsl): the blurred picture (texture 0) over the frame at the
    // alpha the focus ramp (texture 1) gives at the depth (texture 2) times `alpha`; blended by that alpha, no depth or
    // culling.
    void sendFinalMaterial(JUTTexture* pTexture0, JUTTexture* pTexture1, JUTTexture* pTexture2, u8 alpha) {
        NebulaNativeMaterial material;
        NativeShader::init(material, NEBULA_SHADER_DOF_FINAL, NativeShader::BLEND_ALPHA, NEBULA_COMPARE_ALWAYS, false,
                           NEBULA_CULL_NONE);
        f32 scale = 0.5f;
        for (s32 e = ::sScaleExp; e > 0; e--) {
            scale *= 2.0f;
        }
        for (s32 e = ::sScaleExp; e < 0; e++) {
            scale *= 0.5f;
        }
        material.constants[0] = alpha / 255.0f;
        material.constants[1] = scale;
        material.constants[2] = pTexture1->getWidth();
        material.constants[3] = pTexture1->getHeight();
        NativeShader::setTexture(material, 0, pTexture0);
        NativeShader::setTexture(material, 1, pTexture1);
        NativeShader::setTexture(material, 2, pTexture2);
        NativeShader::send(material);
    }
};  // namespace

DepthOfFieldBlur::DepthOfFieldBlur(const char* pName)
    : ImageEffectBase(pName), _14(), mFrameKey(), mFrameDepthKey(), _20(), _24(), _28(), mIntensity(1.0f), mBlurMaxDist(248), mBlurMinDist(242) {
    MR::connectToSceneImageEffect(this);
    MR::createImageEffectSystemHolder();
    MR::getImageEffectResource()->createDOFTexture();
    _14 = MR::getImageEffectResource()->_0;
    mFrameKey = ImageEffectLocalUtil::createFrameKey();
    mFrameDepthKey = ImageEffectLocalUtil::createFrameKey();
    _20 = MR::getImageEffectResource()->_8;
    _24 = MR::getImageEffectResource()->_C;
    _28 = new JUTTexture(128, 2);
    _28->mWrapS = NEBULA_WRAP_CLAMP;
    _28->mWrapT = NEBULA_WRAP_CLAMP;
    _28->mMinType = NEBULA_FILTER_LINEAR;
    _28->mMagType = NEBULA_FILTER_LINEAR;
    mQuarter = new JUTTexture(_24->getWidth(), _24->getHeight());
    mQuarter->mWrapS = NEBULA_WRAP_CLAMP;
    mQuarter->mWrapT = NEBULA_WRAP_CLAMP;
    mQuarter->mMinType = NEBULA_FILTER_LINEAR;
    mQuarter->mMagType = NEBULA_FILTER_LINEAR;
    mBlurScratch = new JUTTexture(_24->getWidth(), _24->getHeight());
    mBlurScratch->mWrapS = NEBULA_WRAP_CLAMP;
    mBlurScratch->mWrapT = NEBULA_WRAP_CLAMP;
    mBlurScratch->mMinType = NEBULA_FILTER_LINEAR;
    mBlurScratch->mMagType = NEBULA_FILTER_LINEAR;
}

void DepthOfFieldBlur::draw() const {
    if (!isSomething()) {
        return;
    }

    J3DNativeTarget::begin(_28->mImage, _28->getWidth(), _28->getHeight(), 0);
    drawZAlphaTex();
    J3DNativeTarget::end(_28->mImage, NEBULA_TARGET_RED);
    ImageEffectLocalUtil::setupDrawTexture();
    createBlurTexture();
    ImageEffectLocalUtil::drawFrameDepth(_20, mFrameDepthKey);
    drawFinal();
    MR::loadProjectionMtx();
    MR::loadViewMtx();
}

void DepthOfFieldBlur::drawZAlphaTex() const {
    Mtx44 projectionMtx;
    NebulaMtxOrtho(projectionMtx, 0.0f, static_cast< s32 >(NebulaFrameClock::cScreenUnitsDown), 0.0f,
               static_cast< s32 >(NebulaFrameClock::cScreenUnitsAcross), -::sViewDistance, ::sViewDistance);
    J3DNativeView::setOrthographic(projectionMtx);
    MR::loadViewMtxFor2DModel();

    TMtx34f posMtx;
    posMtx.identity();
    J3DNativeShape::setPosition(0, posMtx.toMtxPtr());
    J3DPass::positionMatrix(0);
    J3DPass::writeColor(true);
    J3DPass::writeAlpha(false);
    J3DPass::lineWidth(2.0f);
    sendZAlphaMaterial();

    J3DNativeDraw::begin(NEBULA_PRIMITIVE_LINE_STRIP, 4, NEBULA_SHAPE_COLOR0);
    {
        u8 blurMinDist;
        u8 blurMaxDist = 248;

        if (mBlurMaxDist >= 0) {
            blurMaxDist = mBlurMaxDist;
        }

        blurMinDist = 242;

        if (mBlurMinDist >= 0) {
            blurMinDist = mBlurMinDist;
        }

        if (blurMaxDist >= blurMinDist) {
            J3DNativeDraw::position(0.0f, 1.0f, 0.0f);
            J3DNativeDraw::color(0, 0, 0, 0xFF);
            J3DNativeDraw::position(blurMinDist * 0.5f, 1.0f, 0.0f);
            J3DNativeDraw::color(0, 0, 0, 0xFF);
            J3DNativeDraw::position(blurMaxDist * 0.5f, 1.0f, 0.0f);
            J3DNativeDraw::color(255, 255, 255, 0xFF);
            J3DNativeDraw::position(128.0f, 1.0f, 0.0f);
            J3DNativeDraw::color(255, 255, 255, 0xFF);
        } else {
            J3DNativeDraw::position(128.0f, 1.0f, 0.0f);
            J3DNativeDraw::color(0, 0, 0, 0xFF);
            J3DNativeDraw::position(blurMinDist * 0.5f, 1.0f, 0.0f);
            J3DNativeDraw::color(0, 0, 0, 0xFF);
            J3DNativeDraw::position(blurMaxDist * 0.5f, 1.0f, 0.0f);
            J3DNativeDraw::color(255, 255, 255, 0xFF);
            J3DNativeDraw::position(0.0f, 1.0f, 0.0f);
            J3DNativeDraw::color(255, 255, 255, 0xFF);
        }
    }
}

void DepthOfFieldBlur::createBlurTexture() const {
    J3DNativeTarget::beginOverFrame(mQuarter->mImage, mQuarter->getWidth(), mQuarter->getHeight(), 0, mFrameKey->mImage, NEBULA_TARGET_RGB,
                                    nullptr);
    const NebulaColor8 none = {0, 0, 0, 0};
    ImageEffectLocalUtil::setMode(ImageEffectLocalUtil::Mode_QuarterBox, none, none);
    ImageEffectLocalUtil::drawTexture(mFrameKey, 4, 0, 255, ImageEffectLocalUtil::TexDrawType_0);
    ImageEffectLocalUtil::setupDrawTexture();
    J3DNativeTarget::end(mQuarter->mImage, NEBULA_TARGET_RGB);
    ImageEffectLocalUtil::blurTexture(mQuarter, mBlurScratch, _24, mIntensity * get_10() * 0.005f, NEBULA_TARGET_RGB);
}

void DepthOfFieldBlur::drawFinal() const {
    sendFinalMaterial(_24, _28, _20, static_cast< u8 >((s32)(mIntensity * get_10() * 255.0f)));
    ImageEffectLocalUtil::sendTextureVertex(1, 0);
    ImageEffectLocalUtil::setupDrawTexture();
}
