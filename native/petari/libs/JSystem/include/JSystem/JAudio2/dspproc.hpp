#pragma once

#include <revolution/types.h>

void DSPReleaseHalt2(u32 msg);
void DsetupTable(u32 param_0, uintptr_t param_1, uintptr_t param_2, uintptr_t param_3, uintptr_t param_4);
void DsetMixerLevel(f32 level);
void DsyncFrame2ch(u32 param_0, uintptr_t param_1, uintptr_t param_2);
void DsyncFrame4ch(u32 param_0, uintptr_t param_1, uintptr_t param_2, uintptr_t param_3, uintptr_t param_4);
void DsetVARAM(uintptr_t param_0);
