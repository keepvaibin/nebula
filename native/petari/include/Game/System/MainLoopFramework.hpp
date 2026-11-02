#pragma once

#include "Game/Util/Color.hpp"
#include <NebulaGpu.h>
#include <revolution/mtx.h>
#include <revolution/os.h>

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

    static MainLoopFramework* createManager(bool useAlpha);
    void ctor_subroutine(bool);
    void prepareCopyDisp();
    void preGX();
    void endGX();
    void waitForRetrace();
    void beginRender();
    void endRender();
    void endFrame();
    void waitBlanking(int);
    void clearEfb(NebulaColor8);
    static void setForOSResetSystem();

    /* 0x00 */ Color8 mClearColor;
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
