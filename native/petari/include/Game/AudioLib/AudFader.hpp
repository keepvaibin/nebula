#pragma once


class AudFader {
public:
    enum FadeState {
        FadeState_FadeOut = 0,
        FadeState_FadeIn = 1,
        FadeState_Mute = 2,
        FadeState_Unmute = 3,
    };

    AudFader();

    void set(f32 desiredVolume, s32 fadeTime);
    void update();

    f32 getVolume() {
        return mCurrentVolume;
    }

    f32 mCurrentVolume;
    f32 mFinalVolume;
    f32 mStepVolume;  ///< Volume intensity increase/decrease per AudFader::update() call until mCurrentVolume reaches mFinalVolume value
};
