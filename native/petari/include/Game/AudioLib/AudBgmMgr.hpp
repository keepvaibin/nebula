#pragma once

#include "Game/AudioLib/AudBgmKeeper.hpp"
#include "Game/AudioLib/AudBgmVolumeController.hpp"

class SoundHandle;

class AudBgmMgr {
public:
    enum BgmType {
        BgmType_Stage = 0,
        BgmType_Sub = 1,
    };

    AudBgmMgr();

    void movement();
    SoundHandle* start(s32, u32, bool);
    void setNextBGM(s32, u32);
    void clearNextBGM(s32);
    SoundHandle* startLastBGM(s32);
    void clearLastBGM(s32);
    void pause();
    void unpause();
    void volDownLevel(bool);
    void volDownStageBgmWhenSubBgmPlaying();
    void startNextBgmWhenStopping(s32);
    void releaseStoppingBgm(s32);
    bool sendToRhythmSystem(s32);
    void setBgmToRhythmDominant(s32);
    void stopRhythmProcess(s32);

    AudBgm* mBgm[2];
    u32 mNextBGM[2];
    u32 mCurrentBGM[2];
    u32 mLastBGM[2];
    AudBgmKeeper mKeeper;
    AudBgmVolumeController mVolumeController[2];
};
