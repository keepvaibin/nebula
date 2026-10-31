#pragma once

#include <revolution/gx/GXEnum.h>

class BloomEffect;
class JUTTexture;
class NameObj;
struct NebulaMaterialTevStage;

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
    };

    void capture(JUTTexture* pTexture, s32 divisions, s32 tile, GXTexFmt format, bool clear, u8 filter);
    void setStage(EStage stage);
    // A stage of the caller's own (its address the key: a static), with setupDrawTexture's order (texture 0,
    // coordinate 0, colour channel 0); its registers and konst colours stay as the caller set them.
    void setCallerStage(const NebulaMaterialTevStage* pStage);
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
