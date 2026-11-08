#pragma once


class AudSystem;
class NebulaArena;
class AssetFolder;

class AudSystemWrapper {
public:
    explicit AudSystemWrapper(NebulaArena*);

    void createAudioSystem();
    void createSoundNameConverter();
    void movement();
    void stopAllSound(u32);
    void setPlayerLuigi(bool);
    bool isCreated() const {
        return mAudSystem != nullptr;
    }
    void prepareReset();
    void requestReset(bool);
    bool isResetDone();
    void resumeReset();
    void receiveResourceForInitialize();

    AudSystem* mAudSystem;
    NebulaArena* mArena;
    AssetFolder* mSeqArchive;
    AssetFolder* mChordArchive;
    AssetFolder* mMeArchive;
    AssetFolder* mRemixArchive;
    NebulaArena* mSpeakerArena;
    AssetFolder* mSpkRes;
    bool mIsResetDone;
};
