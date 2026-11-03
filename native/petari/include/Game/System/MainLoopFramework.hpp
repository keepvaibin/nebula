#pragma once

#include "Game/Util/Color.hpp"
#include <NebulaGpu.h>
#include <nebula_math.h>

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
    void beginRender();
    void endRender();
    void endFrame();
    void clearFrame(NebulaColor8);
    static void prepareForExit();

    Color8 mClearColor;
    bool mUseAlpha;
    u8 _1A;
    u8 _1B;
    f32 mCombinationRatio;
    s32 mRenderInterval;
    s32 mRenderCounter;
    u8 mDoRenderFrame;
    u8 _35;
    u8 _36;
    u8 _37;
    PreRenderCallback mPreRenderCallback;

    u32 _pad1;
    u32 _pad2;

    static MainLoopFramework* sManager;
};
