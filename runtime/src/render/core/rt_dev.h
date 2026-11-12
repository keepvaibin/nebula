// SPDX-License-Identifier: GPL-3.0-only
// The ray tracer's development tunables (RT director's section 5; FA l63): what a test run sets without a build, so a
// look is tuned by runs. A run with --diagnostics (any) reads rt_dev.txt in the settings folder (--settings, else the
// data folder) as ray tracing starts: "name=value" items, any number a line, "#" to the line's end a comment. A name
// not given keeps the code's own number; a player's run (no --diagnostics) reads nothing. The log names each value
// taken ("[rt-dev]") and each item not understood.
#pragma once

#include <cstdint>
#include <string_view>

namespace galaxy::render {

enum class RtDev : std::uint8_t {
    // The host's (rt_walk.cpp, rt_bounce.cpp, rt_pass_indirect.cpp).
    Rate,           // the rate the filters and the build budget take, frames a second (0: measured): pinned for
                    // picture runs on a busy machine
    Form,           // rt_look_[0] kForm (program.hlsli traced_painted, FC c36)
    ArtFacing,      // rt_look_[1] the sun's height over the player's up for the relight's exposure (c39 M3)
    Debake,         // rt_look_[2] kDebake
    AmbientLook,    // rt_look_[3] kAmbientLook
    ShadowTint,     // a shadow's tint by the sky's colour: view_stage_a.w and the shading frame's key[3]
    AmbientGain,    // the stage's ambient gain, over its row's (rt_stages.tsv ambient_gain)
    SunGain,        // the stage's sun gain, over its row's (sun_gain)
    FullFacing,     // g_look.z: the n.l from which a painted surface keeps its authored brightness
    BounceAmbient,  // the shading frame's ambient gain (rt_bounce.cpp kAmbientGain)
    Wrap,           // a local light's facing wrap (kWrap)
    SkyGain,        // the dome as light (kSkyGain)
    AreaEmission,   // kAreaEmission
    HitSky,         // kHitSky
    Bounce,         // kBounce
    BounceMax,      // the indirect ratio's upper limit (kBounceMax, rt_pass_indirect.cpp kIndirectBounceMax)
    SkyMin,         // its lower limit (kSkyMin, kIndirectSkyMin)
    HitDark,        // kHitDark
    SunGlint,       // kSunGlint
    HitGain,        // 1: a hit's surfaces met against their running mean (1 / mean where over 1; b46), 0 held at 1 (b51)
    AirScale,       // the stage row's air (rt_stages.tsv air) times this
    SkyInShadow,    // g_look.x: what the sun's shadow keeps of the sky's light (1 under the relight, 0.5 Painted)
    LightSrgb,      // 1: the lights' colours (display values) taken as linear light of the same luminance; 0 as given
    // The programs' (program.hlsli view_dev): from BaseFloor.
    BaseFloor,      // traced_colour's kBaseFloor: a lit program's colour over at most 1 / this
    SunStrength,    // traced_painted: the relight's sun over the stage's key (M2, October's 1.25)
    SkyChroma,      // traced_painted: the share of the sky's chroma its light keeps in shade (M8, October's 0.4)
    // The tracer's (rt_frame.hlsli g_dev): from GlassShine, in this order.
    GlassShine,       // rt_mirror.hlsli: glass's F0 over ((ior - 1) / (ior + 1))^2 (kShine 2.0)
    WaterShine,       // water's (1.5)
    PaintedGlass,     // a painted reflection's head-on share: glass 0.1
    PaintedMetal,     // metal 0.55
    PaintedGloss,     // gloss 0.05
    PaintedMirror,    // what a painted reflection takes of the colour under it: 1 - this x its share (0.6)
    SubstituteGlass,  // rt_reflect_out.hlsl: what a painted glass reflection's R is taken at (1.3)
    KeepMetal,        // what a painted metal reflection keeps where R is dark (0.5)
    KeepGloss,        // gloss (0.25)
    Count,
};
inline constexpr unsigned kRtDevCount = static_cast<unsigned>(RtDev::Count);
static_assert(kRtDevCount <= 64u, "one bit a tunable (RenderCore::rt_dev_set_)");
inline constexpr unsigned kRtDevProgramFirst = static_cast<unsigned>(RtDev::BaseFloor);
inline constexpr unsigned kRtDevTracerFirst = static_cast<unsigned>(RtDev::GlassShine);
inline constexpr unsigned kRtDevProgramSlots = kRtDevTracerFirst - kRtDevProgramFirst;  // view_dev[0]: 4 at most
inline constexpr unsigned kRtDevTracerSlots = kRtDevCount - kRtDevTracerFirst;          // g_dev: 32 at most
static_assert(kRtDevProgramSlots <= 4u && kRtDevTracerSlots <= 32u);

inline constexpr std::string_view kRtDevNames[kRtDevCount] = {
    "rate",           "form",          "art_facing",     "debake",        "ambient_look",  "shadow_tint",
    "ambient_gain",   "sun_gain",      "full_facing",    "bounce_ambient", "wrap",         "sky_gain",
    "area_emission",  "hit_sky",       "bounce",         "bounce_max",    "sky_min",       "hit_dark",
    "sun_glint",      "hit_gain",      "air_scale",      "sky_in_shadow", "light_srgb",    "base_floor",
    "sun_strength",   "sky_chroma",    "glass_shine",    "water_shine",   "painted_glass", "painted_metal",
    "painted_gloss",  "painted_mirror", "substitute_glass", "keep_metal", "keep_gloss",
};

}  // namespace galaxy::render
