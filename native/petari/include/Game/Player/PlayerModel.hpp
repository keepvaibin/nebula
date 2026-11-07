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
    // The drawing state flag `flag` of mFlags sets.
    static void sendFlagState(u32 flag, MaterialDraw& rDraw);
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

    struct Flags {
        inline void clear() {
            memset(this, 0, sizeof(*this));
        }

        // Flag i is bit i of the word.
        u32 word() const {
            u32 bits;
            memcpy(&bits, this, sizeof(bits));
            return bits;
        }

        unsigned _1F : 1;
        unsigned _1E : 1;
        unsigned _1D : 1;
        unsigned _1C : 1;
        unsigned _1B : 1;
        unsigned _1A : 1;
        unsigned _19 : 1;
        unsigned _18 : 1;
        unsigned _17 : 1;
        unsigned _16 : 1;
        unsigned _15 : 1;
        unsigned _14 : 1;
        unsigned _13 : 1;
        unsigned _12 : 1;
        unsigned _11 : 1;
        unsigned _10 : 1;
        unsigned _F : 1;
        unsigned _E : 1;
        unsigned _D : 1;
        unsigned _C : 1;
        unsigned _B : 1;
        unsigned _A : 1;
        unsigned _9 : 1;
        unsigned _8 : 1;
        unsigned _7 : 1;
        unsigned _6 : 1;
        unsigned _5 : 1;
        unsigned _4 : 1;
        unsigned _3 : 1;
        unsigned _2 : 1;
        unsigned _1 : 1;
        unsigned _0 : 1;
    };

    u8 _DC;
    u8 _DD;
    ModelPose** mExtraPoses;  // _DD of them; models that copy them share the list
    Flags mFlags;
    // What the next draws take (lighting, alpha); what was set for them.
    const MarioModelTint* _1B4;
    const MarioModelTint* _1B8;
    // Per material: the image it draws with (texture swaps); the one set for its next draws.
    const void** _1C4;
    const void** _1C8;
    u8 _1E4;
    u8 _1E5;
    void (*mDrawHook)(MaterialDraw&);
};
