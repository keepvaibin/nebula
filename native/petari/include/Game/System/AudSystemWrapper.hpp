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

    /* 0x00 */ AudSystem* mAudSystem;
    /* 0x04 */ NebulaArena* mSolidHeap;
    /* 0x08 */ NebulaArena* mHeap;
    /* 0x0C */ void* mSmrRes;
    /* 0x10 */ AssetFolder* mJaiSeqRes;
    /* 0x14 */ AssetFolder* mJaiChordRes;
    /* 0x18 */ AssetFolder* mJaiMeRes;
    /* 0x1C */ AssetFolder* mJaiRemixSeqRes;
    /* 0x20 */ NebulaArena* mSpkHeap;
    /* 0x24 */ AssetFolder* mSpkRes;
    /* 0x28 */ bool _28;
    /* 0x29 */ bool mIsResetDone;
    /* 0x2A */ bool mDisableReset;
};
