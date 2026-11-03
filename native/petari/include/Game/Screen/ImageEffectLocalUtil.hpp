#pragma once

#include <NebulaGpuTypes.h>

class BloomEffect;
class JUTTexture;
class NameObj;

namespace ImageEffectLocalUtil {
    enum ETexDrawType {
        TexDrawType_0,
        TexDrawType_1,
        TexDrawType_2,
        TexDrawType_3,
    };

    // The stage the texture draws use: setupDrawTexture's (modulate) or the brightness threshold (red above the
    // material colour's, constant alpha).
    enum EStage {
        Stage_Modulate,
        Stage_Threshold,
        Stage_Luminance,
    };

    void setStage(EStage stage);
    // The fall-out field's passes (runtime/src/render/materials/image_effect.hlsl modes 2-5): its mask by the texture's
    // alpha under or over the constant's, the alpha alone (its blur), the edge colour at the texture's alpha.
    enum EMode {
        Mode_MaskBelow = 2,
        Mode_MaskAbove = 3,
        Mode_Alpha = 4,
        Mode_Edge = 5,
        Mode_QuarterBox = 7,
    };
    void setMode(EMode mode, const NebulaColor8& rKonst, const NebulaColor8& rEdge);
    void setupDrawTexture();
    void drawTexture(JUTTexture* pTexture, s32 divisions, s32 tile, u8 intensity, ETexDrawType texDrawType);
    void sendTextureVertex(s32 divisions, s32 tile);
    // A small texture whose key stands for the frame or its depth in a pass that samples them.
    JUTTexture* createFrameKey();
    // The frame's depth into `pTarget`, half the screen; `pDepthKey` from createFrameKey. Leaves setupDrawTexture's state.
    void drawFrameDepth(JUTTexture* pTarget, JUTTexture* pDepthKey);
    // A Gaussian standing for a ring of `radius` (fractions of the screen), one pass across into `pScratch`, one down
    // into `pTarget`, all of the picture's size (a quarter of the screen). An alpha picture's result is times `alphaGain`,
    // clamped.
    void blurTexture(JUTTexture* pSource, JUTTexture* pScratch, JUTTexture* pTarget, f32 radius, NebulaTargetChannels channels,
                     f32 alphaGain = 1.0f);
};  // namespace ImageEffectLocalUtil

namespace MR {
    void connectToSceneImageEffect(NameObj* pObj);
    void connectToSceneImageEffectMovement(NameObj* pObj);
    void connectToSceneNormalBloom(BloomEffect* pBloomEffect);
};  // namespace MR
