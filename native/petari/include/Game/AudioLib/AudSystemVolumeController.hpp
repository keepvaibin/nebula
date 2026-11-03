#pragma once
#include "Game/Util/Array.hpp"


class AudSystem;

class AudSystemVolumeController {
public:
    enum VolumeSet {
        VolumeSet_0 = 0,
        VolumeSet_1 = 1,
        VolumeSet_2 = 2,
        VolumeSet_3 = 3,
        VolumeSet_ScreenSaver = 4,
        VolumeSet_HomeButtonMenu = 5,
        VolumeSet_PauseMenu = 6,
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
