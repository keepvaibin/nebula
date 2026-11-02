#pragma once


class JAUSectionHeap;

struct AudSceneSoundInfo {
    const char* mSceneName;
    const char* mStageName;
    s8 mStageId;
    s8 mScenarioId;
};

class AudSceneMgr {
public:
    enum PlayerMode {
        PlayerMode_Mario = 1,
        PlayerMode_Luigi = 2,
    };

    AudSceneMgr(JAUSectionHeap*);

    bool isLoadDoneSystemInit();
    void loadStaticResource();
    bool isLoadDoneStaticResource();
    void loadStageResource(const char*, const char*);
    void loadStageResource(const AudSceneSoundInfo*);
    bool isLoadDoneStageResource();
    void loadScenarioResource(const char*, const char*, s32);
    void loadScenarioResource(const AudSceneSoundInfo*, s32);
    bool isLoadDoneScenarioResource();
    void startScene();
    const AudSceneSoundInfo* findSceneSoundInfo(const char*, const char*);
    void eraseLastBgmWaveSet();
    void eraseLastSeWaveSet();
    void eraseLastSeScenarioWaveSet();
    void loadWaveSet(const s8*, s32);
    void setPlayerModeMario();
    void setPlayerModeLuigi();
    bool loadPlayerResource();
    bool isPlayerResourceLoaded();

    bool isPlayerModeMario() {
        return mPlayerMode == PlayerMode_Mario;
    }

    bool isPlayerModeLuigi() {
        return mPlayerMode == PlayerMode_Luigi;
    }

    JAUSectionHeap* mSectionHeap;
    u32 _4;
    s32 mSeWaveSetId;
    s32 mSeScenarioWaveSetId;
    s32 mBgmWaveSetId;
    s32 mPlayerMode;
    s32 mPrevPlayerMode;
    bool mIsNewPlayerMode;
    bool _1D;
};
