#include <JSystem/J3DGraphBase/J3DNativeTarget.hpp>
#include "Game/Screen/BloomEffectSimple.hpp"
#include <JSystem/J3DGraphBase/J3DNativeState.hpp>
#include "Game/Screen/ImageEffectLocalUtil.hpp"
#include "Game/Screen/ImageEffectResource.hpp"
#include "Game/Screen/ImageEffectSystemHolder.hpp"
#include "Game/Util/DrawUtil.hpp"
#include "Game/Util/Color.hpp"
#include "Game/Util/SystemUtil.hpp"
#include <JSystem/JUtility/JUTTexture.hpp>
#include <nebula_game_api.h>
#include "Game/Util/RenderSignals.hpp"
#include <revolution/os.h>
#include "Game/System/NebulaHost.hpp"

namespace {
    // static const s32 sLen = _;
    // static const s32 sDivNum = _;
};  // namespace

namespace {
    void beginPass(JUTTexture* pTexture) {
        J3DNativeTarget::begin(pTexture->mImage, pTexture->getWidth(), pTexture->getHeight(), 0, 0xFFFFFF);
    }
};  // namespace

BloomEffectSimple::BloomEffectSimple()
    : ImageEffectBase("\212\310\210\325\203\165\203\213\201\133\203\200" /* 簡易ブルーム */), _14(), _18(), _1C(), _20(), _24(), _28(), _2C(), mMaskFilter(), mThreshold(205), mIntensity(0.3f) {
    MR::connectToSceneImageEffect(this);
    MR::createImageEffectSystemHolder();
    MR::getImageEffectResource()->createBloomTexture();

    _14 = MR::getImageEffectResource()->_0;
    _18 = MR::getImageEffectResource()->_4;
    _1C = MR::getImageEffectResource()->_C;
    _20 = MR::getImageEffectResource()->_10;
    _24 = MR::getImageEffectResource()->_14;
    _28 = MR::getImageEffectResource()->_18;
    _2C = MR::getImageEffectResource()->_1C;
}

void BloomEffectSimple::draw() const {
    if (!isSomething()) {
        return;
    }

    const u32 route = NebulaHost::option(NEBULA_OPTION_BLOOM);
    if (route == 2) {
        // The chain below, drawn by the renderer at the internal resolution (galaxy/gx/game_bloom.h): the threshold,
        // the channel, the ring's tap weight (blurTexture's), the intensity and the ring's vertical radius per unit;
        // then the GX state the chain leaves.
        const u8 alpha = 255.0f * mIntensity * get_10();
        const u32 tap = static_cast< u8 >(static_cast< s32 >(255.0f * 1.0f / 8));
        const f32 vertical = MR::isScreen16Per9() ? 1.333f : 1.0f;
        u32 verticalBits;
        __builtin_memcpy(&verticalBits, &vertical, sizeof(verticalBits));
        NebulaRender::bloomMarker(NebulaRender::kBloomSimple,
                              mThreshold | static_cast< u32 >(mMaskFilter & 0xFF) << 8 | tap << 16 | static_cast< u32 >(alpha) << 24,
                              verticalBits);
        MR::reinitGX();
        ImageEffectLocalUtil::setupDrawTexture();
        _28->load(GPU_TEXMAP0);
        J3DNativeState::setChanMatColor(GPU_COLOR0A0, Color8(alpha, alpha, alpha, 255));
        J3DNativeState::setChanAmbColor(GPU_COLOR0A0, Color8(0, 0, 0, 255));
        J3DNativeState::setBlendMode(GPU_BM_BLEND, GPU_BL_ONE, GPU_BL_ONE, GPU_LO_CLEAR);
        return;
    }

    // Every step a pass into its own texture: the frame is read once and never drawn on.
    ImageEffectLocalUtil::capture(_18, 4, 0, GPU_TF_RGB565, true, 0);
    MR::reinitGX();
    ImageEffectLocalUtil::setupDrawTexture();
    beginPass(_1C);
    ImageEffectLocalUtil::drawTexture(_18, 4, 0, 255, ImageEffectLocalUtil::TexDrawType_0);
    J3DNativeTarget::end(_1C->mImage, NEBULA_TARGET_RGB);

    beginPass(_2C);
    if (mMaskFilter == 0) {
        ImageEffectLocalUtil::setStage(ImageEffectLocalUtil::Stage_Luminance);
    }
    ImageEffectLocalUtil::drawTexture(_1C, 4, 0, 255, ImageEffectLocalUtil::TexDrawType_0);
    ImageEffectLocalUtil::setStage(ImageEffectLocalUtil::Stage_Modulate);
    J3DNativeTarget::end(_2C->mImage, mMaskFilter == 2   ? NEBULA_TARGET_GREEN
                                      : mMaskFilter == 3 ? NEBULA_TARGET_BLUE
                                                         : NEBULA_TARGET_RED);

    beginPass(_20);
    ImageEffectLocalUtil::drawTexture(_1C, 4, 0, 255, ImageEffectLocalUtil::TexDrawType_0);
    ImageEffectLocalUtil::setStage(ImageEffectLocalUtil::Stage_Threshold);
    ImageEffectLocalUtil::drawTexture(_2C, 4, 0, mThreshold, ImageEffectLocalUtil::TexDrawType_3);
    ImageEffectLocalUtil::setStage(ImageEffectLocalUtil::Stage_Modulate);
    J3DNativeTarget::end(_20->mImage, NEBULA_TARGET_RGB);

    beginPass(_24);
    ImageEffectLocalUtil::drawTexture(_20, 8, 0, 255, ImageEffectLocalUtil::TexDrawType_0);
    J3DNativeTarget::end(_24->mImage, NEBULA_TARGET_RGB);

    J3DNativeState::setBlendMode(GPU_BM_NONE, GPU_BL_ZERO, GPU_BL_ZERO, GPU_LO_CLEAR);
    ImageEffectLocalUtil::setupDrawTexture();
    beginPass(_28);
    ImageEffectLocalUtil::blurTexture(_24, 8, 0, 8, 0.009f, 1.0f);
    J3DNativeTarget::end(_28->mImage, NEBULA_TARGET_RGB);

    const u8 alpha = 255.0f * mIntensity * get_10();
    ImageEffectLocalUtil::drawTexture(_28, 1, 0, alpha, ImageEffectLocalUtil::TexDrawType_1);
}
