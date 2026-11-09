#pragma once

#include "Game/Sound/EffectManager.hpp"

namespace AudParams {
    extern const f32 scCtgVolume[][cEffectCategoryNum];
    extern const f32 outputGain;
    extern const s32 numInspectableSoundObj;
    extern const f32 masterVolBgm;
    extern const f32 masterVolSe;
    extern const f32 masterVolStream;
    extern const CategoryArrangementItem arrangeSE_SY;
    extern const CategoryArrangementItem arrangeSE_PV;
    extern const CategoryArrangementItem arrangeSE_PM;
    extern const CategoryArrangementItem arrangeSE_BV;
    extern const CategoryArrangementItem arrangeSE_BM;
    extern const CategoryArrangementItem arrangeSE_EV;
    extern const CategoryArrangementItem arrangeSE_EM;
    extern const CategoryArrangementItem arrangeSE_SV;
    extern const CategoryArrangementItem arrangeSE_SM;
    extern const CategoryArrangementItem arrangeSE_OJ;
    extern const CategoryArrangementItem arrangeSE_AT;
    extern const CategoryArrangementItem arrangeSE_DM;
    extern const CategoryArrangementItem arrangeSE_RS;
    extern const CategoryArrangementItem arrangeHOMESE;
    extern const f32 faderVolDownVolume;
    extern const f32 faderInterruptedVolume;
    extern const s32 faderVolDownTime;
    extern const s32 faderInterruptedTime;
    extern const s32 faderVolDownRecoverTime;
    extern const s32 faderInterruptedRecoverTime;
    extern const f32 micFarDistance;
    extern const f32 micNearDistance;
    extern const f32 micMinVolume;
    extern const f32 micFrontDolbyZ;
    extern const f32 micBehindDolbyZ;
    extern const f32 micMaxDolby;
    extern const f32 micMinDolby;
    extern const f32 micCenterDolby;
    extern const f32 micPanAmplitude;
    extern const f32 micSonicSpeed;
    extern const f32 micPitchDeltaRatioRange;
    extern const s32 micPriorityDownRange;
};  // namespace AudParams
