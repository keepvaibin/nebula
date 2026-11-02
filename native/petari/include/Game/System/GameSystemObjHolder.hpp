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
class GamePadHolder;

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
    u16 getPictureSpan() const {
        return mPictureSpan;
    }
    void initDvd();
    void initNAND();
    void initAudio();
    void initPicture();
    void initNameObj();
    void initFunctionAsyncExecutor();
    void initResourceHolderManager();
    void initGameController();
    void initGamePad();
    void initStarPointerDirector();
    void initDisplay();

    NameObjHolder* mObjHolder;
    ParticleResourceHolder* mParticleResHolder;
    u16 mPictureSpan;
    CaptureScreenDirector* mCaptureScreenDirector;
    ScreenPreserver* mScreenPreserver;
    AudSystemWrapper* mAudioSystem;
    GamePadHolder* mGamePadHolder;
    FunctionAsyncExecutor* mFunctionAsyncExecutor;
    MessageHolder* mMessageHolder;
    StarPointerDirector* mStarPointerDirector;
    JMath::TRandom_fast_ mRandom;
    u32 mLanguage;
};
