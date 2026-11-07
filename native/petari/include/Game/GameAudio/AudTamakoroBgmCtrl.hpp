#pragma once


class SoundHandle;

class AudTamakoroBgmCtrl {
public:
    AudTamakoroBgmCtrl();

    void init();
    void control(f32, bool, s32);
    void modifyTempo(SoundHandle*, f32);
    void modifyPitch(SoundHandle*, f32);

    u32 mSteps;
};
