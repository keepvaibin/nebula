#pragma once

#include <JSystem/JMath/random.hpp>
#include <NebulaGpu.h>
#include <nebula_math.h>

namespace JMath {
    struct TRandom_fast_;
};  // namespace JMath

class AudSystemWrapper;
class CaptureScreenDirector;
class FunctionAsyncExecutor;
class MessageHolder;
class NameObjHolder;
class ParticleResourceHolder;
class ScreenPreserver;
class StarPointerDirector;
class WPadHolder;

class GameSystemObjHolder {
public:
    /// @brief Creates a new `GameSystemObjHolder`.
    GameSystemObjHolder();

    void initAfterStationedResourceLoaded();
    void initMessageResource();
    void init();
    void createAudioSystem();
    void update();
    void updateAudioSystem();
    void clearRequestFileInfo(bool);
    void drawStarPointer();
    void drawBeforeEndRender();
    void captureIfAllowForScreenPreserver();
    // The picture's width on a television's 720-pixel line, as chosen at boot: the safety frame scales from it.
    u16 getViWidth() const {
        return mViWidth;
    }
    void initDvd();
    void initNAND();
    void initAudio();
    void initRenderMode();
    void initNameObj();
    void initFunctionAsyncExecutor();
    void initResourceHolderManager();
    void initGameController();
    void initWPad();
    void initStarPointerDirector();
    void initDisplay();

    /* 0x00 */ NameObjHolder* mObjHolder;
    /* 0x04 */ ParticleResourceHolder* mParticleResHolder;
    u16 mViWidth;
    /* 0x18 */ CaptureScreenDirector* mCaptureScreenDirector;
    /* 0x1C */ ScreenPreserver* mScreenPreserver;
    /* 0x20 */ AudSystemWrapper* mAudioSystem;
    /* 0x24 */ WPadHolder* mWPadHolder;
    /* 0x28 */ FunctionAsyncExecutor* mFunctionAsyncExecutor;
    /* 0x2C */ MessageHolder* mMessageHolder;
    /* 0x30 */ StarPointerDirector* mStarPointerDirector;
    /* 0x34 */ JMath::TRandom_fast_ mRandom;
    /* 0x38 */ u32 mLanguage;
};
