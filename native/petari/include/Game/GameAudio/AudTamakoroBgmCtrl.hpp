#pragma once


class JAISoundHandle;

class AudTamakoroBgmCtrl {
public:
    AudTamakoroBgmCtrl();

    void init();
    void control(f32, bool, s32);
    void modifyTempo(JAISoundHandle*, f32);
    void modifyPitch(JAISoundHandle*, f32);

    u32 mSteps;
};
