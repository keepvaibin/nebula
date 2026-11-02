#pragma once

#include <NebulaGpuEnums.h>
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
    // material colour's, konst alpha).
    enum EStage {
        Stage_Modulate,
        Stage_Threshold,
        Stage_Luminance,
    };

    // `filtered`: through the display's vertical filter (NEBULA_CAPTURE_FILTERED).
    void setStage(EStage stage);
    // The fall-out field's passes (runtime/src/render/materials/image_effect.hlsl modes 2-5): its mask by the texture's
    // alpha under or over the constant's, the alpha alone (its blur), the edge colour at the texture's alpha.
    enum EMode {
        Mode_MaskBelow = 2,
        Mode_MaskAbove = 3,
        Mode_Alpha = 4,
        Mode_Edge = 5,
    };
    void setMode(EMode mode, const NebulaColor8& rKonst, const NebulaColor8& rEdge);
    void setupDrawTexture();
    void drawTexture(JUTTexture* pTexture, s32 divisions, s32 tile, u8 intensity, ETexDrawType texDrawType);
    void sendTextureVertex(s32 divisions, s32 tile);
    void blurTexture(JUTTexture* pTexture, s32 divisions, s32 tile, u32 sampleCount, f32 radius, f32 intensity);
};  // namespace ImageEffectLocalUtil

namespace MR {
    void connectToSceneImageEffect(NameObj* pObj);
    void connectToSceneImageEffectMovement(NameObj* pObj);
    void connectToSceneNormalBloom(BloomEffect* pBloomEffect);
};  // namespace MR
