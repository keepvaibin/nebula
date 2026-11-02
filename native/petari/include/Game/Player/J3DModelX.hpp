#pragma once

#include <string.h>
#include <JSystem/JGeometry/TVec.hpp>

#include <JSystem/J3DGraphAnimator/J3DModel.hpp>
#include "Game/Player/MarioModelTint.hpp"

class J3DShape;
class J3DShapeX;
class J3DShapePacketX;

class J3DModelX : public J3DModel {
public:
    J3DModelX(J3DModelData*, u32, u32);

    virtual ~J3DModelX();

    void viewCalc2();
    void viewCalc3(u32, MtxPtr);
    void viewCalcRef(u32, J3DModel*);
    void viewCalcRefPos(u32, J3DModel*, const TVec3f&, const TVec3f&);
    bool simpleDrawSetup(J3DMaterial*);
    void simpleDrawShape(J3DMaterial*);
    // The drawing state flag `flag` of mFlags sets.
    static void sendFlagState(u32 flag);
    void shapePacketDrawFast(J3DShapePacketX*) const;
    void shapeDrawFast(J3DShapeX*) const;
    void copyExtraMtxBuffer(const J3DModelX*);
    void copyAnmMtxBuffer(const J3DModelX*);
    void swapDrawBuffer(u32);
    void setDynamicList(const MarioModelTint* pTint);
    void setDrawViewBuffer(MtxPtr);
    void setDrawView(u32);
    void directDraw(J3DModel*);
    void drawIn(J3DMaterial*, bool, MtxPtr, J3DModel*);

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
    union {
        struct {
            Mtx* _E0;
            Mtx* _E4;
            Mtx* _E8;
            Mtx* _EC;
            Mtx* _F0;
            Mtx* _F4;
            Mtx* _F8;
            Mtx* _FC;
            Mtx* _100;
            Mtx* _104;
            Mtx* _108;
            Mtx* _10C;
            Mtx* _110;
            Mtx* _114;
            Mtx* _118;
            Mtx* _11C;
        };
        Mtx* mExtraMtxBuffer[16];
    };
    void (*_120)(void*, u16);
    void (*mShapeCallback)(J3DShape*);
    void* _128;
    J3DModel* _12C;
    Flags mFlags;
    // The dynamic state list (lighting, alpha) the next draws use; the one set for them.
    const MarioModelTint* _1B4;
    const MarioModelTint* _1B8;
    u16 _1C0;
    // Per material: the state list it draws with (texture swaps); the one set for its next draws.
    const void** _1C4;
    const void** _1C8;
    u8 _1D0;
    f32 _1D4;
    u32 _1D8;
    u32 _1DC;
    s32 _1E0;
    u8 _1E4;
    u8 _1E5;
};
