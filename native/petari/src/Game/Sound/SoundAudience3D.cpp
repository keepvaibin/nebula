#include "Game/Sound/SoundAudience3D.hpp"
#include "Game/Sound/SoundAudibleParam.hpp"
#include "Game/Util/Tunable.hpp"

NEBULA_TUNABLE_NAMED(f32, cNum_init_f1, "init.f1", 7000.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_init_f2, "init.f2", 800.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_init_f3, "init.f3", 0.005f);
NEBULA_TUNABLE_NAMED(f32, cNum_init_f4, "init.f4", 8000.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_init_f5, "init.f5", 4000.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_init_f6, "init.f6", 0.1f);
NEBULA_TUNABLE_NAMED(f32, cNum_init_f7, "init.f7", 0.8f);
NEBULA_TUNABLE_NAMED(f32, cNum_init_f8, "init.f8", 0.5f);
NEBULA_TUNABLE_NAMED(f32, cNum_init_f9, "init.f9", 3000.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_init_f10, "init.f10", 1.5f);

void SoundAudience3DSetting::init() {
    mFarDistance = cNum_init_f1;
    mNearDistance = cNum_init_f2;
    mMinVolume = cNum_init_f3;
    mBehindDolbyZ = cNum_init_f4;
    mFrontDolbyZ = -cNum_init_f5;
    mMaxFxMix = cNum_init_f6;
    mMinFxMix = 0.0f;
    mMaxDolby = 1.0f;
    mCenterDolby = cNum_init_f7;
    mMinDolby = 0.0f;
    mPanAmplitude = cNum_init_f8;
    mPriorityDownRange = 255;
    mSonicSpeed = cNum_init_f9;
    mPitchDeltaRatioRange = cNum_init_f10;
}

