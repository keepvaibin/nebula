#pragma once

#include <JSystem/JMath/random.hpp>
#include <NebulaGpu.h>
#include <nebula_math.h>

namespace JMath {
    class TRandom_fast_;
};  // namespace JMath

class AudSystemWrapper;
class CaptureScreenDirector;
class FunctionAsyncExecutor;
class MessageHolder;
class NameObjHolder;
class ParticleResourceHolder;
class ScreenPreserver;
class StarPointerDirector;
class PlayerInputs;

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
    void initStore();
    void initNAND();
    void initAudio();
    void initPicture();
    void initNameObj();
    void initFunctionAsyncExecutor();
    void initResourceHolderManager();
    void initGameController();
    void initPlayerInput();
    void initStarPointerDirector();
    void initDisplay();

    NameObjHolder* mObjHolder;
    ParticleResourceHolder* mParticleResHolder;
    u16 mPictureSpan;
    CaptureScreenDirector* mCaptureScreenDirector;
    ScreenPreserver* mScreenPreserver;
    AudSystemWrapper* mAudioSystem;
    PlayerInputs* mPlayerInputs;
    FunctionAsyncExecutor* mFunctionAsyncExecutor;
    MessageHolder* mMessageHolder;
    StarPointerDirector* mStarPointerDirector;
    JMath::TRandom_fast_ mRandom;
    u32 mLanguage;
};
