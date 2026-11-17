// SPDX-License-Identifier: GPL-3.0-only
// The ray tracer's development tunables: what a test run sets without a build, so a
// look is tuned by runs. A run with --diagnostics (any) reads rt_dev.txt in the settings folder (--settings, else the
// data folder) as ray tracing starts: "name=value" items, any number a line, "#" to the line's end a comment. A name
// not given keeps the code's own number; a player's run (no --diagnostics) reads nothing. The log names each value
// taken ("[rt-dev]") and each item not understood.
#pragma once

#include <cstdint>
#include <string_view>

namespace galaxy::render {

enum class RtDev : std::uint8_t {
    // The host's (rt_walk.cpp).
    Rate,           // the rate the filters and the build budget take, frames a second (0: measured): pinned for
                    // picture runs on a busy machine
    AmbientGain,    // the stage's ambient gain, over its row's (rt_stages.tsv ambient_gain)
    SunGain,        // the stage's sun gain, over its row's (sun_gain)
    AirScale,       // the stage row's air (rt_stages.tsv air) times this
    LightSrgb,      // 1: the lights' colours (display values) taken as linear light of the same luminance; 0 as given
    SunSize,        // the tangent of the sun disc's half angle, over the stage row's (sun_size; kSunTan 0.0131)
    NearField,      // 1: the sky and bounce light's near field at High (rt_near.hlsl); 0 the gather alone
    LandingAll,     // 1: every drop shadow the tracer takes gives the landing cue, not the player's alone
    IndirectDrawn,  // 1: the game's indirect draws that read a picture of the scene are drawn under the traced
                    // transmission as without it (0, their fake refraction left out)
    SunVisible,     // 1 (the default): the sky's visible sun (BrightSun) casts as the sky shows it: no lift, taken again
                    // where it first shows after the key; 0: the key's lifted direction, its angle to the sky's logged
    SunOccluders,   // 1: each sun ray's blocker counted by item and logged every 300 frames ("[rt-occluders]"); 2 the
                    // second layer's pixels by their own item instead; 0 none
    TransmitWhy,    // 1: water's T shows how its refracted walk ended (rt_transmit.hlsl why); 2: each water draw
                    // shows which way its program took (program.hlsli traced_sea_why); 0 neither
    Count,
};

inline constexpr unsigned kRtDevCount = static_cast<unsigned>(RtDev::Count);
static_assert(kRtDevCount <= 64u, "one bit a tunable (RenderCore::rt_dev_set_)");

inline constexpr std::string_view kRtDevNames[kRtDevCount] = {
    "rate",        "ambient_gain",  "sun_gain",      "air_scale",    "light_srgb",     "sun_size",      "near_field",
    "landing_all", "indirect_drawn", "sun_visible",  "sun_occluders", "transmit_why",
};

}  // namespace galaxy::render
