#include "Game/Screen/BloomEffectSimple.hpp"
#include <JSystem/J3DGraphBase/J3DNativeState.hpp>
#include "Game/Screen/ImageEffectLocalUtil.hpp"
#include "Game/Screen/ImageEffectSystemHolder.hpp"
#include "Game/Util/DrawUtil.hpp"
#include "Game/Util/Color.hpp"
#include "Game/Util/SystemUtil.hpp"
#include <nebula_game_api.h>
#include "Game/Util/RenderSignals.hpp"
#include "Game/System/NebulaHost.hpp"

BloomEffectSimple::BloomEffectSimple()
    : ImageEffectBase("\212\310\210\325\203\165\203\213\201\133\203\200" /* 簡易ブルーム */), mMaskFilter(), mThreshold(205), mIntensity(0.3f) {
    MR::connectToSceneImageEffect(this);
    MR::createImageEffectSystemHolder();
}

void BloomEffectSimple::draw() const {
    if (!isSomething() || NebulaHost::option(NEBULA_OPTION_BLOOM) == 0) {
        return;
    }

    // Drawn by the renderer at the frame's own size: the threshold, the channel, the ring's tap weight, the intensity
    // and the ring's vertical radius per unit.
    const u8 alpha = 255.0f * mIntensity * get_10();
    const u32 tap = static_cast< u8 >(static_cast< s32 >(255.0f * 1.0f / 8));
    const f32 vertical = MR::isScreen16Per9() ? 1.333f : 1.0f;
    u32 verticalBits;
    __builtin_memcpy(&verticalBits, &vertical, sizeof(verticalBits));
    NebulaRender::bloomMarker(NebulaRender::kBloomSimple,
                          mThreshold | static_cast< u32 >(mMaskFilter & 0xFF) << 8 | tap << 16 | static_cast< u32 >(alpha) << 24,
                          verticalBits);
    MR::resetDrawState();
    ImageEffectLocalUtil::setupDrawTexture();
    J3DPass::blend(NEBULA_FACTOR_ONE, NEBULA_FACTOR_ONE);
}
