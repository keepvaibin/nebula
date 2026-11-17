// SPDX-License-Identifier: GPL-3.0-only
// The passes' counters (rt_backend.h kCounters, read back by the host a frame slot later: rt_walk.cpp rt_report, the
// exposure's meter; rt_bounce.cpp rt_dome_read): u8, bound where a pass's binds ask for it (PassBinds::counters).
#ifndef NEBULA_RT_COUNTERS_HLSLI
#define NEBULA_RT_COUNTERS_HLSLI

RWStructuredBuffer<uint> g_counters : register(u8);

// A count into the counters: one atomic a wave, not one a thread (the same sums; every lane of a wave would otherwise
// queue on the one address).
void rt_count(uint counter) {
    // The ballot's four words counted, not WaveActiveCountBits: SPIRV-Cross's MSL helper for that does not compile on
    // iOS (vulkan-sdk-1.4.350.1). The same count on every API for any wave up to 128 lanes.
    const uint4 ballot = WaveActiveBallot(true);
    const uint lanes = countbits(ballot.x) + countbits(ballot.y) + countbits(ballot.z) + countbits(ballot.w);
    if (WaveIsFirstLane()) {
        InterlockedAdd(g_counters[counter], lanes);
    }
}
void rt_count_sum(uint counter, uint value) {
    const uint sum = WaveActiveSum(value);
    if (WaveIsFirstLane()) {
        InterlockedAdd(g_counters[counter], sum);
    }
}

#endif
