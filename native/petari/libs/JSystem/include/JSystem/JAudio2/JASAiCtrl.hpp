#pragma once

#include "Game/System/NebulaAtomic.hpp"


namespace JASDriver {
    // The sound system runs this many ticks a second: one mix, one step of each sequence and envelope.
    const u32 cTicksPerSecond = 400;
    // The ticks in the unit of time the sequences and resets count in.
    const u32 cTicksPerStep = 7;
    // What one tick moves a vibrato or tremolo and an envelope, as the sound data was made for.
    const f32 cLfoStep = 1.000890625f;
    const f32 cEnvelopeStep = 1.5f;

    void startAudio();
    void startClock();
    void stopClock();
    void updateFrame();
    void mixFrame();
    void updateSubFrame();
    void subframeCallback();
    void syncCallback();

};  // namespace JASDriver
