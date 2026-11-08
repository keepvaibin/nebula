#pragma once

#include <nebula_geometry.hpp>
#include <NebulaGpu.h>
#include <nebula_math.h>

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
    GameSystemObjHolder();

    void initAfterSystemArchiveLoaded();
    void initMessageResource();
    void init();
    void createAudioSystem();
    void update();
    void updateAudioSystem();
    void drawStarPointer();
    void drawBeforeEndRender();
    void captureIfAllowForScreenPreserver();
    // The picture's width on a television's 720-pixel line, as chosen at boot: the safety frame scales from it.
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
    FastRandom mRandom;
    u32 mLanguage;
};
