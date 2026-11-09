#pragma once

#include "Game/Render/TypedDraw.hpp"

#include <NebulaGpuTypes.h>

class BloomEffect;
class GameTexture;
class NameObj;

namespace ImageEffectLocalUtil {
    // The draw calls pass 0 to 3 as plain numbers (casts).
    enum ETexDrawType {
        TexDrawType_0,
        TexDrawType_1,
        TexDrawType_2,
        TexDrawType_3,
    };

    // The fall-out field's passes (runtime/src/render/materials/image_effect.hlsl modes 4, 5): the alpha alone (its blur),
    // the edge colour at the texture's alpha.
    enum EMode {
        Mode_Alpha = 4,
        Mode_Edge = 5,
        Mode_QuarterBox = 7,
    };
    void setMode(EMode mode, const NebulaColor8& rEdge);
    void setupDrawTexture();
    // What the draws that follow write: until the next setupDrawTexture, colour and not alpha.
    void setWrites(bool color, bool alpha);
    void drawTexture(GameTexture* pTexture, u8 intensity, ETexDrawType texDrawType);
    void sendTextureVertex(TypedDraw& rDraw);
    // The material over the whole of the target.
    void drawQuad(const SiteMaterial& rMaterial);
    // A small texture whose key stands for the frame or its depth in a pass that samples them.
    GameTexture* createFrameKey();
    // The frame's depth into `pTarget`, half the screen; `pDepthKey` from createFrameKey. Leaves setupDrawTexture's state.
    void drawFrameDepth(GameTexture* pTarget, GameTexture* pDepthKey);
    // A Gaussian standing for a ring of `radius` (fractions of the screen), one pass across into `pScratch`, one down
    // into `pTarget`, all of the picture's size (a quarter of the screen). An alpha picture's result is times `alphaGain`,
    // clamped.
    void blurTexture(GameTexture* pSource, GameTexture* pScratch, GameTexture* pTarget, f32 radius, NebulaTargetChannels channels,
                     f32 alphaGain = 1.0f);
};  // namespace ImageEffectLocalUtil

namespace MR {
    void connectToSceneImageEffect(NameObj* pObj);
    void connectToSceneImageEffectMovement(NameObj* pObj);
    void connectToSceneNormalBloom(BloomEffect* pBloomEffect);
};  // namespace MR
