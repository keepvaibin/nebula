#pragma once

class SoundArchive;

class AudSceneMgr {
public:
    enum PlayerMode {
        PlayerMode_Mario = 1,
        PlayerMode_Luigi = 2,
    };

    // The bank of the player's voices: a group for each player, with the same waves.
    static const u32 cPlayerVoiceBank = 34;

    explicit AudSceneMgr(SoundArchive*);

    void startScene();
    void setPlayerLuigi(bool isLuigi);

    bool isPlayerModeMario() {
        return mPlayerMode == PlayerMode_Mario;
    }

    bool isPlayerModeLuigi() {
        return mPlayerMode == PlayerMode_Luigi;
    }

    SoundArchive* mAudioData;
    s32 mPlayerMode;
    bool mIsCubeBgmChangeInvalid;
};
