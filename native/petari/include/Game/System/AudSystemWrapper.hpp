#pragma once


class AudSystem;
class NebulaArena;
class NebulaArena;
class AssetFolder;
class NebulaArena;

class AudSystemWrapper {
public:
    AudSystemWrapper(NebulaArena*, NebulaArena*);

    void requestResourceForInitialize();
    void createAudioSystem();
    void createSoundNameConverter();
    void updateRhythm();
    void movement();
    void stopAllSound(u32);
    bool isLoadDoneWaveDataAtSystemInit() const;
    void loadStaticWaveData();
    bool isLoadDoneStaticWaveData() const;
    void loadStageWaveData(const char*, const char*, bool);
    bool isLoadDoneStageWaveData() const;
    void loadScenarioWaveData(const char*, const char*, s32);
    bool isLoadDoneScenarioWaveData() const;
    bool isPermitToReset() const;
    void prepareReset();
    void requestReset(bool);
    bool isResetDone();
    void resumeReset();
    void receiveResourceForInitialize();

    AudSystem* mAudSystem;
    NebulaArena* mSolidHeap;
    NebulaArena* mHeap;
    void* mSmrRes;
    AssetFolder* mJaiSeqRes;
    AssetFolder* mJaiChordRes;
    AssetFolder* mJaiMeRes;
    AssetFolder* mJaiRemixSeqRes;
    NebulaArena* mSpkHeap;
    AssetFolder* mSpkRes;
    bool _28;
    bool mIsResetDone;
    bool mDisableReset;
};
