#include "Game/AudioLib/AudParams.hpp"

namespace AudParams {

    // By AudSystemVolumeController::VolumeSet, then effect category.
    const f32 scCtgVolume[][cEffectCategoryNum] = {
        {1.2f, 1.2f, 1.2f, 1.3f, 1.2f, 1.2f, 1.2f, 1.2f, 1.2f, 1.2f, 1.2f, 1.2f, 1.0f, 1.5f, 1.0f, 1.0f},
        {1.2f, 1.2f, 1.2f, 0.3f, 0.3f, 0.3f, 0.3f, 1.2f, 0.3f, 0.3f, 0.3f, 0.3f, 0.3f, 1.5f, 1.0f, 1.0f},
        {1.2f, 0.8f, 0.6f, 0.0f, 0.0f, 0.0f, 0.0f, 1.2f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.5f, 1.0f, 1.0f},
        {1.2f, 0.8f, 0.8f, 1.3f, 0.6f, 0.6f, 0.6f, 1.2f, 0.6f, 0.6f, 1.2f, 0.6f, 0.6f, 1.5f, 1.0f, 1.0f},
        {1.2f, 0.3f, 0.3f, 0.3f, 0.3f, 0.3f, 0.3f, 0.3f, 0.3f, 0.3f, 0.3f, 0.3f, 0.3f, 1.5f, 1.0f, 1.0f},
        {0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.5f, 1.0f, 1.0f},
        {1.2f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.5f, 1.0f, 1.0f},
        {1.2f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.5f, 1.0f, 1.0f},
    };

    const f32 outputGain = 0.9f;
    const s32 numInspectableSoundObj = 8;
    const f32 masterVolBgm = 1.2f;
    const f32 masterVolSe = 1.2f;
    const f32 masterVolStream = 2.6f;
    const CategoryArrangementItem arrangeSE_SY = {32, 64};
    const CategoryArrangementItem arrangeSE_PV = {2, 4};
    const CategoryArrangementItem arrangeSE_PM = {40, 72};
    const CategoryArrangementItem arrangeSE_BV = {8, 12};
    const CategoryArrangementItem arrangeSE_BM = {80, 96};
    const CategoryArrangementItem arrangeSE_EV = {10, 18};
    const CategoryArrangementItem arrangeSE_EM = {96, 112};
    const CategoryArrangementItem arrangeSE_SV = {10, 20};
    const CategoryArrangementItem arrangeSE_SM = {96, 128};
    const CategoryArrangementItem arrangeSE_OJ = {128, 144};
    const CategoryArrangementItem arrangeSE_AT = {64, 80};
    const CategoryArrangementItem arrangeSE_DM = {32, 48};
    const CategoryArrangementItem arrangeSE_RS = {64, 80};
    const CategoryArrangementItem arrangeHOMESE = {8, 16};
    const f32 faderVolDownVolume = 0.3333f;
    const f32 faderInterruptedVolume = 0.0f;
    const s32 faderVolDownTime = 3;
    const s32 faderInterruptedTime = 5;
    const s32 faderVolDownRecoverTime = 20;
    const s32 faderInterruptedRecoverTime = 60;
    const f32 micFarDistance = 7000.0f;
    const f32 micNearDistance = 600.0f;
    const f32 micMinVolume = 0.0f;
    const f32 micFrontDolbyZ = -1000.0f;
    const f32 micBehindDolbyZ = 6000.0f;
    const f32 micMaxDolby = 1.0f;
    const f32 micMinDolby = 0.0f;
    const f32 micCenterDolby = 0.85f;
    const f32 micPanAmplitude = 0.5f;
    const f32 micSonicSpeed = 800.0f;
    const f32 micPitchDeltaRatioRange = 1.5f;
    const s32 micPriorityDownRange = 255;
};  // namespace AudParams
