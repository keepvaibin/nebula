#pragma once
#include "Game/Util/Array.hpp"


class AudSystem;

class AudSystemVolumeController {
public:
    enum VolumeSet {
        VolumeSet_Normal = 0,
        VolumeSet_StageClear = 1,
        VolumeSet_Miss = 2,  // the player's down and game-over events
        VolumeSet_Talk = 3,
        VolumeSet_ScreenSaver = 4,
        VolumeSet_HomeButtonMenu = 5,
        VolumeSet_PauseMenu = 6,
        VolumeSet_MissEnd = 7,  // system sounds only
    };

    AudSystemVolumeController(AudSystem*);

    void init();
    void update();
    void setSeVolumeSetTrig(s32, u32);
    void recoverSeVolumeSet(u32);
    void updateSeVolumeSetLevel();
    void setSeVolumeSetInner(s32, u32);
    void pushVolumeSet();
    void popVolumeSet();

    AudSystem* mSystem;
    s32 mVolumeSetDelay;
    s32 mCurrVolumeSet;
    MR::GrowTable< s32, 8 > mVolumeSets;
    s32 mNumVolumeSets;
};
