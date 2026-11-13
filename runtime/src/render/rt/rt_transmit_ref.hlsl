// SPDX-License-Identifier: GPL-3.0-only
// The reference's T (the master plan's section 18; FB): rt_transmit.hlsl's paths through glass, crystal, ice and
// water, unchanged, and where they land the light by path tracing as the reference's (rt_reference_paths.hlsli
// ref_mirror), the mean of the walk's paths; summed over walks while nothing moves (t12 the walk before's sums,
// u1 this walk's: rgb the mean, a the samples). Writes T as the transmit pass does (rgb what arrives through the
// surface toward the eye, a 1 where traced). Pass constants: g_pass.z bit 0 start again; g_pass.w the shading
// frame's first byte; g_pass_value[0].x paths a walk, .y the most samples kept.
#define RT_TRANSMIT_REFERENCE 1
#include "rt_transmit.hlsl"
