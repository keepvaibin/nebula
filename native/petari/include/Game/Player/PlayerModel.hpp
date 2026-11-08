#pragma once

#include <string.h>
#include <nebula_geometry.hpp>

#include "Game/Player/MarioModelTint.hpp"
#include "Game/Render/Model.hpp"

// A model of the player's: more than one set of matrices at a time (the main view, its reflection, its shadows), and the
// passes its draws go through instead of a draw buffer.
class PlayerModel : public Model {
public:
    PlayerModel(ModelData* pData, u32 flags, u32 unused);
    virtual ~PlayerModel();

    void viewCalc2();
    void viewCalc3(u32 view, const f32 (*pMtx)[4]);
    void viewCalcRef(u32 view, Model* pModel);
    void viewCalcRefPos(u32 view, Model* pModel, const Vec3f& rPos, const Vec3f& rNormal);
    bool simpleDrawSetup(Material* pMaterial, MaterialDraw* pDraw);
    void simpleDrawShape(Material* pMaterial, MaterialDraw& rDraw);
    // The drawing state pass `pass` (one DrawPass bit) sets.
    static void sendPassState(u32 pass, MaterialDraw& rDraw);
    void initExtraPoses(u8 count);
    void copyExtraPoses(const PlayerModel* pModel);
    void copyAnmMtxBuffer(const PlayerModel* pModel);
    void swapDrawBuffer(u32 drawBuffer);
    void setDynamicList(const MarioModelTint* pTint);
    void setDrawView(u32 view);
    void directDraw();
    void drawIn(Material* pMaterial, u32 index);
    // Called on each draw just before it is added.
    void setDrawHook(void (*pHook)(MaterialDraw&)) {
        mDrawHook = pHook;
    }

    // The passes the next draws take, as bits; drawIn adds each one's state (sendPassState) and its flat colour.
    enum DrawPass : u32 {
        Pass_ShadowCapture = 1u << 1,      // a flat alpha of 1 (1/4 with Pass_ShadowLight), no depth test: the capture
        Pass_AddTexture = 1u << 2,         // texture 0 at a quarter alpha, added
        Pass_BehindMark = 1u << 3,         // where the model is behind a surface: marked in the stencil
        Pass_DepthOnly = 1u << 5,          // depth only, written everywhere
        Pass_ScreenCoordinates = 1u << 6,  // texture coordinates from the screen image
        Pass_Reflection = 1u << 11,        // the reflection model
        Pass_BlurGhost = 1u << 12,         // the blur ghosts, in the ghost tint's colour
        Pass_ShadowLight = 1u << 14,       // with Pass_ShadowCapture
        Pass_OutsideDarkMask = 1u << 15,   // only where the dark mask did not mark
    };

    u8 mPoseBuffer;  // 0 or the draw buffer last swapped in: the half of the extra poses the views use
    ModelPose** mExtraPoses;  // models that copy them share the list
    u32 mDrawPasses;  // DrawPass bits
    // What the next draws take (lighting, alpha); what was set for them.
    const MarioModelTint* mTint;
    const MarioModelTint* mNextTint;
    // Per material: the image it draws with (texture swaps); the one set for its next draws.
    const void** mImages;
    const void** mNextImages;
    u8 mBlurPoseFresh;   // the blur took a pose since its last draw
    u8 mBlurDrawMissed;  // the blur drew with no pose taken: it starts over
    void (*mDrawHook)(MaterialDraw&);
};
