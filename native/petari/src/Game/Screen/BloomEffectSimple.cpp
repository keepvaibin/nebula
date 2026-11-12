#include "Game/Screen/BloomEffectSimple.hpp"
#include "Game/Render/FrameList.hpp"
#include "Game/Screen/ImageEffectLocalUtil.hpp"
#include "Game/Screen/ImageEffectSystemHolder.hpp"
#include "Game/Util/DrawUtil.hpp"
#include "Game/Util/Color.hpp"
#include "Game/Util/SystemUtil.hpp"
#include <nebula_game_api.h>
#include "Game/System/NebulaHost.hpp"
#include "Game/System/LumaCore.hpp"

BloomEffectSimple::BloomEffectSimple()
    : ImageEffectBase("簡易ブルーム"), mMaskFilter(), mThreshold(205), mIntensity(0.3f) {
    lumacore::setClass(this, lumacore::kClassId< BloomEffectSimple >);
    MR::connectToSceneImageEffect(this);
    MR::createImageEffectSystemHolder();
}

void BloomEffectSimple::draw() const {
    if (!isSomething() || NebulaHost::option(NEBULA_OPTION_BLOOM) == 0) {
        return;
    }

    // Drawn by the renderer at the frame's own size: the threshold, the channel, the ring's tap weight, the intensity
    // and the ring's vertical radius per unit.
    const u8 alpha = 255.0f * mIntensity * getFadeLevel();
    const u32 tap = static_cast< u8 >(static_cast< s32 >(255.0f * 1.0f / 8));
    const f32 vertical = MR::isScreen16Per9() ? 1.333f : 1.0f;
    u32 verticalBits;
    __builtin_memcpy(&verticalBits, &vertical, sizeof(verticalBits));
    Frame::bloom(NEBULA_PASS_BLOOM_SIMPLE,
                          mThreshold | static_cast< u32 >(mMaskFilter & 0xFF) << 8 | tap << 16 | static_cast< u32 >(alpha) << 24,
                          verticalBits);
    MR::resetDrawState();
    ImageEffectLocalUtil::setupDrawTexture();
}
