#pragma once

#include <JSystem/JMath/random.hpp>
#include <NebulaGpu.h>
#include <nebula_math.h>

namespace JMath {
    class TRandom_fast_;
};  // namespace JMath

class AudSystemWrapper;
class CaptureScreenDirector;
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
    void drawStarPointer();
    void drawBeforeEndRender();
    void captureIfAllowForScreenPreserver();
    // The picture's width on a television's 720-pixel line, as chosen at boot: the safety frame scales from it.
    void initStore();
    void initAudio();
    void initPicture();
    void initNameObj();
    void initResourceHolderManager();
    void initGameController();
    void initPlayerInput();
    void initStarPointerDirector();
    void initDisplay();

    NameObjHolder* mObjHolder;
    ParticleResourceHolder* mParticleResHolder;
    CaptureScreenDirector* mCaptureScreenDirector;
    ScreenPreserver* mScreenPreserver;
    AudSystemWrapper* mAudioSystem;
    PlayerInputs* mPlayerInputs;
    MessageHolder* mMessageHolder;
    StarPointerDirector* mStarPointerDirector;
    JMath::TRandom_fast_ mRandom;
    u32 mLanguage;
};
