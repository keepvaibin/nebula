#include "Game/Screen/BloomEffectSimple.hpp"
#include <JSystem/J3DGraphBase/J3DNativeState.hpp>
#include "Game/Screen/CopyFilterNegater.hpp"
#include "Game/Screen/ImageEffectLocalUtil.hpp"
#include "Game/Screen/ImageEffectResource.hpp"
#include "Game/Screen/ImageEffectSystemHolder.hpp"
#include "Game/Util/DrawUtil.hpp"
#ifdef TARGET_PC
#include "Game/Util/Color.hpp"
#include "Game/Util/SystemUtil.hpp"
#include <JSystem/JUtility/JUTTexture.hpp>
#include <nebula_game_api.h>
#include <nebula_gx_fifo.h>
#include <revolution/os.h>
#endif

namespace {
    // static const s32 sLen = _;
    // static const s32 sDivNum = _;
};  // namespace

namespace {
    class TexSpec {
    public:
        TexSpec() {
        }

        void set(JUTTexture*, s32, s32, GXTexFmt);

        /* 0x00 */ JUTTexture* _0;
        /* 0x04 */ s32 _4;
        /* 0x08 */ s32 _8;
        /* 0x0C */ GXTexFmt _C;
    };
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

#ifdef TARGET_PC
    const u32 route = __NebulaGXLinked != 0 ? __NebulaOption(NEBULA_OPTION_BLOOM) : 0;
    if (route == 2) {
        // The chain below, drawn by the renderer at the internal resolution (galaxy/gx/game_bloom.h): the threshold,
        // the channel, the ring's tap weight (blurTexture's), the intensity and the ring's vertical radius per unit;
        // then the GX state the chain leaves.
        const u8 alpha = 255.0f * mIntensity * get_10();
        const u32 tap = static_cast< u8 >(static_cast< s32 >(255.0f * 1.0f / 8));
        const f32 vertical = MR::isScreen16Per9() ? 1.333f : 1.0f;
        u32 verticalBits;
        __builtin_memcpy(&verticalBits, &vertical, sizeof(verticalBits));
        __NebulaGXBloomMarker(NEBULA_GX_BLOOM_SIMPLE,
                              mThreshold | static_cast< u32 >(mMaskFilter & 0xFF) << 8 | tap << 16 | static_cast< u32 >(alpha) << 24,
                              verticalBits);
        MR::reinitGX();
        ImageEffectLocalUtil::setupDrawTexture();
        _28->load(GX_TEXMAP0);
        J3DNativeState::setChanMatColor(GX_COLOR0A0, Color8(alpha, alpha, alpha, 255));
        J3DNativeState::setChanAmbColor(GX_COLOR0A0, Color8(0, 0, 0, 255));
        J3DNativeState::setBlendMode(GX_BM_BLEND, GX_BL_ONE, GX_BL_ONE, GX_LO_CLEAR);
        return;
    }
#endif

    CopyFilterNegater copyFilterNegator;

    ::TexSpec texSpec;
    texSpec.set(_20, 4, 0, GX_TF_RGB565);

#ifdef TARGET_PC
    // The renderer keeps the scene in the EFB's own format: the corner the chain works in is saved as a texture copy,
    // which cuts the ray tracer's HDR scene at 1 there.
    if (route != 0) {
        __NebulaGXBloomMarker(NEBULA_GX_BLOOM_KEEP, 0, 0);
    } else
#endif
    {
        ImageEffectLocalUtil::capture(texSpec._0, texSpec._4, texSpec._8, texSpec._C, false, 0);
    }
    ImageEffectLocalUtil::capture(_18, texSpec._4, texSpec._8, GX_TF_RGB565, true, 0);
    MR::reinitGX();
    ImageEffectLocalUtil::setupDrawTexture();
    ImageEffectLocalUtil::drawTexture(_18, 4, 0, 255, ImageEffectLocalUtil::TexDrawType_0);
    ImageEffectLocalUtil::capture(_1C, 4, 0, GX_TF_RGB565, false, 0);

    switch (mMaskFilter) {
    case 0:
        ImageEffectLocalUtil::capture(_2C, 4, 0, GX_TF_I8, false, 0);
        break;
    case 1:
        ImageEffectLocalUtil::capture(_2C, 4, 0, GX_CTF_R8, false, 0);
        break;
    case 2:
        ImageEffectLocalUtil::capture(_2C, 4, 0, GX_CTF_G8, false, 0);
        break;
    case 3:
        ImageEffectLocalUtil::capture(_2C, 4, 0, GX_CTF_B8, false, 0);
        break;
    }

    J3DNativeState::setTevOrder(GX_TEVSTAGE0, GX_TEXCOORD0, GX_TEXMAP0, GX_COLOR0A0);
    J3DNativeState::setTevColorIn(GX_TEVSTAGE0, GX_CC_TEXC, GX_CC_RASC, GX_CC_ONE, GX_CC_ZERO);
    J3DNativeState::setTevColorOp(GX_TEVSTAGE0, GX_TEV_COMP_R8_GT, GX_TB_ZERO, GX_CS_SCALE_1, GX_TRUE, GX_TEVPREV);
    J3DNativeState::setTevAlphaIn(GX_TEVSTAGE0, GX_CA_ZERO, GX_CA_ZERO, GX_CA_ZERO, GX_CA_KONST);
    J3DNativeState::setTevAlphaOp(GX_TEVSTAGE0, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1, GX_TRUE, GX_TEVPREV);
    ImageEffectLocalUtil::drawTexture(_2C, 4, 0, mThreshold, ImageEffectLocalUtil::TexDrawType_3);
    ImageEffectLocalUtil::capture(_24, 4, 0, GX_TF_RGB565, true, 0);
    J3DNativeState::setBlendMode(GX_BM_NONE, GX_BL_ZERO, GX_BL_ZERO, GX_LO_CLEAR);
    J3DNativeState::setTevOrder(GX_TEVSTAGE0, GX_TEXCOORD0, GX_TEXMAP0, GX_COLOR0A0);
    J3DNativeState::setTevOp(GX_TEVSTAGE0, GX_MODULATE);
    ImageEffectLocalUtil::setupDrawTexture();
    texSpec.set(_24, 8, 0, GX_TF_RGB565);
    ImageEffectLocalUtil::blurTexture(texSpec._0, texSpec._4, texSpec._8, 8, 0.009f, 1.0f);
    ImageEffectLocalUtil::capture(_28, texSpec._4, texSpec._8, texSpec._C, false, 0);
#ifdef TARGET_PC
    if (route != 0) {
        // The kept scene drawn back over the corner the chain worked in (the rest of the frame is unchanged).
        __NebulaGXBloomMarker(NEBULA_GX_BLOOM_RESTORE, 0, 0);
    } else
#endif
    {
        ImageEffectLocalUtil::drawTexture(_20, 4, 0, 255, ImageEffectLocalUtil::TexDrawType_0);
    }
    const u8 alpha = 255.0f * mIntensity * get_10();
    ImageEffectLocalUtil::drawTexture(_28, 1, 0, alpha, ImageEffectLocalUtil::TexDrawType_1);
}

void ::TexSpec::set(JUTTexture* pParam1, s32 param2, s32 param3, GXTexFmt param4) {
    _0 = pParam1;
    _4 = param2;
    _8 = param3;
    _C = param4;
}
