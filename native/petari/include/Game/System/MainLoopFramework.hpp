#pragma once

#include "Game/Util/Color.hpp"
#include <revolution.h>

typedef void (*PreRenderCallback)();

class MainLoopFramework {
public:
    void setClearColor(Color8 color) {
        mClearColor = color;
    }

    MainLoopFramework(bool useAlpha) : mClearColor(0xffffffff) {
        ctor_subroutine(useAlpha);
    }

    ~MainLoopFramework();

    static MainLoopFramework* getManager() {
        return sManager;
    }

    static MainLoopFramework* createManager(const GXRenderModeObj*, bool);
    void ctor_subroutine(bool);
    void prepareCopyDisp();
    void preGX();
    void endGX();
    void waitForRetrace();
    void beginRender();
    void endRender();
    void endFrame();
    void waitBlanking(int);
    void clearEfb(GXColor);
    void clearEfb(int, int, int, int, GXColor);
    static void setForOSResetSystem();

    /* 0x00 */ Color8 mClearColor;
    /* 0x04 */ u32 mClearZ;
    /* 0x08 */ u16 mDispCopyGamma;
    /* 0x0A */ u16 mCopyClamp;
    /* 0x10 */ u16 mRetraceCount;
    /* 0x18 */ bool mUseAlpha;
    /* 0x19 */ bool mUseVFilter;
    u8 _1A;
    u8 _1B;
    /* 0x1C */ f32 mCombinationRatio;
    /* 0x2C */ s32 mRenderInterval;
    /* 0x30 */ s32 mRenderCounter;
    /* 0x34 */ u8 mDoRenderFrame;
    u8 _35;
    u8 _36;
    u8 _37;
    /* 0x38 */ PreRenderCallback mPreRenderCallback;

    u32 _pad1;
    u32 _pad2;

    static MainLoopFramework* sManager;
};
