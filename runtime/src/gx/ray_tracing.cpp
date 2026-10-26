// SPDX-License-Identifier: GPL-3.0-only
// SceneRayTracer (ray_tracing.h): scene geometry and acceleration structures, the lighting trace, the picture capture
// and the composite. The camera, engine, reflection and water passes are in ray_tracing_*.cpp.
#include "galaxy/gx/ray_tracing.h"
#include "galaxy/gx/rt_materials.h"
#include "galaxy/gx/rt_surfaces.h"
#include "galaxy/gx/render_config.h"
#include "galaxy/gx/gx_bitfields.h"

#include <bit>
#include "galaxy/gx_d3d12.h"

#include "rt_capture_cs.h"
#include "rt_sky_light_map_cs.h"
#include "rt_sky_light_sh_cs.h"
#include "rt_light_texture_ps.h"
#include "rt_clear_composite_ps.h"
#include "rt_emit_merge_cs.h"
#include "rt_glow_base_cs.h"
#include "rt_glow_emit_cs.h"
#include "rt_glow_emitter_cs.h"
#include "rt_authored_emit_cs.h"
#include "rt_sky_state_cs.h"
#include "rt_surface_emit_cs.h"
#include "rt_composite_ps.h"
#include "rt_composite_vs.h"
#include "rt_star_sphere_cs.h"
#include "rt_delight_cs.h"
#include "rt_emit_triangle_cs.h"
#include "rt_emit_scan_cs.h"
#include "rt_delight_mip_cs.h"
#include "rt_sky_probe_tex_cs.h"
#include "rt_sky_trace_sh_cs.h"
#include "rt_geometry_cs.h"
#include "rt_validate_cs.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <mutex>
#include <cstdio>
#include <cstdlib>
#include <cstddef>
#include <cstring>
#include <cwchar>
#include <fstream>
#include <intrin.h>
#include <iostream>
#include <map>
#include <string>

namespace galaxy::gx {
namespace {

using Microsoft::WRL::ComPtr;

// Mirrors cbuffer RtFrame in ray_tracing.hlsl.
struct RtFrameConstants {
    std::uint32_t draw_count;
    std::uint32_t triangle_count;
    std::uint32_t vertex_ring_limit;
    std::uint32_t resident_limit;
    std::uint32_t index_ring_limit;
    std::uint32_t constant_ring_limit;
    std::uint32_t matrix_ring_limit;
    std::uint32_t frame_flags;
    float projection[4];
    float depth[4];
    float viewport[4];
    std::uint32_t target_size[2];
    float near_plane;
    std::uint32_t triangle_base;
};
static_assert(sizeof(RtFrameConstants) == 96u);

// Mirrors cbuffer RtLight in rt_lighting.hlsl.
struct RtLightConstants {
    float projection[4];
    float depth[4];
    float viewport[4];
    std::uint32_t target_size[2];
    std::uint32_t sample_size[2];
    float near_plane;
    std::uint32_t frame;
    float range;
    float strength;
    std::uint32_t rays;
    std::uint32_t factor;
    std::uint32_t flags;
    std::uint32_t history_frames;
    float reproject[3][4];
    float picture[3][4];
    float contact;
    float contact_range;
    float sky_saturation;
    float fallback;
    float sun[4];
    float sun_light[4];
    std::uint32_t point_count;
    float point_light;
    float point_size;
    float sun_reach;
    float points[2u * kRtPointLights][4];
    float to_eye[3][4];
    float ambient[4];
    float point_saturation;
    std::uint32_t draw_count;
    float sky_ibl;
    float fill;
    float emit[4];
    float self[4];
    float ao[4];
    float night[4];
    float night2[4];
    std::uint32_t star_first[8];   // the stars' triangles (StarSphereCS): first, count (0: none)
    std::uint32_t star_count[8];
    std::uint32_t core_point;      // the star core's light among points (~0: none)
    float sun_cap;                 // the soft cap of the night's added light where the core's light dominates
    float motion_jitter[2];        // the previous samples' jitter less this frame's (render pixels, RtView::jitter)
    float point_radius[kRtPointLights];   // per point light: its emitter's radius (its shadows' penumbra)
    std::uint32_t reference_bounces;      // the reference (GALAXY_RT_REFERENCE): its bounces,
    float reference_exposure;             // its exposure over the geometric mean of its light,
    std::uint32_t reference_reset;        // 1: it starts again this frame
    std::uint32_t emit_scene;            // the scene's emitting triangles among tev_info[2] (the rest: glow emitters)
    float history_projection[4];          // the previous samples' projection (their frame's jitter)
    float picture_projection[4];          // the previous picture's
    float sky_radiance[4];                // GALAXY_RT_PHYSICAL: the sky's radiance in units of the stage's full light
    float full_light[4];                  // the stage's full light in linear light (rgb); 1: the traced sky (w)
    std::uint32_t tev_info[4];            // TEV records of the scene's draws, of the sky's (rt_tev.hlsli); emitters
    float up[4];                          // the player's up in eye space (the game's gravity; else the view's up)
    std::uint32_t probe[4];               // GALAXY_RT_PROBE: its sample (x, y), on (z)
    float light_fit[4];                   // physical: the camera's exposure, the light fitted to the baked light (1)
    RtEngineConstants engine;             // the ray tracing engine (ray_tracing_engine.cpp)
};
static_assert(sizeof(RtLightConstants) == 1408u);

// Mirrors cbuffer RtEmit in rt_lighting.hlsl (root constants).
struct RtEmitConstants {
    float projection[4];
    float depth[4];
    float viewport[4];
    std::uint32_t target_size[2];
    std::uint32_t sample_size[2];
    float to_world[3][4];
    float to_eye[3][4];
    float gain;
    float near_plane;
    std::uint32_t reset;
    std::uint32_t factor;
    float authored_power;
    float time;
};
static_assert(sizeof(RtEmitConstants) == 184u);
constexpr std::uint32_t kEmitLights = 512u;     // remembered glow lights (rt_lighting.hlsl kEmitLights)
constexpr std::uint64_t kEmitLightBytes = 64u;  // per remembered light (rt_lighting.hlsl kEmitLightVectors)
constexpr std::uint32_t kPhysicalFlag = 16384u;  // the game's light units (rt_lighting.hlsl kPhysicalFlag)
constexpr std::uint32_t kGlowMaxTriangles = 65536u;
// A game point light's emitter radius (eye units) other than the star core's (its fitted ball): about a Luma's body,
// so the actor that asked for the light does not shadow it, and its shadows have the penumbra of a light that size.
constexpr float kPointEmitterRadius = 60.0f;
// g_draws word 6 (rt_lighting.hlsl kDrawTableTint): the emission takes the draw's own colour.
constexpr std::uint32_t kDrawTableTint = 1u;
// g_draws word 6 (rt_lighting.hlsl kDrawTableGlow): the table gives the light source's own glow, in the
// high half as unorm16 of glow / kDrawGlowScale.
constexpr std::uint32_t kDrawTableGlow = 2u;
// g_draws word 6 (rt_lighting.hlsl kDrawTableFoliage): alpha-cut leaves, grass and flower cards.
constexpr std::uint32_t kDrawTableFoliage = 4u;
// g_draws word 6 (rt_lighting.hlsl kDrawTableStar): a star's burning surface.
constexpr std::uint32_t kDrawTableStar = 8u;
// Scene TLAS instance masks (rt_lighting.hlsl kSceneMask): the frame's geometry; the late geometry, which
// camera rays to the frame's own surfaces skip.
constexpr UINT kSceneMask = 1u;
constexpr UINT kLateMask = 2u;
// g_draws word 6 (rt_lighting.hlsl kDrawTableCard): an alpha-tested card, traced whole (kRtDrawCard).
constexpr std::uint32_t kDrawTableCard = 16u;
// g_draws word 6 (rt_lighting.hlsl kDrawTableAlpha): an alpha-tested draw traced whole (kRtDrawAlphaTest).
constexpr std::uint32_t kDrawTableAlpha = 32u;
// g_draws word 6 (rt_reflections.hlsl kDrawTableMirror): the table's `mirror` (the Observatory fountain's plates).
constexpr std::uint32_t kDrawTableMirror = 64u;
// g_draws word 6 (rt_lighting.hlsl kDrawTableRecolour): a light source in the table's colour.
constexpr std::uint32_t kDrawTableRecolour = 128u;
// g_draws word 6 (rt_lighting.hlsl kDrawTableNearGlow): a light source only near a glow light (`near-glow`).
constexpr std::uint32_t kDrawTableNearGlow = 256u;
// g_draws word 6 (rt_lighting.hlsl kDrawTableUnseen): a trace-only draw (kRtDrawUnseen), no camera ray's surface.
constexpr std::uint32_t kDrawTableUnseen = 512u;
// g_draws word 6 (rt_lighting.hlsl kDrawTableSolid): a clear solid the clear pass traces through (the table's `traced
// solid`, drawn unseen); bits 16-31 its row's range, the world units its colour is the transmittance after.
constexpr std::uint32_t kDrawTableSolid = 1024u;
constexpr float kDrawGlowScale = 16.0f;
// AuthoredLight.flags (rt_lighting.hlsl kAuthored*): the colour the draw shows; a spot light.
constexpr std::uint32_t kAuthoredTint = 1u;
constexpr std::uint32_t kAuthoredSpot = 2u;
// AuthoredLight.flags (rt_lighting.hlsl kAuthoredStar): a star: its power by its size.
constexpr std::uint32_t kAuthoredStar = 4u;
// AuthoredLight.flags (rt_lighting.hlsl kAuthoredStrip): a long emitter, a light per cell. Its draw is split into
// pieces of kStripTriangles, numbered in the light's identity. An emitter group holds 64 cells and a cable's triangles
// are long: 32 of them never cross more cells than that, so no two cells share a slot.
constexpr std::uint32_t kAuthoredStrip = 8u;
constexpr std::uint32_t kStripTriangles = 32u;
// Glow light flags (rt_lighting.hlsl kGlowEffect): the table names the glow a particle (no light).
constexpr std::uint32_t kGlowEffect = 32u;
// glow_lights_ flags (rt_lighting.hlsl kGlowTable*): the light scales by the table's intensity; it takes
// the table's colour.
constexpr std::uint32_t kGlowTableIntensity = 4u;
constexpr std::uint32_t kGlowTableColor = 8u;
// A black hole's lens draw among the glow draws: GlowEmitCS skips it (rt_lighting.hlsl kGlowLens).
constexpr std::uint32_t kGlowLens = 16u;
// A table light source drawn as a glow, under physical light: an area light (GlowEmitterCS), not a glow light
// (rt_lighting.hlsl kGlowPhysical).
constexpr std::uint32_t kGlowPhysical = 64u;
// A physical glow light source's flags (glow_emitters_ word 6; rt_lighting.hlsl kGlowEmitter*): a sprite standing for a
// glowing volume (isotropic; else its own faces emit); its light the colour it draws (the table's `tint`: its TEV output
// over its triangles, as much of it as its blend adds to the picture; else the table's colour over its texture's
// coverage); bits 4-6 its source blend factor (GX BlendFactor).
constexpr std::uint32_t kGlowEmitterIsotropic = 1u;
constexpr std::uint32_t kGlowEmitterDrawn = 2u;
// A glowing volume drawn as geometry (the table's `volume`: a beam, a flame jet): a sprite's light, not its faces'.
constexpr std::uint32_t kGlowEmitterVolume = 8u;
constexpr std::uint32_t kGlowEmitterBlendShift = 4u;

// Two values 0-1 as unorm16 (0-65534) in one word, the first in the low half; 0xFFFF where a value
// is negative (unset).
std::uint32_t unit_pair(float a, float b) {
    const auto unit = [](float v) {
        return v < 0.0f ? 0xFFFFu : static_cast<std::uint32_t>(std::lround(std::min(v, 1.0f) * 65534.0f));
    };
    return unit(a) | (unit(b) << 16u);
}

// Mirrors cbuffer RtCapture in rt_lighting.hlsl (root constants).
struct RtCaptureConstants {
    float depth[4];
    float viewport[4];
    std::uint32_t target_size[2];
    std::uint32_t sample_size[2];
    std::uint32_t factor;
    float emissive;
    std::uint32_t divide;   // bit 0: this frame's lighting is divided out; bit 1: what the glows added
    float strength;
    float split;
    float surface_gain;
    float sky_distance;
    float exposure_day;
    float projection[4];
    float to_world[3][4];
    float exposure_night;
    float daylight;
    std::uint32_t exposure;
    float grade;
};
// 40 of the capture root signature's 64 words (with its 2 tables and 10 root views: 62).
static_assert(sizeof(RtCaptureConstants) == 160u);

// Mirrors cbuffer RtComposite in rt_lighting.hlsl (root constants).
struct RtCompositeConstants {
    float projection[4];
    float depth[4];
    float viewport[4];
    std::uint32_t sample_size[2];
    std::uint32_t factor;
    std::uint32_t flags;
    std::uint32_t slot;
    float near_plane;
    float split;
    float efb_scale;     // EFB pixels per game pixel
    float sun[4];        // towards the traced sun (eye space, traced frame); relief height (eye units)
    float sun_light[4];  // its light (rgb)
    std::uint32_t target_size[2];   // the traced frame's EFB pixels (its per-pixel normals)
    std::uint32_t pad[2];
    float water[4];      // the camera in water (x: 1), its depth (y, eye units)
    float water_sky[4];  // the stage's full light (rgb, the lighting's full_light): what the water scatters
};
static_assert(sizeof(RtCompositeConstants) == 160u);

enum LightParameter : UINT {
    kLightFrame = 0,
    kLightScene,
    kLightDepth,
    kLightPrevZ,
    kLightPrevHistory,
    kLightHistoryIn,
    kLightPicture,
    kLightPictureSums,
    kLightPositions,
    kLightSurfaces,
    kLightEmitters,
    kLightGlowBase,
    kLightGlowAdded,
    kLightDraws,
    kLightSky,
    kLightZ,
    kLightNormal,
    kLightRaw,
    kLightHistory,
    kLightFinal,
    kLightPrimary,
    kLightSurfaceUv,
    kLightDrawTextures,
    kLightReference,
    kLightSkyMap,
    kLightTev,
    kLightPixels,
    kLightEmitTris,
    kLightReservoirIn,
    kLightReservoirOut,
    kLightCount,
};

enum CaptureParameter : UINT {
    kCaptureConstants = 0,
    kCaptureSources,
    kCaptureFinal,
    kCaptureGlowBase,
    kCapturePicture,
    kCaptureSums,
    kCaptureGlow,
    kCaptureFound,
    kCaptureSky,
    kCaptureSkyRadiance,
    kCaptureSkySh,
    kCaptureExposure,
    kCaptureGame,
    kCaptureCount,
};

enum EmitParameter : UINT {
    kEmitConstants = 0,
    kEmitSources,
    kEmitDraws,
    kEmitPositions,
    kEmitAdded,
    kEmitTiles,
    kEmitLightsSlot,
    kEmitList,
    kEmitGlowOut,
    kEmitGlowUv,
    kEmitCount,
};

// A result slot's state: read by the composite (pixel) and the picture capture (compute).
constexpr D3D12_RESOURCE_STATES kLightRead =
    D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
// Sky r g b n, scene r g b n (cleared per capture); the sky's luminance over time, known (kept);
// the lighting's luminance sum (x 256) and count (cleared); the exposure, known (kept); per star slot
// (SceneRayTracer::kStarSlots) its samples' game colour r g b n (cleared); the stars' colour over time
// r g b, known (kept) (rt_lighting.hlsl kStarSums, kStarColour).
constexpr UINT64 kPictureSumsCleared = 32u;
constexpr UINT64 kPictureStarSlots = 8u;
constexpr UINT64 kPictureStarSums = 64u;
constexpr UINT64 kPictureStarColour = kPictureStarSums + kPictureStarSlots * 16u;
// Physical: the eye's adaptation (rt_lighting.hlsl kAdaptation): its factor, known (kept); the traced light's log2 sum
// (x 256) and count (cleared); its histogram (kAdaptationBuckets words, cleared).
constexpr UINT64 kPictureSumsBytes = kPictureStarColour + 32u + 16u * 4u;
// The composite's exposure measurement (rt_lighting.hlsl kExposureBuckets x 16 bytes). Always
// in UNORDERED_ACCESS: the composite (pixel) adds, the capture (compute) reads and clears.
constexpr UINT64 kExposureSumsBytes = 16u * 16u;

constexpr unsigned kReprojectEntries = 256u;
constexpr UINT64 kReprojectEntryBytes = 256u;

// a * b for 3x4 affine matrices (rows; implicit last row 0 0 0 1).
std::array<float, 12> affine_multiply(const std::array<float, 12>& a, const std::array<float, 12>& b) {
    std::array<float, 12> c{};
    for (unsigned r = 0; r < 3u; ++r) {
        for (unsigned k = 0; k < 4u; ++k) {
            c[r * 4u + k] = a[r * 4u] * b[k] + a[r * 4u + 1u] * b[4u + k] + a[r * 4u + 2u] * b[8u + k] +
                            (k == 3u ? a[r * 4u + 3u] : 0.0f);
        }
    }
    return c;
}

// Inverse of a 3x4 affine matrix; false when its 3x3 part is singular.
bool affine_inverse(const std::array<float, 12>& m, std::array<float, 12>& out) {
    const double a = m[0], b = m[1], c = m[2], d = m[4], e = m[5], f = m[6], g = m[8], h = m[9], i = m[10];
    const double det = a * (e * i - f * h) - b * (d * i - f * g) + c * (d * h - e * g);
    if (!(std::abs(det) > 1e-12)) {
        return false;
    }
    const double inv[9] = {(e * i - f * h) / det, (c * h - b * i) / det, (b * f - c * e) / det,
                           (f * g - d * i) / det, (a * i - c * g) / det, (c * d - a * f) / det,
                           (d * h - e * g) / det, (b * g - a * h) / det, (a * e - b * d) / det};
    for (unsigned r = 0; r < 3u; ++r) {
        out[r * 4u] = static_cast<float>(inv[r * 3u]);
        out[r * 4u + 1u] = static_cast<float>(inv[r * 3u + 1u]);
        out[r * 4u + 2u] = static_cast<float>(inv[r * 3u + 2u]);
        out[r * 4u + 3u] = static_cast<float>(-(inv[r * 3u] * m[3] + inv[r * 3u + 1u] * m[7] +
                                                inv[r * 3u + 2u] * m[11]));
    }
    return true;
}

constexpr std::array<float, 12> kIdentity34{1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f};

// GALAXY_RT_* "a,b,c" triples.
std::array<float, 3> env_triple(const char* name, std::array<float, 3> fallback) {
    char value[96]{};
    std::size_t length = 0u;
    if (getenv_s(&length, value, sizeof(value), name) != 0 || length <= 1u) {
        return fallback;
    }
    std::array<float, 3> out = fallback;
    char* cursor = value;
    for (float& v : out) {
        char* end = nullptr;
        const float parsed = std::strtof(cursor, &end);
        if (end == cursor) {
            return fallback;
        }
        v = parsed;
        cursor = *end == ',' ? end + 1 : end;
    }
    return out;
}

float env_float(const char* name, float fallback) {
    char value[32]{};
    std::size_t length = 0u;
    return getenv_s(&length, value, sizeof(value), name) == 0 && length > 1u ? std::strtof(value, nullptr)
                                                                              : fallback;
}

// Near plane: depth 1 (reversed Z) is ndc 1, z = -[2][3] / (1 + [2][2]).
float near_plane(const RtView& view) {
    const float denominator = 1.0f + view.z_scale;
    return denominator != 0.0f ? std::max(0.0f, view.z_offset / denominator) : 0.0f;
}

enum RootParameter : UINT {
    kRootFrame = 0,
    kRootDraws,
    kRootVertexRing,
    kRootResident,
    kRootIndexRing,
    kRootConstantRing,
    kRootMatrixRing,
    kRootPositions,
    kRootScene,
    kRootDepth,
    kRootStats,
    kRootDump,
    kRootSurfaces,
    kRootSurfaceUv,
    kRootCount,
};

constexpr std::uint32_t kVsConstantsBytes = 1536u;   // sizeof(GxVsConstants)
constexpr std::uint32_t kPaletteMatrixBytes = 48u;
constexpr UINT kTimestampsPerSlot = 4u;
constexpr UINT64 kReadbackSlotBytes = 128u;

// Highest readable byte offset for a `size`-byte load (0 for an empty or too small buffer).
std::uint32_t read_limit(std::uint64_t bytes, std::uint32_t size) {
    return bytes > size ? static_cast<std::uint32_t>(std::min<std::uint64_t>(bytes - size, 0xFFFFFFF0u)) : 0u;
}

D3D12_RESOURCE_BARRIER transition(ID3D12Resource* resource, D3D12_RESOURCE_STATES before,
                                  D3D12_RESOURCE_STATES after) {
    D3D12_RESOURCE_BARRIER barrier{};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition.pResource = resource;
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    barrier.Transition.StateBefore = before;
    barrier.Transition.StateAfter = after;
    return barrier;
}

D3D12_RESOURCE_BARRIER uav_barrier(ID3D12Resource* resource) {
    D3D12_RESOURCE_BARRIER barrier{};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
    barrier.UAV.pResource = resource;
    return barrier;
}

// A light's direction d held between `lowest` and `highest` degrees over the horizon of `up` (any space, all three in
// it), its compass direction kept. Near straight above or below (within 60 degrees of the axis, weighed in smoothly)
// that compass direction is a short vector that swings round with small changes of d, so the light turns toward
// `away` there instead: its own direction as last traced, so it keeps its side rather than flipping between frames
// (the camera's back before there is one: the shadows fall away from it). d comes back unit length.
void hold_elevation(std::array<float, 3>& d, const std::array<float, 3>& up, float lowest, float highest,
                    const std::array<float, 3>& away) noexcept {
    const float up_length = std::sqrt(up[0] * up[0] + up[1] * up[1] + up[2] * up[2]);
    const float length = std::sqrt(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
    if (length <= 1e-6f || up_length <= 1e-6f) {
        return;
    }
    const std::array<float, 3> u{up[0] / up_length, up[1] / up_length, up[2] / up_length};
    for (float& v : d) v /= length;
    const float e = d[0] * u[0] + d[1] * u[1] + d[2] * u[2];
    const float min_e = std::sin(lowest * 3.14159265f / 180.0f);
    const float max_e = std::sin(highest * 3.14159265f / 180.0f);
    if (e >= min_e && e <= max_e) {
        return;
    }
    const float target = std::clamp(e, min_e, max_e);
    // Its own compass direction (length: the cosine of its elevation) and away's.
    const std::array<float, 3> h{d[0] - e * u[0], d[1] - e * u[1], d[2] - e * u[2]};
    const float h_length = std::sqrt(h[0] * h[0] + h[1] * h[1] + h[2] * h[2]);
    const float along = away[0] * u[0] + away[1] * u[1] + away[2] * u[2];
    const std::array<float, 3> a{away[0] - along * u[0], away[1] - along * u[1], away[2] - along * u[2]};
    const float a_length = std::sqrt(a[0] * a[0] + a[1] * a[1] + a[2] * a[2]);
    // Its own direction's weight: none on the axis, whole from 60 degrees off it (cosine 0.5), smoothstep between.
    const float t = std::clamp(h_length / 0.5f, 0.0f, 1.0f);
    const float own = a_length > 1e-6f ? t * t * (3.0f - 2.0f * t) : 1.0f;
    std::array<float, 3> compass{};
    for (unsigned i = 0; i < 3u; ++i) {
        compass[i] = (h_length > 1e-6f ? own * h[i] / h_length : 0.0f) +
                     (a_length > 1e-6f ? (1.0f - own) * a[i] / a_length : 0.0f);
    }
    float c_length = std::sqrt(compass[0] * compass[0] + compass[1] * compass[1] + compass[2] * compass[2]);
    if (c_length < 1e-3f) {
        // Its own and away's opposed, equally weighed: its own.
        compass = h_length > 1e-6f ? h : a;
        c_length = h_length > 1e-6f ? h_length : a_length;
    }
    if (c_length > 1e-6f) {
        const float across = std::sqrt(std::max(1.0f - target * target, 0.0f));
        for (unsigned i = 0; i < 3u; ++i) {
            d[i] = compass[i] / c_length * across + u[i] * target;
        }
    }
}

// The stage's up (world Y): the horizon the light is held over (hold_light).
constexpr std::array<float, 3> kStageUp{0.0f, 1.0f, 0.0f};

// The unit direction a fraction `amount` of the way from unit `from` to unit `to` along the great circle.
std::array<float, 3> slerp_direction(const std::array<float, 3>& from, const std::array<float, 3>& to,
                                     float amount) noexcept {
    const float c = std::clamp(from[0] * to[0] + from[1] * to[1] + from[2] * to[2], -1.0f, 1.0f);
    const float angle = std::acos(c);
    std::array<float, 3> out{};
    if (angle < 1e-4f) {
        out = to;
    } else if (angle > 3.14159265f - 1e-3f) {
        // Opposed: any great circle joins them; the one through the axis `from` is least aligned with.
        const unsigned k = std::abs(from[0]) < std::abs(from[1]) ? (std::abs(from[0]) < std::abs(from[2]) ? 0u : 2u)
                                                                 : (std::abs(from[1]) < std::abs(from[2]) ? 1u : 2u);
        std::array<float, 3> p{};
        p[k] = 1.0f;
        const float along = p[0] * from[0] + p[1] * from[1] + p[2] * from[2];
        for (unsigned i = 0; i < 3u; ++i) p[i] -= along * from[i];
        const float p_length = std::sqrt(p[0] * p[0] + p[1] * p[1] + p[2] * p[2]);
        const float turn = amount * 3.14159265f;
        for (unsigned i = 0; i < 3u; ++i) out[i] = std::cos(turn) * from[i] + std::sin(turn) * p[i] / p_length;
    } else {
        const float s = std::sin(angle);
        const float wf = std::sin((1.0f - amount) * angle) / s;
        const float wt = std::sin(amount * angle) / s;
        for (unsigned i = 0; i < 3u; ++i) out[i] = wf * from[i] + wt * to[i];
    }
    const float length = std::sqrt(out[0] * out[0] + out[1] * out[1] + out[2] * out[2]);
    for (float& v : out) v = length > 1e-6f ? v / length : 0.0f;
    return out;
}

}  // namespace

bool SceneRayTracer::initialize(ID3D12Device* device, std::uint64_t timestamp_frequency, bool validate,
                                DXGI_FORMAT scene_format, DXGI_FORMAT display_format) {
    shutdown();
    static_assert(kPictureStarColour + 16u == kPictureAdaptation);
    if (device == nullptr || FAILED(device->QueryInterface(IID_PPV_ARGS(&device_)))) {
        device_.Reset();
        return false;
    }
    D3D12_FEATURE_DATA_D3D12_OPTIONS5 options5{};
    D3D12_FEATURE_DATA_SHADER_MODEL shader_model{D3D_SHADER_MODEL_6_5};
    if (FAILED(device_->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS5, &options5, sizeof(options5))) ||
        options5.RaytracingTier < D3D12_RAYTRACING_TIER_1_1 ||
        FAILED(device_->CheckFeatureSupport(D3D12_FEATURE_SHADER_MODEL, &shader_model, sizeof(shader_model))) ||
        shader_model.HighestShaderModel < D3D_SHADER_MODEL_6_5) {
        std::cerr << "[rt] unavailable: DXR 1.1 with shader model 6.5 required (tier "
                  << static_cast<int>(options5.RaytracingTier) << ")\n";
        device_.Reset();
        return false;
    }

    D3D12_DESCRIPTOR_RANGE depth_range{};
    depth_range.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    depth_range.NumDescriptors = 1u;
    depth_range.BaseShaderRegister = 7u;
    depth_range.OffsetInDescriptorsFromTableStart = 0u;
    D3D12_ROOT_PARAMETER params[kRootCount]{};
    params[kRootFrame].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    params[kRootFrame].Descriptor.ShaderRegister = 0u;
    for (UINT i = kRootDraws; i <= kRootMatrixRing; ++i) {
        params[i].ParameterType = D3D12_ROOT_PARAMETER_TYPE_SRV;
        params[i].Descriptor.ShaderRegister = i - kRootDraws;
    }
    params[kRootPositions].ParameterType = D3D12_ROOT_PARAMETER_TYPE_UAV;
    params[kRootPositions].Descriptor.ShaderRegister = 0u;
    params[kRootScene].ParameterType = D3D12_ROOT_PARAMETER_TYPE_SRV;
    params[kRootScene].Descriptor.ShaderRegister = 6u;
    params[kRootDepth].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    params[kRootDepth].DescriptorTable.NumDescriptorRanges = 1u;
    params[kRootDepth].DescriptorTable.pDescriptorRanges = &depth_range;
    params[kRootStats].ParameterType = D3D12_ROOT_PARAMETER_TYPE_UAV;
    params[kRootStats].Descriptor.ShaderRegister = 1u;
    params[kRootDump].ParameterType = D3D12_ROOT_PARAMETER_TYPE_UAV;
    params[kRootDump].Descriptor.ShaderRegister = 2u;
    params[kRootSurfaces].ParameterType = D3D12_ROOT_PARAMETER_TYPE_UAV;
    params[kRootSurfaces].Descriptor.ShaderRegister = 3u;
    params[kRootSurfaceUv].ParameterType = D3D12_ROOT_PARAMETER_TYPE_UAV;
    params[kRootSurfaceUv].Descriptor.ShaderRegister = 4u;
    for (auto& param : params) {
        param.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    }
    D3D12_ROOT_SIGNATURE_DESC rsd{};
    rsd.NumParameters = kRootCount;
    rsd.pParameters = params;
    ComPtr<ID3DBlob> blob, error;
    if (FAILED(D3D12SerializeRootSignature(&rsd, D3D_ROOT_SIGNATURE_VERSION_1, &blob, &error)) ||
        FAILED(device_->CreateRootSignature(0, blob->GetBufferPointer(), blob->GetBufferSize(),
                                            IID_PPV_ARGS(&root_)))) {
        std::cerr << "[rt] root signature failed\n";
        shutdown();
        return false;
    }
    D3D12_COMPUTE_PIPELINE_STATE_DESC pso{};
    pso.pRootSignature = root_.Get();
    pso.CS = {g_rt_geometry_cs, sizeof(g_rt_geometry_cs)};
    if (FAILED(device_->CreateComputePipelineState(&pso, IID_PPV_ARGS(&geometry_pso_)))) {
        std::cerr << "[rt] geometry pipeline failed\n";
        shutdown();
        return false;
    }
    pso.CS = {g_rt_validate_cs, sizeof(g_rt_validate_cs)};
    if (FAILED(device_->CreateComputePipelineState(&pso, IID_PPV_ARGS(&validate_pso_)))) {
        std::cerr << "[rt] validation pipeline failed\n";
        shutdown();
        return false;
    }

    if (!create_buffer(kStatsWords * 4u, D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_STATE_COPY_DEST,
                       D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS, stats_, L"rt stats") ||
        !create_buffer(kStatsWords * 4u, D3D12_HEAP_TYPE_UPLOAD, D3D12_RESOURCE_STATE_GENERIC_READ,
                       D3D12_RESOURCE_FLAG_NONE, stats_zero_, L"rt stats zero") ||
        !create_buffer(kResultSlots * kReadbackSlotBytes, D3D12_HEAP_TYPE_READBACK, D3D12_RESOURCE_STATE_COPY_DEST,
                       D3D12_RESOURCE_FLAG_NONE, readback_, L"rt readback") ||
        !create_buffer(kBallReadbacks * kBallReadbackBytes, D3D12_HEAP_TYPE_READBACK, D3D12_RESOURCE_STATE_COPY_DEST,
                       D3D12_RESOURCE_FLAG_NONE, ball_readback_, L"rt star balls readback")) {
        shutdown();
        return false;
    }
    void* zero = nullptr;
    const D3D12_RANGE no_read{0, 0};
    if (FAILED(stats_zero_->Map(0, &no_read, &zero))) {
        shutdown();
        return false;
    }
    std::memset(zero, 0, kStatsWords * 4u);
    stats_zero_->Unmap(0, nullptr);
    constexpr UINT64 kSkyBytes = kSkyBins * kSkyBinWords * 4u;
    if (!create_buffer(kSkyBytes, D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_STATE_COPY_DEST,
                       D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS, sky_bins_, L"rt sky bins") ||
        !create_buffer(kSkyBytes, D3D12_HEAP_TYPE_UPLOAD, D3D12_RESOURCE_STATE_GENERIC_READ, D3D12_RESOURCE_FLAG_NONE,
                       sky_zero_, L"rt sky zero") ||
        !create_buffer(kSkyRing * kSkyBytes, D3D12_HEAP_TYPE_READBACK, D3D12_RESOURCE_STATE_COPY_DEST,
                       D3D12_RESOURCE_FLAG_NONE, sky_readback_, L"rt sky readback") ||
        !create_buffer(kSkyBins * 16u, D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                       D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS, sky_radiance_, L"rt sky radiance") ||
        !create_buffer(128u, D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                       D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS, sky_sh_, L"rt sky light") ||
        FAILED(sky_zero_->Map(0, &no_read, &zero))) {
        shutdown();
        return false;
    }
    std::memset(zero, 0, kSkyBytes);
    sky_zero_->Unmap(0, nullptr);
    sky_bins_state_ = D3D12_RESOURCE_STATE_COPY_DEST;
    sky_radiance_state_ = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    sky_sh_state_ = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    sky_radiance_generation_ = ~0u;
    D3D12_QUERY_HEAP_DESC query{};
    query.Type = D3D12_QUERY_HEAP_TYPE_TIMESTAMP;
    query.Count = kResultSlots * kTimestampsPerSlot;
    if (timestamp_frequency != 0u && FAILED(device_->CreateQueryHeap(&query, IID_PPV_ARGS(&timestamps_)))) {
        timestamps_.Reset();
    }
    pass_entries_ = {};
    next_pass_entry_ = 0u;
    if (timestamp_frequency != 0u && env_float("GALAXY_RT_PASS_TIMING", 0.0f) != 0.0f) {
        query.Count = kPassEntries * 2u;
        if (FAILED(device_->CreateQueryHeap(&query, IID_PPV_ARGS(&pass_timestamps_))) ||
            !create_buffer(kPassEntries * 2u * sizeof(std::uint64_t), D3D12_HEAP_TYPE_READBACK,
                           D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_FLAG_NONE, pass_readback_,
                           L"rt pass readback")) {
            pass_timestamps_.Reset();
            pass_readback_.Reset();
        }
    }
    timestamp_frequency_ = timestamp_frequency;
    validate_ = validate;
    {
        char value[32]{};
        std::size_t length = 0u;
        dump_scene_ = getenv_s(&length, value, sizeof(value), "GALAXY_RT_DUMP") == 0 && length > 1u
            ? std::strtoull(value, nullptr, 10) : 0u;
        dump_every_ = static_cast<std::uint64_t>(std::max(0.0f, env_float("GALAXY_RT_DUMP_EVERY", 0.0f)));
        dump_remaining_ = static_cast<std::uint64_t>(std::max(1.0f, env_float("GALAXY_RT_DUMP_COUNT", 1.0f)));
    }
    lighting_enabled_ = env_float("GALAXY_RT_LIGHT", 1.0f) != 0.0f;
    // Quality (config rt_quality, GALAXY_RT_QUALITY): high traces every pixel of the internal resolution with 4
    // sky and occlusion rays, 4 sun shadow rays and two glow lights per sample; medium one pixel per 2 x 2 with
    // 2 sky rays, 1 sun ray and one glow light.
    quality_ = std::min(get_render_config().rt_quality, 2u);
    high_quality_ = quality_ == 1u;
    // Low (rt_quality 2): RTX 3050-class cards: one sample per 3 x 3 internal pixels, one sky ray, one sun ray.
    low_quality_ = quality_ == 2u;
    rays_ = static_cast<std::uint32_t>(
        std::clamp(env_float("GALAXY_RT_RAYS", high_quality_ ? 4.0f : low_quality_ ? 1.0f : 2.0f), 1.0f, 16.0f));
    range_ = std::clamp(env_float("GALAXY_RT_RANGE", 1600.0f), 10.0f, 100000.0f);
    strength_ = std::clamp(env_float("GALAXY_RT_STRENGTH", 1.0f), 0.0f, 1.0f);
    history_frames_ = static_cast<std::uint32_t>(std::clamp(env_float("GALAXY_RT_HISTORY", 32.0f), 1.0f, 64.0f));
    sky_dump_at_ = static_cast<std::uint64_t>(std::max(env_float("GALAXY_RT_SKY_DUMP", 0.0f), 0.0f));
    {
        char value[32]{};
        std::size_t length = 0u;
        unsigned x = 0u;
        unsigned y = 0u;
        probe_ = getenv_s(&length, value, sizeof(value), "GALAXY_RT_PROBE") == 0 && length > 1u &&
                 sscanf_s(value, "%u,%u", &x, &y) == 2;
        probe_pixel_ = {x, y};
        float disc[3]{};
        light_meter_ = getenv_s(&length, value, sizeof(value), "GALAXY_RT_LIGHT_METER") == 0 && length > 1u &&
                       sscanf_s(value, "%f,%f,%f", &disc[0], &disc[1], &disc[2]) == 3 && disc[2] > 0.0f;
        light_meter_disc_ = {disc[0], disc[1], disc[2]};
    }
    emit_log_interval_ = static_cast<std::uint64_t>(std::clamp(env_float("GALAXY_RT_LOG_INTERVAL", 120.0f), 1.0f, 100000.0f));
    // GALAXY_RT_REFERENCE_FROM: the present the engine's reference (GALAXY_RT_ENGINE_REFERENCE) starts at (a still
    // view; before it the real-time lighting alone, at full speed).
    reference_from_ = static_cast<std::uint64_t>(std::max(env_float("GALAXY_RT_REFERENCE_FROM", 0.0f), 0.0f));
    factor_override_ = static_cast<std::uint32_t>(std::clamp(env_float("GALAXY_RT_FACTOR", 0.0f), 0.0f, 4.0f));
    contact_ = std::clamp(env_float("GALAXY_RT_CONTACT", 0.3f), 0.0f, 1.0f);
    contact_range_ = std::clamp(env_float("GALAXY_RT_CONTACT_RANGE", 250.0f), 1.0f, 100000.0f);
    sky_saturation_ = std::clamp(env_float("GALAXY_RT_SKY", 0.12f), 0.0f, 1.0f);
    fallback_ = std::clamp(env_float("GALAXY_RT_FALLBACK", 0.55f), 0.0f, 2.0f);
    emissive_ = std::clamp(env_float("GALAXY_RT_EMISSIVE", 3.0f), 0.0f, 16.0f);
    show_lighting_ = env_float("GALAXY_RT_VIEW", 0.0f) != 0.0f;
    show_term_ = static_cast<std::uint32_t>(std::clamp(env_float("GALAXY_RT_VIEW", 0.0f), 0.0f, 8.0f));
    view_cycle_ = static_cast<std::uint32_t>(std::clamp(env_float("GALAXY_RT_VIEW_CYCLE", 0.0f), 0.0f, 100000.0f));
    point_radius_ = std::clamp(env_float("GALAXY_RT_POINT_RADIUS", 800.0f), 10.0f, 1.0e6f);
    point_size_ = std::clamp(env_float("GALAXY_RT_POINT_SIZE", 600.0f), 0.0f, 1.0e5f);
    point_light_ = std::clamp(env_float("GALAXY_RT_POINT_LIGHT", 2.0f), 0.0f, 8.0f);
    point_saturation_ = std::clamp(env_float("GALAXY_RT_POINT_SATURATION", 0.4f), 0.0f, 1.0f);
    split_ = std::clamp(env_float("GALAXY_RT_SPLIT", 0.0f), 0.0f, 1.0f);
    emission_ = std::clamp(env_float("GALAXY_RT_EMISSION", 40.0f), 0.0f, 256.0f);
    authored_power_ = std::clamp(env_float("GALAXY_RT_AUTHORED_POWER", 60000.0f), 0.0f, 1.0e7f);
    log_emitters_ = env_float("GALAXY_RT_LOG_EMITTERS", 0.0f) != 0.0f;
    log_physical_ = env_float("GALAXY_RT_LOG_PHYSICAL", 0.0f) != 0.0f;
    ab_ = env_float("GALAXY_RT_AB", 0.0f) != 0.0f;
    glow_cutoff_ = std::clamp(env_float("GALAXY_RT_GLOW_CUTOFF", 0.004f), 1e-5f, 1.0f);
    glow_reach_ = std::clamp(env_float("GALAXY_RT_GLOW_REACH", 30000.0f), 10.0f, 1.0e6f);
    glow_light_ = std::clamp(env_float("GALAXY_RT_GLOW_LIGHT", 1.0f), 0.0f, 64.0f);
    surface_light_ = std::clamp(env_float("GALAXY_RT_SURFACE_LIGHT", 1.0f), 0.0f, 16.0f);
    night_ = std::clamp(env_float("GALAXY_RT_NIGHT", 1.0f), 0.0f, 1.0f);
    daylight_ = std::clamp(env_float("GALAXY_RT_DAYLIGHT", 0.35f), 0.01f, 1.0f);
    glow_boost_ = std::clamp(env_float("GALAXY_RT_GLOW_BOOST", 0.4f), 0.0f, 1.0f);
    glow_exposure_ = std::clamp(env_float("GALAXY_RT_GLOW_EXPOSURE", 1.0f), 1.0f, 6.0f);
    glow_spill_ = std::clamp(env_float("GALAXY_RT_GLOW_SPILL", 0.7f), 0.0f, 2.0f);
    glow_model_exposure_ = std::clamp(env_float("GALAXY_RT_GLOW_MODEL_EXPOSURE", 1.0f), 1.0f, 8.0f);
    glow_model_spill_ = std::clamp(env_float("GALAXY_RT_GLOW_MODEL_SPILL", 1.0f), 0.0f, 2.0f);
    sky_distance_ = std::clamp(env_float("GALAXY_RT_SKY_DISTANCE", 60000.0f), 1000.0f, 1.0e7f);
    sky_ibl_ = std::clamp(env_float("GALAXY_RT_SKY_IBL", 0.8f), 0.0f, 1.0f);
    fill_ = std::clamp(env_float("GALAXY_RT_FILL", 0.08f), 0.0f, 0.8f);
    self_area_ = std::clamp(env_float("GALAXY_RT_SELF_AREA", 1.2f), 0.0f, 8.0f);
    self_point_ = std::clamp(env_float("GALAXY_RT_SELF_POINT", 1.0f), 0.0f, 8.0f);
    area_radiance_ = std::clamp(env_float("GALAXY_RT_AREA_RADIANCE", 3.0f), 0.0f, 16.0f);
    ao_reach_ = std::clamp(env_float("GALAXY_RT_AO_REACH", 150.0f), 0.0f, 2000.0f);
    ao_day_ = std::clamp(env_float("GALAXY_RT_AO", 0.8f), 0.0f, 1.0f);
    ao_night_ = std::clamp(env_float("GALAXY_RT_AO_NIGHT", 1.0f), 0.0f, 1.0f);
    night_level_ = std::clamp(env_float("GALAXY_RT_NIGHT_LEVEL", 0.7f), 0.2f, 1.5f);
    night_occlusion_ = std::clamp(env_float("GALAXY_RT_NIGHT_AO", 0.75f), 0.0f, 1.0f);
    night_shadow_ = std::clamp(env_float("GALAXY_RT_NIGHT_SHADOW", 0.6f), 0.0f, 1.0f);
    night_lights_ = std::clamp(env_float("GALAXY_RT_NIGHT_LIGHTS", 2.0f), 0.0f, 8.0f);
    night_lights_cap_ = std::clamp(env_float("GALAXY_RT_NIGHT_LIGHTS_CAP", 1.0f), 0.05f, 8.0f);
    sun_cap_ = std::clamp(env_float("GALAXY_RT_SUN_CAP", 2.5f), 0.05f, 8.0f);
    night_key_ = std::clamp(env_float("GALAXY_RT_NIGHT_KEY", 0.5f), 0.0f, 4.0f);
    day_environment_ = std::clamp(env_float("GALAXY_RT_DAY_ENVIRONMENT", 0.92f), 0.3f, 1.2f);
    relief_ = std::clamp(env_float("GALAXY_RT_RELIEF", 3.0f), 0.0f, 64.0f);
    delight_ = env_float("GALAXY_RT_DELIGHT", 1.0f) != 0.0f;
    // Relief from the de-light map's shading: 1 and above read too rough on grass and sand.
    delight_relief_ = std::clamp(env_float("GALAXY_RT_DELIGHT_RELIEF", 0.5f), 0.0f, 32.0f);
    gi_ = env_float("GALAXY_RT_GI", 1.0f) != 0.0f;
    core_radius_ = std::clamp(env_float("GALAXY_RT_CORE_RADIUS", 1667.0f), 1.0f, 100000.0f);
    core_light_ = std::clamp(env_float("GALAXY_RT_CORE_LIGHT", 8.0f), 0.0f, 16.0f);
    exposure_ = env_float("GALAXY_RT_EXPOSURE", 1.0f) != 0.0f;
    exposure_day_ = std::clamp(env_float("GALAXY_RT_EXPOSURE_DAY", 1.0f), 0.3f, 1.5f);
    exposure_night_ = std::clamp(env_float("GALAXY_RT_EXPOSURE_NIGHT", 1.0f), 0.3f, 1.5f);
    sun_strength_ = std::clamp(env_float("GALAXY_RT_SUN_STRENGTH", 1.25f), 0.25f, 4.0f);
    surface_emission_ = std::clamp(env_float("GALAXY_RT_SURFACE_EMISSION", 3.0f), 0.0f, 256.0f);
    {
        char value[96]{};
        std::size_t length = 0u;
        sun_override_ = getenv_s(&length, value, sizeof(value), "GALAXY_RT_SUN") == 0 && length > 1u;
    }
    sun_direction_ = env_triple("GALAXY_RT_SUN", sun_direction_);
    sun_color_ = env_triple("GALAXY_RT_SUN_COLOR", sun_color_);
    // The sun's angular radius in degrees: the real sun's 0.27, for shadows as crisp as daylight's. A stage's rt-suns
    // row sets its own size (a moon, a key).
    sun_size_ = std::clamp(env_float("GALAXY_RT_SUN_SIZE", 0.27f), 0.0f, 30.0f);
    key_size_ = std::clamp(env_float("GALAXY_RT_KEY_SIZE", 1.5f), 0.0f, 30.0f);
    sun_min_elevation_ = std::clamp(env_float("GALAXY_RT_SUN_MIN_ELEVATION", 30.0f), 0.0f, 89.0f);
    sun_max_elevation_ = std::clamp(env_float("GALAXY_RT_SUN_MAX_ELEVATION", 65.0f), 0.0f, 90.0f);
    key_min_elevation_ = std::clamp(env_float("GALAXY_RT_KEY_MIN_ELEVATION", 50.0f), 0.0f, sun_max_elevation_);
    sun_light_ = std::clamp(env_float("GALAXY_RT_SUN_LIGHT", 1.2f), 0.0f, 4.0f);
    sky_light_ = std::clamp(env_float("GALAXY_RT_SKY_LIGHT", 0.6f), 0.0f, 4.0f);
    if (lighting_enabled_ && !create_lighting_pipelines(scene_format)) {
        std::cerr << "[rt] lighting unavailable\n";
        lighting_enabled_ = false;
    }
    if (lighting_enabled_) {
        static_cast<void>(reflections_.initialize(device_.Get(), timestamp_frequency, scene_format));
    }
    // The renderer draws the scene into the HDR scene EFB only with a camera to bring it back.
    if (!create_camera_pipelines(scene_format, display_format)) {
        shutdown();
        return false;
    }
    // The engine is the lighting: without it nothing is traced.
    engine_ready_ = lighting_enabled_ && create_engine_pipelines();
    if (lighting_enabled_ && !engine_ready_) {
        std::cerr << "[rt] lighting unavailable: the engine's pipelines failed\n";
        lighting_enabled_ = false;
    }
    ready_ = true;
    std::cerr << "[rt] ready tier=1.1 validate=" << (validate ? 1 : 0) << " lighting=" << (lighting_enabled_ ? 1 : 0)
              << " rays=" << rays_ << " range=" << range_ << " strength=" << strength_
              << " history=" << history_frames_ << " factor=" << factor_override_ << " contact=" << contact_
              << " sky=" << sky_saturation_ << " emissive=" << emissive_ << " sun=" << sun_direction_[0] << ','
              << sun_direction_[1] << ',' << sun_direction_[2] << " sun-light=" << sun_light_
              << " sky-light=" << sky_light_ << '\n';
    return true;
}

void SceneRayTracer::shutdown() noexcept {
    reflections_.shutdown();
    ready_ = false;
    scene_open_ = false;
    draws_.clear();
    texture_view_blocks_ = {};
    texture_view_slots_.clear();
    texture_view_count_ = 0u;
    texture_views_usable_ = false;
    retired_.clear();
    slots_ = {};
    window_ = {};
    positions_.Reset();
    surfaces_.Reset();
    blas_.Reset();
    tlas_.Reset();
    scratch_.Reset();
    late_draws_.clear();
    late_open_ = false;
    late_built_ = 0u;
    late_positions_.Reset();
    late_surfaces_.Reset();
    late_blas_.Reset();
    late_scratch_.Reset();
    sky_draws_.clear();
    sky_records_.clear();
    sky_open_ = false;
    sky_built_ = 0u;
    sky_positions_.Reset();
    sky_surfaces_.Reset();
    sky_uv_.Reset();
    sky_blas_.Reset();
    sky_tlas_.Reset();
    sky_scratch_.Reset();
    sky_probe_map_.Reset();
    sky_probe_pso_.Reset();
    sky_trace_sh_pso_.Reset();
    emit_tris_.Reset();
    emit_triangle_pso_.Reset();
    emit_scan_pso_.Reset();
    emit_ranges_.clear();
    emit_range_keys_.clear();
    emit_range_materials_.clear();
    emit_triangles_ = 0u;
    for (auto& buffer : light_pixels_) buffer.Reset();
    light_pixels_bytes_ = {};
    // The views stay the renderer's (set_delight_views): every slot free again.
    reset_delight();
    delight_scratch_.Reset();
    delight_root_.Reset();
    delight_pso_.Reset();
    delight_mip_pso_.Reset();
    stats_.Reset();
    stats_zero_.Reset();
    readback_.Reset();
    ball_readback_.Reset();
    ball_readback_frame_ = {};
    ball_readback_colour_ = {};
    ball_read_frame_ = 0u;
    core_ball_radius_ = 0.0f;
    core_colour_known_ = false;
    star_colour_from_frame_ = ~0ull;
    sky_bins_.Reset();
    sky_zero_.Reset();
    sky_readback_.Reset();
    sky_radiance_.Reset();
    sky_sh_.Reset();
    sky_reads_ = {};
    timestamps_.Reset();
    pass_timestamps_.Reset();
    pass_readback_.Reset();
    pass_entries_ = {};
    dump_.Reset();
    dump_readback_.Reset();
    dump_pending_ = false;
    geometry_pso_.Reset();
    validate_pso_.Reset();
    root_.Reset();
    star_sphere_pso_.Reset();
    light_root_.Reset();
    capture_pso_.Reset();
    capture_root_.Reset();
    glow_base_pso_.Reset();
    light_tex_root_.Reset();
    light_tex_pso_.Reset();
    light_tex_.Reset();
    light_tex_rtv_heap_.Reset();
    light_tex_srv_heap_.Reset();
    light_tex_srv_ = {};
    light_tex_width_ = light_tex_height_ = 0u;
    light_tex_ready_ = false;
    camera_root_.Reset();
    camera_down_pso_.Reset();
    camera_up_pso_.Reset();
    camera_pso_.Reset();
    camera_widen_pso_.Reset();
    for (auto& buffer : camera_pyramid_) buffer.Reset();
    camera_texels_ = 0u;
    engine_ready_ = false;
    engine_running_ = false;
    engine_gbuffer_pso_.Reset();
    engine_direct_pso_.Reset();
    engine_direct_spatial_pso_.Reset();
    engine_light_grid_pso_.Reset();
    engine_light_grid_.Reset();
    engine_shadow_blur_pso_.Reset();
    engine_shadow_post_pso_.Reset();
    engine_shadow_stabilize_pso_.Reset();
    engine_indirect_pso_.Reset();
    engine_indirect_spatial_pso_.Reset();
    engine_specular_pso_.Reset();
    engine_temporal_pso_.Reset();
    engine_atrous_pso_.Reset();
    engine_final_pso_.Reset();
    engine_reference_pso_.Reset();
    engine_reference_sums_.Reset();
    engine_reference_pixels_ = 0u;
    for (auto& buffer : engine_buffers_) buffer.Reset();
    for (auto& buffer : engine_slots_) buffer.Reset();
    engine_slot_valid_ = {};
    engine_width_ = engine_height_ = 0u;
    engine_fresh_ = true;
    engine_picture_ = nullptr;
    adaptation_readback_.Reset();
    adaptation_readback_frame_ = {};
    adaptation_cpu_ = 1.0f;
    sky_state_pso_.Reset();
    surface_emit_pso_.Reset();
    sky_light_map_pso_.Reset();
    sky_light_sh_pso_.Reset();
    glow_emit_pso_.Reset();
    authored_emit_pso_.Reset();
    emit_merge_pso_.Reset();
    emit_root_.Reset();
    glow_base_.Reset();
    glow_added_.Reset();
    glow_positions_.Reset();
    glow_surfaces_.Reset();
    glow_uv_.Reset();
    emit_tiles_.Reset();
    emit_lights_.Reset();
    emit_list_.Reset();
    emit_readback_.Reset();
    composite_pso_.Reset();
    clear_composite_pso_.Reset();
    engine_clear_pso_.Reset();
    for (auto& buffer : engine_clear_) buffer.Reset();
    engine_clear_valid_ = {};
    engine_bind_ = {};
    water_clear_.clear();
    composite_root_.Reset();
    for (auto& buffer : light_z_) buffer.Reset();
    light_normal_.Reset();
    light_raw_.Reset();
    light_primary_.Reset();
    for (auto& buffer : light_final_) buffer.Reset();
    picture_.Reset();
    picture_sums_.Reset();
    exposure_sums_.Reset();
    reproject_.Reset();
    reproject_upload_.Reset();
    reproject_mapped_ = nullptr;
    light_slots_ = {};
    light_samples_ = 0u;
    light_slot_ = -1;
    history_valid_ = picture_valid_ = false;
    camera_known_ = history_camera_known_ = picture_camera_known_ = false;
    device_.Reset();
    capacity_ = 0u;
    triangles_ = 0u;
    positions_state_ = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    surfaces_state_ = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    water_draws_.clear();
    water_open_ = false;
    water_positions_.Reset();
    water_surfaces_.Reset();
    water_blas_.Reset();
    water_tlas_.Reset();
    water_scratch_.Reset();
    water_instance_.Reset();
    water_positions_state_ = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    water_surfaces_state_ = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
}

bool SceneRayTracer::create_buffer(std::uint64_t bytes, D3D12_HEAP_TYPE heap, D3D12_RESOURCE_STATES state,
                                   D3D12_RESOURCE_FLAGS flags, ComPtr<ID3D12Resource>& out, const wchar_t* name) {
    D3D12_HEAP_PROPERTIES properties{};
    properties.Type = heap;
    D3D12_RESOURCE_DESC desc{};
    desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    desc.Width = std::max<std::uint64_t>(bytes, 256u);
    desc.Height = 1u;
    desc.DepthOrArraySize = 1u;
    desc.MipLevels = 1u;
    desc.SampleDesc.Count = 1u;
    desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    desc.Flags = flags;
    if (FAILED(device_->CreateCommittedResource(&properties, D3D12_HEAP_FLAG_NONE, &desc, state, nullptr,
                                                IID_PPV_ARGS(&out)))) {
        std::cerr << "[rt] buffer allocation failed bytes=" << bytes << '\n';
        out.Reset();
        return false;
    }
    out->SetName(name);
    return true;
}

void SceneRayTracer::retire(ComPtr<ID3D12Resource>& resource) {
    if (resource) {
        retired_.push_back({std::move(resource), frame_index_});
    }
}

bool SceneRayTracer::ensure_capacity(ID3D12GraphicsCommandList* list, std::uint32_t triangles,
                                     std::uint32_t preserved) {
    if (triangles <= capacity_ && blas_ && tlas_) {
        return true;
    }
    ComPtr<ID3D12Resource> previous_positions = positions_;
    const D3D12_RESOURCE_STATES previous_state = positions_state_;
    ComPtr<ID3D12Resource> previous_surfaces = surfaces_;
    const D3D12_RESOURCE_STATES previous_surfaces_state = surfaces_state_;
    if (triangles > kMaxTriangles) {
        return false;
    }
    std::uint32_t capacity = std::max<std::uint32_t>(capacity_, 128u * 1024u);
    while (capacity < triangles) {
        capacity *= 2u;
    }
    capacity = std::min(capacity, kMaxTriangles);

    D3D12_RAYTRACING_GEOMETRY_DESC geometry{};
    geometry.Type = D3D12_RAYTRACING_GEOMETRY_TYPE_TRIANGLES;
    geometry.Flags = D3D12_RAYTRACING_GEOMETRY_FLAG_OPAQUE;
    geometry.Triangles.VertexFormat = DXGI_FORMAT_R32G32B32_FLOAT;
    geometry.Triangles.VertexCount = capacity * 3u;
    geometry.Triangles.VertexBuffer.StrideInBytes = 12u;
    D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS blas_inputs{};
    blas_inputs.Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL;
    blas_inputs.Flags = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_BUILD;
    blas_inputs.NumDescs = 1u;
    blas_inputs.DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY;
    blas_inputs.pGeometryDescs = &geometry;
    D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO blas_info{};
    device_->GetRaytracingAccelerationStructurePrebuildInfo(&blas_inputs, &blas_info);
    D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS tlas_inputs{};
    tlas_inputs.Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL;
    tlas_inputs.Flags = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_TRACE;
    tlas_inputs.NumDescs = 2u;   // the frame's geometry and the late geometry (build_tlas)
    tlas_inputs.DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY;
    D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO tlas_info{};
    device_->GetRaytracingAccelerationStructurePrebuildInfo(&tlas_inputs, &tlas_info);
    if (blas_info.ResultDataMaxSizeInBytes == 0u || tlas_info.ResultDataMaxSizeInBytes == 0u) {
        return false;
    }
    const auto align = [](std::uint64_t value) {
        return (value + D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BYTE_ALIGNMENT - 1u) &
               ~std::uint64_t{D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BYTE_ALIGNMENT - 1u};
    };
    const std::uint64_t scratch_tlas_offset = align(blas_info.ScratchDataSizeInBytes);
    const std::uint64_t scratch_bytes = scratch_tlas_offset + align(tlas_info.ScratchDataSizeInBytes);

    ComPtr<ID3D12Resource> previous_uv = surface_uv_;
    const D3D12_RESOURCE_STATES previous_uv_state = surface_uv_state_;
    retire(positions_);
    retire(surfaces_);
    retire(surface_uv_);
    retire(blas_);
    retire(tlas_);
    retire(scratch_);
    capacity_ = 0u;
    if (!create_buffer(static_cast<std::uint64_t>(capacity) * 16u, D3D12_HEAP_TYPE_DEFAULT,
                       D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS, surfaces_,
                       L"rt surfaces") ||
        !create_buffer(static_cast<std::uint64_t>(capacity) * 36u, D3D12_HEAP_TYPE_DEFAULT,
                       D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS, positions_,
                       L"rt positions") ||
        !create_buffer(blas_info.ResultDataMaxSizeInBytes, D3D12_HEAP_TYPE_DEFAULT,
                       D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE,
                       D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS, blas_, L"rt blas") ||
        !create_buffer(tlas_info.ResultDataMaxSizeInBytes, D3D12_HEAP_TYPE_DEFAULT,
                       D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE,
                       D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS, tlas_, L"rt tlas") ||
        !create_buffer(scratch_bytes, D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                       D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS, scratch_, L"rt scratch")) {
        positions_.Reset();
        surfaces_.Reset();
        blas_.Reset();
        tlas_.Reset();
        scratch_.Reset();
        return false;
    }
    positions_state_ = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    surfaces_state_ = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    // Texture coordinates and colours per triangle (kRtTriUvStride): the alpha tests, the hits' TEV programs (both
    // qualities) and high quality's textured reflection hits.
    if (create_buffer(static_cast<std::uint64_t>(capacity) * kRtTriUvStride, D3D12_HEAP_TYPE_DEFAULT,
                      D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS, surface_uv_,
                      L"rt surface uv")) {
        surface_uv_state_ = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
        if (previous_uv && preserved != 0u && list != nullptr) {
            D3D12_RESOURCE_BARRIER barriers[2] = {
                transition(previous_uv.Get(), previous_uv_state, D3D12_RESOURCE_STATE_COPY_SOURCE),
                transition(surface_uv_.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_DEST)};
            list->ResourceBarrier(2u, barriers);
            list->CopyBufferRegion(surface_uv_.Get(), 0u, previous_uv.Get(), 0u,
                                   static_cast<std::uint64_t>(preserved) * kRtTriUvStride);
            barriers[0] = transition(previous_uv.Get(), D3D12_RESOURCE_STATE_COPY_SOURCE, previous_uv_state);
            barriers[1] = transition(surface_uv_.Get(), D3D12_RESOURCE_STATE_COPY_DEST,
                                     D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
            list->ResourceBarrier(2u, barriers);
        }
    } else {
        surface_uv_.Reset();
    }
    if (previous_surfaces && preserved != 0u && list != nullptr) {
        D3D12_RESOURCE_BARRIER barriers[2] = {
            transition(previous_surfaces.Get(), previous_surfaces_state, D3D12_RESOURCE_STATE_COPY_SOURCE),
            transition(surfaces_.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_DEST)};
        list->ResourceBarrier(2u, barriers);
        list->CopyBufferRegion(surfaces_.Get(), 0u, previous_surfaces.Get(), 0u,
                               static_cast<std::uint64_t>(preserved) * 16u);
        barriers[0] = transition(previous_surfaces.Get(), D3D12_RESOURCE_STATE_COPY_SOURCE, previous_surfaces_state);
        barriers[1] = transition(surfaces_.Get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        list->ResourceBarrier(2u, barriers);
    }
    if (previous_positions && preserved != 0u && list != nullptr) {
        // Triangles flushed by earlier chunks of this scene move to the larger buffer.
        D3D12_RESOURCE_BARRIER barriers[2] = {
            transition(previous_positions.Get(), previous_state, D3D12_RESOURCE_STATE_COPY_SOURCE),
            transition(positions_.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_DEST)};
        list->ResourceBarrier(2u, barriers);
        list->CopyBufferRegion(positions_.Get(), 0u, previous_positions.Get(), 0u,
                               static_cast<std::uint64_t>(preserved) * 36u);
        barriers[0] = transition(previous_positions.Get(), D3D12_RESOURCE_STATE_COPY_SOURCE, previous_state);
        barriers[1] = transition(positions_.Get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        list->ResourceBarrier(2u, barriers);
    }
    blas_bytes_ = blas_info.ResultDataMaxSizeInBytes;
    tlas_bytes_ = tlas_info.ResultDataMaxSizeInBytes;
    scratch_bytes_ = scratch_tlas_offset;

    capacity_ = capacity;
    std::cerr << "[rt] capacity triangles=" << capacity << " blas-mb=" << (blas_bytes_ >> 20u)
              << " scratch-mb=" << (scratch_bytes >> 20u) << " positions-mb="
              << ((static_cast<std::uint64_t>(capacity) * (36u + 16u)) >> 20u) << '\n';
    return true;
}

void SceneRayTracer::begin_frame(std::uint64_t frame_index) {
    frame_index_ = frame_index;
    // GALAXY_RT_VIEW_CYCLE=N (diagnostics, documentation pictures): every N frames the next view of the lighting:
    // the picture, the light alone, the sun, the sky and bounce, the lamps and glows, the sun's blocker distance,
    // the contact occlusion, the day model's terms (GALAXY_RT_VIEW 0-8); the A/B mark shows its number.
    if (view_cycle_ != 0u) {
        static constexpr std::uint32_t kCycle[] = {0u, 1u, 2u, 3u, 4u, 6u, 7u, 8u};
        show_term_ = kCycle[(frame_index / view_cycle_) % 8u];
        show_lighting_ = show_term_ != 0u;
    }
    if (!ready_) {
        return;
    }
    reflections_.begin_frame(frame_index);
    read_sky();
    std::erase_if(retired_, [&](const Retired& entry) { return entry.frame + kResultDelay <= frame_index; });
    // De-light maps' views of evicted maps are free once no frame in flight can read them.
    std::erase_if(delight_retired_slots_, [&](const std::pair<std::uint32_t, std::uint64_t>& entry) {
        if (entry.second + kResultDelay + 2u > frame_index) {
            return false;
        }
        delight_free_.push_back(entry.first);
        return true;
    });
    for (unsigned slot = 0; slot < kResultSlots; ++slot) {
        ResultSlot& result = slots_[slot];
        if (!result.pending || result.frame + kResultDelay > frame_index) {
            continue;
        }
        result.pending = false;
        const D3D12_RANGE range{slot * kReadbackSlotBytes, (slot + 1u) * kReadbackSlotBytes};
        void* mapped = nullptr;
        if (FAILED(readback_->Map(0, &range, &mapped))) {
            continue;
        }
        const auto* base = static_cast<const std::byte*>(mapped) + range.Begin;
        if (result.validated) {
            std::uint32_t stats[kStatsWords]{};
            std::memcpy(stats, base, sizeof(stats));
            for (unsigned i = 0; i < kStatsWords; ++i) {
                window_.stats[i] += stats[i];
            }
            ++window_.validated;
        }
        if (timestamps_ && timestamp_frequency_ != 0u) {
            std::uint64_t ticks[kTimestampsPerSlot]{};
            std::memcpy(ticks, base + 64u, sizeof(ticks));
            const double to_ms = 1000.0 / static_cast<double>(timestamp_frequency_);
            if (ticks[1] >= ticks[0] && ticks[1] - ticks[0] < timestamp_frequency_) {
                window_.build_ms += static_cast<double>(ticks[1] - ticks[0]) * to_ms;
                ++window_.timed;
                if (result.validated && ticks[2] >= ticks[1]) {
                    window_.validate_ms += static_cast<double>(ticks[2] - ticks[1]) * to_ms;
                }
                if (ticks[3] >= ticks[2] && ticks[3] - ticks[2] < timestamp_frequency_) {
                    window_.light_ms += static_cast<double>(ticks[3] - ticks[2]) * to_ms;
                }
            }
        }
        const D3D12_RANGE no_write{0, 0};
        readback_->Unmap(0, &no_write);
    }
    if (sky_dump_taken_ != 0u && sky_dump_taken_ + kResultDelay <= frame_index && sky_dump_readback_) {
        const UINT64 texels = static_cast<UINT64>(kSkyMapSize) * kSkyMapSize;
        const D3D12_RANGE range{0, static_cast<SIZE_T>((texels + 16u) * 16u)};
        void* mapped = nullptr;
        if (SUCCEEDED(sky_dump_readback_->Map(0, &range, &mapped))) {
            const float* map = static_cast<const float*>(mapped);
            const auto encode = [](float v) {
                v = std::clamp(v, 0.0f, 1.0f);
                v = v <= 0.0031308f ? v * 12.92f : 1.055f * std::pow(v, 1.0f / 2.4f) - 0.055f;
                return static_cast<unsigned char>(std::lround(v * 255.0f));
            };
            char dir[512]{};
            std::size_t dir_length = 0u;
            const bool dir_set = getenv_s(&dir_length, dir, sizeof(dir), "GALAXY_PRESENT_SNAPSHOT_DIR") == 0 && dir_length > 1u;
            const std::string path = std::string(dir_set ? dir : ".") + "/sky_probe_" +
                                     std::to_string(sky_dump_taken_ - 1u) + ".ppm";
            std::ofstream out(path, std::ios::binary);
            constexpr unsigned kScale = 4u;
            out << "P6\n" << kSkyMapSize * kScale << ' ' << kSkyMapSize * kScale << "\n255\n";
            for (unsigned y = 0; y < kSkyMapSize * kScale; ++y) {
                for (unsigned x = 0; x < kSkyMapSize * kScale; ++x) {
                    const float* t = map + ((y / kScale) * kSkyMapSize + x / kScale) * 4u;
                    const unsigned char px[3]{encode(t[0]), encode(t[1]), encode(t[2])};
                    out.write(reinterpret_cast<const char*>(px), 3);
                }
            }
            std::cerr << "[rt-sky] sh";
            for (unsigned b = 0; b < 9u; ++b) {
                const float* c = map + (texels + b) * 4u;
                std::cerr << ' ' << c[0] << ',' << c[1] << ',' << c[2];
            }
            std::cerr << " -> " << path << '\n';
            const D3D12_RANGE no_write{0, 0};
            sky_dump_readback_->Unmap(0, &no_write);
        }
        sky_dump_taken_ = 0u;
        sky_dump_at_ = 0u;
    }
    for (unsigned k = 0; adaptation_readback_ && k < kAdaptationReadbacks; ++k) {
        const std::uint64_t taken = adaptation_readback_frame_[k];
        if (taken == 0u || taken + kResultDelay > frame_index) {
            continue;
        }
        adaptation_readback_frame_[k] = 0u;
        const D3D12_RANGE range{k * 16u, k * 16u + 8u};
        void* mapped = nullptr;
        if (SUCCEEDED(adaptation_readback_->Map(0, &range, &mapped))) {
            std::uint32_t values[2]{};
            std::memcpy(values, static_cast<const std::byte*>(mapped) + range.Begin, sizeof(values));
            const D3D12_RANGE no_write{0, 0};
            adaptation_readback_->Unmap(0, &no_write);
            const float adaptation = std::bit_cast<float>(values[0]);
            adaptation_cpu_ = values[1] != 0u && std::isfinite(adaptation) && adaptation > 0.0f ? adaptation : 1.0f;
        }
    }
    for (unsigned k = 0; ball_readback_ && k < kBallReadbacks; ++k) {
        const std::uint64_t taken = ball_readback_frame_[k];
        if (taken == 0u || taken + kResultDelay > frame_index) {
            continue;
        }
        ball_readback_frame_[k] = 0u;
        const D3D12_RANGE range{k * kBallReadbackBytes, (k + 1u) * kBallReadbackBytes};
        void* mapped = nullptr;
        if (taken <= ball_read_frame_ || FAILED(ball_readback_->Map(0, &range, &mapped))) {
            continue;
        }
        float balls[kStarSlots * 4u]{};
        std::uint32_t colour[4]{};
        std::uint32_t probe[kProbeRecords * 4u]{};
        std::memcpy(balls, static_cast<const std::byte*>(mapped) + range.Begin, sizeof(balls));
        std::memcpy(colour, static_cast<const std::byte*>(mapped) + range.Begin + sizeof(balls), sizeof(colour));
        std::memcpy(probe, static_cast<const std::byte*>(mapped) + range.Begin + sizeof(balls) + sizeof(colour),
                    sizeof(probe));
        if (probe_ && taken >= probe_printed_ + 60u) {
            probe_printed_ = taken;
            const std::vector<std::uint16_t>& materials = probe_materials_[k];
            const auto material = [&](std::uint32_t record) {
                return record < materials.size() ? static_cast<int>(materials[record]) : -1;
            };
            float t = 0.0f;
            float z = 0.0f;
            float facing = 0.0f;
            std::memcpy(&t, &probe[4], 4u);
            std::memcpy(&z, &probe[6], 4u);
            std::memcpy(&facing, &probe[7], 4u);
            if (engine_active()) {
                // The engine's G-buffer at the probed pixel (rt_engine.hlsl EngineGBufferCS): its triangle + 1, draw,
                // emission word, G-buffer flags (kGb*) and whether the draw glows there (bit 16); its emission, the
                // picture's luminance, eye z and its own share.
                float picture = 0.0f;
                std::memcpy(&picture, &probe[5], 4u);
                std::fprintf(stderr,
                             "[rt-probe-engine] frame=%llu pixel=%u,%u tri=%u draw=%d material=%d emission-word=0x%08x "
                             "flags=0x%x glows=%u emission=%.2f picture=%.3f z=%.1f own=%.3f\n",
                             static_cast<unsigned long long>(taken), probe_pixel_[0], probe_pixel_[1], probe[0],
                             static_cast<int>(probe[1]), material(probe[1]), probe[2], probe[3] & 0xFFFFu,
                             probe[3] >> 16u, t, picture, z, facing);
            float region_u = 0.0f;
            float region_v = 0.0f;
            std::memcpy(&region_u, &probe[12], 4u);
            std::memcpy(&region_v, &probe[13], 4u);
            std::fprintf(stderr, "[rt-probe-uv] frame=%llu draw=%u tri=%u region-uv=%.3f,%.3f\n",
                         static_cast<unsigned long long>(taken), probe[14], probe[15], region_u, region_v);
            // What stops the sun's centre ray there (rt_engine.hlsl EngineDirectCS): its triangle + 1 (0: open), draw
            // (in instance 0, the scene's), instance, distance, and the water above (water_depth_along).
            float sun_t = 0.0f;
            float sun_water = 0.0f;
            std::memcpy(&sun_t, &probe[30], 4u);
            std::memcpy(&sun_water, &probe[31], 4u);
            const std::uint32_t sun_draw = probe[29] & 0xFFFFu;
            std::fprintf(stderr, "[rt-probe-sun] frame=%llu tri=%u draw=%d material=%d instance=%u t=%.1f water=%.1f\n",
                         static_cast<unsigned long long>(taken), probe[28],
                         sun_draw == 0xFFFFu ? -1 : static_cast<int>(sun_draw),
                         sun_draw == 0xFFFFu ? -1 : material(sun_draw), probe[29] >> 16u, sun_t, sun_water);
            } else
            std::fprintf(stderr,
                         "[rt-probe] frame=%llu pixel=%u,%u seen-tri=%u draw=%d material=%d sun-blocker-tri=%u draw=%d "
                         "material=%d t=%.1f instance=%d z=%.1f facing-sun=%.3f\n",
                         static_cast<unsigned long long>(taken), probe_pixel_[0], probe_pixel_[1], probe[0],
                         static_cast<int>(probe[1]), material(probe[1]), probe[2], static_cast<int>(probe[3]),
                         material(probe[3]), t, static_cast<int>(probe[5]), z, facing);
        }
        // [rt-light-meter]: the means over the meter's disc of the light sources' direct light, the whole diffuse light
        // and the sun's (m: a white surface under the stage's full light reads 1), and the diffuse history's length.
        if (light_meter_ && probe[19] != 0u) {
            const double n = static_cast<double>(probe[19]);
            std::fprintf(stderr,
                         "[rt-light-meter] frame=%llu lights=%.4f whole=%.4f sun=%.4f bounce=%.4f filtered=%.4f "
                         "history=%.2f pixels=%u\n",
                         static_cast<unsigned long long>(taken), probe[16] / 256.0 / n, probe[17] / 256.0 / n,
                         probe[18] / 256.0 / n, probe[20] / 256.0 / n, probe[21] / 256.0 / n, probe[23] / 16.0 / n,
                         probe[19]);
        }
        // [rt-motion-meter]: where the raster's motion takes the disc's pixels against the camera's reprojection (mean
        // x, y and distance, render pixels) and the share the histories follow it for (more than half a pixel off).
        if (light_meter_ && probe[27] != 0u) {
            const double n = static_cast<double>(probe[27]);
            std::fprintf(stderr, "[rt-motion-meter] frame=%llu dx=%.3f dy=%.3f distance=%.3f followed=%.3f pixels=%u\n",
                         static_cast<unsigned long long>(taken), static_cast<std::int32_t>(probe[24]) / 256.0 / n,
                         static_cast<std::int32_t>(probe[25]) / 256.0 / n, probe[26] / 256.0 / n, probe[22] / n,
                         probe[27]);
        }
        // The engine's diffuse light against the converged reference ([rt-reference-engine]): the means over the
        // ordinary non-metal surfaces of the reference's, the engine's and their absolute difference (luminance, the
        // stage's light units), the engine's bias and its mean error relative to the reference.
        const std::uint64_t paths = engine_reference_taken_paths_[k];
        if (paths != 0u && probe[11] != 0u && (paths >= engine_reference_printed_ + 60u || paths < engine_reference_printed_)) {
            engine_reference_printed_ = paths;
            const double pixels = static_cast<double>(probe[11]);
            const double reference = probe[8] / 64.0 / pixels;
            const double engine = probe[9] / 64.0 / pixels;
            const double difference = probe[10] / 64.0 / pixels;
            std::fprintf(stderr,
                         "[rt-reference-engine] frame=%llu paths=%llu pixels=%u reference=%.4f engine=%.4f bias=%+.4f "
                         "mean-abs=%.4f relative-bias=%+.4f relative-error=%.4f\n",
                         static_cast<unsigned long long>(taken), static_cast<unsigned long long>(paths), probe[11],
                         reference, engine, engine - reference, difference,
                         reference > 0.0 ? (engine - reference) / reference : 0.0,
                         reference > 0.0 ? difference / reference : 0.0);
        }
        const D3D12_RANGE no_write{0, 0};
        ball_readback_->Unmap(0, &no_write);
        // Taken after the stage's first capture (which forgot the last stage's colour).
        if (ball_readback_colour_[k] && taken > star_colour_from_frame_ && colour[3] != 0u) {
            std::array<float, 3> rgb{};
            std::memcpy(rgb.data(), colour, sizeof(rgb));
            if (std::isfinite(rgb[0]) && std::isfinite(rgb[1]) && std::isfinite(rgb[2])) {
                core_colour_ = rgb;
                core_colour_known_ = true;
            }
        }
        float largest = 0.0f;
        for (unsigned s = 0; s < kStarSlots; ++s) {
            const float radius = balls[s * 4u + 3u];
            if (std::isfinite(radius) && radius > largest && radius < 1.0e6f) {
                largest = radius;
            }
        }
        core_ball_radius_ = largest;
        ball_read_frame_ = taken;
    }
    if (emit_log_pending_ && emit_log_frame_ + kResultDelay <= frame_index) {
        emit_log_pending_ = false;
        log_emitters();
    }
    if (physical_log_pending_ && emit_log_frame_ + kResultDelay <= frame_index) {
        physical_log_pending_ = false;
        log_physical();
    }
    collect_passes(frame_index);
    if (dump_pending_ && dump_frame_ + kResultDelay <= frame_index) {
        dump_pending_ = false;
        write_dump();
    }
    if (window_.scenes >= 240u) {
        report();
    }
}

// [rt-physical]: the physical light sources of the logged frame (rt_lighting.hlsl g_emit_tris: per scene emitting
// triangle v0 and area, e1, e2, radiance and power; per glow emitter the same, in world space): per material their
// count and power, then the nearest the camera (the scene's in eye space, so at their own distance; the glows' from the
// camera's world position).
void SceneRayTracer::log_physical() {
    const D3D12_RANGE range{0, kEmitBufferBytes};
    void* mapped = nullptr;
    if (!physical_readback_ || FAILED(physical_readback_->Map(0, &range, &mapped))) {
        return;
    }
    const auto* elements = static_cast<const float*>(mapped);
    struct Light {
        float centre[3];
        float distance;
        float area;
        float power;
        float radiance[3];
        unsigned material;
        bool glow;
    };
    std::vector<Light> lights;
    const auto read = [&](std::uint64_t first, unsigned material, bool glow) {
        const float* a = elements + first * 4u;
        Light l{};
        for (unsigned k = 0; k < 3u; ++k) {
            l.centre[k] = a[k] + (a[4u + k] + a[8u + k]) / 3.0f;
            l.radiance[k] = a[12u + k];
        }
        l.area = a[3];
        l.power = a[15];
        l.material = material;
        l.glow = glow;
        const float* from = glow ? emit_log_eye_.data() : nullptr;
        float d2 = 0.0f;
        for (unsigned k = 0; k < 3u; ++k) {
            const float d = l.centre[k] - (from != nullptr ? from[k] : 0.0f);
            d2 += d * d;
        }
        l.distance = std::sqrt(d2);
        if (std::isfinite(l.power) && std::isfinite(l.distance)) {
            lights.push_back(l);
        }
    };
    for (std::uint32_t i = 0; i < physical_log_scene_ && i < kEmitTriangles; ++i) {
        read(1u + 4ull * i, i < physical_log_scene_materials_.size() ? physical_log_scene_materials_[i] : 0u, false);
    }
    const std::uint64_t glow_elements = 1ull + 5ull * kEmitTriangles + kGlowEmitters;
    for (std::uint32_t i = 0; i < physical_log_glows_ && i < kGlowEmitters; ++i) {
        read(glow_elements + 4ull * i, i < physical_log_glow_materials_.size() ? physical_log_glow_materials_[i] : 0u,
             true);
    }
    const D3D12_RANGE no_write{0, 0};
    physical_readback_->Unmap(0, &no_write);
    // Per material: count, power, most, area, the brightest radiance's luminance, the power-weighted radiance (rgb).
    std::map<std::pair<unsigned, bool>, std::array<double, 8>> per;
    for (const Light& l : lights) {
        auto& s = per[{l.material, l.glow}];
        s[0] += 1.0;
        s[1] += l.power;
        s[2] = std::max<double>(s[2], l.power);
        s[3] += l.area;
        s[4] = std::max<double>(s[4], 0.2126 * l.radiance[0] + 0.7152 * l.radiance[1] + 0.0722 * l.radiance[2]);
        for (unsigned k = 0; k < 3u; ++k) {
            s[5u + k] += static_cast<double>(l.radiance[k]) * l.power;
        }
    }
    std::fprintf(stderr, "[rt-physical] frame=%llu scene=%u glows=%u\n", static_cast<unsigned long long>(emit_log_frame_),
                 physical_log_scene_, physical_log_glows_);
    for (const auto& [key, s] : per) {
        const double w = s[1] > 0.0 ? 1.0 / s[1] : 0.0;
        std::fprintf(stderr,
                     "[rt-physical]   %s m%u triangles=%.0f power=%.0f most=%.1f area=%.0f peak=%.3f mean=%.3f,%.3f,%.3f\n",
                     key.second ? "glow" : "scene", key.first, s[0], s[1], s[2], s[3], s[4], s[5] * w, s[6] * w, s[7] * w);
    }
    const auto print = [](const char* kind, const Light& l) {
        std::fprintf(stderr,
                     "[rt-physical]   %s %s m%u at=%.0f,%.0f,%.0f dist=%.0f area=%.1f power=%.2f radiance=%.3f,%.3f,%.3f\n",
                     kind, l.glow ? "glow" : "scene", l.material, l.centre[0], l.centre[1], l.centre[2], l.distance,
                     l.area, l.power, l.radiance[0], l.radiance[1], l.radiance[2]);
    };
    std::sort(lights.begin(), lights.end(), [](const Light& a, const Light& b) { return a.power > b.power; });
    for (std::size_t i = 0; i < std::min<std::size_t>(lights.size(), 12u); ++i) {
        print("strong", lights[i]);
    }
    std::sort(lights.begin(), lights.end(), [](const Light& a, const Light& b) { return a.distance < b.distance; });
    for (std::size_t i = 0; i < std::min<std::size_t>(lights.size(), 40u); ++i) {
        print("near", lights[i]);
    }
}

// [rt-emit]: the glow lights' count and total power, then the strongest (world position, radius,
// distance from the camera, power luminance and colour), the nearest the camera, and this frame's found
// lights nearest the camera with their source: the pass that found them and the material table entry of
// their draw.
void SceneRayTracer::log_emitters() {
    const D3D12_RANGE range{0, kEmitLogBytes + kEmitLogSums + kEmitFoundBytes};
    void* mapped = nullptr;
    if (!emit_readback_ || FAILED(emit_readback_->Map(0, &range, &mapped))) {
        return;
    }
    const auto* bytes = static_cast<const std::byte*>(mapped);
    std::uint32_t count = 0u;
    float total = 0.0f;
    std::memcpy(&count, bytes, 4u);
    std::memcpy(&total, bytes + 4u, 4u);
    struct Entry {
        float p[4];
        float power[4];
        float spot[4];
    };
    static_assert(sizeof(Entry) == kEmitStride);
    std::vector<Entry> lights(std::min<std::uint32_t>(count, kEmitLogLights));
    for (std::size_t i = 0; i < lights.size(); ++i) {
        std::memcpy(&lights[i], bytes + 16u + i * kEmitStride, sizeof(Entry));
    }
    std::uint32_t sums[16]{};
    std::memcpy(sums, bytes + kEmitLogBytes, sizeof(sums));
    const std::byte* const found_bytes = bytes + kEmitLogBytes + kEmitLogSums;
    std::uint32_t found_count = 0u;
    std::memcpy(&found_count, found_bytes, 4u);
    struct Found {
        Entry light;
        std::uint32_t source;
    };
    std::vector<Found> found(std::min(found_count, kEmitFound));
    for (std::size_t i = 0; i < found.size(); ++i) {
        std::memcpy(&found[i].light, found_bytes + 16u + i * kEmitStride, sizeof(Entry));
        std::memcpy(&found[i].source, found_bytes + 16u + std::size_t{kEmitFound} * kEmitStride + i * 8u, 4u);
    }
    const D3D12_RANGE no_write{0, 0};
    emit_readback_->Unmap(0, &no_write);
    const auto lum = [](const Entry& e) {
        return 0.2126f * e.power[0] + 0.7152f * e.power[1] + 0.0722f * e.power[2];
    };
    std::sort(lights.begin(), lights.end(), [&](const Entry& a, const Entry& b) { return lum(a) > lum(b); });
    char line[256];
    std::snprintf(line, sizeof(line),
                  "[rt-emit] frame=%llu lights=%u total=%.0f eye=%.0f,%.0f,%.0f table-lights=%u glow-draws=%u "
                  "table-glows=%u",
                  static_cast<unsigned long long>(emit_log_frame_), count, total, emit_log_eye_[0], emit_log_eye_[1],
                  emit_log_eye_[2], emit_log_draws_[0], emit_log_draws_[1], emit_log_draws_[2]);
    std::cerr << line << '\n';
    {
        // Sky and scene means (8-bit units), the sky state, the exposure and the lit / game ratio.
        const auto mean = [&](unsigned at) {
            const double n = std::max<double>(sums[at + 3u], 1.0);
            return std::array<double, 3>{sums[at] / n, sums[at + 1u] / n, sums[at + 2u] / n};
        };
        const auto sky = mean(0u);
        const auto scene = mean(4u);
        float state = 0.0f;
        float exposure = 0.0f;
        std::memcpy(&state, &sums[8], 4u);
        std::memcpy(&exposure, &sums[12], 4u);
        std::snprintf(line, sizeof(line),
                      "[rt-emit] sky=%.0f,%.0f,%.0f (n=%u) scene=%.0f,%.0f,%.0f (n=%u) state=%.3f%s exposure=%.3f%s "
                      "lit/game=%.3f lit-samples=%u",
                      sky[0], sky[1], sky[2], sums[3], scene[0], scene[1], scene[2], sums[7], state,
                      sums[9] != 0u ? "" : "(unset)", exposure, sums[13] != 0u ? "" : "(unset)",
                      static_cast<double>(sums[14]) / std::max<double>(sums[15], 1.0), sums[11]);
        std::cerr << line << '\n';
        std::snprintf(line, sizeof(line), "[rt-emit] sun=%.2f,%.2f,%.2f sky-weight=%.2f keyed=%.0f strength=%.2f",
                      emit_log_sun_[0], emit_log_sun_[1], emit_log_sun_[2], emit_log_sun_[3], emit_log_sun_[4],
                      emit_log_sun_[5]);
        std::cerr << line << '\n';
    }
    const auto distance = [&](const Entry& e) {
        const float dx = e.p[0] - emit_log_eye_[0];
        const float dy = e.p[1] - emit_log_eye_[1];
        const float dz = e.p[2] - emit_log_eye_[2];
        return std::sqrt(dx * dx + dy * dy + dz * dz);
    };
    const auto print = [&](const char* kind, const Entry& e) {
        const float l = std::max(lum(e), 1e-6f);
        std::snprintf(line, sizeof(line),
                      "[rt-emit]   %s at=%.0f,%.0f,%.0f r=%.0f dist=%.0f power=%.0f rgb=%.2f,%.2f,%.2f "
                      "spot=%.2f,%.2f,%.2f/%.3f",
                      kind, e.p[0], e.p[1], e.p[2], e.p[3], distance(e), l, e.power[0] / l, e.power[1] / l,
                      e.power[2] / l, e.spot[0], e.spot[1], e.spot[2], e.spot[3]);
        std::cerr << line << '\n';
    };
    for (std::size_t i = 0; i < std::min<std::size_t>(lights.size(), 12u); ++i) {
        print("strong", lights[i]);
    }
    // Then the nearest the camera: the lights that light what it sees.
    std::sort(lights.begin(), lights.end(), [&](const Entry& a, const Entry& b) { return distance(a) < distance(b); });
    for (std::size_t i = 0; i < std::min<std::size_t>(lights.size(), 16u); ++i) {
        print("near", lights[i]);
    }
    // This frame's found lights nearest the camera, by source (rt_lighting.hlsl kEmitSource*): a glowing surface of
    // the picture, a glow draw or a material-table light, with its draw's material table entry.
    std::sort(found.begin(), found.end(),
              [&](const Found& a, const Found& b) { return distance(a.light) < distance(b.light); });
    std::snprintf(line, sizeof(line), "[rt-emit] found=%u", found_count);
    std::cerr << line << '\n';
    for (std::size_t i = 0; i < std::min<std::size_t>(found.size(), 24u); ++i) {
        const std::uint32_t kind = found[i].source >> 30u;
        const std::uint32_t draw = found[i].source & 0x3FFFFFFFu;
        const std::vector<std::uint16_t>& materials =
            kind == 2u ? emit_log_glow_materials_ : emit_log_authored_materials_;
        const unsigned material = (kind == 2u || kind == 3u) && draw < materials.size() ? materials[draw] : 0u;
        char kind_name[32];
        std::snprintf(kind_name, sizeof(kind_name), "%s m%u", kind == 1u ? "surface" : kind == 2u ? "glow" : "table",
                      material);
        print(kind_name, found[i].light);
    }
    // What became of the table's light sources' draws over the logged interval.
    std::vector<std::pair<std::uint16_t, std::array<std::uint32_t, kOutcomes>>> outcomes(emit_log_outcomes_.begin(),
                                                                                         emit_log_outcomes_.end());
    std::sort(outcomes.begin(), outcomes.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
    for (const auto& [material, counts] : outcomes) {
        std::snprintf(line, sizeof(line), "[rt-emit]   table-draws m%u light=%u glow=%u late=%u closed=%u",
                      static_cast<unsigned>(material), counts[kOutcomeLight], counts[kOutcomeGlow],
                      counts[kOutcomeLate], counts[kOutcomeClosed]);
        std::cerr << line << '\n';
    }
}

void SceneRayTracer::write_dump() {
    const std::uint64_t pixels = static_cast<std::uint64_t>(dump_width_) * dump_height_;
    const D3D12_RANGE range{0, static_cast<SIZE_T>(pixels * 16u)};
    void* mapped = nullptr;
    if (!dump_readback_ || FAILED(dump_readback_->Map(0, &range, &mapped))) {
        return;
    }
    const auto* texels = static_cast<const float*>(mapped);
    std::string directory = ".";
    {
        char value[MAX_PATH]{};
        std::size_t length = 0u;
        if (getenv_s(&length, value, sizeof(value), "GALAXY_RT_DUMP_DIR") == 0 && length > 1u) {
            directory = value;
        }
    }
    static constexpr std::uint8_t kColors[7][3] = {
        {0, 0, 0}, {0, 255, 255}, {0, 200, 0}, {255, 255, 0}, {255, 0, 0}, {0, 0, 255}, {255, 0, 255}};
    const auto shade = [](float z) {
        // log10 of the eye distance over 1 .. 1e6.
        const float d = std::max(-z, 1.0f);
        return static_cast<std::uint8_t>(std::clamp(std::log10(d) / 6.0f, 0.0f, 1.0f) * 255.0f);
    };
    const char* names[3] = {"class", "raster", "traced"};
    for (unsigned image = 0; image < 3u; ++image) {
        const std::string path = directory + "\\rt_" + names[image] + '_' + std::to_string(dump_scene_) + ".ppm";
        std::ofstream file(path, std::ios::binary);
        file << "P6\n" << dump_width_ << ' ' << dump_height_ << "\n255\n";
        std::vector<std::uint8_t> row(static_cast<std::size_t>(dump_width_) * 3u);
        for (std::uint32_t y = 0; y < dump_height_; ++y) {
            for (std::uint32_t x = 0; x < dump_width_; ++x) {
                const float* texel = texels + (static_cast<std::size_t>(y) * dump_width_ + x) * 4u;
                std::uint8_t* out = row.data() + x * 3u;
                if (image == 0u) {
                    const unsigned kind = std::min(static_cast<unsigned>(texel[0]), 6u);
                    std::memcpy(out, kColors[kind], 3u);
                } else {
                    const bool background = texel[0] < 1.5f;
                    const std::uint8_t v = image == 1u ? (background ? 0u : shade(texel[1]))
                                                       : (texel[2] != 0.0f ? shade(texel[2]) : 0u);
                    out[0] = out[1] = out[2] = v;
                }
            }
            file.write(reinterpret_cast<const char*>(row.data()), static_cast<std::streamsize>(row.size()));
        }
    }
    // Pixels per class for every draw (by the traced triangle), then the draws in table order. The
    // surface image colours each pixel by its triangle's surface class (rt_surfaces.h): diffuse grey,
    // glossy cyan, metal orange, unlit magenta, water blue.
    static constexpr std::uint8_t kSurfaceColors[6][3] = {
        {70, 70, 70}, {0, 220, 255}, {255, 170, 0}, {255, 0, 255}, {0, 80, 255}, {255, 255, 255}};
    std::vector<std::uint8_t> surface_image(static_cast<std::size_t>(pixels) * 3u, 0u);
    std::vector<std::array<std::uint32_t, 8>> per_draw(dump_draws_.size());
    for (std::uint64_t i = 0; i < pixels; ++i) {
        const float* texel = texels + i * 4u;
        std::uint32_t triangle = 0u;
        std::memcpy(&triangle, texel + 3, 4u);
        if (triangle == 0xFFFFFFFFu || dump_draws_.empty()) {
            continue;
        }
        const auto it = std::upper_bound(dump_draws_.begin(), dump_draws_.end(), triangle,
                                         [](std::uint32_t t, const RtDraw& d) { return t < d.first_triangle; });
        const std::size_t index = static_cast<std::size_t>(std::distance(dump_draws_.begin(), it)) - 1u;
        if (index < per_draw.size()) {
            ++per_draw[index][std::min(static_cast<unsigned>(texel[0]), 7u)];
            const unsigned surface = std::min((dump_draws_[index].flags & kRtDrawClassMask) >> kRtDrawClassShift, 5u);
            std::memcpy(surface_image.data() + i * 3u, kSurfaceColors[surface], 3u);
        }
    }
    {
        std::ofstream file(directory + "\\rt_surface_" + std::to_string(dump_scene_) + ".ppm", std::ios::binary);
        file << "P6\n" << dump_width_ << ' ' << dump_height_ << "\n255\n";
        file.write(reinterpret_cast<const char*>(surface_image.data()),
                   static_cast<std::streamsize>(surface_image.size()));
    }
    std::ofstream report(directory + "\\rt_draws_" + std::to_string(dump_scene_) + ".txt");
    report << "camera";
    if (dump_camera_known_) {
        for (float v : dump_camera_) report << ' ' << v;
    } else {
        report << " unknown";
    }
    report << "\ndraw first triangles flags vertices proj(A B C D E F) depth(min max) cull phase | "
              "bg bg-hit match0.1 match1 nearer farther miss outside | channel ambient material "
              "light(index px py pz dx dy dz color) x2 | surface(class features) ind/texgens/stages "
              "env-stage/combiner chan0 chan1 | material tev-header channel0 light0\n";
    for (std::size_t d = 0; d < dump_draws_.size(); ++d) {
        const RtDraw& draw = dump_draws_[d];
        report << d << ' ' << draw.first_triangle << ' ' << draw.triangle_count << ' ' << draw.flags << ' '
               << (draw.vertex_count & kRtVertexCountMask);
        if (d < dump_infos_.size()) {
            const RtDrawInfo& info = dump_infos_[d];
            for (float p : info.projection) report << ' ' << p;
            report << ' ' << info.min_depth << ' ' << info.max_depth << ' ' << info.cull << ' ' << info.phase;
        }
        report << " |";
        for (std::uint32_t n : per_draw[d]) report << ' ' << n;
        if (d < dump_infos_.size()) {
            const RtDrawInfo& info = dump_infos_[d];
            char words[64];
            std::snprintf(words, sizeof(words), " | %08x %08x %08x", info.channel, info.ambient, info.material);
            report << words;
            for (unsigned l = 0; l < 2u; ++l) {
                report << ' ' << info.light_index[l];
                for (float v : info.light[l]) report << ' ' << v;
                std::snprintf(words, sizeof(words), " %08x", info.light_color[l]);
                report << words;
            }
            std::snprintf(words, sizeof(words), " | %u %02x %06x %08x %08x %08x",
                          (info.surface & kRtDrawClassMask) >> kRtDrawClassShift, info.shading[0] >> 24u,
                          info.shading[0] & 0xFFFFFFu, info.shading[1], info.shading[2], info.shading[3]);
            report << words;
            // The material table entry; the TEV record's header and colour channel 0 (material, light).
            std::snprintf(words, sizeof(words), " | %u %08x %08x %08x", static_cast<unsigned>(info.rt_material),
                          info.tev.header, info.tev.channel[0], info.tev.channel[1]);
            report << words;
            for (unsigned s = 0; s < std::min(info.tev.header & 0x1Fu, 4u); ++s) {
                std::snprintf(words, sizeof(words), " %06x/%06x/%03x", info.tev_stages[s].color_env & 0xFFFFFFu,
                              info.tev_stages[s].alpha_env & 0xFFFFFFu, info.tev_stages[s].order & 0x3FFu);
                report << words;
            }
        }
        report << '\n';
    }
    const D3D12_RANGE no_write{0, 0};
    dump_readback_->Unmap(0, &no_write);
    std::cerr << "[rt] dump scene=" << dump_scene_ << " " << dump_width_ << 'x' << dump_height_ << " dir=" << directory
              << '\n';
    if (dump_remaining_ > 1u && dump_every_ != 0u) {
        --dump_remaining_;
        dump_scene_ += dump_every_;
    }
}

void SceneRayTracer::report() {
    const auto per = [](double value, std::uint64_t n) { return n != 0u ? value / static_cast<double>(n) : 0.0; };
    const auto pct = [&](unsigned word, unsigned of) {
        return window_.stats[of] != 0u
            ? 100.0 * static_cast<double>(window_.stats[word]) / static_cast<double>(window_.stats[of])
            : 0.0;
    };
    char line[768];
    std::snprintf(line, sizeof(line),
                  "[rt-stats] scenes=%llu skipped=%llu draws-per-scene=%.1f triangles-per-scene=%.0f "
                  "max-triangles=%llu capacity=%u build-ms=%.3f validate-ms=%.3f light-ms=%.3f point-lights=%.2f composites=%llu captures=%llu validated=%llu "
                  "geometry-pixels=%.2f%% match-0.1%%=%.3f%% match-1%%=%.3f%% hit-nearer=%.3f%% hit-farther=%.3f%% "
                  "miss=%.3f%% background-hit=%.3f%% glow-draws=%.1f glow-triangles=%.0f late-triangles=%.0f "
                  "exposure=%.3f adaptation=%.3f\n",
                  static_cast<unsigned long long>(window_.scenes), static_cast<unsigned long long>(window_.skipped),
                  per(static_cast<double>(window_.draws), window_.scenes),
                  per(static_cast<double>(window_.triangles), window_.scenes),
                  static_cast<unsigned long long>(window_.max_triangles), capacity_,
                  per(window_.build_ms, window_.timed), per(window_.validate_ms, window_.validated),
                  per(window_.light_ms, window_.timed), per(static_cast<double>(window_.point_lights), window_.scenes),
                  static_cast<unsigned long long>(window_.composites),
                  static_cast<unsigned long long>(window_.captures),
                  static_cast<unsigned long long>(window_.validated), pct(2u, 0u), pct(3u, 2u), pct(4u, 2u),
                  pct(5u, 2u), pct(6u, 2u), pct(7u, 2u), pct(8u, 1u),
                  per(static_cast<double>(window_.glow_draws), window_.captures),
                  per(static_cast<double>(window_.glow_triangles), window_.captures),
                  per(static_cast<double>(window_.late_triangles), window_.scenes),
                  static_cast<double>(physical_exposure_), static_cast<double>(adaptation_cpu_));
    std::cerr << line;
    if (delight_pso_) {
        // Texture de-lighting: maps made, maps held and their memory.
        // Draws: seen, not paint, no material map, no texture key, with a map, without one yet.
        std::fprintf(stderr,
                     "[rt-delight] made=%llu held=%zu kb=%llu draws=%llu not-paint=%llu no-map=%llu no-key=%llu "
                     "mapped=%llu waiting=%llu views=%u\n",
                     static_cast<unsigned long long>(delight_made_), delight_maps_.size(),
                     static_cast<unsigned long long>(delight_bytes_ >> 10u),
                     static_cast<unsigned long long>(delight_seen_[0]), static_cast<unsigned long long>(delight_seen_[1]),
                     static_cast<unsigned long long>(delight_seen_[2]), static_cast<unsigned long long>(delight_seen_[3]),
                     static_cast<unsigned long long>(delight_seen_[4]), static_cast<unsigned long long>(delight_seen_[5]),
                     delight_view_increment_);
        delight_seen_ = {};
        // Physical emitters: the last scene's emitting draws and triangles.
        std::fprintf(stderr, "[rt-emit] draws=%zu triangles=%u glow-draws=%zu glow-triangles=%u", emit_ranges_.size(),
                     emit_triangles_, glow_emitters_.size(), glow_emit_count_);
        // The emitting materials since the last report (table id: triangles).
        for (const auto& [id, triangles] : emit_materials_) {
            std::fprintf(stderr, " %u:%llu", static_cast<unsigned>(id), static_cast<unsigned long long>(triangles));
        }
        std::fprintf(stderr, "\n");
        emit_materials_.clear();
    }
    if (pass_timestamps_) {
        // Per scene: geometry and every pass after the trace (sub-frames' replays included); per pass:
        // one call's average and calls per scene. Reflections report in [rt-reflect].
        const auto& ms = window_.pass_ms;
        const auto& n = window_.passes;
        double passes_ms = 0.0;
        // The capture's inner passes (lights, bloom blur) are inside its own time, the engine's (but its glass pass)
        // inside the lighting's.
        for (unsigned p = 0; p < kPassCount; ++p) {
            if (p != kPassLights && p != kPassBloomBlur && p != kPassEmit && p != kPassMerge &&
                (p < kPassEngineGBuffer || p == kPassEngineClear)) {
                passes_ms += ms[p];
            }
        }
        const double scene_ms = per(window_.build_ms, window_.timed) + per(window_.light_ms, window_.timed) +
                                per(passes_ms, window_.scenes);
        const auto calls = [&](Pass pass) { return per(static_cast<double>(n[pass]), window_.scenes); };
        const auto call_ms = [&](Pass pass) { return per(ms[pass], n[pass]); };
        std::snprintf(line, sizeof(line),
                      "[rt-pass] scenes=%llu geometry-ms=%.3f geometry-flushes=%.2f composite-ms=%.3f "
                      "composites=%.2f glow-base-ms=%.3f capture-ms=%.3f captures=%.2f post-copy-ms=%.3f "
                      "post-bright-ms=%.3f post-focus-ms=%.3f post-shafts-ms=%.3f post-blur-ms=%.3f "
                      "post-finish-ms=%.3f posts=%.2f bloom-ms=%.3f blooms=%.2f lights-ms=%.3f (emit %.3f merge %.3f) "
                      "bloom-blur-ms=%.3f camera-ms=%.3f dropped=%llu gpu-ms-per-scene=%.3f\n",
                      static_cast<unsigned long long>(window_.scenes), per(ms[kPassGeometry], window_.scenes),
                      calls(kPassGeometry), call_ms(kPassComposite), calls(kPassComposite), call_ms(kPassGlowBase),
                      call_ms(kPassCapture), calls(kPassCapture), call_ms(kPassPostCopy), call_ms(kPassPostBright),
                      call_ms(kPassPostFocus), call_ms(kPassPostShafts), call_ms(kPassPostBlur),
                      call_ms(kPassPostFinish), calls(kPassPostFinish), call_ms(kPassBloom), calls(kPassBloom),
                      call_ms(kPassLights), call_ms(kPassEmit), call_ms(kPassMerge), call_ms(kPassBloomBlur),
                      call_ms(kPassCamera), static_cast<unsigned long long>(window_.passes_dropped), scene_ms);
        std::cerr << line;
        if (n[kPassEngineGBuffer] != 0u) {
            const double engine_ms = call_ms(kPassEngineGBuffer) + call_ms(kPassEngineDirect) +
                                     call_ms(kPassEngineShadow) + call_ms(kPassEngineIndirect) +
                                     call_ms(kPassEngineSpecular) + call_ms(kPassEngineDenoise) +
                                     call_ms(kPassEngineFinal);
            std::snprintf(line, sizeof(line),
                          "[rt-engine-pass] gbuffer-ms=%.3f direct-ms=%.3f shadow-ms=%.3f indirect-ms=%.3f "
                          "specular-ms=%.3f denoise-ms=%.3f final-ms=%.3f engine-ms=%.3f clear-ms=%.3f clears=%.2f\n",
                          call_ms(kPassEngineGBuffer), call_ms(kPassEngineDirect), call_ms(kPassEngineShadow),
                          call_ms(kPassEngineIndirect), call_ms(kPassEngineSpecular), call_ms(kPassEngineDenoise),
                          call_ms(kPassEngineFinal), engine_ms, call_ms(kPassEngineClear), calls(kPassEngineClear));
            std::cerr << line;
        }
    }
    window_ = {};
}

int SceneRayTracer::begin_pass(ID3D12GraphicsCommandList* list) {
    if (!pass_timestamps_ || list == nullptr) {
        return -1;
    }
    const unsigned entry = next_pass_entry_;
    if (pass_entries_[entry].pending) {
        ++window_.passes_dropped;
        return -1;
    }
    next_pass_entry_ = (entry + 1u) % kPassEntries;
    list->EndQuery(pass_timestamps_.Get(), D3D12_QUERY_TYPE_TIMESTAMP, entry * 2u);
    return static_cast<int>(entry);
}

void SceneRayTracer::end_pass(ID3D12GraphicsCommandList* list, int entry, Pass pass) {
    if (entry < 0) {
        return;
    }
    const auto index = static_cast<unsigned>(entry);
    list->EndQuery(pass_timestamps_.Get(), D3D12_QUERY_TYPE_TIMESTAMP, index * 2u + 1u);
    list->ResolveQueryData(pass_timestamps_.Get(), D3D12_QUERY_TYPE_TIMESTAMP, index * 2u, 2u, pass_readback_.Get(),
                           index * 2u * sizeof(std::uint64_t));
    pass_entries_[index] = PassEntry{frame_index_, pass, true};
}

void SceneRayTracer::collect_passes(std::uint64_t frame_index) {
    if (!pass_readback_ || timestamp_frequency_ == 0u) {
        return;
    }
    const std::uint64_t* ticks = nullptr;
    const double to_ms = 1000.0 / static_cast<double>(timestamp_frequency_);
    for (unsigned index = 0; index < kPassEntries; ++index) {
        PassEntry& entry = pass_entries_[index];
        if (!entry.pending || entry.frame + kResultDelay > frame_index) {
            continue;
        }
        entry.pending = false;
        if (ticks == nullptr) {
            void* mapped = nullptr;
            const D3D12_RANGE all{0, kPassEntries * 2u * sizeof(std::uint64_t)};
            if (FAILED(pass_readback_->Map(0, &all, &mapped))) {
                return;
            }
            ticks = static_cast<const std::uint64_t*>(mapped);
        }
        const std::uint64_t begin = ticks[index * 2u];
        const std::uint64_t end = ticks[index * 2u + 1u];
        if (end >= begin && end - begin < timestamp_frequency_) {
            window_.pass_ms[entry.pass] += static_cast<double>(end - begin) * to_ms;
            ++window_.passes[entry.pass];
        }
    }
    if (ticks != nullptr) {
        const D3D12_RANGE no_write{0, 0};
        pass_readback_->Unmap(0, &no_write);
    }
}

void SceneRayTracer::set_texture_views(D3D12_CPU_DESCRIPTOR_HANDLE cpu, std::uint32_t first, UINT increment) noexcept {
    texture_view_cpu_ = cpu;
    texture_view_first_ = first;
    texture_view_increment_ = increment;
    texture_view_blocks_ = {};
    texture_view_slots_.clear();
    texture_view_count_ = 0u;
    texture_views_usable_ = false;
}

std::uint64_t SceneRayTracer::texture_views_fence() const noexcept {
    const TextureViewBlock& next = texture_view_blocks_[(texture_view_scene_ + 1u) % kTextureViewScenes];
    return next.closed ? ~0ull : next.fence;
}

void SceneRayTracer::note_submission(std::uint64_t fence_value) noexcept {
    for (TextureViewBlock& block : texture_view_blocks_) {
        if (block.closed) {
            block.closed = false;
            block.fence = fence_value;
        }
    }
}

// GALAXY_D3D_DRED=1: a breadcrumb context before each pass, so a removed device's dump names the pass it stopped in
// (renderer_d3d12.cpp dump_device_removal).
void SceneRayTracer::dred_marker(ID3D12GraphicsCommandList* list, const wchar_t* name) {
    static const bool on = env_float("GALAXY_D3D_DRED", 0.0f) != 0.0f;
    if (on) {
        list->SetMarker(0u, name, static_cast<UINT>((std::wcslen(name) + 1u) * sizeof(wchar_t)));
        // The first passes recorded, in order (the breadcrumbs' markers carry no names on every driver).
        static std::uint32_t recorded = 0u;
        if (recorded < 600u) {
            std::fprintf(stderr, "[dred] marker %u %ls\n", recorded++, name);
        }
    }
}

std::uint32_t SceneRayTracer::texture_view(const RtTextureSource& source) {
    if (!texture_views_usable_ || source.resource == nullptr || source.view == 0u || !device_) {
        return 0xFFFFFFFFu;
    }
    auto* const resource = static_cast<ID3D12Resource*>(source.resource);
    if (const auto found = texture_view_slots_.find(source.view);
        found != texture_view_slots_.end() && found->second.resource == resource) {
        return found->second.index;
    }
    if (texture_view_count_ >= kTextureViewsPerScene) {
        if (!texture_view_full_logged_) {
            texture_view_full_logged_ = true;
            std::cerr << "[rt] a scene samples more than " << kTextureViewsPerScene
                      << " textures: the rest are traced untextured\n";
        }
        return 0xFFFFFFFFu;
    }
    const std::uint32_t slot = texture_view_scene_ * kTextureViewsPerScene + texture_view_count_++;
    D3D12_CPU_DESCRIPTOR_HANDLE destination = texture_view_cpu_;
    destination.ptr += static_cast<SIZE_T>(slot) * texture_view_increment_;
    device_->CopyDescriptorsSimple(1u, destination, D3D12_CPU_DESCRIPTOR_HANDLE{source.view},
                                   D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    texture_view_blocks_[texture_view_scene_].resources.emplace_back(resource);
    const std::uint32_t index = texture_view_first_ + slot;
    texture_view_slots_[source.view] = {index, resource};
    return index;
}

std::uint32_t SceneRayTracer::buffer_view(ID3D12Resource* resource, std::uint64_t offset, std::uint64_t bytes) {
    // Raw views address 16-byte-aligned offsets in whole 4-byte words.
    if (!texture_views_usable_ || resource == nullptr || !device_ || (offset & 15u) != 0u || bytes < 4u ||
        texture_view_count_ >= kTextureViewsPerScene) {
        return 0xFFFFFFFFu;
    }
    const std::uint32_t slot = texture_view_scene_ * kTextureViewsPerScene + texture_view_count_++;
    D3D12_CPU_DESCRIPTOR_HANDLE destination = texture_view_cpu_;
    destination.ptr += static_cast<SIZE_T>(slot) * texture_view_increment_;
    D3D12_SHADER_RESOURCE_VIEW_DESC view{};
    view.Format = DXGI_FORMAT_R32_TYPELESS;
    view.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
    view.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    view.Buffer.FirstElement = offset / 4u;
    view.Buffer.NumElements = static_cast<UINT>(std::min<std::uint64_t>(bytes / 4u, 0x7FFFFFFFu));
    view.Buffer.Flags = D3D12_BUFFER_SRV_FLAG_RAW;
    device_->CreateShaderResourceView(resource, &view, destination);
    texture_view_blocks_[texture_view_scene_].resources.emplace_back(resource);
    return texture_view_first_ + slot;
}

void SceneRayTracer::begin_scene() noexcept {
    // The closing scene's views stay in use until the renderer frame being recorded is done (its water trace can
    // be in it). This scene takes the next block, which the renderer waited for (texture_views_fence); a block
    // still in unsubmitted work is left alone and this scene's draws are traced untextured.
    texture_view_blocks_[texture_view_scene_].closed = texture_view_increment_ != 0u;
    texture_view_scene_ = (texture_view_scene_ + 1u) % kTextureViewScenes;
    TextureViewBlock& views = texture_view_blocks_[texture_view_scene_];
    texture_views_usable_ = texture_view_increment_ != 0u && !views.closed;
    if (texture_views_usable_) {
        views.resources.clear();
        views.fence = 0u;
    }
    texture_view_slots_.clear();
    texture_view_count_ = 0u;
    emit_ranges_.clear();
    emit_range_keys_.clear();
    emit_range_materials_.clear();
    emit_triangles_ = 0u;
    // De-light jobs the last scene's trace did not run: their source views are gone with its block.
    for (const DelightJob& job : delight_jobs_) {
        if (const auto found = delight_maps_.find(job.pack); found != delight_maps_.end()) {
            delight_bytes_ -= found->second.bytes;
            delight_free_.push_back(found->second.slot);
            retire(found->second.texture);
            delight_maps_.erase(found);
        }
    }
    delight_jobs_.clear();
    light_tex_ready_ = false;
    draws_.clear();
    glow_draws_.clear();
    lens_draws_ = 0u;
    lens_ready_ = false;
    star_first_ = {};
    star_count_ = {};
    star_draws_ = 0u;
    solid_draws_ = 0u;
    glow_lights_.clear();
    glow_emitters_.clear();
    glow_emitter_keys_.clear();
    glow_tev_records_.clear();
    glow_tev_stages_.clear();
    glow_emitter_materials_.clear();
    glow_emitter_slots_ = 0u;
    authored_.clear();
    glow_materials_.clear();
    authored_materials_.clear();
    glow_triangles_ = 0u;
    glow_open_ = false;
    glow_base_valid_ = false;
    water_draws_.clear();
    water_waves_.clear();
    water_clear_.clear();
    water_game_waves_ = false;
    water_triangles_ = 0u;
    water_flushed_draws_ = 0u;
    water_flushed_triangles_ = 0u;
    if (water_open_ && log_seq_) {
        std::fprintf(stderr, "[rt-seq] frame=%llu begin-scene closes an open water window (%zu draws)\n",
                     static_cast<unsigned long long>(frame_index_), water_draws_.size());
    }
    water_open_ = false;
    water_incomplete_ = false;
    draw_colors_.clear();
    draw_textures_.clear();
    draw_materials_.clear();
    tev_records_.clear();
    tev_stages_.clear();
    infos_.clear();
    triangles_ = 0u;
    flushed_draws_ = 0u;
    flushed_triangles_ = 0u;
    view_known_ = false;
    scene_open_ = ready_;
    scene_incomplete_ = false;
    light_slot_ = -1;
    late_draws_.clear();
    late_triangles_ = 0u;
    late_flushed_draws_ = 0u;
    late_flushed_triangles_ = 0u;
    late_open_ = false;
    late_incomplete_ = false;
}

void SceneRayTracer::note_tev(const RtDrawInfo& info, std::vector<RtTevMaterial>& records,
                              std::vector<RtTevStage>& stages, std::uint32_t delight) {
    RtTevMaterial record = info.tev;
    record.stage_offset = static_cast<std::uint32_t>(stages.size());
    const unsigned count = std::min(record.header & 0x1Fu, kRtTevMaxStages);
    // GALAXY_RT_LOG_TEV=<table material id>: that material's TEV program, once (its colours and stages as the hits
    // evaluate them; rt_types.h RtTevMaterial, RtTevStage).
    static const std::uint32_t log_tev = static_cast<std::uint32_t>(env_float("GALAXY_RT_LOG_TEV", 0.0f));
    if (log_tev != 0u && info.rt_material == log_tev) [[unlikely]] {
        static bool logged = false;
        if (!logged) {
            logged = true;
            std::fprintf(stderr,
                         "[rt-tev] material=%u header=%08X alpha-test=%08X channel=%08X,%08X,%08X,%08X "
                         "konst=%08X,%08X,%08X,%08X regs=%08X,%08X,%08X,%08X,%08X,%08X,%08X,%08X\n",
                         log_tev, record.header, record.alpha_test, record.channel[0], record.channel[1],
                         record.channel[2], record.channel[3], record.konst[0], record.konst[1], record.konst[2],
                         record.konst[3], record.regs[0], record.regs[1], record.regs[2], record.regs[3], record.regs[4],
                         record.regs[5], record.regs[6], record.regs[7]);
            for (unsigned s = 0; s < count; ++s) {
                std::fprintf(stderr, "[rt-tev]   stage %u color=%08X alpha=%08X order=%08X ksel=%08X scale=%g,%g env=%u\n",
                             s, info.tev_stages[s].color_env, info.tev_stages[s].alpha_env, info.tev_stages[s].order,
                             info.tev_stages[s].ksel, info.tev_stages[s].scale_s, info.tev_stages[s].scale_t,
                             info.tev_stages[s].pad[0]);
            }
        }
    }
    stages.insert(stages.end(), info.tev_stages.begin(), info.tev_stages.begin() + count);
    for (unsigned m = 0; m < 8u; ++m) {
        record.maps[m] = info.tev_maps[m].resource != nullptr ? texture_view(info.tev_maps[m]) : 0xFFFFFFFFu;
    }
    // The material texture: its de-light map (pad[1]), its map, the texture slot and coordinate scale of the first stage
    // that samples it (the coordinates the raster's texel came from; past the generators coordinate 0, as fill_rt_tev),
    // and the table's glowing region of it (pad[2] bits 8-31: u0, v0, u1, v1 in 63rds; 0: all of it).
    record.pad[1] = 0xFFFFFFFFu;
    record.pad[2] = 0u;
    std::uint32_t region = 0u;
    if (const RtMaterial* const material = rt_material(info.rt_material);
        material != nullptr && material->emission != RtEmission::None &&
        (material->region[0] > 0.0f || material->region[1] > 0.0f || material->region[2] < 1.0f ||
         material->region[3] < 1.0f)) {
        for (unsigned k = 0; k < 4u; ++k) {
            region |= static_cast<std::uint32_t>(std::lround(material->region[k] * 63.0f)) << (8u + 6u * k);
        }
    }
    // A table light with a `cone` sends a beam (a lamp's reflector, a headlamp's lens): its radiance falls off as cos^n
    // from its face's normal, n the exponent that halves it at the cone's half angle (pad[0] bits 16-31; 0: Lambertian).
    if (const RtMaterial* const material = rt_material(info.rt_material);
        material != nullptr && material->emission != RtEmission::None && material->cone > 0.0f) {
        const float c = std::cos(std::clamp(material->cone, 1.0f, 89.0f) * 3.14159265f / 180.0f);
        const float n = std::log(0.5f) / std::log(c);
        record.pad[0] = (record.pad[0] & 0xFFFFu) | (static_cast<std::uint32_t>(std::clamp(std::lround(n), 1L, 65535L)) << 16u);
    }
    if (info.texture_map < 8u) {
        const unsigned texgens = record.header >> 28u;
        for (unsigned s = 0; s < count; ++s) {
            const std::uint32_t order = info.tev_stages[s].order;
            if ((order & (1u << 6u)) == 0u || (order & 7u) != info.texture_map || texgens == 0u) {
                continue;
            }
            const unsigned coord = ((order >> 3u) & 7u) < texgens ? (order >> 3u) & 7u : 0u;
            record.pad[1] = delight;
            record.pad[2] = info.texture_map | (((record.header >> (8u + 2u * coord)) & 1u) << 3u) |
                            ((info.texture_wrap & 0xFu) << 4u) | region;
            record.pad[3] = std::bit_cast<std::uint32_t>(info.tev_stages[s].scale_s);
            record.pad[4] = std::bit_cast<std::uint32_t>(info.tev_stages[s].scale_t);
            break;
        }
    }
    records.push_back(record);
}

namespace {

// A de-light map's levels in the scratch buffer: placed footprints (offsets 512-aligned, rows 256-aligned), a byte a
// texel.
struct DelightLayout {
    std::array<std::uint64_t, 12> offset{};
    std::array<std::uint32_t, 12> pitch{};
    std::uint64_t bytes = 0;
};

DelightLayout delight_layout(std::uint32_t width, std::uint32_t height, std::uint32_t levels) {
    constexpr std::uint64_t kPlace = D3D12_TEXTURE_DATA_PLACEMENT_ALIGNMENT;
    constexpr std::uint32_t kPitch = D3D12_TEXTURE_DATA_PITCH_ALIGNMENT;
    DelightLayout layout;
    std::uint64_t at = 0u;
    for (std::uint32_t l = 0; l < levels; ++l) {
        const std::uint32_t lw = std::max(1u, width >> l);
        const std::uint32_t lh = std::max(1u, height >> l);
        at = (at + kPlace - 1u) & ~(kPlace - 1u);
        layout.offset[l] = at;
        layout.pitch[l] = (lw + kPitch - 1u) & ~(kPitch - 1u);
        at += static_cast<std::uint64_t>(layout.pitch[l]) * lh;
    }
    layout.bytes = (at + kPlace - 1u) & ~(kPlace - 1u);
    return layout;
}

// rt_delight.hlsl RtDelight.
struct DelightConstants {
    std::uint32_t source;
    std::uint32_t size[2];
    std::uint32_t source_levels;
    std::uint32_t out;
    std::uint32_t out_pitch;
    std::uint32_t out_size[2];
    std::uint32_t in;
    std::uint32_t in_pitch;
    std::uint32_t in_size[2];
    std::uint32_t wrap;
};
static_assert(sizeof(DelightConstants) == 52u);

}  // namespace

void SceneRayTracer::set_delight_views(D3D12_CPU_DESCRIPTOR_HANDLE cpu, std::uint32_t first, UINT increment) noexcept {
    delight_view_cpu_ = cpu;
    delight_view_first_ = first;
    delight_view_increment_ = increment;
    reset_delight();
}

// No de-light maps: every view free (the ray tracer starting or stopping; the GPU is idle then).
void SceneRayTracer::reset_delight() noexcept {
    delight_maps_.clear();
    delight_jobs_.clear();
    delight_retired_slots_.clear();
    delight_bytes_ = 0u;
    delight_free_.clear();
    for (std::uint32_t slot = kDelightViews; slot-- > 0u;) {
        delight_free_.push_back(slot);
    }
}

// The de-light map of a traced draw's material texture: its view, or 0xFFFFFFFF until it is made. A texture first
// drawn in this scene gets its map and a job in this scene's trace (run_delight), within the frame's budget of
// texels; the rest wait for a later scene.
std::uint32_t SceneRayTracer::delight_view(const RtTextureSource& source, std::uint32_t wrap) {
    if (!delight_ || !delight_pso_ || delight_view_increment_ == 0u || source.pack == 0u ||
        source.resource == nullptr || source.width < 4u || source.height < 4u) {
        return 0xFFFFFFFFu;
    }
    if (const auto found = delight_maps_.find(source.pack); found != delight_maps_.end()) {
        found->second.used = scene_serial_;
        return found->second.ready ? delight_view_first_ + found->second.slot : 0xFFFFFFFFu;
    }
    const std::uint32_t width = source.width;
    const std::uint32_t height = source.height;
    std::uint32_t levels = 1u;
    while ((std::max(width, height) >> levels) != 0u) {
        ++levels;
    }
    std::uint64_t texels = static_cast<std::uint64_t>(width) * height;
    std::uint64_t scratch = delight_layout(width, height, levels).bytes;
    for (const DelightJob& job : delight_jobs_) {
        texels += static_cast<std::uint64_t>(job.width) * job.height;
        scratch += delight_layout(job.width, job.height, job.levels).bytes;
    }
    if ((!delight_jobs_.empty() && texels > kDelightTexelsPerFrame) || scratch > kDelightScratchBytes) {
        return 0xFFFFFFFFu;
    }
    std::uint64_t bytes = 0u;
    for (std::uint32_t l = 0; l < levels; ++l) {
        bytes += static_cast<std::uint64_t>(std::max(1u, width >> l)) * std::max(1u, height >> l);
    }
    // Past the budget or the views the least recently drawn maps go (never one this or the last two scenes drew).
    while (delight_free_.empty() || delight_bytes_ + bytes > kDelightBytes) {
        auto oldest = delight_maps_.end();
        for (auto it = delight_maps_.begin(); it != delight_maps_.end(); ++it) {
            if (it->second.ready && it->second.used + 2u < scene_serial_ &&
                (oldest == delight_maps_.end() || it->second.used < oldest->second.used)) {
                oldest = it;
            }
        }
        if (oldest == delight_maps_.end()) {
            return 0xFFFFFFFFu;
        }
        delight_bytes_ -= oldest->second.bytes;
        delight_retired_slots_.emplace_back(oldest->second.slot, frame_index_);
        retire(oldest->second.texture);
        delight_maps_.erase(oldest);
    }
    const std::uint32_t source_view = texture_view(source);
    if (source_view == 0xFFFFFFFFu) {
        return 0xFFFFFFFFu;
    }
    DelightMap map;
    D3D12_HEAP_PROPERTIES heap{};
    heap.Type = D3D12_HEAP_TYPE_DEFAULT;
    D3D12_RESOURCE_DESC desc{};
    desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    desc.Width = width;
    desc.Height = height;
    desc.DepthOrArraySize = 1u;
    desc.MipLevels = static_cast<UINT16>(levels);
    desc.Format = DXGI_FORMAT_R8_SNORM;
    desc.SampleDesc.Count = 1u;
    desc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
    if (FAILED(device_->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_COPY_DEST,
                                                nullptr, IID_PPV_ARGS(&map.texture)))) {
        return 0xFFFFFFFFu;
    }
    map.texture->SetName(L"rt delight map");
    map.slot = delight_free_.back();
    delight_free_.pop_back();
    map.bytes = bytes;
    map.used = scene_serial_;
    D3D12_SHADER_RESOURCE_VIEW_DESC view{};
    view.Format = DXGI_FORMAT_R8_SNORM;
    view.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    view.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    view.Texture2D.MipLevels = levels;
    D3D12_CPU_DESCRIPTOR_HANDLE at = delight_view_cpu_;
    at.ptr += static_cast<SIZE_T>(map.slot) * delight_view_increment_;
    device_->CreateShaderResourceView(map.texture.Get(), &view, at);
    delight_bytes_ += bytes;
    delight_jobs_.push_back(
        {source.pack, source_view, width, height, std::max<std::uint32_t>(source.levels, 1u), levels, wrap});
    delight_maps_.emplace(source.pack, std::move(map));
    return 0xFFFFFFFFu;
}

// This scene's de-light jobs: level 0 of each map (DelightCS), then each further level from the one above
// (DelightMipCS), all in the scratch buffer, then copied into the maps.
void SceneRayTracer::run_delight(ID3D12GraphicsCommandList* list) {
    if (delight_jobs_.empty()) {
        return;
    }
    if (!delight_scratch_ &&
        !create_buffer(kDelightScratchBytes, D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                       D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS, delight_scratch_, L"rt delight scratch")) {
        for (const DelightJob& job : delight_jobs_) {
            if (const auto found = delight_maps_.find(job.pack); found != delight_maps_.end()) {
                delight_bytes_ -= found->second.bytes;
                delight_free_.push_back(found->second.slot);
                retire(found->second.texture);
                delight_maps_.erase(found);
            }
        }
        delight_jobs_.clear();
        return;
    }
    to_state(list, delight_scratch_.Get(), delight_scratch_state_, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    std::vector<DelightLayout> layouts;
    std::vector<std::uint64_t> bases;
    std::uint64_t at = 0u;
    std::uint32_t most_levels = 1u;
    for (const DelightJob& job : delight_jobs_) {
        layouts.push_back(delight_layout(job.width, job.height, job.levels));
        bases.push_back(at);
        at += layouts.back().bytes;
        most_levels = std::max(most_levels, job.levels);
    }
    list->SetComputeRootSignature(delight_root_.Get());
    list->SetComputeRootUnorderedAccessView(1u, delight_scratch_->GetGPUVirtualAddress());
    dred_marker(list, L"delight");
    list->SetPipelineState(delight_pso_.Get());
    // GALAXY_RT_DELIGHT_STRENGTH (default 0.5): the share of each texel's measured shading the map takes out, in the
    // wrap word's bits 8-15 (rt_delight.hlsl). Retinex cannot tell painted light from a pattern of one colour (a
    // stone's cracks, sand's grain): taking all of it out flattens such textures to grey, while half keeps the pattern
    // and the texture's hue and removes half the painted light.
    static const std::uint32_t strength =
        static_cast<std::uint32_t>(std::lround(std::clamp(env_float("GALAXY_RT_DELIGHT_STRENGTH", 0.5f), 0.0f, 1.0f) *
                                               255.0f));
    for (std::size_t i = 0; i < delight_jobs_.size(); ++i) {
        const DelightJob& job = delight_jobs_[i];
        const DelightConstants constants{job.source, {job.width, job.height}, job.source_levels,
                                         static_cast<std::uint32_t>(bases[i]), layouts[i].pitch[0],
                                         {job.width, job.height}, 0u, 0u, {0u, 0u}, job.wrap | (strength << 8u)};
        list->SetComputeRoot32BitConstants(0u, sizeof(constants) / 4u, &constants, 0u);
        list->Dispatch((((job.width + 3u) / 4u) + 7u) / 8u, (job.height + 7u) / 8u, 1u);
    }
    const D3D12_RESOURCE_BARRIER scratch_uav = uav_barrier(delight_scratch_.Get());
    list->ResourceBarrier(1u, &scratch_uav);
    dred_marker(list, L"delight_mip");
    list->SetPipelineState(delight_mip_pso_.Get());
    for (std::uint32_t l = 1u; l < most_levels; ++l) {
        for (std::size_t i = 0; i < delight_jobs_.size(); ++i) {
            const DelightJob& job = delight_jobs_[i];
            if (l >= job.levels) {
                continue;
            }
            const std::uint32_t w = std::max(1u, job.width >> l);
            const std::uint32_t h = std::max(1u, job.height >> l);
            const DelightConstants constants{0u, {0u, 0u}, 0u,
                                             static_cast<std::uint32_t>(bases[i] + layouts[i].offset[l]),
                                             layouts[i].pitch[l], {w, h},
                                             static_cast<std::uint32_t>(bases[i] + layouts[i].offset[l - 1u]),
                                             layouts[i].pitch[l - 1u],
                                             {std::max(1u, job.width >> (l - 1u)), std::max(1u, job.height >> (l - 1u))},
                                             job.wrap};
            list->SetComputeRoot32BitConstants(0u, sizeof(constants) / 4u, &constants, 0u);
            list->Dispatch((((w + 3u) / 4u) + 7u) / 8u, (h + 7u) / 8u, 1u);
        }
        list->ResourceBarrier(1u, &scratch_uav);
    }
    to_state(list, delight_scratch_.Get(), delight_scratch_state_, D3D12_RESOURCE_STATE_COPY_SOURCE);
    std::vector<D3D12_RESOURCE_BARRIER> ready;
    for (std::size_t i = 0; i < delight_jobs_.size(); ++i) {
        const DelightJob& job = delight_jobs_[i];
        const auto found = delight_maps_.find(job.pack);
        if (found == delight_maps_.end()) {
            continue;
        }
        for (std::uint32_t l = 0; l < job.levels; ++l) {
            D3D12_TEXTURE_COPY_LOCATION destination{};
            destination.pResource = found->second.texture.Get();
            destination.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
            destination.SubresourceIndex = l;
            D3D12_TEXTURE_COPY_LOCATION from{};
            from.pResource = delight_scratch_.Get();
            from.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
            from.PlacedFootprint.Offset = bases[i] + layouts[i].offset[l];
            from.PlacedFootprint.Footprint = {DXGI_FORMAT_R8_SNORM, std::max(1u, job.width >> l),
                                              std::max(1u, job.height >> l), 1u, layouts[i].pitch[l]};
            list->CopyTextureRegion(&destination, 0u, 0u, 0u, &from, nullptr);
        }
        ready.push_back(transition(found->second.texture.Get(), D3D12_RESOURCE_STATE_COPY_DEST,
                                   D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE |
                                       D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE));
        found->second.ready = true;
        ++delight_made_;
    }
    if (!ready.empty()) {
        list->ResourceBarrier(static_cast<UINT>(ready.size()), ready.data());
    }
    delight_jobs_.clear();
}

namespace {

// TEV records and their stages as rt_tev.hlsli reads them: the records, then the stages, each record's stage_offset
// the byte offset of its first stage in the buffer they are written into at byte `base`.
std::size_t tev_block_bytes(const std::vector<RtTevMaterial>& records, const std::vector<RtTevStage>& stages) {
    return records.size() * sizeof(RtTevMaterial) + std::max<std::size_t>(stages.size(), 1u) * sizeof(RtTevStage);
}

void write_tev_block(const std::vector<RtTevMaterial>& records, const std::vector<RtTevStage>& stages, std::byte* out,
                     std::size_t base) {
    const std::size_t record_bytes = records.size() * sizeof(RtTevMaterial);
    auto* const record_out = reinterpret_cast<RtTevMaterial*>(out);
    for (std::size_t i = 0; i < records.size(); ++i) {
        record_out[i] = records[i];
        record_out[i].stage_offset =
            static_cast<std::uint32_t>(base + record_bytes + records[i].stage_offset * sizeof(RtTevStage));
    }
    if (!stages.empty()) {
        std::memcpy(out + record_bytes, stages.data(), stages.size() * sizeof(RtTevStage));
    }
}

}  // namespace

D3D12_GPU_VIRTUAL_ADDRESS SceneRayTracer::upload_tev(const std::vector<RtTevMaterial>& records,
                                                     const std::vector<RtTevStage>& stages, Allocate allocate,
                                                     void* allocate_context, RtUpload* uploaded,
                                                     std::uint64_t* uploaded_bytes) {
    if (records.empty()) {
        return 0u;
    }
    const std::size_t bytes = tev_block_bytes(records, stages);
    const RtUpload upload = allocate(allocate_context, bytes, 256u);
    if (upload.cpu == nullptr) {
        return 0u;
    }
    write_tev_block(records, stages, static_cast<std::byte*>(upload.cpu), 0u);
    if (uploaded != nullptr) {
        *uploaded = upload;
    }
    if (uploaded_bytes != nullptr) {
        *uploaded_bytes = bytes;
    }
    return upload.gpu;
}

void SceneRayTracer::begin_sky() noexcept {
    sky_draws_.clear();
    sky_records_.clear();
    sky_tev_records_.clear();
    sky_tev_stages_.clear();
    sky_triangles_ = 0u;
    sky_flushed_draws_ = 0u;
    sky_flushed_triangles_ = 0u;
    sky_incomplete_ = false;
    sky_built_ = 0u;
    sky_open_ = lighting_active() && sky_probe_pso_ && sky_trace_sh_pso_;
}

void SceneRayTracer::add_draw(const RtDraw& draw, const RtDrawInfo& info) {
    // The game's sky lists: the traced sky's geometry, its record in sky_records_.
    if ((info.surface & kRtDrawSky) != 0u) {
        if (sky_open_ && draw.triangle_count != 0u && sky_triangles_ + draw.triangle_count <= kSkyMaxTriangles &&
            sky_records_.size() < 0xFFFFu) {
            RtDraw& entry = sky_draws_.emplace_back(draw);
            entry.first_triangle = sky_triangles_;
            entry.flags = (entry.flags & 0xFFFFu) | (static_cast<std::uint32_t>(sky_records_.size()) << 16u);
            sky_triangles_ += draw.triangle_count;
            const XfChannelCtrl channel = decode_xf_channel_ctrl(info.channel);
            sky_records_.push_back({texture_view(info.texture), (channel.material_from_vertex ? 1u : 0u) | (info.blend << 1u),
                                    info.material, info.alpha_from});
            note_tev(info, sky_tev_records_, sky_tev_stages_);
        }
        return;
    }
    const RtMaterial* const material = rt_material(info.rt_material);
    // The emitter log's count of what became of the table's light sources' draws.
    const auto outcome = [&](EmitterOutcome kind) {
        if (log_emitters_ && material != nullptr && material->emission != RtEmission::None) {
            ++emitter_outcomes_[info.rt_material][kind];
        }
    };
    // A lens is a glow draw with the lens bit (a water draw's mirror bit is the same bit).
    if ((info.surface & (kRtDrawGlow | kRtDrawLens)) == (kRtDrawGlow | kRtDrawLens)) {
        if (glow_open_ && draw.triangle_count != 0u && lens_draws_ < kLensSlots &&
            glow_triangles_ + draw.triangle_count <= kGlowMaxTriangles) {
            RtDraw& entry = glow_draws_.emplace_back(draw);
            entry.first_triangle = glow_triangles_;
            glow_lights_.push_back({glow_triangles_, draw.triangle_count, 0u, 0.0f, 0u, 0.0f, kGlowLens, -1.0f});
            glow_materials_.push_back(info.rt_material);
            lens_first_[lens_draws_] = glow_triangles_;
            lens_count_[lens_draws_] = draw.triangle_count;
            ++lens_draws_;
            glow_triangles_ += draw.triangle_count;
        }
        return;
    }
    if ((info.surface & kRtDrawGlow) != 0u) {
        // A glow the material table marks as no light (an atmosphere, a halo) lights nothing.
        if (material != nullptr && material->no_light) {
            return;
        }
        if (glow_open_ && draw.triangle_count != 0u && glow_triangles_ + draw.triangle_count <= kGlowMaxTriangles) {
            RtDraw& entry = glow_draws_.emplace_back(draw);
            entry.first_triangle = glow_triangles_;
            // A glow the table names as a light (fire, lava sparks, electricity) shines in the table's colour
            // at its intensity's share of what the picture shows it adding; else as the picture shows it.
            AuthoredLight light{glow_triangles_, draw.triangle_count, 0u, 1.0f, info.rt_material, 0.0f, 0u, -1.0f};
            if (material != nullptr && material->emission != RtEmission::None) {
                const auto unit = [](float v) {
                    return static_cast<std::uint32_t>(std::lround(std::clamp(v, 0.0f, 1.0f) * 255.0f));
                };
                light.color = (unit(material->color[0]) << 24u) | (unit(material->color[1]) << 16u) |
                              (unit(material->color[2]) << 8u);
                light.intensity = material->intensity;
                light.pulse = material->pulse;
                light.flags = kGlowTableIntensity | (material->tint ? 0u : kGlowTableColor);
                // Under physical light an area light of its own triangles (GlowEmitterCS): its radiance the table's
                // radiance (at least 1: what it shows) times the table's colour, or for a `tint` light source the colour
                // it draws (its TEV over its triangles; its record in glow_tev_records_), lit from the next frame.
                // GALAXY_RT_GLOW_EMITTERS=0: none (the art glow lights take them); GALAXY_RT_GLOW_ISOTROPIC=0: each
                // triangle a two-sided flat panel. Isotropic is the default because a sprite faces the camera: as a
                // panel its light turns with the view and stops at the sprite's plane in a hard edge.
                static const bool glow_emitters = env_float("GALAXY_RT_GLOW_EMITTERS", 1.0f) != 0.0f;
                static const bool isotropic = env_float("GALAXY_RT_GLOW_ISOTROPIC", 1.0f) != 0.0f;
                if (glow_emitters && !material->no_light && !material->hide &&
                    glow_emitter_slots_ + draw.triangle_count <= kGlowEmitters) {
                    light.flags |= kGlowPhysical;
                    glow_emitter_materials_.push_back(static_cast<std::uint16_t>(info.rt_material));
                    std::uint32_t record = 0xFFFFFFFFu;
                    if (material->tint) {
                        record = static_cast<std::uint32_t>(glow_tev_records_.size());
                        note_tev(info, glow_tev_records_, glow_tev_stages_);
                    }
                    const std::uint32_t flags = (isotropic ? kGlowEmitterIsotropic : 0u) |
                                                (material->tint ? kGlowEmitterDrawn : 0u) |
                                                (material->volume ? kGlowEmitterVolume : 0u) |
                                                (((info.blend >> 1u) & 7u) << kGlowEmitterBlendShift);
                    glow_emitters_.push_back({glow_triangles_, draw.triangle_count, glow_emitter_slots_, light.color,
                                              std::bit_cast<std::uint32_t>(std::max(material->intensity, 1.0f)),
                                              texture_view(info.texture), flags, record});
                    glow_emitter_keys_.push_back(info.light_key);
                    glow_emitter_slots_ += draw.triangle_count;
                }
            } else if (material != nullptr && material->particle) {
                light.flags = kGlowEffect;
            }
            glow_lights_.push_back(light);
            glow_materials_.push_back(info.rt_material);
            outcome(kOutcomeGlow);
            glow_triangles_ += draw.triangle_count;
        }
        return;
    }
    if ((info.surface & kRtDrawWater) != 0u) {
        if (water_open_ && draw.triangle_count != 0u && water_triangles_ + draw.triangle_count <= kWaterMaxTriangles) {
            RtDraw& entry = water_draws_.emplace_back(draw);
            entry.first_triangle = water_triangles_;
            // Its record (bits 16-31: rt_reflections.hlsl water_wave reads its waves by it).
            entry.flags = (entry.flags & 0xFFFFu) |
                          (static_cast<std::uint32_t>(std::min<std::size_t>(water_draws_.size() - 1u, 0xFFFFu)) << 16u);
            water_triangles_ += draw.triangle_count;
            // Its clear record: the table's roughness; the engine's glass (glass or crystal, not mirror glass).
            {
                const RtMaterial* const clear = rt_material(info.rt_material);
                const auto surface_class = (info.surface >> kRtDrawClassShift) & 15u;
                const float roughness = clear != nullptr ? clear->roughness : -1.0f;
                water_clear_.push_back(
                    (roughness >= 0.0f ? static_cast<std::uint32_t>(std::lround(std::min(roughness, 1.0f) * 65534.0f))
                                       : 0xFFFFu) |
                    (surface_class == static_cast<std::uint32_t>(RtSurfaceClass::Crystal) &&
                             (info.surface & kRtDrawWaterMirror) == 0u
                         ? 0x10000u
                         : 0u));
            }
            std::array<std::uint32_t, 12> waves{};
            waves[0] = 0xFFFFFFFFu;
            const std::uint32_t wave_flags = std::bit_cast<std::uint32_t>(info.wave_params[0]);
            if ((wave_flags & 1u) != 0u && info.wave.resource != nullptr) {
                waves[0] = texture_view(info.wave);
                for (unsigned k = 0; k < 11u; ++k) {
                    waves[1u + k] = std::bit_cast<std::uint32_t>(info.wave_params[k]);
                }
                water_game_waves_ = water_game_waves_ || waves[0] != 0xFFFFFFFFu;
            }
            water_waves_.push_back(waves);
        }
        return;
    }
    if (!scene_open_ && info.phase == kRtLatePhase) {
        outcome(kOutcomeLate);
        // Shadow, sky and reflection rays meet it; nothing reads its draw record (no lights, no emission).
        if (late_open_ && draw.triangle_count != 0u && late_triangles_ + draw.triangle_count <= kLateMaxTriangles) {
            RtDraw& entry = late_draws_.emplace_back(draw);
            entry.first_triangle = late_triangles_;
            entry.flags &= 0xFFFFu;
            late_triangles_ += draw.triangle_count;
        }
        return;
    }
    if (!scene_open_ || draw.triangle_count == 0u || triangles_ + draw.triangle_count > kMaxTriangles) {
        outcome(kOutcomeClosed);
        return;
    }
    RtDraw& entry = draws_.emplace_back(draw);
    entry.first_triangle = triangles_;
    entry.flags = (entry.flags & 0xFFFFu) |
                  (static_cast<std::uint32_t>(std::min<std::size_t>(draws_.size() - 1u, 0xFFFFu)) << 16u);
    triangles_ += draw.triangle_count;
    // Its material's emission (rt_lighting.hlsl kEmit*): colour RGBA8 with the kind (1 point, 2 area),
    // whether the table names it (4) and whether it never lights (8) in the low byte; intensity;
    // roughness and metalness (unorm16 pair, 0xFFFF: the class's).
    std::uint32_t emission = 0u;
    float intensity = 0.0f;
    std::uint32_t surface = unit_pair(-1.0f, -1.0f);
    std::uint32_t flags = 0u;
    if (material != nullptr) {
        const auto unit = [](float v) { return static_cast<std::uint32_t>(std::lround(std::clamp(v, 0.0f, 1.0f) * 255.0f)); };
        emission = (unit(material->color[0]) << 24u) | (unit(material->color[1]) << 16u) |
                   (unit(material->color[2]) << 8u) | static_cast<std::uint32_t>(material->emission) | 4u |
                   (material->no_light ? 8u : 0u);
        intensity = material->intensity;
        surface = unit_pair(material->matte ? 1.0f : material->roughness, material->metallic);
        if (material->star && star_draws_ < kStarSlots) {
            star_first_[star_draws_] = entry.first_triangle;
            star_count_[star_draws_] = entry.triangle_count;
            ++star_draws_;
        }
        flags = (material->tint ? kDrawTableTint : 0u) | (material->foliage ? kDrawTableFoliage : 0u) |
                (material->star ? kDrawTableStar : 0u) |
                ((info.surface & kRtDrawCard) != 0u ? kDrawTableCard : 0u) |
                (material->mirror ? kDrawTableMirror : 0u) | (material->recolour ? kDrawTableRecolour : 0u) |
                (material->near_glow ? kDrawTableNearGlow : 0u);
        if (material->glow >= 0.0f) {
            flags |= kDrawTableGlow |
                     (static_cast<std::uint32_t>(std::lround(std::min(material->glow / kDrawGlowScale, 1.0f) * 65535.0f))
                      << 16u);
        }
        // A clear solid the camera's rays pass (the game's draw hidden): the clear pass traces through it.
        if (material->solid && (info.surface & kRtDrawUnseen) != 0u) {
            flags = (flags & 0xFFFFu) | kDrawTableSolid |
                    (static_cast<std::uint32_t>(std::lround(std::clamp(material->range, 0.0f, 65535.0f))) << 16u);
            ++solid_draws_;
        }
        // A lamp's lit housing in its glass's texture: only the glass emits.
        const bool emits = !material->unlit_only || (info.surface & kRtDrawLit) == 0u;
        if (!emits) {
            emission &= ~3u;
            intensity = 0.0f;
        }
        if (emits && material->emission != RtEmission::None && !material->no_light && !material->seen) {
            // Physical: a light source of the table is an area light of its own triangles (EmitTriangleCS), its
            // radiance what it shows; not the art model's light at its centre. A `seen` one shows its radiance
            // (the G-buffer's emitters) while the game's point light there is its light.
            if (emit_triangles_ + entry.triangle_count <= kEmitTriangles) {
                emit_ranges_.push_back({entry.first_triangle, entry.triangle_count,
                                        static_cast<std::uint32_t>(draws_.size() - 1u) |
                                            (info.cull == 0u ? 0x80000000u : 0u),
                                        emit_triangles_});
                emit_range_materials_.push_back(static_cast<std::uint16_t>(info.rt_material));
                emit_range_keys_.push_back(info.light_key);
                emit_triangles_ += entry.triangle_count;
                emit_materials_[info.rt_material] += entry.triangle_count;
            }
        } else if (emits && material->emission == RtEmission::Point && intensity > 0.0f &&
                   authored_.size() < kAuthoredLights) {
            const bool spot = material->cone > 0.0f;
            const std::uint32_t light_flags = (material->tint ? kAuthoredTint : 0u) | (spot ? kAuthoredSpot : 0u) |
                                              (material->star ? kAuthoredStar : 0u) |
                                              (material->strip ? kAuthoredStrip : 0u);
            const std::uint32_t piece = material->strip ? kStripTriangles : entry.triangle_count;
            for (std::uint32_t first = 0u, n = 0u; first < entry.triangle_count && authored_.size() < kAuthoredLights;
                 first += piece, ++n) {
                authored_.push_back({entry.first_triangle + first, std::min(piece, entry.triangle_count - first), emission,
                                     intensity, info.rt_material | (n << 16u), material->pulse, light_flags,
                                     spot ? std::cos(material->cone * 3.14159265f / 180.0f) : -1.0f});
                authored_materials_.push_back(info.rt_material);
            }
            outcome(kOutcomeLight);
        }
    }
    const float relief = material != nullptr && !material->particle ? material->relief : 0.0f;
    // GX light 0 as the game lights the draw (not attenuated: a direction from its position).
    const XfChannelCtrl channel = decode_xf_channel_ctrl(info.channel);
    const bool light0 = info.light_index[0] == 0u && (channel.atten_func == 0u || channel.atten_func == 2u);
    std::uint32_t light_kind = !light0 ? 0u : channel.diffuse_func == 0u ? 1u : 2u;
    // A clear solid's index of refraction in the word's upper half (1 + bits / 16384; 0: quartz's, rt_engine.hlsl
    // solid_ior).
    if (material != nullptr && material->solid && material->ior >= 1.0f) {
        light_kind |= static_cast<std::uint32_t>(std::lround(std::min((material->ior - 1.0f) * 16384.0f, 65535.0f)))
                      << 16u;
    }
    {
        // High quality's textured hits: the material texture's view, and the colour the texture is
        // multiplied by: the vertices' colour 0 where channel 0 takes its material from them, else the
        // material register; a lit draw's at its light's mean facing (0.55, as the de-lit draws), in the game's
        // light units (physical) at its full light, as the raster's albedo.
        std::uint32_t colour = info.material;
        if ((info.surface & kRtDrawLit) != 0u) {
            const auto byte = [](std::uint32_t c, unsigned shift) { return float((c >> shift) & 0xFFu) / 255.0f; };
            const std::uint32_t light = info.light_index[0] == 0u ? info.light_color[0] : 0u;
            const float facing = physical_active_ ? 1.0f : 0.55f;
            colour = 0xFFu;
            for (const unsigned shift : {24u, 16u, 8u}) {
                const float lit = std::min(byte(info.ambient, shift) + facing * byte(light, shift), 1.0f) *
                                  byte(info.material, shift);
                colour |= static_cast<std::uint32_t>(std::lround(std::clamp(lit, 0.0f, 1.0f) * 255.0f)) << shift;
            }
        }
        const std::uint32_t alpha = info.alpha_from != 0u ? texture_view(info.alpha) : 0xFFFFFFFFu;
        draw_textures_.push_back({texture_view(info.texture), channel.material_from_vertex ? 1u : 0u, colour,
                                  alpha <= 0xFFFFFFu ? (info.alpha_from << 24u) | alpha : 0u});
    }
    draw_colors_.push_back({info.ambient, info.light_index[0] == 0u ? info.light_color[0] : 0u, emission,
                            std::bit_cast<std::uint32_t>(intensity), surface, info.material,
                            flags | ((info.surface & kRtDrawAlphaTest) != 0u ? kDrawTableAlpha : 0u) |
                                ((info.surface & kRtDrawUnseen) != 0u ? kDrawTableUnseen : 0u),
                            std::bit_cast<std::uint32_t>(relief), std::bit_cast<std::uint32_t>(info.light[0][0]),
                            std::bit_cast<std::uint32_t>(info.light[0][1]),
                            std::bit_cast<std::uint32_t>(info.light[0][2]), light_kind, info.fog[0], info.fog[1],
                            info.fog[2], info.fog[3]});
    // Physical: the material texture's de-light map, for opaque surfaces the game lights or bakes (not water, clear
    // crystal or glass, the sky, effects, light sources: their colour is light, not paint over a surface).
    std::uint32_t delight = 0xFFFFFFFFu;
    {
        const std::uint32_t surface_class = (info.surface & kRtDrawClassMask) >> kRtDrawClassShift;
        const bool paint = surface_class != static_cast<std::uint32_t>(RtSurfaceClass::Water) &&
                           surface_class != static_cast<std::uint32_t>(RtSurfaceClass::Crystal) &&
                           (material == nullptr ||
                            (!material->particle && !material->sky && !material->star && !material->lens &&
                             !material->hide && !material->mirror && !material->helper && !material->painted &&
                             material->emission == RtEmission::None));
        ++delight_seen_[0];
        if (!paint) {
            ++delight_seen_[1];
        } else if (info.texture_map >= 8u) {
            ++delight_seen_[2];
        } else if (info.texture.pack == 0u) {
            ++delight_seen_[3];
        } else {
            delight = delight_view(info.texture, info.texture_wrap);
            ++delight_seen_[delight != 0xFFFFFFFFu ? 4u : 5u];
        }
    }
    note_tev(info, tev_records_, tev_stages_, delight);
    draw_materials_.push_back(info.rt_material);
    if (dump_scene_ != 0u && scene_serial_ == dump_scene_) {
        infos_.push_back(info);
    }
}

void SceneRayTracer::set_view(const RtView& view) noexcept {
    view_ = view;
    view_known_ = true;
}

void SceneRayTracer::set_camera(const std::array<float, 12>* view) noexcept {
    camera_known_ = view != nullptr;
    camera_ = view != nullptr ? *view : kIdentity34;
}

bool SceneRayTracer::sun_direction(std::array<float, 3>& world, float* distance) const noexcept {
    if (stage_sun_ != nullptr && stage_sun_->source == RtStageSun::Source::None && !sun_override_) {
        return false;
    }
    if (sun_override_) {
        world = sun_direction_;
        if (distance != nullptr) {
            *distance = 0.0f;
        }
        return true;
    }
    return game_sun(world, sun_held_ ? 86'400'000u : 2000u, distance);
}

void SceneRayTracer::set_key_light(bool valid, const std::array<float, 3>& world, const std::array<float, 3>& color,
                                   float strength, bool shown) noexcept {
    key_valid_ = valid;
    key_shown_ = shown;
    key_direction_ = world;
    // Colour at its peak 1, as the sun's.
    const float peak = std::max({color[0], color[1], color[2]});
    key_color_ = peak > 1e-3f ? std::array<float, 3>{color[0] / peak, color[1] / peak, color[2] / peak}
                              : std::array<float, 3>{1.0f, 1.0f, 1.0f};
    // As bright as the game's own light (a stage without a sun keeps its level: a night stage's dim
    // world light stays dim; full from a peak of 0.75 up).
    key_strength_ = std::clamp(strength, 0.0f, 1.0f) * std::clamp(peak / 0.75f, 0.15f, 1.0f);
}

bool SceneRayTracer::sky_sun(std::array<float, 3>& world, std::array<float, 3>& color) const noexcept {
    if (!sky_sun_.valid) {
        return false;
    }
    world = sky_sun_.direction;
    color = sky_sun_.color;
    return true;
}

bool SceneRayTracer::sky_key(std::array<float, 3>& world, float& strength) const noexcept {
    static const bool enabled = env_float("GALAXY_RT_SKY_KEY", 1.0f) != 0.0f;
    if (!enabled || !sky_key_.valid) {
        return false;
    }
    world = sky_key_.direction;
    strength = sky_key_.strength;
    return true;
}

void SceneRayTracer::reset_sky() noexcept {
    // Another stage: its exposure starts from 1, not from the last stage's; its reflections remember
    // nothing of the last stage, its star core's light nothing of the last stage's stars.
    exposure_reset_ = true;
    core_colour_known_ = false;
    star_colour_from_frame_ = ~0ull;
    reflections_.reset_memory();
    late_built_ = 0u;
    sky_map_.reset();
    sky_sun_ = {};
    sky_key_ = {};
    ++sky_generation_;
}

// The captures' sky bins once the GPU has written them: into the map, then the sun it shows
// (every fourth read). GALAXY_TRACE_LIGHTS: the search every 120th read and each change of the
// sun, with the TSC cycles of all reads so far; GALAXY_RT_SKY_DUMP=<dir>: the map every 600th read
// as an equirectangular image (+y up, unseen bins dark red, the sun a cross).
void SceneRayTracer::read_sky() {
    constexpr UINT64 kSkyBytes = kSkyBins * kSkyBinWords * 4u;
    static const bool trace = env_float("GALAXY_TRACE_LIGHTS", 0.0f) != 0.0f;
    const std::uint64_t start = trace ? __rdtsc() : 0u;
    bool added = false;
    for (unsigned i = 0; i < kSkyRing; ++i) {
        SkyRead& read = sky_reads_[i];
        if (!read.pending || read.frame + kResultDelay > frame_index_) {
            continue;
        }
        read.pending = false;
        const D3D12_RANGE range{i * kSkyBytes, (i + 1u) * kSkyBytes};
        void* mapped = nullptr;
        if (read.generation != sky_generation_ || FAILED(sky_readback_->Map(0, &range, &mapped))) {
            continue;
        }
        sky_map_.add(reinterpret_cast<const std::uint32_t*>(static_cast<const std::byte*>(mapped) + range.Begin));
        const D3D12_RANGE no_write{0, 0};
        sky_readback_->Unmap(0, &no_write);
        added = true;
    }
    if (!added) {
        return;
    }
    if (++sky_reads_done_ % 4u != 0u) {
        sky_cycles_ += start != 0u ? __rdtsc() - start : 0u;
        return;
    }
    const SkySun before = sky_sun_;
    sky_map_.update(sky_sun_);
    sky_key_ = sky_map_.key();
    sky_cycles_ += start != 0u ? __rdtsc() - start : 0u;
    const bool changed = sky_sun_.valid != before.valid ||
                         (sky_sun_.valid && sky_sun_.direction[0] * before.direction[0] +
                                                    sky_sun_.direction[1] * before.direction[1] +
                                                    sky_sun_.direction[2] * before.direction[2] < 0.9994f);
    if (trace && (changed || sky_reads_done_ % 120u == 0u)) [[unlikely]] {
        const SkySun now = sky_map_.find();
        std::fprintf(stderr,
                     "[rt-sky] reads=%llu cycles=%llu bins=%u peak=%.3f distant=%.3f rest=%.3f found=%d "
                     "sun=%d dir=(%.3f %.3f %.3f) color=(%.2f %.2f %.2f)%s\n",
                     static_cast<unsigned long long>(sky_reads_done_), static_cast<unsigned long long>(sky_cycles_),
                     now.bins, now.peak, now.distant, now.rest, now.valid ? 1 : 0, sky_sun_.valid ? 1 : 0,
                     sky_sun_.direction[0], sky_sun_.direction[1],
                     sky_sun_.direction[2], sky_sun_.color[0], sky_sun_.color[1], sky_sun_.color[2],
                     changed ? " changed" : "");
    }
    static const std::string dump = [] {
        char value[MAX_PATH]{};
        std::size_t length = 0u;
        return getenv_s(&length, value, sizeof(value), "GALAXY_RT_SKY_DUMP") == 0 && length > 1u ? std::string(value)
                                                                                                : std::string();
    }();
    if (!dump.empty() && sky_reads_done_ % 600u == 0u) [[unlikely]] {
        constexpr unsigned kWidth = 256u;
        constexpr unsigned kHeight = 128u;
        constexpr float kPi = 3.14159265f;
        std::vector<std::uint8_t> image(kWidth * kHeight * 3u, 0u);
        for (unsigned y = 0; y < kHeight; ++y) {
            const float lat = kPi * 0.5f - (static_cast<float>(y) + 0.5f) / kHeight * kPi;
            for (unsigned x = 0; x < kWidth; ++x) {
                const float lon = (static_cast<float>(x) + 0.5f) / kWidth * 2.0f * kPi - kPi;
                const std::array<float, 3> w{std::cos(lat) * std::sin(lon), std::sin(lat), -std::cos(lat) * std::cos(lon)};
                const unsigned bin = sky_bin(w);
                std::uint8_t* px = image.data() + (y * kWidth + x) * 3u;
                if (!sky_map_.seen(bin)) {
                    px[0] = 60u;
                    continue;
                }
                for (unsigned i = 0; i < 3u; ++i) {
                    px[i] = static_cast<std::uint8_t>(std::clamp(sky_map_.color(bin)[i], 0.0f, 1.0f) * 255.0f + 0.5f);
                }
            }
        }
        if (sky_sun_.valid) {
            const auto& d = sky_sun_.direction;
            const float lat = std::asin(std::clamp(d[1], -1.0f, 1.0f));
            const float lon = std::atan2(d[0], -d[2]);
            const int cx = static_cast<int>((lon + kPi) / (2.0f * kPi) * kWidth);
            const int cy = static_cast<int>((kPi * 0.5f - lat) / kPi * kHeight);
            for (int k = -6; k <= 6; ++k) {
                for (const auto& [x, y] : {std::pair<int, int>{cx + k, cy}, std::pair<int, int>{cx, cy + k}}) {
                    if (x >= 0 && y >= 0 && x < static_cast<int>(kWidth) && y < static_cast<int>(kHeight)) {
                        std::uint8_t* px = image.data() + (static_cast<unsigned>(y) * kWidth + static_cast<unsigned>(x)) * 3u;
                        px[0] = 255u;
                        px[1] = 0u;
                        px[2] = 0u;
                    }
                }
            }
        }
        std::ofstream file(dump + "\\sky_" + std::to_string(frame_index_) + ".ppm", std::ios::binary);
        file << "P6\n" << kWidth << ' ' << kHeight << "\n255\n";
        file.write(reinterpret_cast<const char*>(image.data()), static_cast<std::streamsize>(image.size()));
    }
}

std::uint32_t SceneRayTracer::sample_factor(std::uint32_t width, std::uint32_t height) const noexcept {
    if (factor_override_ != 0u) {
        return factor_override_;
    }
    // The internal resolution decides the samples: high traces every pixel up to 4K-class pictures (3840 x 2160)
    // and one per 2 x 2 past them; medium one per 2 x 2, low one per 3 x 3.
    const std::uint64_t pixels = static_cast<std::uint64_t>(width) * height;
    if (high_quality_) {
        return pixels > 8'400'000u ? 2u : 1u;
    }
    return low_quality_ ? 3u : 2u;
}

bool SceneRayTracer::create_lighting_pipelines(DXGI_FORMAT scene_format) {
    const auto serialize = [&](const D3D12_ROOT_SIGNATURE_DESC& desc, ComPtr<ID3D12RootSignature>& out) {
        ComPtr<ID3DBlob> blob, error;
        return SUCCEEDED(D3D12SerializeRootSignature(&desc, D3D_ROOT_SIGNATURE_VERSION_1, &blob, &error)) &&
               SUCCEEDED(device_->CreateRootSignature(0, blob->GetBufferPointer(), blob->GetBufferSize(),
                                                      IID_PPV_ARGS(&out)));
    };
    const auto compute = [&](ID3D12RootSignature* root, const void* code, std::size_t bytes,
                             ComPtr<ID3D12PipelineState>& out) {
        D3D12_COMPUTE_PIPELINE_STATE_DESC desc{};
        desc.pRootSignature = root;
        desc.CS = {code, bytes};
        return SUCCEEDED(device_->CreateComputePipelineState(&desc, IID_PPV_ARGS(&out)));
    };

    D3D12_DESCRIPTOR_RANGE depth_range{};
    depth_range.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    depth_range.NumDescriptors = 1u;
    depth_range.BaseShaderRegister = 7u;
    D3D12_ROOT_PARAMETER params[kLightCount]{};
    params[kLightFrame].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    params[kLightScene].ParameterType = D3D12_ROOT_PARAMETER_TYPE_SRV;
    params[kLightScene].Descriptor.ShaderRegister = 6u;
    params[kLightDepth].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    params[kLightDepth].DescriptorTable.NumDescriptorRanges = 1u;
    params[kLightDepth].DescriptorTable.pDescriptorRanges = &depth_range;
    for (UINT i = 0; i <= kLightSky - kLightPrevZ; ++i) {
        params[kLightPrevZ + i].ParameterType = D3D12_ROOT_PARAMETER_TYPE_SRV;
        params[kLightPrevZ + i].Descriptor.ShaderRegister = 8u + i;
    }
    for (UINT i = 0; i <= kLightPrimary - kLightZ; ++i) {
        params[kLightZ + i].ParameterType = D3D12_ROOT_PARAMETER_TYPE_UAV;
        params[kLightZ + i].Descriptor.ShaderRegister = i;
    }
    // Alpha-tested geometry: per triangle its texture coordinates (t20), per draw its alpha map (t21).
    params[kLightSurfaceUv].ParameterType = D3D12_ROOT_PARAMETER_TYPE_SRV;
    params[kLightSurfaceUv].Descriptor.ShaderRegister = 20u;
    params[kLightDrawTextures].ParameterType = D3D12_ROOT_PARAMETER_TYPE_SRV;
    params[kLightDrawTextures].Descriptor.ShaderRegister = 21u;
    // The reference's sums (u6; a stand-in when it is off; the traced sky's map in the sky probe).
    params[kLightReference].ParameterType = D3D12_ROOT_PARAMETER_TYPE_UAV;
    params[kLightReference].Descriptor.ShaderRegister = 6u;
    // The traced sky's map (t22; a stand-in without one).
    params[kLightSkyMap].ParameterType = D3D12_ROOT_PARAMETER_TYPE_SRV;
    params[kLightSkyMap].Descriptor.ShaderRegister = 22u;
    // The draws' TEV records for ray hits (t23; a stand-in without them).
    params[kLightTev].ParameterType = D3D12_ROOT_PARAMETER_TYPE_SRV;
    params[kLightTev].Descriptor.ShaderRegister = 23u;
    // Physical: the per-pixel normals (u7; a stand-in without them).
    params[kLightPixels].ParameterType = D3D12_ROOT_PARAMETER_TYPE_UAV;
    params[kLightPixels].Descriptor.ShaderRegister = 7u;
    // Physical: the emitting draws' ranges while their triangles are written, then the triangles (t24; a stand-in).
    params[kLightEmitTris].ParameterType = D3D12_ROOT_PARAMETER_TYPE_SRV;
    params[kLightEmitTris].Descriptor.ShaderRegister = 24u;
    // Reservoir slots (t25, u8), which no shader declares; bound to stand-ins.
    params[kLightReservoirIn].ParameterType = D3D12_ROOT_PARAMETER_TYPE_SRV;
    params[kLightReservoirIn].Descriptor.ShaderRegister = 25u;
    params[kLightReservoirOut].ParameterType = D3D12_ROOT_PARAMETER_TYPE_UAV;
    params[kLightReservoirOut].Descriptor.ShaderRegister = 8u;
    for (auto& param : params) {
        param.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    }
    // s0 point, s1 trilinear (the de-light maps), both repeating.
    D3D12_STATIC_SAMPLER_DESC wrap[2]{};
    for (UINT s = 0; s < 2u; ++s) {
        wrap[s].Filter = s == 0u ? D3D12_FILTER_MIN_MAG_MIP_POINT : D3D12_FILTER_MIN_MAG_MIP_LINEAR;
        wrap[s].AddressU = wrap[s].AddressV = wrap[s].AddressW = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
        wrap[s].MaxLOD = D3D12_FLOAT32_MAX;
        wrap[s].ShaderRegister = s;
        wrap[s].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    }
    D3D12_ROOT_SIGNATURE_DESC rsd{};
    rsd.NumParameters = kLightCount;
    rsd.pParameters = params;
    rsd.NumStaticSamplers = 2u;
    rsd.pStaticSamplers = wrap;
    // The textured trace (alpha maps and textures straight from the descriptor heap): shader model 6.6 and resource
    // binding tier 3. GALAXY_RT_ALPHA_TEST=0 turns it off.
    {
        D3D12_FEATURE_DATA_SHADER_MODEL model{D3D_SHADER_MODEL_6_6};
        D3D12_FEATURE_DATA_D3D12_OPTIONS options{};
        light_textured_ = SUCCEEDED(device_->CheckFeatureSupport(D3D12_FEATURE_SHADER_MODEL, &model, sizeof(model))) &&
                          model.HighestShaderModel >= D3D_SHADER_MODEL_6_6 &&
                          SUCCEEDED(device_->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS, &options,
                                                                 sizeof(options))) &&
                          options.ResourceBindingTier >= D3D12_RESOURCE_BINDING_TIER_3 &&
                          env_float("GALAXY_RT_ALPHA_TEST", 1.0f) != 0.0f;
    }
    // The engine reads every texture, TEV record and buffer it traces straight from the descriptor heap, so the
    // lighting needs the textured trace.
    if (!light_textured_) {
        std::cerr << "[rt] lighting unavailable: shader model 6.6 and resource binding tier 3 needed\n";
        return false;
    }
    rsd.Flags = D3D12_ROOT_SIGNATURE_FLAG_CBV_SRV_UAV_HEAP_DIRECTLY_INDEXED;
    if (!serialize(rsd, light_root_) ||
        !compute(light_root_.Get(), g_rt_star_sphere_cs, sizeof(g_rt_star_sphere_cs), star_sphere_pso_)) {
        return false;
    }
    // Texture de-lighting (rt_delight.hlsl) reads the textures from the descriptor heap. Its root: the constants
    // (b0), the scratch buffer (u0).
    if (light_textured_) {
        D3D12_ROOT_PARAMETER delight_params[2]{};
        delight_params[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
        delight_params[0].Constants.Num32BitValues = 13u;
        delight_params[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_UAV;
        for (auto& param : delight_params) {
            param.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
        }
        D3D12_ROOT_SIGNATURE_DESC delight_desc{};
        delight_desc.NumParameters = 2u;
        delight_desc.pParameters = delight_params;
        delight_desc.Flags = D3D12_ROOT_SIGNATURE_FLAG_CBV_SRV_UAV_HEAP_DIRECTLY_INDEXED;
        if (!serialize(delight_desc, delight_root_) ||
            !compute(delight_root_.Get(), g_rt_delight_cs, sizeof(g_rt_delight_cs), delight_pso_) ||
            !compute(delight_root_.Get(), g_rt_delight_mip_cs, sizeof(g_rt_delight_mip_cs), delight_mip_pso_)) {
            delight_root_.Reset();
            delight_pso_.Reset();
            delight_mip_pso_.Reset();
        }
    }
    // Physical emitters: their radiance from their draws' TEV programs, read from the descriptor heap.
    if (!light_textured_ ||
        !compute(light_root_.Get(), g_rt_emit_triangle_cs, sizeof(g_rt_emit_triangle_cs), emit_triangle_pso_) ||
        !compute(light_root_.Get(), g_rt_emit_scan_cs, sizeof(g_rt_emit_scan_cs), emit_scan_pso_)) {
        emit_triangle_pso_.Reset();
        emit_scan_pso_.Reset();
    }
    // The traced sky reads its draws' textures from the descriptor heap.
    if (!light_textured_ ||
        !compute(light_root_.Get(), g_rt_sky_probe_tex_cs, sizeof(g_rt_sky_probe_tex_cs), sky_probe_pso_) ||
        !compute(light_root_.Get(), g_rt_sky_trace_sh_cs, sizeof(g_rt_sky_trace_sh_cs), sky_trace_sh_pso_)) {
        sky_probe_pso_.Reset();
        sky_trace_sh_pso_.Reset();
    }
    D3D12_DESCRIPTOR_RANGE sources_range{};
    sources_range.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    sources_range.NumDescriptors = 2u;
    D3D12_ROOT_PARAMETER capture_params[kCaptureCount]{};
    capture_params[kCaptureConstants].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
    capture_params[kCaptureConstants].Constants.Num32BitValues = sizeof(RtCaptureConstants) / 4u;
    capture_params[kCaptureSources].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    capture_params[kCaptureSources].DescriptorTable.NumDescriptorRanges = 1u;
    capture_params[kCaptureSources].DescriptorTable.pDescriptorRanges = &sources_range;
    capture_params[kCaptureFinal].ParameterType = D3D12_ROOT_PARAMETER_TYPE_SRV;
    capture_params[kCaptureFinal].Descriptor.ShaderRegister = 2u;
    capture_params[kCaptureGlowBase].ParameterType = D3D12_ROOT_PARAMETER_TYPE_SRV;
    capture_params[kCaptureGlowBase].Descriptor.ShaderRegister = 3u;
    capture_params[kCapturePicture].ParameterType = D3D12_ROOT_PARAMETER_TYPE_UAV;
    capture_params[kCaptureSums].ParameterType = D3D12_ROOT_PARAMETER_TYPE_UAV;
    capture_params[kCaptureSums].Descriptor.ShaderRegister = 1u;
    capture_params[kCaptureGlow].ParameterType = D3D12_ROOT_PARAMETER_TYPE_UAV;
    capture_params[kCaptureGlow].Descriptor.ShaderRegister = 2u;
    capture_params[kCaptureFound].ParameterType = D3D12_ROOT_PARAMETER_TYPE_UAV;
    capture_params[kCaptureFound].Descriptor.ShaderRegister = 3u;
    capture_params[kCaptureSky].ParameterType = D3D12_ROOT_PARAMETER_TYPE_UAV;
    capture_params[kCaptureSky].Descriptor.ShaderRegister = 4u;
    capture_params[kCaptureSkyRadiance].ParameterType = D3D12_ROOT_PARAMETER_TYPE_UAV;
    capture_params[kCaptureSkyRadiance].Descriptor.ShaderRegister = 5u;
    capture_params[kCaptureSkySh].ParameterType = D3D12_ROOT_PARAMETER_TYPE_UAV;
    capture_params[kCaptureSkySh].Descriptor.ShaderRegister = 6u;
    capture_params[kCaptureExposure].ParameterType = D3D12_ROOT_PARAMETER_TYPE_UAV;
    capture_params[kCaptureExposure].Descriptor.ShaderRegister = 7u;
    D3D12_DESCRIPTOR_RANGE game_range{};
    game_range.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    game_range.NumDescriptors = 1u;
    game_range.BaseShaderRegister = 4u;
    capture_params[kCaptureGame].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    capture_params[kCaptureGame].DescriptorTable.NumDescriptorRanges = 1u;
    capture_params[kCaptureGame].DescriptorTable.pDescriptorRanges = &game_range;
    for (auto& param : capture_params) {
        param.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    }
    D3D12_ROOT_SIGNATURE_DESC capture_rsd{};
    capture_rsd.NumParameters = kCaptureCount;
    capture_rsd.pParameters = capture_params;
    if (!serialize(capture_rsd, capture_root_) ||
        !compute(capture_root_.Get(), g_rt_capture_cs, sizeof(g_rt_capture_cs), capture_pso_) ||
        !compute(capture_root_.Get(), g_rt_glow_base_cs, sizeof(g_rt_glow_base_cs), glow_base_pso_) ||
        !compute(capture_root_.Get(), g_rt_sky_state_cs, sizeof(g_rt_sky_state_cs), sky_state_pso_) ||
        !compute(capture_root_.Get(), g_rt_surface_emit_cs, sizeof(g_rt_surface_emit_cs), surface_emit_pso_) ||
        !compute(capture_root_.Get(), g_rt_sky_light_map_cs, sizeof(g_rt_sky_light_map_cs), sky_light_map_pso_) ||
        !compute(capture_root_.Get(), g_rt_sky_light_sh_cs, sizeof(g_rt_sky_light_sh_cs), sky_light_sh_pso_)) {
        return false;
    }

    D3D12_ROOT_PARAMETER emit_params[kEmitCount]{};
    emit_params[kEmitConstants].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
    emit_params[kEmitConstants].Constants.Num32BitValues = sizeof(RtEmitConstants) / 4u;
    emit_params[kEmitSources].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    emit_params[kEmitSources].DescriptorTable.NumDescriptorRanges = 1u;
    emit_params[kEmitSources].DescriptorTable.pDescriptorRanges = &sources_range;
    for (UINT i = 0; i < 3u; ++i) {
        emit_params[kEmitDraws + i].ParameterType = D3D12_ROOT_PARAMETER_TYPE_SRV;
        emit_params[kEmitDraws + i].Descriptor.ShaderRegister = 2u + i;
        emit_params[kEmitTiles + i].ParameterType = D3D12_ROOT_PARAMETER_TYPE_UAV;
        emit_params[kEmitTiles + i].Descriptor.ShaderRegister = i;
    }
    emit_params[kEmitGlowOut].ParameterType = D3D12_ROOT_PARAMETER_TYPE_UAV;
    emit_params[kEmitGlowOut].Descriptor.ShaderRegister = 3u;
    emit_params[kEmitGlowUv].ParameterType = D3D12_ROOT_PARAMETER_TYPE_SRV;
    emit_params[kEmitGlowUv].Descriptor.ShaderRegister = 5u;
    for (auto& param : emit_params) {
        param.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    }
    D3D12_ROOT_SIGNATURE_DESC emit_rsd{};
    emit_rsd.NumParameters = kEmitCount;
    emit_rsd.pParameters = emit_params;
    // The glow light sources' TEV (rt_tev.hlsli) samples their textures as the lighting does.
    emit_rsd.NumStaticSamplers = 2u;
    emit_rsd.pStaticSamplers = wrap;
    // The glow light sources read their textures from the frame's heap (GlowEmitterCS).
    emit_rsd.Flags = light_textured_ ? D3D12_ROOT_SIGNATURE_FLAG_CBV_SRV_UAV_HEAP_DIRECTLY_INDEXED
                                     : D3D12_ROOT_SIGNATURE_FLAG_NONE;
    if (!serialize(emit_rsd, emit_root_) ||
        !compute(emit_root_.Get(), g_rt_glow_emit_cs, sizeof(g_rt_glow_emit_cs), glow_emit_pso_) ||
        (light_textured_ &&
         !compute(emit_root_.Get(), g_rt_glow_emitter_cs, sizeof(g_rt_glow_emitter_cs), glow_emitter_pso_)) ||
        !compute(emit_root_.Get(), g_rt_authored_emit_cs, sizeof(g_rt_authored_emit_cs), authored_emit_pso_) ||
        !compute(emit_root_.Get(), g_rt_emit_merge_cs, sizeof(g_rt_emit_merge_cs), emit_merge_pso_)) {
        return false;
    }

    {
        D3D12_ROOT_PARAMETER light_tex_params[2]{};
        light_tex_params[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
        light_tex_params[0].Constants.Num32BitValues = 8u;
        light_tex_params[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_SRV;
        for (auto& param : light_tex_params) {
            param.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
        }
        D3D12_ROOT_SIGNATURE_DESC light_tex_rsd{};
        light_tex_rsd.NumParameters = 2u;
        light_tex_rsd.pParameters = light_tex_params;
        if (!serialize(light_tex_rsd, light_tex_root_)) {
            return false;
        }
        D3D12_GRAPHICS_PIPELINE_STATE_DESC light_tex{};
        light_tex.pRootSignature = light_tex_root_.Get();
        light_tex.VS = {g_rt_composite_vs, sizeof(g_rt_composite_vs)};
        light_tex.PS = {g_rt_light_texture_ps, sizeof(g_rt_light_texture_ps)};
        light_tex.BlendState.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
        light_tex.SampleMask = UINT_MAX;
        light_tex.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
        light_tex.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
        light_tex.RasterizerState.DepthClipEnable = TRUE;
        light_tex.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
        light_tex.NumRenderTargets = 1u;
        light_tex.RTVFormats[0] = DXGI_FORMAT_R16G16B16A16_FLOAT;
        light_tex.SampleDesc.Count = 1u;
        if (FAILED(device_->CreateGraphicsPipelineState(&light_tex, IID_PPV_ARGS(&light_tex_pso_)))) {
            return false;
        }
        D3D12_DESCRIPTOR_HEAP_DESC rtv_heap{};
        rtv_heap.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
        rtv_heap.NumDescriptors = 1u;
        D3D12_DESCRIPTOR_HEAP_DESC srv_heap{};
        srv_heap.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
        srv_heap.NumDescriptors = 1u;
        if (FAILED(device_->CreateDescriptorHeap(&rtv_heap, IID_PPV_ARGS(&light_tex_rtv_heap_))) ||
            FAILED(device_->CreateDescriptorHeap(&srv_heap, IID_PPV_ARGS(&light_tex_srv_heap_)))) {
            return false;
        }
        light_tex_srv_ = light_tex_srv_heap_->GetCPUDescriptorHandleForHeapStart();
    }

    D3D12_DESCRIPTOR_RANGE composite_depth{};
    composite_depth.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    composite_depth.NumDescriptors = 1u;
    composite_depth.BaseShaderRegister = 1u;
    D3D12_DESCRIPTOR_RANGE composite_color{};
    composite_color.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    composite_color.NumDescriptors = 1u;
    composite_color.BaseShaderRegister = 3u;
    D3D12_ROOT_PARAMETER composite_params[6]{};
    composite_params[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
    composite_params[0].Constants.Num32BitValues = sizeof(RtCompositeConstants) / 4u;
    composite_params[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_SRV;
    composite_params[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    composite_params[2].DescriptorTable.NumDescriptorRanges = 1u;
    composite_params[2].DescriptorTable.pDescriptorRanges = &composite_depth;
    composite_params[3].ParameterType = D3D12_ROOT_PARAMETER_TYPE_SRV;
    composite_params[3].Descriptor.ShaderRegister = 2u;
    composite_params[4].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    composite_params[4].DescriptorTable.NumDescriptorRanges = 1u;
    composite_params[4].DescriptorTable.pDescriptorRanges = &composite_color;
    composite_params[5].ParameterType = D3D12_ROOT_PARAMETER_TYPE_SRV;
    composite_params[5].Descriptor.ShaderRegister = 4u;
    for (auto& param : composite_params) {
        param.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    }
    D3D12_ROOT_SIGNATURE_DESC composite_rsd{};
    composite_rsd.NumParameters = 6u;
    composite_rsd.pParameters = composite_params;
    if (!serialize(composite_rsd, composite_root_)) {
        return false;
    }
    D3D12_GRAPHICS_PIPELINE_STATE_DESC graphics{};
    graphics.pRootSignature = composite_root_.Get();
    graphics.VS = {g_rt_composite_vs, sizeof(g_rt_composite_vs)};
    graphics.PS = {g_rt_composite_ps, sizeof(g_rt_composite_ps)};
    // The lit colour over the EFB's (read from a copy); the destination alpha the game may read
    // stays.
    D3D12_RENDER_TARGET_BLEND_DESC& blend = graphics.BlendState.RenderTarget[0];
    blend.BlendEnable = FALSE;
    blend.LogicOp = D3D12_LOGIC_OP_NOOP;
    blend.RenderTargetWriteMask =
        D3D12_COLOR_WRITE_ENABLE_RED | D3D12_COLOR_WRITE_ENABLE_GREEN | D3D12_COLOR_WRITE_ENABLE_BLUE;
    graphics.SampleMask = UINT_MAX;
    graphics.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
    graphics.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
    graphics.RasterizerState.DepthClipEnable = TRUE;
    graphics.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    graphics.NumRenderTargets = 1u;
    graphics.RTVFormats[0] = scene_format;
    graphics.SampleDesc.Count = 1u;
    if (FAILED(device_->CreateGraphicsPipelineState(&graphics, IID_PPV_ARGS(&composite_pso_)))) {
        return false;
    }
    // The engine's clear surfaces over the picture (rt_lighting.hlsl ClearCompositePS): none without it.
    graphics.PS = {g_rt_clear_composite_ps, sizeof(g_rt_clear_composite_ps)};
    if (FAILED(device_->CreateGraphicsPipelineState(&graphics, IID_PPV_ARGS(&clear_composite_pso_)))) {
        clear_composite_pso_.Reset();
    }
    if (!create_buffer(kReprojectEntryBytes, D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_STATE_COPY_DEST,
                       D3D12_RESOURCE_FLAG_NONE, reproject_, L"rt light reprojection") ||
        !create_buffer(kReprojectEntries * kReprojectEntryBytes, D3D12_HEAP_TYPE_UPLOAD,
                       D3D12_RESOURCE_STATE_GENERIC_READ, D3D12_RESOURCE_FLAG_NONE, reproject_upload_,
                       L"rt light reprojection upload") ||
        !create_buffer(kPictureSumsBytes, D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                       D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS, picture_sums_, L"rt picture sums") ||
        !create_buffer(kExposureSumsBytes, D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                       D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS, exposure_sums_, L"rt exposure sums") ||
        !create_buffer(static_cast<std::uint64_t>(kGlowMaxTriangles) * 36u, D3D12_HEAP_TYPE_DEFAULT,
                       D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS,
                       glow_positions_, L"rt glow triangles") ||
        !create_buffer(static_cast<std::uint64_t>(kGlowMaxTriangles) * 16u, D3D12_HEAP_TYPE_DEFAULT,
                       D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS,
                       glow_surfaces_, L"rt glow surfaces") ||
        !create_buffer(static_cast<std::uint64_t>(kGlowMaxTriangles) * kRtTriUvStride, D3D12_HEAP_TYPE_DEFAULT,
                       D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS,
                       glow_uv_, L"rt glow texture coordinates") ||
        !create_buffer(kEmitFoundBytes, D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                       D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS, emit_tiles_, L"rt glow lights") ||
        !create_buffer(kEmitLights * kEmitLightBytes, D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                       D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS, emit_lights_, L"rt emitters") ||
        !create_buffer(16u + kEmitLights * kEmitStride, D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                       D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS, emit_list_, L"rt emitter list") ||
        (log_emitters_ && !create_buffer(kEmitLogBytes + kEmitLogSums + kEmitFoundBytes, D3D12_HEAP_TYPE_READBACK,
                                         D3D12_RESOURCE_STATE_COPY_DEST,
                                         D3D12_RESOURCE_FLAG_NONE, emit_readback_, L"rt emitter log")) ||
        (log_emitters_ && log_physical_ &&
         !create_buffer(kEmitBufferBytes, D3D12_HEAP_TYPE_READBACK, D3D12_RESOURCE_STATE_COPY_DEST,
                        D3D12_RESOURCE_FLAG_NONE, physical_readback_, L"rt physical emitter log"))) {
        return false;
    }
    emit_log_pending_ = false;
    glow_positions_state_ = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    glow_uv_state_ = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    emit_tiles_state_ = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    emit_list_state_ = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    emit_seen_ = false;
    reproject_state_ = D3D12_RESOURCE_STATE_COPY_DEST;
    picture_sums_state_ = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    // Its contents are undefined until the first capture clears them.
    exposure_reset_ = true;
    void* mapped = nullptr;
    const D3D12_RANGE no_read{0, 0};
    if (FAILED(reproject_upload_->Map(0, &no_read, &mapped))) {
        return false;
    }
    reproject_mapped_ = static_cast<std::byte*>(mapped);
    return true;
}

bool SceneRayTracer::ensure_lighting_buffers(std::uint32_t samples) {
    if (samples <= light_samples_ && light_raw_) {
        return true;
    }
    for (auto& buffer : light_z_) retire(buffer);
    retire(light_normal_);
    retire(light_raw_);
    retire(light_primary_);
    for (auto& buffer : light_final_) retire(buffer);
    retire(picture_);
    retire(glow_base_);
    retire(glow_added_);
    glow_base_valid_ = false;
    // Results of the old buffers are gone with them.
    light_slots_ = {};
    light_samples_ = 0u;
    history_valid_ = picture_valid_ = false;
    const std::uint64_t n = samples;
    const D3D12_RESOURCE_FLAGS uav = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
    const D3D12_RESOURCE_STATES state = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    const auto make = [&](std::uint64_t bytes, ComPtr<ID3D12Resource>& out, const wchar_t* name) {
        return create_buffer(bytes, D3D12_HEAP_TYPE_DEFAULT, state, uav, out, name);
    };
    bool ok = n != 0u && make(n * 4u, light_normal_, L"rt light normal") && make(n * 16u, light_raw_, L"rt light raw") &&
              make(n * 8u, light_primary_, L"rt light primary") && make(n * 8u, picture_, L"rt picture") &&
              make(n * 4u, glow_base_, L"rt glow base") && make(n * 4u, glow_added_, L"rt glow added");
    glow_base_state_ = glow_added_state_ = state;
    for (unsigned i = 0; ok && i < 2u; ++i) {
        ok = make(n * 4u, light_z_[i], L"rt light eye z");
        light_z_state_[i] = state;
    }
    for (unsigned i = 0; ok && i < kLightSlots; ++i) {
        ok = make((n + kStarSlots + kProbeRecords) * 16u, light_final_[i], L"rt light result");
        light_final_state_[i] = state;
    }
    picture_state_ = state;
    if (!ok) {
        for (auto& buffer : light_z_) buffer.Reset();
        light_normal_.Reset();
        light_raw_.Reset();
        light_primary_.Reset();
            for (auto& buffer : light_final_) buffer.Reset();
        picture_.Reset();
        glow_base_.Reset();
        glow_added_.Reset();
        return false;
    }
    light_samples_ = samples;
    std::cerr << "[rt] light samples=" << samples << " mb=" << ((n * (4u + 16u + 8u + 8u + 2u * 4u + 12u * kLightSlots)) >> 20u)
              << '\n';
    return true;
}

void SceneRayTracer::to_state(ID3D12GraphicsCommandList* list, ID3D12Resource* resource,
                              D3D12_RESOURCE_STATES& state, D3D12_RESOURCE_STATES wanted) {
    if (state == wanted) {
        return;
    }
    const D3D12_RESOURCE_BARRIER barrier = transition(resource, state, wanted);
    list->ResourceBarrier(1u, &barrier);
    state = wanted;
}

void SceneRayTracer::trace_lighting(ID3D12GraphicsCommandList* list, Allocate allocate, void* allocate_context,
                                    D3D12_GPU_DESCRIPTOR_HANDLE depth_srv, std::uint32_t width,
                                    std::uint32_t height) {
    light_slot_ = -1;
    draw_table_ = 0u;
    if (!lighting_active() || !engine_ready_ || !view_known_ || depth_srv.ptr == 0u || width == 0u || height == 0u) {
        return;
    }
    const std::uint32_t factor = sample_factor(width, height);
    const std::uint32_t sample_width = (width + factor - 1u) / factor;
    const std::uint32_t sample_height = (height + factor - 1u) / factor;
    if (!ensure_lighting_buffers(sample_width * sample_height)) {
        return;
    }
    if (sample_width != history_width_ || sample_height != history_height_ || factor != history_factor_) {
        history_valid_ = false;
        history_width_ = sample_width;
        history_height_ = sample_height;
        history_factor_ = factor;
    }
    const RtUpload upload = allocate(allocate_context, sizeof(RtLightConstants), 256u);
    // Per draw (RtDraw.flags bits 16-31): draw_colors_.
    const std::size_t draw_count = std::max<std::size_t>(draw_colors_.size(), 1u);
    const RtUpload draw_table = allocate(allocate_context, draw_count * sizeof(draw_colors_[0]), 256u);
    if (upload.cpu == nullptr || draw_table.cpu == nullptr) {
        return;
    }
    if (draw_colors_.empty()) {
        std::memset(draw_table.cpu, 0, sizeof(draw_colors_[0]));
    } else {
        std::memcpy(draw_table.cpu, draw_colors_.data(), draw_colors_.size() * sizeof(draw_colors_[0]));
    }
    draw_table_ = draw_colors_.empty() ? 0u : draw_table.gpu;
    draw_texture_table_ = 0u;
    if (surface_uv_ && !draw_textures_.empty()) {
        const RtUpload textures = allocate(allocate_context, draw_textures_.size() * sizeof(draw_textures_[0]), 256u);
        if (textures.cpu != nullptr) {
            std::memcpy(textures.cpu, draw_textures_.data(), draw_textures_.size() * sizeof(draw_textures_[0]));
            draw_texture_table_ = textures.gpu;
        }
    }
    // The draws' TEV records for ray hits (scene; the sky's with the traced sky).
    RtUpload tev_upload{};
    std::uint64_t tev_bytes = 0u;
    const D3D12_GPU_VIRTUAL_ADDRESS tev_table =
        upload_tev(tev_records_, tev_stages_, allocate, allocate_context, &tev_upload, &tev_bytes);
    D3D12_GPU_VIRTUAL_ADDRESS sky_tev_table = 0u;
    RtLightConstants c{};
    c.draw_count = static_cast<std::uint32_t>(draw_count);
    c.tev_info[0] = tev_table != 0u ? static_cast<std::uint32_t>(tev_records_.size()) : 0u;
    if (probe_) {
        probe_sample_ = {probe_pixel_[0] / factor, probe_pixel_[1] / factor};
        c.probe[0] = probe_sample_[0];
        c.probe[1] = probe_sample_[1];
        c.probe[2] = probe_sample_[0] < sample_width && probe_sample_[1] < sample_height ? 1u : 0u;
    }
    if (light_meter_) {
        const auto px = [](float v, std::uint32_t size) {
            return static_cast<std::uint32_t>(std::clamp(v * static_cast<float>(size), 0.0f, 4095.0f));
        };
        c.probe[3] = px(light_meter_disc_[0], width) | (px(light_meter_disc_[1], height) << 12u) |
                     (std::clamp(px(light_meter_disc_[2], width) / 4u, 1u, 255u) << 24u);
    }
    c.sky_ibl = sky_ibl_;
    c.fill = fill_;
    c.emit[0] = glow_cutoff_;
    c.emit[1] = glow_reach_;
    c.emit[2] = glow_light_;
    c.emit[3] = surface_light_;
    c.self[0] = self_area_;
    c.self[1] = self_point_;
    c.self[2] = area_radiance_;
    c.ao[0] = ao_reach_;
    c.ao[1] = ao_day_;
    c.ao[2] = ao_night_;
    c.ao[3] = night_lights_cap_;
    c.night[0] = night_level_ * (stage_sun_ != nullptr ? stage_sun_->ambient : 1.0f);
    c.night[1] = night_occlusion_;
    c.night[2] = night_shadow_;
    c.night[3] = night_lights_;
    c.night2[0] = night_key_;
    c.night2[1] = day_environment_;
    std::copy(star_first_.begin(), star_first_.end(), c.star_first);
    std::copy(star_count_.begin(), star_count_.end(), c.star_count);
    // The stage's day model share (rt-suns.tsv day); < 0: by its sky's brightness.
    c.self[3] = stage_sun_ != nullptr && stage_sun_->day >= 0.0f ? stage_sun_->day : -1.0f;
    c.point_saturation = point_saturation_;
    std::copy(view_.projection.begin(), view_.projection.end(), c.projection);
    c.depth[0] = view_.z_scale;
    c.depth[1] = view_.z_offset;
    c.depth[2] = view_.min_depth;
    c.depth[3] = view_.max_depth;
    std::copy(view_.viewport.begin(), view_.viewport.end(), c.viewport);
    c.target_size[0] = width;
    c.target_size[1] = height;
    c.sample_size[0] = sample_width;
    c.sample_size[1] = sample_height;
    c.near_plane = near_plane(view_);
    c.frame = light_frame_++;
    c.range = range_;
    c.strength = strength_;
    c.rays = rays_;
    c.factor = factor;
    c.history_frames = history_frames_;
    c.contact = contact_;
    c.contact_range = contact_range_;
    c.sky_saturation = sky_saturation_;
    c.fallback = fallback_;
    // The sun the game shows (its BrightSun, else the stage table's): the physical light's direction too.
    bool visible_sun = false;
    {
        // The game's visible sun (GALAXY_RT_SUN overrides it), else the scene's key light (the sun its sky shows or
        // the world-fixed light its lit draws take, set_key_light); without either, no sun light. The sun's colour:
        // the game's world-fixed light from the same direction, else GALAXY_RT_SUN_COLOR; the key's: its own.
        std::array<float, 3> l = sun_direction_;
        float sun_distance = 0.0f;
        bool sun_present = sun_direction(l, &sun_distance);
        const bool keyed = !sun_present && key_valid_;
        const float strength = keyed ? key_strength_ : 1.0f;
        if (keyed) {
            l = key_direction_;
            sun_present = true;
            // Halfway to where a BrightSun model would stand (100000 units out): the scene's
            // background sits farther than that.
            sun_distance = 100000.0f;
        }
        // Sun rays end halfway to the game's sun model (BrightSun, drawn that far out along the
        // sun's direction): it would shadow everything near the camera.
        c.sun_reach = sun_distance > 0.0f ? 0.5f * sun_distance : 1.0e6f;
        // The game's visible sun lights from the side the sky shows it on (its BrightSun), its shafts from its disc.
        // A sky's painted sun often stands below the playfield's horizon (Good Egg's is behind the planet), where it
        // would light nothing directly, so it is held over the stage's horizon (below).
        // Where the sky shows the sun (world): the shafts' centre, whatever lift the light takes below.
        const std::array<float, 3> shown_direction = l;
        // The light's elevation over the stage's horizon (world Y, the up the stage and its sky are built to), its
        // compass direction kept, so the light stands still in the world as a sun does: the player's own up turns with
        // every planet (round a small one, over a rim, into another's gravity), and a light held over it swung across
        // the sky as the player walked (two highlights trading places, the whole scene relit by a long jump), with
        // nowhere to stand once the sky's sun was under the player's feet. A planet's far side falls in its shade,
        // lit by the sky. The game's sun model (BrightSun) at least GALAXY_RT_SUN_MIN_ELEVATION (30) degrees
        // up; a key light (no sun model in the stage's sky) between GALAXY_RT_KEY_MIN_ELEVATION (50) and
        // GALAXY_RT_SUN_MAX_ELEVATION (65) degrees: high as a midday sun, so an enclosed playfield is lit across
        // (lower, a tube or wall shadows half of every floor) while the characters' shadows still show beside them
        // (straight overhead they hide under them). A key the sky map shows (key_shown_: the sky detector's bright spot
        // of a backdrop) is held as a key, its shafts from the spot. Physical light holds its final direction the same
        // way (hold_light, below).
        const bool lift_sun = sun_present && !keyed && !sun_override_;
        const bool lift_key = sun_present && keyed && !sun_override_;
        if (lift_sun || lift_key) {
            hold_elevation(l, kStageUp, lift_sun ? sun_min_elevation_ : key_min_elevation_,
                           lift_sun ? 90.0f : sun_max_elevation_,
                           light_world_valid_ ? light_world_ : std::array<float, 3>{camera_[8], camera_[9], camera_[10]});
        }
        std::array<float, 3> sun_color = keyed ? key_color_ : sun_color_;
        const RtStageSun* const stage = stage_sun_;
        if (sun_present && !keyed) {
            std::array<GameLight, 16> world{};
            const std::size_t count = game_world_lights(world, 2000u);
            const float sun_length = std::sqrt(l[0] * l[0] + l[1] * l[1] + l[2] * l[2]);
            float best = 0.9f;
            for (std::size_t i = 0; i < count && sun_length > 1e-6f; ++i) {
                const auto& p = world[i].position;
                const float length = std::sqrt(p[0] * p[0] + p[1] * p[1] + p[2] * p[2]);
                const float alignment = length > 1e-3f ? (p[0] * l[0] + p[1] * l[1] + p[2] * l[2]) / (length * sun_length) : 0.0f;
                const auto& rgb = world[i].color;
                const float peak = std::max({rgb[0], rgb[1], rgb[2]});
                if (alignment > best && peak > 0.05f) {
                    best = alignment;
                    sun_color = {rgb[0] / peak, rgb[1] / peak, rgb[2] / peak};
                }
            }
        }
        // The stage's own sun colour and strength (rt-suns.tsv).
        float stage_strength = 1.0f;
        if (stage != nullptr) {
            if (stage->has_color) {
                const float peak = std::max({stage->color[0], stage->color[1], stage->color[2], 1e-3f});
                sun_color = {stage->color[0] / peak, stage->color[1] / peak, stage->color[2] / peak};
            }
            stage_strength = stage->strength;
        }
        {
            // The game's off-screen shadow casters sweep along this sun (ClippingJudge::updateShadowCasters).
            const float world_length = std::sqrt(l[0] * l[0] + l[1] * l[1] + l[2] * l[2]);
            set_traced_sun({l[0] / std::max(world_length, 1e-6f), l[1] / std::max(world_length, 1e-6f),
                            l[2] / std::max(world_length, 1e-6f)},
                           sun_present && world_length > 1e-6f);
        }
        if (camera_known_) {
            l = {camera_[0] * l[0] + camera_[1] * l[1] + camera_[2] * l[2],
                 camera_[4] * l[0] + camera_[5] * l[1] + camera_[6] * l[2],
                 camera_[8] * l[0] + camera_[9] * l[1] + camera_[10] * l[2]};
            const std::array<float, 3>& v = shown_direction;
            sun_visible_eye_ = {camera_[0] * v[0] + camera_[1] * v[1] + camera_[2] * v[2],
                                camera_[4] * v[0] + camera_[5] * v[1] + camera_[6] * v[2],
                                camera_[8] * v[0] + camera_[9] * v[1] + camera_[10] * v[2]};
        } else {
            sun_visible_eye_ = shown_direction;
        }
        {
            const float v = std::sqrt(sun_visible_eye_[0] * sun_visible_eye_[0] +
                                      sun_visible_eye_[1] * sun_visible_eye_[1] +
                                      sun_visible_eye_[2] * sun_visible_eye_[2]);
            for (float& x : sun_visible_eye_) x = v > 1e-6f ? x / v : 0.0f;
        }
        const float length = std::sqrt(l[0] * l[0] + l[1] * l[1] + l[2] * l[2]);
        const std::array<float, 3> fallback_sun{0.3f, 0.8f, 0.5f};
        {
            // Half its colour: a strongly tinted world light turned grass yellow and white objects cyan.
            const float grey = 0.2126f * sun_color[0] + 0.7152f * sun_color[1] + 0.0722f * sun_color[2];
            for (float& v : sun_color) v = 0.5f * (v + grey);
            const float peak = std::max({sun_color[0], sun_color[1], sun_color[2], 1e-3f});
            for (float& v : sun_color) v /= peak;
        }
        for (unsigned i = 0; i < 3u; ++i) {
            c.sun[i] = length > 1e-6f ? l[i] / length : fallback_sun[i] / std::sqrt(0.98f);
            c.sun_light[i] = sun_present ? sun_color[i] * sun_light_ * strength * stage_strength : 0.0f;
        }
        // A key light the sky does not show has no disc: its shadows are soft.
        const float disc = stage != nullptr && stage->size >= 0.0f ? stage->size : sun_size_;
        c.sun[3] = std::tan((keyed && !key_shown_ ? key_size_ : disc) * 3.14159265f / 180.0f);
        // Without a sun the sky and bounce light carry the whole picture.
        c.sun_light[3] = sun_present ? 1.0f + (sky_light_ - 1.0f) * strength : 1.0f;
        emit_log_sun_ = {c.sun_light[0], c.sun_light[1], c.sun_light[2], c.sun_light[3], keyed ? 1.0f : 0.0f,
                         strength};
        visible_sun = sun_present && !keyed && length > 1e-6f;
        sun_shown_ = sun_present && (!keyed || key_shown_) && length > 1e-6f;
    }
    // GALAXY_RT_PHYSICAL: the game's own light units. Its planet light lights a surface as material x min(ambient + key
    // x N.L, 1) in display-encoded colour. Every traced surface's colour is its colour at white light (lit draws:
    // header bit 31; unlit draws are drawn at their program's neutral light), so the light is in the game's units in
    // linear light (white 1): the sky's radiance the ambient, the sun's irradiance over pi the rest of the full light.
    // A surface facing the sun unshadowed shows its colour as the game draws it, one in shade at the ambient. Where the
    // game shows no sun, the light comes from the key light's position, where the game lights from (below).
    GameAreaLight area{};
    RtUpload sky_record_table{};
    RtUpload emit_range_table{};
    physical_active_ = game_area_light(GameAreaLightKind::Stage, area, 2000u);
    if (!physical_active_) {
        // No stage light (the game sets one each frame a stage draws): nothing to light in its units, the game's
        // picture stays.
        return;
    }
    {
        const auto lin = [](float v) {
            v = std::clamp(v, 0.0f, 1.0f);
            return v <= 0.04045f ? v / 12.92f : std::pow((v + 0.055f) / 1.055f, 2.4f);
        };
        // In the game's own units, each channel as the stage sets it: the sky the ambient, the sun the rest of the full
        // light (ambient + key, as the game clamps it). Not divided by the full light, which would make every stage's
        // light white and as bright as day.
        for (unsigned i = 0; i < 3u; ++i) {
            const float full = lin(area.ambient[i] + area.lights[0].color[i]);
            const float sky = lin(area.ambient[i]);
            c.sky_radiance[i] = sky;
            c.sun_light[i] = std::max(full - sky, 0.0f);
            c.full_light[i] = full;
        }
        // A stage without a sun (rt-suns none: Melty Molten's glowing haze) gives the table's `strength` share of the
        // key's light to its sky, an overcast sky (physical_overcast): open ground lit from above, a soft shadow under
        // whatever stands on it. The key itself stands below the player's horizon there and would light nothing,
        // leaving the lava's red light alone. The sky takes the table's colour when it has one. The camera stays
        // exposed for the stage's own light (its key as a sun 30 degrees up): a dimmer haze leaves the lava brighter.
        const auto luma3 = [](const float* rgb) { return 0.2126f * rgb[0] + 0.7152f * rgb[1] + 0.0722f * rgb[2]; };
        const bool overcast = stage_sun_ != nullptr && stage_sun_->source == RtStageSun::Source::None && !sun_override_;
        const float overcast_reference = luma3(c.sky_radiance) + 0.5f * luma3(c.sun_light);
        if (overcast) {
            const float level = luma3(c.sky_radiance) + std::min(stage_sun_->strength, 1.0f) * luma3(c.sun_light);
            const float tint = stage_sun_->has_color ? std::max(luma3(stage_sun_->color), 1e-3f) : 0.0f;
            const float full = std::max(luma3(c.full_light), 1e-4f);
            for (unsigned i = 0; i < 3u; ++i) {
                c.sky_radiance[i] = tint > 0.0f ? level * stage_sun_->color[i] / tint : level * c.full_light[i] / full;
                c.sun_light[i] = 0.0f;
            }
            c.sky_radiance[3] = 1.0f;
        }
        light_full_ = {std::max(c.full_light[0], 1e-3f), std::max(c.full_light[1], 1e-3f),
                       std::max(c.full_light[2], 1e-3f)};
        sky_fill_ = {};
        // The stage's light as the game sets it (display-encoded), when it changes.
        static std::array<float, 6> logged_light{-1.0f, -1.0f, -1.0f, -1.0f, -1.0f, -1.0f};
        const std::array<float, 6> stage_light{area.ambient[0], area.ambient[1], area.ambient[2],
                                               area.lights[0].color[0], area.lights[0].color[1],
                                               area.lights[0].color[2]};
        if (stage_light != logged_light) {
            logged_light = stage_light;
            const std::array<float, 3>& at = area.lights[0].position;
            // The camera in the world: minus the rotation's transpose times the translation.
            std::array<float, 3> camera_world{};
            for (unsigned r = 0; r < 3u; ++r) {
                for (unsigned k = 0; k < 3u; ++k) {
                    camera_world[k] -= camera_[r * 4u + k] * camera_[r * 4u + 3u];
                }
            }
            std::fprintf(stderr,
                         "[rt-stage] ambient=%.3f,%.3f,%.3f key=%.3f,%.3f,%.3f follows-camera=%d key-at=%.0f,%.0f,%.0f "
                         "camera-at=%.0f,%.0f,%.0f\n",
                         stage_light[0], stage_light[1], stage_light[2], stage_light[3], stage_light[4],
                         stage_light[5], area.lights[0].follows_camera ? 1 : 0, at[0], at[1], at[2], camera_world[0],
                         camera_world[1], camera_world[2]);
        }
        // The traced sky: the sky's draws of this scene, each one's record for the probe.
        if (sky_built_ != 0u && sky_probe_pso_ && sky_trace_sh_pso_ && sky_probe_map_ && !sky_records_.empty()) {
            sky_record_table = allocate(allocate_context, sky_records_.size() * sizeof(sky_records_[0]), 256u);
            sky_tev_table = upload_tev(sky_tev_records_, sky_tev_stages_, allocate, allocate_context);
            if (sky_record_table.cpu != nullptr && sky_tev_table != 0u) {
                std::memcpy(sky_record_table.cpu, sky_records_.data(), sky_records_.size() * sizeof(sky_records_[0]));
                c.full_light[3] = 1.0f;
                c.tev_info[1] = static_cast<std::uint32_t>(sky_tev_records_.size());
            }
        }
        // The emitting draws' ranges for EmitTriangleCS (which needs the scene's TEV records): the scene's emitting
        // triangles, then the glow emitters the last frame wrote (GlowEmitterCS).
        const std::uint32_t scene_emitters = tev_table != 0u ? emit_triangles_ : 0u;
        if (emit_triangle_pso_ && emit_scan_pso_ && scene_emitters + glow_emit_count_ != 0u) {
            if (!emit_tris_ &&
                !create_buffer(kEmitBufferBytes, D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                               D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS, emit_tris_, L"rt emitters")) {
                emit_tris_.Reset();
            }
            emit_range_table = allocate(allocate_context, 16u + emit_ranges_.size() * 16u, 256u);
            if (emit_tris_ && emit_range_table.cpu != nullptr) {
                auto* const words = static_cast<std::uint32_t*>(emit_range_table.cpu);
                words[0] = static_cast<std::uint32_t>(scene_emitters != 0u ? emit_ranges_.size() : 0u);
                words[1] = words[2] = words[3] = 0u;
                std::memcpy(words + 4u, emit_ranges_.data(), emit_ranges_.size() * 16u);
                c.tev_info[2] = scene_emitters + glow_emit_count_;
                c.emit_scene = scene_emitters;
            }
        }
        c.sun_light[3] = 1.0f;
        // The real sun's disc (0.533 degrees across): penumbrae as wide as distance from the caster makes them.
        c.sun[3] = std::tan(0.2665f * 3.14159265f / 180.0f);
        std::copy(scene_up_.begin(), scene_up_.end(), c.up);
        // The textures' de-light maps (EngineGBufferCS's relief, the hits' albedo): their relief height (on: above 0).
        c.up[3] = delight_ && delight_pso_ ? std::max(delight_relief_, 1e-3f) : 0.0f;
        // From the sun the game shows where it shows one (the key light is a lighting rig for the characters, and
        // shadows that follow it do not match the visible sun); a night stage's moon (rt-suns `moon`) from where the
        // table puts it; else from the key light's position.
        const bool table_moon = stage_sun_ != nullptr && stage_sun_->source == RtStageSun::Source::Moon && !sun_override_;
        const std::array<float, 3>& key =
            table_moon ? std::array<float, 3>{stage_sun_->direction[0], stage_sun_->direction[1], stage_sun_->direction[2]}
                       : area.lights[0].position;
        const std::array<float, 3> eye = visible_sun ? std::array<float, 3>{c.sun[0], c.sun[1], c.sun[2]} :
            area.lights[0].follows_camera && !table_moon
                ? key
                : std::array<float, 3>{camera_[0] * key[0] + camera_[1] * key[1] + camera_[2] * key[2],
                                       camera_[4] * key[0] + camera_[5] * key[1] + camera_[6] * key[2],
                                       camera_[8] * key[0] + camera_[9] * key[1] + camera_[10] * key[2]};
        const float length = std::sqrt(eye[0] * eye[0] + eye[1] * eye[1] + eye[2] * eye[2]);
        if (length > 1e-6f) {
            for (unsigned i = 0; i < 3u; ++i) {
                c.sun[i] = eye[i] / length;
            }
        }
        // Whatever it comes from, the light keeps its elevation band over the stage's horizon (world Y, above):
        // a sun the sky shows at least GALAXY_RT_SUN_MIN_ELEVATION up, anything else (the key, the game's stage key
        // light, the baked-light fit) between GALAXY_RT_KEY_MIN_ELEVATION and GALAXY_RT_SUN_MAX_ELEVATION. A stage key
        // as it stands can be a few degrees over the horizon (Bowser's Star Reactor), leaving half of every floor in
        // shadow.
        const auto hold_light = [&](bool shown) {
            if (sun_override_) {
                return;
            }
            // In world space (eye to world: the rotation's transpose), where the light's last direction is kept.
            std::array<float, 3> world{};
            std::array<float, 3> world_up{};
            for (unsigned r = 0; r < 3u; ++r) {
                for (unsigned k = 0; k < 3u; ++k) {
                    world[k] += camera_[r * 4u + k] * c.sun[r];
                    world_up[k] += camera_[r * 4u + k] * scene_up_[r];
                }
            }
            const int source = shown ? 0 : table_moon ? 1 : 2;
            const auto now = std::chrono::steady_clock::now();
            const float since = light_world_valid_ ? std::chrono::duration<float>(now - light_world_time_).count() : 1.0e9f;
            // Half a second without a trace (a load, a pause), another stage or another source: the light starts
            // where it stands.
            const bool continues = light_world_valid_ && light_world_source_ == source &&
                                   light_world_stage_ == stage_sun_ && since < 0.5f;
            // A game key that follows the camera (its light rig turns with the view) would turn the shadows with every
            // camera move: it stays where it stood in the world when the stage's light was first traced.
            const bool camera_key = !shown && !table_moon && area.lights[0].follows_camera;
            if (camera_key && continues) {
                world = light_world_;
            }
            hold_elevation(world, kStageUp, shown ? sun_min_elevation_ : key_min_elevation_,
                           shown ? 90.0f : sun_max_elevation_,
                           continues ? light_world_ : std::array<float, 3>{camera_[8], camera_[9], camera_[10]});
            // A change of the light's own direction (the game's sun or key moving, the sky's sun found again) eases
            // over a quarter of a second, as a sun moving across the sky rather than a cut.
            if (continues) {
                world = slerp_direction(light_world_, world, 1.0f - std::exp(-std::max(since, 0.0f) / 0.25f));
            }
            light_world_ = world;
            light_world_valid_ = camera_known_;
            light_world_source_ = source;
            light_world_stage_ = stage_sun_;
            light_world_time_ = now;
            for (unsigned r = 0; r < 3u; ++r) {
                c.sun[r] = camera_[r * 4u] * world[0] + camera_[r * 4u + 1u] * world[1] + camera_[r * 4u + 2u] * world[2];
            }
            // The game's off-screen shadow casters sweep along it.
            set_traced_sun(world, camera_known_);
            // GALAXY_TRACE_SUN=1: per traced scene the light's world direction, where it comes from (the sun the game
            // shows, else its key light or the table's moon) and the player's up.
            static const bool trace_sun = env_float("GALAXY_TRACE_SUN", 0.0f) != 0.0f;
            if (trace_sun) {
                std::fprintf(stderr, "[rt-sun] frame=%u source=%s world=(%.3f %.3f %.3f) up=(%.3f %.3f %.3f)\n",
                             c.frame, shown ? "sun" : table_moon ? "moon" : "key", world[0], world[1], world[2],
                             world_up[0], world_up[1], world_up[2]);
            }
        };
        hold_light(visible_sun);
        // The camera's exposure from the game's own stage light: a white surface facing the player's up under its
        // ambient and its key, the key at no less than 30 degrees up (physical_exposure). Set here so that a light
        // fitted dimmer to the baked light stays dimmer.
        {
            const auto luma = [](const float* rgb) { return 0.2126f * rgb[0] + 0.7152f * rgb[1] + 0.0722f * rgb[2]; };
            const float up_key = std::max(scene_up_[0] * c.sun[0] + scene_up_[1] * c.sun[1] + scene_up_[2] * c.sun[2],
                                          0.5f);
            const float reference = overcast ? overcast_reference : luma(c.sky_radiance) + luma(c.sun_light) * up_key;
            c.light_fit[0] = reference > 1e-4f ? luma(c.full_light) / reference : 0.0f;
            physical_exposure_ = c.light_fit[0];
            // The brightest the stage's own light makes a surface (its full light, facing it), as the picture shows it.
            physical_highlight_ = luma(c.full_light) * physical_exposure_;
            // The ambient's fill as the picture shows it (the reflections' sky floor).
            const auto srgb = [](float v) {
                v = std::clamp(v, 0.0f, 1.0f);
                return v <= 0.0031308f ? v * 12.92f : 1.055f * std::pow(v, 1.0f / 2.4f) - 0.055f;
            };
            for (unsigned i = 0; i < 3u; ++i) {
                sky_fill_[i] = srgb(c.sky_radiance[i] * c.light_fit[0]);
            }
            // The Comet Observatory (rt-suns `core`): night in space, lit by its star core and its lamps. The game's
            // key (a world-fixed light at 700000, 700000, 700000, the same in every galaxy) is no sun there: traced as
            // one it throws daylight shadows. It keeps the table's `strength` share as moonlight in the table's colour;
            // the core carries the light (its ball a sphere light, below). The camera stays exposed for the stage's
            // full light. A night stage's moon (rt-suns `moon`) is treated the same: its key a moon, the night kept.
            if (stage_sun_ != nullptr &&
                (stage_sun_->source == RtStageSun::Source::Core || stage_sun_->source == RtStageSun::Source::Moon) &&
                !sun_override_) {
                const float moon = luma(c.sun_light) * std::min(stage_sun_->strength, 1.0f);
                const float tint = stage_sun_->has_color ? std::max(luma(stage_sun_->color), 1e-3f) : 0.0f;
                const float full = std::max(luma(c.sun_light), 1e-4f);
                for (unsigned i = 0; i < 3u; ++i) {
                    c.sun_light[i] = tint > 0.0f ? moon * stage_sun_->color[i] / tint : c.sun_light[i] * moon / full;
                }
            } else if (!overcast) {
                // A sun (the game's or the table's): its light at sun_strength_ x the stage's key (GALAXY_RT_SUN_STRENGTH,
                // 1.25: a third of a stop), the camera still exposed for the stage's own light, so sunlit ground and walls
                // show brighter than the game's flat key and the shade stays as it was (owner review, 25 October). The
                // eye adaptation's highlight limit follows the brighter sun.
                for (unsigned i = 0; i < 3u; ++i) {
                    c.sun_light[i] *= sun_strength_;
                }
                physical_highlight_ = (luma(c.sky_radiance) + luma(c.sun_light)) * physical_exposure_;
            }
        }
    }
    std::array<std::uint32_t, kRtPointLights> point_keys{};   // the point lights' identities (light_remap)
    {
        // The game's point lights nearest in light to the camera, in eye space.
        std::array<GameLight, 32> lights{};
        const std::size_t count = game_point_lights(lights, 500u);
        struct Candidate {
            float importance;
            std::array<float, 3> eye;
            std::array<float, 3> color;
            float half;
            bool core = false;
            float radius = kPointEmitterRadius;   // its emitter's radius
            std::uint32_t key = 0u;               // its identity (GameLight.key; the star core's own)
        };
        std::array<Candidate, 32> candidates{};
        std::size_t used = 0u;
        for (std::size_t i = 0; i < count; ++i) {
            const auto& w = lights[i].position;
            std::array<float, 3> e = w;
            if (camera_known_) {
                for (unsigned r = 0; r < 3u; ++r) {
                    e[r] = camera_[r * 4u] * w[0] + camera_[r * 4u + 1u] * w[1] + camera_[r * 4u + 2u] * w[2] +
                           camera_[r * 4u + 3u];
                }
            } else {
                continue;
            }
            // Its colour less saturated (light mixes towards white on what it reaches); its
            // falloff the game's own (halved at half_distance), else GALAXY_RT_POINT_RADIUS.
            const auto& c0 = lights[i].color;
            const float l0 = 0.2126f * c0[0] + 0.7152f * c0[1] + 0.0722f * c0[2];
            std::array<float, 3> rgb{};
            for (unsigned k = 0; k < 3u; ++k) {
                rgb[k] = l0 + (c0[k] - l0) * point_saturation_;
            }
            const float half = lights[i].half_distance > 1.0f ? lights[i].half_distance : point_radius_;
            const float distance2 = e[0] * e[0] + e[1] * e[1] + e[2] * e[2];
            const float importance = l0 / (1.0f + distance2 / (144.0f * half * half));
            candidates[used++] = {importance, e, rgb, half, false, kPointEmitterRadius, lights[i].key};
        }
        // The star core: the brightest light of the Comet Observatory, a burning ball whose light grows with its
        // surface as the domes revive it. Its size is its fitted ball (StarSphereCS, read back): the actor's scale
        // stays 1 while the revival animations grow the ball, and an unrevived core draws none and lights nothing.
        // Its light halves at 1.2 ball radii from its centre.
        {
            std::array<float, 3> core{};
            float scale = 0.0f;
            if (camera_known_ && core_light_ > 0.0f && core_ball_radius_ > 0.0f && used < candidates.size() &&
                star_core(core, scale, 500u)) {
                std::array<float, 3> e{};
                for (unsigned r = 0; r < 3u; ++r) {
                    e[r] = camera_[r * 4u] * core[0] + camera_[r * 4u + 1u] * core[1] + camera_[r * 4u + 2u] * core[2] +
                           camera_[r * 4u + 3u];
                }
                // Its ball's radius at full revival (six domes) is 600 units: the game's sizes run from 100 to 600
                // with the domes (AstroCore's sensor table in the Petari decompilation).
                constexpr float kRevivedRadius = 600.0f;
                const float size = std::min(core_ball_radius_ / kRevivedRadius, 1.5f);
                float l = core_light_ * size * size;
                float half = core_ball_radius_ * 1.2f;
                // Physical (rt-suns `core`): a burning ball of the table's radiance. A sphere of radius R and
                // radiance L lights a surface facing it at distance d with L (R / d)^2 of a white surface's full light:
                // the game's falloff (h^2 / (h^2 + d^2)) with h = R, at L over the point lights' weight.
                if (physical_active_ && stage_sun_ != nullptr && stage_sun_->source == RtStageSun::Source::Core &&
                    stage_sun_->core > 0.0f) {
                    l = stage_sun_->core / std::max(point_light_, 1e-3f);
                    half = core_ball_radius_;
                }
                // In the core's own colour as the game draws it (red through blue as the domes revive it) at the
                // luminance of a warm white, which it keeps until the core has been seen on the stage.
                std::array<float, 3> hue{1.0f, 0.72f, 0.38f};
                const float warm = 0.2126f * hue[0] + 0.7152f * hue[1] + 0.0722f * hue[2];
                const float seen = 0.2126f * core_colour_[0] + 0.7152f * core_colour_[1] + 0.0722f * core_colour_[2];
                if (core_colour_known_ && seen > 1e-3f) {
                    for (unsigned k = 0; k < 3u; ++k) {
                        hue[k] = core_colour_[k] * warm / seen;
                    }
                }
                // Half its colour, as the sun's: a glowing ball's picture is more saturated than its light, and its
                // full colour tints white surfaces orange-pink.
                for (float& v : hue) {
                    v = warm + 0.5f * (v - warm);
                }
                candidates[used++] = {1.0e6f, e, {hue[0] * l, hue[1] * l, hue[2] * l}, half, true, core_ball_radius_,
                                      0xC0DE0001u};
            }
        }
        std::sort(candidates.begin(), candidates.begin() + static_cast<std::ptrdiff_t>(used),
                  [](const Candidate& a, const Candidate& b) { return a.importance > b.importance; });
        c.point_count = static_cast<std::uint32_t>(std::min<std::size_t>(used, kRtPointLights));
        // More lights than the budget: those near the cut fade with their importance's margin over the first one
        // left out, so a light crossing the cut as the camera moves dims in or out instead of switching.
        const float cut = used > c.point_count ? candidates[c.point_count].importance : 0.0f;
        c.point_light = point_light_;
        c.point_size = point_size_;
        c.core_point = ~0u;
        c.sun_cap = sun_cap_;
        for (std::uint32_t i = 0; i < c.point_count; ++i) {
            if (candidates[i].core) {
                c.core_point = i;
            }
        }
        for (std::uint32_t i = 0; i < c.point_count; ++i) {
            const float fade = cut > 0.0f && !candidates[i].core
                                   ? std::clamp((candidates[i].importance - cut) / (0.5f * cut), 0.0f, 1.0f)
                                   : 1.0f;
            for (unsigned k = 0; k < 3u; ++k) {
                c.points[2u * i][k] = candidates[i].eye[k];
                c.points[2u * i + 1u][k] = candidates[i].color[k] * fade;
            }
            // Reach: twelve half distances (1/145 of the light, windowed to 0).
            c.points[2u * i][3] = 12.0f * candidates[i].half;
            c.points[2u * i + 1u][3] = 1.0f / (candidates[i].half * candidates[i].half);
            point_keys[i] = candidates[i].key;
            c.point_radius[i] = candidates[i].radius;
        }
        window_.point_lights += c.point_count;
        // GALAXY_TRACE_LIGHTS=1: the lights of every 240th traced scene (diagnostics).
        static const bool trace_lights = env_float("GALAXY_TRACE_LIGHTS", 0.0f) != 0.0f;
        if (trace_lights && light_frame_ % 240u == 1u) {
            std::fprintf(stderr, "[rt-light] points %u of %zu candidates\n", c.point_count, used);
            for (std::uint32_t i = 0; i < c.point_count; ++i) {
                std::fprintf(stderr,
                             "[rt-light] point %u eye=(%.0f %.0f %.0f) color=(%.2f %.2f %.2f) half=%.0f importance=%.4f\n",
                             i, candidates[i].eye[0], candidates[i].eye[1], candidates[i].eye[2], candidates[i].color[0],
                             candidates[i].color[1], candidates[i].color[2], candidates[i].half,
                             candidates[i].importance);
            }
            std::array<GameLight, 16> world{};
            const std::size_t world_count = game_world_lights(world, 2000u);
            for (std::size_t i = 0; i < world_count; ++i) {
                std::fprintf(stderr, "[rt-light] world %zu position=(%.0f %.0f %.0f) color=(%.2f %.2f %.2f)\n", i,
                             world[i].position[0], world[i].position[1], world[i].position[2], world[i].color[0],
                             world[i].color[1], world[i].color[2]);
            }
            std::fprintf(stderr, "[rt-light] sun present=%d eye=(%.3f %.3f %.3f) light=(%.2f %.2f %.2f)\n",
                         c.sun_light[0] + c.sun_light[1] + c.sun_light[2] > 0.0f ? 1 : 0, c.sun[0], c.sun[1], c.sun[2],
                         c.sun_light[0], c.sun_light[1], c.sun_light[2]);
        }
    }
    // Earlier frames' eye positions of a point: their camera times the inverse of this one.
    std::array<float, 12> inverse{};
    const bool invertible = camera_known_ && affine_inverse(camera_, inverse);
    const bool history = history_valid_ && invertible && history_camera_known_;
    const bool picture = picture_valid_ && invertible && picture_camera_known_ && picture_width_ == sample_width &&
                         picture_height_ == sample_height && picture_factor_ == factor;
    const std::array<float, 12> reproject = history ? affine_multiply(history_camera_, inverse) : kIdentity34;
    const std::array<float, 12> to_picture = picture ? affine_multiply(picture_camera_, inverse) : kIdentity34;
    c.flags = (history ? 1u : 0u) | (picture ? 2u : 0u) | (show_term_ == 2u ? 4u : 0u) | (show_term_ == 3u ? 8u : 0u) |
              (show_term_ == 4u ? 16u : 0u) | (show_term_ == 5u ? 32u : 0u) |
              (show_term_ == 6u ? 64u : 0u) | (gi_ ? 0u : 128u) | (show_term_ == 7u ? 256u : 0u) |
              (show_term_ == 8u ? 512u : 0u) | (high_quality_ ? 1024u : 0u) |
              (light_textured_ && surface_uv_ && draw_texture_table_ != 0u ? 4096u : 0u) |
              (physical_active_ ? kPhysicalFlag : 0u);
    // Diagnostic flag bits no current shader reads: GALAXY_RT_EMIT_REUSE=0 no temporal reuse (bit 15),
    // GALAXY_RT_EMIT_CANDIDATES=n the candidates per sample (bits 16-23; 0: the quality's).
    {
        static const bool reuse = env_float("GALAXY_RT_EMIT_REUSE", 1.0f) != 0.0f;
        static const auto candidates =
            static_cast<std::uint32_t>(std::clamp(env_float("GALAXY_RT_EMIT_CANDIDATES", 0.0f), 0.0f, 255.0f));
        c.flags |= (reuse ? 0u : 32768u) | (candidates << 16u);
    }
    std::memcpy(c.reproject, reproject.data(), sizeof(c.reproject));
    std::memcpy(c.picture, to_picture.data(), sizeof(c.picture));
    std::copy(history ? history_projection_.begin() : view_.projection.begin(),
              history ? history_projection_.end() : view_.projection.end(), c.history_projection);
    c.motion_jitter[0] = history ? history_jitter_[0] - view_.jitter[0] : 0.0f;
    c.motion_jitter[1] = history ? history_jitter_[1] - view_.jitter[1] : 0.0f;
    std::copy(picture ? picture_projection_.begin() : view_.projection.begin(),
              picture ? picture_projection_.end() : view_.projection.end(), c.picture_projection);
    std::memcpy(c.to_eye, camera_.data(), sizeof(c.to_eye));
    c.ambient[0] = night_;
    c.ambient[1] = daylight_;
    c.ambient[2] = glow_boost_;
    c.ambient[3] = sky_distance_;
    // The ray tracing engine (its slot is the next one), with its pixels' buffer (its G-buffer writes the composite's
    // per-pixel normals, de-lighting and fog there).
    engine_running_ = false;
    {
        const unsigned next = next_light_slot_;
        const UINT64 bytes = static_cast<UINT64>(width) * height * 16u;
        if (!light_pixels_[next] || light_pixels_bytes_[next] < bytes) {
            retire(light_pixels_[next]);
            light_pixels_bytes_[next] = 0u;
            if (create_buffer(bytes, D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                              D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS, light_pixels_[next], L"rt pixel normals")) {
                light_pixels_bytes_[next] = bytes;
                light_pixels_state_[next] = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
            }
        }
        // This trace's lights against the last traced frame's: its reservoirs reach their lights through it.
        light_remap_view_ = light_remap(allocate, allocate_context, c.emit_scene, c.tev_info[2], point_keys.data(),
                                        c.point_count);
        // The camera in water (the game's report): the water medium it looks through.
        const float water_depth = camera_water_depth();
        c.engine.water[0] = water_depth >= 0.0f ? 1.0f : 0.0f;
        c.engine.water[1] = std::max(water_depth, 0.0f);
        const bool ran = light_pixels_[next] && engine_prepare(width, height, next, c.engine, c.flags);
        light_remap_commit(ran);
        if (!ran) {
            return;
        }
    }
    std::memcpy(upload.cpu, &c, sizeof(c));
    // The same lights give the reflections their highlights.
    std::copy(std::begin(c.sun), std::end(c.sun), reflect_lights_.sun.begin());
    reflect_lights_.sun_light = {c.sun_light[0], c.sun_light[1], c.sun_light[2], c.sun_reach};
    for (unsigned i = 0; i < 2u * kRtPointLights; ++i) {
        std::copy(std::begin(c.points[i]), std::end(c.points[i]), reflect_lights_.points[i].begin());
    }
    reflect_lights_.point_count = c.point_count;
    reflect_lights_.point_light = c.point_light;
    reflect_lights_.point_size = c.point_size;
    reflect_lights_.night_lights = c.night[3];
    reflect_lights_.night_cap = c.ao[3];
    reflect_lights_.sun_cap = c.sun_cap;
    reflect_lights_.core_point = c.core_point;
    // Physical: the reflections light their off-screen hits as this lighting does (its TEV records, emitting triangles
    // and traced sky as views in the frame's heap).
    reflect_physical_ = {};
    {
        reflect_physical_.on = true;
        std::copy(std::begin(c.sky_radiance), std::end(c.sky_radiance), reflect_physical_.sky_radiance.begin());
        std::copy(std::begin(c.full_light), std::end(c.full_light), reflect_physical_.full_light.begin());
        std::copy(std::begin(c.up), std::end(c.up), reflect_physical_.up.begin());
        std::copy(std::begin(c.light_fit), std::end(c.light_fit), reflect_physical_.light_fit.begin());
        if (tev_table != 0u && tev_upload.resource != nullptr) {
            reflect_physical_.tev_view = buffer_view(tev_upload.resource, tev_upload.offset, tev_bytes);
            reflect_physical_.tev_count = c.tev_info[0];
        }
        if (c.tev_info[2] != 0u && emit_tris_) {
            reflect_physical_.emit_view = buffer_view(emit_tris_.Get(), 0u, kEmitBufferBytes);
            reflect_physical_.emit_count = c.tev_info[2];
            reflect_physical_.emit_scene = c.emit_scene;
        }
        if (c.full_light[3] != 0.0f && sky_probe_map_) {
            reflect_physical_.sky_view =
                buffer_view(sky_probe_map_.Get(), 0u, (static_cast<std::uint64_t>(kSkyMapSize) * kSkyMapSize + 16u) * 16u);
        }
    }

    const unsigned current = light_parity_;
    const unsigned previous = current ^ 1u;
    const unsigned slot = next_light_slot_;
    next_light_slot_ = (next_light_slot_ + 1u) % kLightSlots;
    light_slot_full_[slot] = physical_active_ ? light_full_ : std::array<float, 3>{1.0f, 1.0f, 1.0f};
    reflections_.invalidate(slot);
    constexpr D3D12_RESOURCE_STATES kUav = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    constexpr D3D12_RESOURCE_STATES kRead = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
    to_state(list, light_z_[current].Get(), light_z_state_[current], kUav);
    to_state(list, light_z_[previous].Get(), light_z_state_[previous], kRead);
    to_state(list, light_final_[slot].Get(), light_final_state_[slot], kUav);
    to_state(list, picture_.Get(), picture_state_, kRead);
    to_state(list, picture_sums_.Get(), picture_sums_state_, kRead);
    to_state(list, positions_.Get(), positions_state_, kRead);
    to_state(list, surfaces_.Get(), surfaces_state_, kRead);
    if (surface_uv_) {
        to_state(list, surface_uv_.Get(), surface_uv_state_, kRead);
    }
    to_state(list, emit_list_.Get(), emit_list_state_, kRead);
    to_state(list, glow_base_.Get(), glow_base_state_, kRead);
    to_state(list, glow_added_.Get(), glow_added_state_, kRead);
    to_state(list, sky_sh_.Get(), sky_sh_state_, kRead);

    run_delight(list);
    list->SetComputeRootSignature(light_root_.Get());
    list->SetComputeRootConstantBufferView(kLightFrame, upload.gpu);
    list->SetComputeRootShaderResourceView(kLightScene, tlas_->GetGPUVirtualAddress());
    list->SetComputeRootDescriptorTable(kLightDepth, depth_srv);
    list->SetComputeRootShaderResourceView(kLightPrevZ, light_z_[previous]->GetGPUVirtualAddress());
    // Stand-ins in the history and reservoir slots, which no shader reads.
    list->SetComputeRootShaderResourceView(kLightPrevHistory, positions_->GetGPUVirtualAddress());
    list->SetComputeRootShaderResourceView(kLightHistoryIn, positions_->GetGPUVirtualAddress());
    list->SetComputeRootShaderResourceView(kLightPicture, picture_->GetGPUVirtualAddress());
    list->SetComputeRootShaderResourceView(kLightPictureSums, picture_sums_->GetGPUVirtualAddress());
    list->SetComputeRootShaderResourceView(kLightPositions, positions_->GetGPUVirtualAddress());
    list->SetComputeRootShaderResourceView(kLightSurfaces, surfaces_->GetGPUVirtualAddress());
    list->SetComputeRootShaderResourceView(kLightEmitters, emit_list_->GetGPUVirtualAddress());
    list->SetComputeRootShaderResourceView(kLightGlowBase, glow_base_->GetGPUVirtualAddress());
    list->SetComputeRootShaderResourceView(kLightGlowAdded, glow_added_->GetGPUVirtualAddress());
    list->SetComputeRootShaderResourceView(kLightDraws, draw_table.gpu);
    list->SetComputeRootShaderResourceView(kLightSky, sky_sh_->GetGPUVirtualAddress());
    list->SetComputeRootUnorderedAccessView(kLightZ, light_z_[current]->GetGPUVirtualAddress());
    list->SetComputeRootUnorderedAccessView(kLightNormal, light_normal_->GetGPUVirtualAddress());
    list->SetComputeRootUnorderedAccessView(kLightRaw, light_raw_->GetGPUVirtualAddress());
    list->SetComputeRootUnorderedAccessView(kLightHistory, light_raw_->GetGPUVirtualAddress());
    list->SetComputeRootUnorderedAccessView(kLightFinal, light_final_[slot]->GetGPUVirtualAddress());
    list->SetComputeRootUnorderedAccessView(kLightPrimary, light_primary_->GetGPUVirtualAddress());
    list->SetComputeRootShaderResourceView(kLightReservoirIn, positions_->GetGPUVirtualAddress());
    list->SetComputeRootUnorderedAccessView(kLightReservoirOut, light_raw_->GetGPUVirtualAddress());
    // The alpha tests' texture coordinates and alpha maps (stand-ins the trace never reads without them: g_flags
    // bit 12 off).
    list->SetComputeRootShaderResourceView(kLightSurfaceUv, surface_uv_ ? surface_uv_->GetGPUVirtualAddress()
                                                                        : positions_->GetGPUVirtualAddress());
    list->SetComputeRootShaderResourceView(kLightDrawTextures,
                                           draw_texture_table_ != 0u ? draw_texture_table_ : draw_table.gpu);
    list->SetComputeRootUnorderedAccessView(kLightReference, light_raw_->GetGPUVirtualAddress());
    // The traced sky (kPhysicalFlag): the probe traces the sky's own geometry from the camera into its map and the
    // map's spherical harmonics, with the sky's buffers in the scene's slots, before the traces read them.
    const bool sky_traced = c.full_light[3] != 0.0f;
    if (sky_traced) {
        to_state(list, sky_probe_map_.Get(), sky_probe_map_state_, kUav);
        list->SetComputeRootShaderResourceView(kLightScene, sky_tlas_->GetGPUVirtualAddress());
        list->SetComputeRootShaderResourceView(kLightPositions, sky_positions_->GetGPUVirtualAddress());
        list->SetComputeRootShaderResourceView(kLightSurfaces, sky_surfaces_->GetGPUVirtualAddress());
        list->SetComputeRootShaderResourceView(kLightSurfaceUv, sky_uv_->GetGPUVirtualAddress());
        list->SetComputeRootShaderResourceView(kLightDrawTextures, sky_record_table.gpu);
        list->SetComputeRootUnorderedAccessView(kLightReference, sky_probe_map_->GetGPUVirtualAddress());
        list->SetComputeRootShaderResourceView(kLightSkyMap, positions_->GetGPUVirtualAddress());
        list->SetComputeRootShaderResourceView(kLightTev, sky_tev_table);
        dred_marker(list, L"sky_probe");
        list->SetPipelineState(sky_probe_pso_.Get());
        list->Dispatch(kSkyMapSize / 8u, kSkyMapSize / 8u, 1u);
        const D3D12_RESOURCE_BARRIER probe_uav = uav_barrier(sky_probe_map_.Get());
        list->ResourceBarrier(1u, &probe_uav);
        dred_marker(list, L"sky_trace_sh");
        list->SetPipelineState(sky_trace_sh_pso_.Get());
        list->Dispatch(1u, 1u, 1u);
        list->ResourceBarrier(1u, &probe_uav);
        const UINT64 map_bytes = (static_cast<UINT64>(kSkyMapSize) * kSkyMapSize + 16u) * 16u;
        if (sky_dump_at_ != 0u && frame_index_ >= sky_dump_at_ && sky_dump_taken_ == 0u &&
            (sky_dump_readback_ || create_buffer(map_bytes, D3D12_HEAP_TYPE_READBACK, D3D12_RESOURCE_STATE_COPY_DEST,
                                                 D3D12_RESOURCE_FLAG_NONE, sky_dump_readback_, L"rt sky dump"))) {
            to_state(list, sky_probe_map_.Get(), sky_probe_map_state_, D3D12_RESOURCE_STATE_COPY_SOURCE);
            list->CopyBufferRegion(sky_dump_readback_.Get(), 0u, sky_probe_map_.Get(), 0u, map_bytes);
            sky_dump_taken_ = frame_index_ + 1u;
            std::cerr << "[rt-sky] dump frame=" << frame_index_ << " draws=" << sky_records_.size()
                      << " triangles=" << sky_built_ << '\n';
        }
        to_state(list, sky_probe_map_.Get(), sky_probe_map_state_, kRead);
        list->SetComputeRootShaderResourceView(kLightScene, tlas_->GetGPUVirtualAddress());
        list->SetComputeRootShaderResourceView(kLightPositions, positions_->GetGPUVirtualAddress());
        list->SetComputeRootShaderResourceView(kLightSurfaces, surfaces_->GetGPUVirtualAddress());
        list->SetComputeRootShaderResourceView(kLightSurfaceUv, surface_uv_ ? surface_uv_->GetGPUVirtualAddress()
                                                                            : positions_->GetGPUVirtualAddress());
        list->SetComputeRootShaderResourceView(kLightDrawTextures,
                                               draw_texture_table_ != 0u ? draw_texture_table_ : draw_table.gpu);
        list->SetComputeRootUnorderedAccessView(kLightReference, light_raw_->GetGPUVirtualAddress());
    }
    list->SetComputeRootShaderResourceView(kLightSkyMap, sky_traced ? sky_probe_map_->GetGPUVirtualAddress()
                                                                    : positions_->GetGPUVirtualAddress());
    list->SetComputeRootShaderResourceView(kLightTev, tev_table != 0u ? tev_table : draw_table.gpu);
    list->SetComputeRootUnorderedAccessView(kLightPixels, light_raw_->GetGPUVirtualAddress());
    // Physical: the emitting triangles (EmitTriangleCS into the emitters' buffer in the reference's slot, then their
    // running power), before the trace samples them.
    if (c.tev_info[2] != 0u) {
        to_state(list, emit_tris_.Get(), emit_tris_state_, kUav);
        list->SetComputeRootShaderResourceView(kLightEmitTris, emit_range_table.gpu);
        list->SetComputeRootUnorderedAccessView(kLightReference, emit_tris_->GetGPUVirtualAddress());
        dred_marker(list, L"emit_triangle");
        list->SetPipelineState(emit_triangle_pso_.Get());
        list->Dispatch((c.emit_scene + 63u) / 64u, 1u, 1u);
        const D3D12_RESOURCE_BARRIER emit_uav = uav_barrier(emit_tris_.Get());
        list->ResourceBarrier(1u, &emit_uav);
        dred_marker(list, L"emit_scan");
        list->SetPipelineState(emit_scan_pso_.Get());
        list->Dispatch(1u, 1u, 1u);
        to_state(list, emit_tris_.Get(), emit_tris_state_, kRead);
        list->SetComputeRootUnorderedAccessView(kLightReference, light_raw_->GetGPUVirtualAddress());
    }
    list->SetComputeRootShaderResourceView(kLightEmitTris, c.tev_info[2] != 0u ? emit_tris_->GetGPUVirtualAddress()
                                                                              : positions_->GetGPUVirtualAddress());
    // The stars' balls, after the samples in the result (the composite shades the stars on them, reflections that
    // meet a star find its ball): every slot each frame, one without a star this frame written empty.
    dred_marker(list, L"star_sphere");
    list->SetPipelineState(star_sphere_pso_.Get());
    list->Dispatch(kStarSlots, 1u, 1u);
    // The per-pixel normals and de-lighting of this frame into the slot's buffer (the composite's).
    to_state(list, light_pixels_[slot].Get(), light_pixels_state_[slot], kUav);
    list->SetComputeRootUnorderedAccessView(kLightPixels, light_pixels_[slot]->GetGPUVirtualAddress());
    // The engine; its constants and bindings kept for the clear pass at the scene's end.
    engine_clear_valid_[slot] = false;
    water_traced_[slot] = false;
    engine_bind_.valid = true;
    {
        engine_bind_.slot = slot;
        engine_bind_.constants.assign(reinterpret_cast<const std::byte*>(&c), reinterpret_cast<const std::byte*>(&c) + sizeof(c));
        engine_bind_.engine_at = offsetof(RtLightConstants, engine);
        engine_bind_.draws = draw_table.gpu;
        engine_bind_.tev = tev_table != 0u ? tev_table : draw_table.gpu;
        engine_bind_.surface_uv = surface_uv_ ? surface_uv_->GetGPUVirtualAddress() : positions_->GetGPUVirtualAddress();
        engine_bind_.draw_textures = draw_texture_table_ != 0u ? draw_texture_table_ : draw_table.gpu;
        engine_bind_.sky_map = sky_traced ? sky_probe_map_->GetGPUVirtualAddress() : positions_->GetGPUVirtualAddress();
        engine_bind_.emit_tris =
            c.tev_info[2] != 0u ? emit_tris_->GetGPUVirtualAddress() : positions_->GetGPUVirtualAddress();
        engine_bind_.tev_records = tev_table != 0u;
        engine_bind_.textured = draw_texture_table_ != 0u;
    }
    trace_engine(list, allocate, allocate_context, &c, sizeof(c), offsetof(RtLightConstants, engine), upload.gpu, slot);
    const D3D12_RESOURCE_BARRIER all_uav = uav_barrier(nullptr);
    list->ResourceBarrier(1u, &all_uav);
    // The stars' balls for the star core's light, read a few frames later (begin_frame).
    if (ball_readback_ && ball_readback_frame_[ball_readback_next_] == 0u) {
        const UINT64 at = static_cast<UINT64>(ball_readback_next_) * kBallReadbackBytes;
        to_state(list, light_final_[slot].Get(), light_final_state_[slot], D3D12_RESOURCE_STATE_COPY_SOURCE);
        list->CopyBufferRegion(ball_readback_.Get(), at, light_final_[slot].Get(),
                               static_cast<UINT64>(sample_width) * sample_height * 16u, kStarSlots * 16u);
        if (probe_ || engine_reference_running_ || light_meter_) {
            list->CopyBufferRegion(ball_readback_.Get(), at + kStarSlots * 16u + 16u, light_final_[slot].Get(),
                                   (static_cast<UINT64>(sample_width) * sample_height + kStarSlots) * 16u,
                                   kProbeRecords * 16u);
            probe_materials_[ball_readback_next_] = draw_materials_;
        }
        engine_reference_taken_paths_[ball_readback_next_] = engine_reference_running_ ? engine_reference_paths_ : 0u;
        ball_readback_colour_[ball_readback_next_] = picture_valid_;
        if (picture_valid_) {
            to_state(list, picture_sums_.Get(), picture_sums_state_, D3D12_RESOURCE_STATE_COPY_SOURCE);
            list->CopyBufferRegion(ball_readback_.Get(), at + kStarSlots * 16u, picture_sums_.Get(),
                                   kPictureStarColour, 16u);
            to_state(list, picture_sums_.Get(), picture_sums_state_, kRead);
        }
        ball_readback_frame_[ball_readback_next_] = frame_index_ + 1u;
        ball_readback_next_ = (ball_readback_next_ + 1u) % kBallReadbacks;
    }
    to_state(list, light_final_[slot].Get(), light_final_state_[slot], kLightRead);

    LightSlot& info = light_slots_[slot];
    info.view = view_;
    info.camera = camera_;
    info.camera_known = camera_known_;
    info.width = width;
    info.height = height;
    info.sample_width = sample_width;
    info.sample_height = sample_height;
    info.factor = factor;
    info.near_plane = c.near_plane;
    info.pixels = true;
    for (unsigned i = 0; i < 3u; ++i) {
        info.sun[i] = c.sun[i];
        info.sun_visible[i] = sun_visible_eye_[i];
        info.sun_shown = sun_shown_;
        info.sun_light[i] = c.sun_light[i];
        info.water_sky[i] = c.full_light[i];
    }
    std::copy(std::begin(c.engine.water), std::end(c.engine.water), info.water.begin());
    history_camera_ = camera_;
    history_camera_known_ = camera_known_;
    history_projection_ = view_.projection;
    history_jitter_ = view_.jitter;
    light_parity_ = previous;
    history_valid_ = true;
    light_slot_ = static_cast<int>(slot);
}

void SceneRayTracer::snapshot_glow_base(ID3D12GraphicsCommandList* list, D3D12_GPU_DESCRIPTOR_HANDLE color_depth_table,
                                        D3D12_GPU_DESCRIPTOR_HANDLE game_srv, std::uint32_t width,
                                        std::uint32_t height) {
    if (!glow_open_ || !glow_base_pso_ || list == nullptr || color_depth_table.ptr == 0u || !view_known_ ||
        width == 0u || height == 0u) {
        return;
    }
    const std::uint32_t factor = sample_factor(width, height);
    const std::uint32_t sample_width = (width + factor - 1u) / factor;
    const std::uint32_t sample_height = (height + factor - 1u) / factor;
    if (!ensure_lighting_buffers(sample_width * sample_height)) {
        return;
    }
    RtCaptureConstants c{};
    c.depth[0] = view_.z_scale;
    c.depth[1] = view_.z_offset;
    c.depth[2] = view_.min_depth;
    c.depth[3] = view_.max_depth;
    std::copy(view_.viewport.begin(), view_.viewport.end(), c.viewport);
    c.target_size[0] = width;
    c.target_size[1] = height;
    c.sample_size[0] = sample_width;
    c.sample_size[1] = sample_height;
    c.factor = factor;
    c.split = view_.viewport[0] + split_ * view_.viewport[2];
    c.sky_distance = sky_distance_;
    const bool measure = exposure_ && exposure_sums_ && game_srv.ptr != 0u;
    // Physical: the eye's adaptation measures this frame's lighting result (c_final) into the picture sums.
    const bool adapt = physical_active_ && light_slot_ >= 0 && light_final_[static_cast<unsigned>(light_slot_)] &&
                       picture_sums_ && physical_exposure_ > 0.0f;
    c.exposure = (measure ? 1u : 0u) | (adapt ? 8u : 0u);
    // The camera's exposure for the stage's light, before the eye's adaptation, in the art exposure's place
    // (rt_lighting.hlsl c_exposure_day).
    if (adapt) {
        c.exposure_day = physical_exposure_;
    }
    const int timed = begin_pass(list);
    to_state(list, glow_base_.Get(), glow_base_state_, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    if (measure) {
        // After the last capture's reads and clears.
        const D3D12_RESOURCE_BARRIER barrier = uav_barrier(exposure_sums_.Get());
        list->ResourceBarrier(1u, &barrier);
    }
    list->SetComputeRootSignature(capture_root_.Get());
    dred_marker(list, L"glow_base");
    list->SetPipelineState(glow_base_pso_.Get());
    list->SetComputeRoot32BitConstants(kCaptureConstants, sizeof(c) / 4u, &c, 0u);
    list->SetComputeRootDescriptorTable(kCaptureSources, color_depth_table);
    list->SetComputeRootDescriptorTable(kCaptureGame, measure ? game_srv : color_depth_table);
    list->SetComputeRootUnorderedAccessView(kCaptureExposure,
                                            exposure_sums_ ? exposure_sums_->GetGPUVirtualAddress()
                                                           : glow_base_->GetGPUVirtualAddress());
    list->SetComputeRootUnorderedAccessView(kCaptureGlow, glow_base_->GetGPUVirtualAddress());
    // The eye's adaptation reads the lighting's result and adds to the picture sums (bound always: no root view of the
    // pass is left unset).
    if (adapt) {
        to_state(list, picture_sums_.Get(), picture_sums_state_, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    }
    list->SetComputeRootShaderResourceView(
        kCaptureFinal, adapt ? light_final_[static_cast<unsigned>(light_slot_)]->GetGPUVirtualAddress()
                             : light_normal_->GetGPUVirtualAddress());
    list->SetComputeRootUnorderedAccessView(kCaptureSums, adapt ? picture_sums_->GetGPUVirtualAddress()
                                                                : glow_base_->GetGPUVirtualAddress());
    list->Dispatch((sample_width + 7u) / 8u, (sample_height + 7u) / 8u, 1u);
    to_state(list, glow_base_.Get(), glow_base_state_, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    end_pass(list, timed, kPassGlowBase);
    glow_base_valid_ = true;
}

void SceneRayTracer::capture_picture(ID3D12GraphicsCommandList* list, const RtSources& sources, Allocate allocate,
                                     void* allocate_context, D3D12_GPU_DESCRIPTOR_HANDLE color_depth_table,
                                     std::uint32_t width, std::uint32_t height, D3D12_GPU_DESCRIPTOR_HANDLE game_srv) {
    finish_late(list, sources, allocate, allocate_context);
    const bool glow = glow_open_ && glow_base_valid_;
    glow_open_ = false;
    if (!lighting_active() || !capture_pso_ || list == nullptr || color_depth_table.ptr == 0u || !view_known_ ||
        width == 0u || height == 0u) {
        return;
    }
    const std::uint32_t factor = sample_factor(width, height);
    const std::uint32_t sample_width = (width + factor - 1u) / factor;
    const std::uint32_t sample_height = (height + factor - 1u) / factor;
    if (!ensure_lighting_buffers(sample_width * sample_height)) {
        return;
    }
    // This scene's own lighting is divided out where its result matches the picture.
    const bool divide = light_slot_ >= 0 && light_slots_[static_cast<unsigned>(light_slot_)].width == width &&
                        light_slots_[static_cast<unsigned>(light_slot_)].height == height;
    RtCaptureConstants c{};
    c.depth[0] = view_.z_scale;
    c.depth[1] = view_.z_offset;
    c.depth[2] = view_.min_depth;
    c.depth[3] = view_.max_depth;
    std::copy(view_.viewport.begin(), view_.viewport.end(), c.viewport);
    c.target_size[0] = width;
    c.target_size[1] = height;
    c.sample_size[0] = sample_width;
    c.sample_size[1] = sample_height;
    c.factor = factor;
    c.emissive = emissive_;
    c.divide = (divide ? 1u : 0u) | (glow ? 2u : 0u);
    c.strength = strength_;
    const bool emit = glow && emitters_active() && emit_merge_pso_ && camera_known_;
    std::array<float, 12> to_world{};
    const bool inverted = camera_known_ && affine_inverse(camera_, to_world);
    const bool surfaces = emit && surface_emission_ > 0.0f && inverted;
    // The sky's colour by world direction, into a free readback.
    SkyRead& sky_read = sky_reads_[sky_next_];
    const bool sky = inverted && !sky_read.pending;
    if (sky) {
        c.divide |= 4u;
        to_state(list, sky_bins_.Get(), sky_bins_state_, D3D12_RESOURCE_STATE_COPY_DEST);
        list->CopyBufferRegion(sky_bins_.Get(), 0u, sky_zero_.Get(), 0u, kSkyBins * kSkyBinWords * 4u);
    }
    to_state(list, sky_bins_.Get(), sky_bins_state_, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    if (sky_radiance_generation_ != sky_generation_) {
        // A new stage: its sky is unknown until it is seen.
        sky_radiance_generation_ = sky_generation_;
        to_state(list, sky_radiance_.Get(), sky_radiance_state_, D3D12_RESOURCE_STATE_COPY_DEST);
        to_state(list, sky_sh_.Get(), sky_sh_state_, D3D12_RESOURCE_STATE_COPY_DEST);
        list->CopyBufferRegion(sky_radiance_.Get(), 0u, sky_zero_.Get(), 0u, kSkyBins * 16u);
        list->CopyBufferRegion(sky_sh_.Get(), 0u, sky_zero_.Get(), 0u, 128u);
    }
    to_state(list, sky_radiance_.Get(), sky_radiance_state_, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    to_state(list, sky_sh_.Get(), sky_sh_state_, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    c.surface_gain = surface_emission_;
    c.sky_distance = sky_distance_;
    c.exposure_day = exposure_day_;
    c.exposure_night = exposure_night_;
    // Physical: the eye's adaptation weighs the light as the camera's exposure shows it, and limits the brightest tenth
    // of the surfaces to the stage's own full light as shown (it had the art exposure, 1, which halved the Observatory's
    // highlight priority against its 1.9).
    if (physical_active_ && physical_exposure_ > 0.0f) {
        c.exposure_day = physical_exposure_;
        c.exposure_night = physical_highlight_;
    }
    c.daylight = daylight_;
    c.exposure = (!exposure_ ? 0u : exposure_reset_ ? 2u : 1u) | (physical_active_ ? 8u : 0u);
    if (exposure_reset_) {
        // A new stage: SkyStateCS forgets the stars' colour; the copies taken after this frame read it again.
        c.divide |= 8u;
        star_colour_from_frame_ = frame_index_ + 1u;
    }
    exposure_reset_ = false;
    std::copy(view_.projection.begin(), view_.projection.end(), c.projection);
    std::memcpy(c.to_world, to_world.data(), sizeof(c.to_world));
    const int timed = begin_pass(list);
    to_state(list, emit_tiles_.Get(), emit_tiles_state_, D3D12_RESOURCE_STATE_COPY_DEST);
    list->CopyBufferRegion(emit_tiles_.Get(), 0u, stats_zero_.Get(), 0u, 16u);
    to_state(list, emit_tiles_.Get(), emit_tiles_state_, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    c.split = view_.viewport[0] + split_ * view_.viewport[2];

    to_state(list, picture_.Get(), picture_state_, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    to_state(list, picture_sums_.Get(), picture_sums_state_, D3D12_RESOURCE_STATE_COPY_DEST);
    list->CopyBufferRegion(picture_sums_.Get(), 0u, stats_zero_.Get(), 0u, kPictureSumsCleared);
    list->CopyBufferRegion(picture_sums_.Get(), 40u, stats_zero_.Get(), 0u, 8u);
    list->CopyBufferRegion(picture_sums_.Get(), 56u, stats_zero_.Get(), 0u, 8u);
    static_assert(kPictureStarSlots == kStarSlots && kPictureStarSlots * 16u % (kStatsWords * 4u) == 0u);
    for (UINT64 at = kPictureStarSums; at < kPictureStarColour; at += kStatsWords * 4u) {
        list->CopyBufferRegion(picture_sums_.Get(), at, stats_zero_.Get(), 0u, kStatsWords * 4u);
    }
    to_state(list, picture_sums_.Get(), picture_sums_state_, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    list->SetComputeRootSignature(capture_root_.Get());
    dred_marker(list, L"capture");
    list->SetPipelineState(capture_pso_.Get());
    list->SetComputeRoot32BitConstants(kCaptureConstants, sizeof(c) / 4u, &c, 0u);
    list->SetComputeRootDescriptorTable(kCaptureSources, color_depth_table);
    to_state(list, glow_base_.Get(), glow_base_state_, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    to_state(list, glow_added_.Get(), glow_added_state_, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    list->SetComputeRootShaderResourceView(
        kCaptureFinal, divide ? light_final_[static_cast<unsigned>(light_slot_)]->GetGPUVirtualAddress()
                              : light_normal_->GetGPUVirtualAddress());
    list->SetComputeRootShaderResourceView(kCaptureGlowBase, glow_base_->GetGPUVirtualAddress());
    list->SetComputeRootUnorderedAccessView(kCapturePicture, picture_->GetGPUVirtualAddress());
    list->SetComputeRootUnorderedAccessView(kCaptureSums, picture_sums_->GetGPUVirtualAddress());
    list->SetComputeRootUnorderedAccessView(kCaptureGlow, glow_added_->GetGPUVirtualAddress());
    list->SetComputeRootUnorderedAccessView(kCaptureFound, emit_tiles_->GetGPUVirtualAddress());
    list->SetComputeRootUnorderedAccessView(kCaptureSky, sky_bins_->GetGPUVirtualAddress());
    list->SetComputeRootUnorderedAccessView(kCaptureSkyRadiance, sky_radiance_->GetGPUVirtualAddress());
    list->SetComputeRootUnorderedAccessView(kCaptureSkySh, sky_sh_->GetGPUVirtualAddress());
    list->SetComputeRootUnorderedAccessView(kCaptureExposure, exposure_sums_->GetGPUVirtualAddress());
    list->SetComputeRootDescriptorTable(kCaptureGame, game_srv.ptr != 0u ? game_srv : color_depth_table);
    list->Dispatch((sample_width + 7u) / 8u, (sample_height + 7u) / 8u, 1u);
    if (sky) {
        // The sky light: the bins into the map over time, then its spherical harmonics.
        const D3D12_RESOURCE_BARRIER barrier = uav_barrier(nullptr);
        list->ResourceBarrier(1u, &barrier);
        dred_marker(list, L"sky_light_map");
        list->SetPipelineState(sky_light_map_pso_.Get());
        list->Dispatch(kSkyBins / 64u, 1u, 1u);
        list->ResourceBarrier(1u, &barrier);
        dred_marker(list, L"sky_light_sh");
        list->SetPipelineState(sky_light_sh_pso_.Get());
        list->Dispatch(1u, 1u, 1u);
        list->ResourceBarrier(1u, &barrier);
        dred_marker(list, L"capture");
        list->SetPipelineState(capture_pso_.Get());
        constexpr UINT64 kSkyBytes = kSkyBins * kSkyBinWords * 4u;
        to_state(list, sky_bins_.Get(), sky_bins_state_, D3D12_RESOURCE_STATE_COPY_SOURCE);
        list->CopyBufferRegion(sky_readback_.Get(), sky_next_ * kSkyBytes, sky_bins_.Get(), 0u, kSkyBytes);
        sky_read = {frame_index_, sky_generation_, true};
        sky_next_ = (sky_next_ + 1u) % kSkyRing;
    }
    if (surfaces) {
        // Glowing surfaces (bright, under their own glow): lights at their own depth.
        const D3D12_RESOURCE_BARRIER barrier = uav_barrier(nullptr);
        list->ResourceBarrier(1u, &barrier);
        dred_marker(list, L"surface_emit");
        list->SetPipelineState(surface_emit_pso_.Get());
        list->Dispatch((sample_width + 7u) / 8u, (sample_height + 7u) / 8u, 1u);
    }
    {
        // The composite's additions (a draw earlier in this list) and the capture's sums.
        const D3D12_RESOURCE_BARRIER barriers[2]{uav_barrier(picture_sums_.Get()), uav_barrier(exposure_sums_.Get())};
        list->ResourceBarrier(2u, barriers);
    }
    dred_marker(list, L"sky_state");
    list->SetPipelineState(sky_state_pso_.Get());
    list->Dispatch(1u, 1u, 1u);
    // The eased adaptation for the CPU (glow gains), a few frames later.
    if (physical_active_ && picture_sums_ &&
        (adaptation_readback_ ||
         create_buffer(kAdaptationReadbacks * 16u, D3D12_HEAP_TYPE_READBACK, D3D12_RESOURCE_STATE_COPY_DEST,
                       D3D12_RESOURCE_FLAG_NONE, adaptation_readback_, L"rt adaptation readback")) &&
        adaptation_readback_frame_[adaptation_readback_next_] == 0u) {
        const D3D12_RESOURCE_BARRIER sums_uav = uav_barrier(picture_sums_.Get());
        list->ResourceBarrier(1u, &sums_uav);
        const D3D12_RESOURCE_STATES before = picture_sums_state_;
        to_state(list, picture_sums_.Get(), picture_sums_state_, D3D12_RESOURCE_STATE_COPY_SOURCE);
        list->CopyBufferRegion(adaptation_readback_.Get(), adaptation_readback_next_ * 16u, picture_sums_.Get(),
                               kPictureAdaptation, 8u);
        to_state(list, picture_sums_.Get(), picture_sums_state_, before);
        adaptation_readback_frame_[adaptation_readback_next_] = frame_index_ + 1u;
        adaptation_readback_next_ = (adaptation_readback_next_ + 1u) % kAdaptationReadbacks;
    }
    if (emitters_active() && emit_merge_pso_ && camera_known_) {
        const int lights_timed = begin_pass(list);
        capture_glow_lights(list, sources, allocate, allocate_context, color_depth_table, width, height, factor,
                            glow);
        end_pass(list, lights_timed, kPassLights);
    }
    to_state(list, picture_.Get(), picture_state_, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    to_state(list, picture_sums_.Get(), picture_sums_state_, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    end_pass(list, timed, kPassCapture);
    ++window_.captures;
    picture_valid_ = true;
    picture_camera_ = camera_;
    picture_camera_known_ = camera_known_;
    picture_projection_ = view_.projection;
    picture_width_ = sample_width;
    picture_height_ = sample_height;
    picture_factor_ = factor;
}

void SceneRayTracer::capture_glow_lights(ID3D12GraphicsCommandList* list, const RtSources& sources,
                                         Allocate allocate, void* allocate_context,
                                         D3D12_GPU_DESCRIPTOR_HANDLE color_depth_table, std::uint32_t width,
                                         std::uint32_t height, std::uint32_t factor, bool glow) {
    std::array<float, 12> to_world{};
    if (!affine_inverse(camera_, to_world)) {
        return;
    }
    // A load (no scene for half a second) or a jump of the camera starts a new set of lights.
    const auto now = std::chrono::steady_clock::now();
    const std::array<float, 3> eye{to_world[3], to_world[7], to_world[11]};
    const float jump = std::abs(eye[0] - emit_last_eye_[0]) + std::abs(eye[1] - emit_last_eye_[1]) +
                       std::abs(eye[2] - emit_last_eye_[2]);
    const bool reset = !emit_seen_ || now - emit_last_time_ > std::chrono::milliseconds(500) || jump > 30000.0f;
    emit_seen_ = true;
    emit_last_time_ = now;
    emit_last_eye_ = eye;
    const std::uint32_t glow_draws = glow ? static_cast<std::uint32_t>(glow_draws_.size()) : 0u;
    // The glow draws' eye-space triangles: the geometry pass into the glow buffers; their texture coordinates and
    // colours (frame flag 2) where a glow light source emits what it draws.
    const bool glow_uvs = physical_active_ && !glow_tev_records_.empty() && glow_uv_;
    RtUpload table{};
    if (glow_draws != 0u) {
        const std::size_t table_bytes = glow_draws_.size() * sizeof(RtDraw);
        table = allocate(allocate_context, table_bytes, 256u);
        RtUpload frame{};
        if (table.cpu == nullptr || !fill_constants(sources, allocate, allocate_context, glow_draws, 0u,
                                                    glow_triangles_, 0u, 0u, glow_uvs ? 2u : 0u, frame)) {
            return;
        }
        std::memcpy(table.cpu, glow_draws_.data(), table_bytes);
        to_state(list, glow_positions_.Get(), glow_positions_state_, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        const D3D12_GPU_VIRTUAL_ADDRESS resident = sources.resident.base != 0u ? sources.resident.base
                                                                               : sources.vertex_ring.base;
        list->SetComputeRootSignature(root_.Get());
        dred_marker(list, L"geometry");
        list->SetPipelineState(geometry_pso_.Get());
        list->SetComputeRootConstantBufferView(kRootFrame, frame.gpu);
        list->SetComputeRootShaderResourceView(kRootDraws, table.gpu);
        list->SetComputeRootShaderResourceView(kRootVertexRing, sources.vertex_ring.base);
        list->SetComputeRootShaderResourceView(kRootResident, resident);
        list->SetComputeRootShaderResourceView(kRootIndexRing, sources.index_ring.base);
        list->SetComputeRootShaderResourceView(kRootConstantRing, sources.constant_ring.base);
        list->SetComputeRootShaderResourceView(kRootMatrixRing, sources.matrix_ring.base);
        list->SetComputeRootUnorderedAccessView(kRootPositions, glow_positions_->GetGPUVirtualAddress());
        list->SetComputeRootUnorderedAccessView(kRootSurfaces, glow_surfaces_->GetGPUVirtualAddress());
        if (glow_uvs) {
            to_state(list, glow_uv_.Get(), glow_uv_state_, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        }
        list->SetComputeRootUnorderedAccessView(kRootSurfaceUv, glow_uvs ? glow_uv_->GetGPUVirtualAddress()
                                                                         : glow_surfaces_->GetGPUVirtualAddress());
        list->Dispatch((glow_triangles_ + 63u) / 64u, 1u, 1u);
        to_state(list, glow_positions_.Get(), glow_positions_state_, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        if (glow_uvs) {
            to_state(list, glow_uv_.Get(), glow_uv_state_, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        }
        lens_ready_ = lens_draws_ != 0u;
    }
    RtEmitConstants c{};
    std::copy(view_.projection.begin(), view_.projection.end(), c.projection);
    c.depth[0] = view_.z_scale;
    c.depth[1] = view_.z_offset;
    c.depth[2] = view_.min_depth;
    c.depth[3] = view_.max_depth;
    std::copy(view_.viewport.begin(), view_.viewport.end(), c.viewport);
    c.target_size[0] = width;
    c.target_size[1] = height;
    c.sample_size[0] = (width + factor - 1u) / factor;
    c.sample_size[1] = (height + factor - 1u) / factor;
    std::memcpy(c.to_world, to_world.data(), sizeof(c.to_world));
    std::memcpy(c.to_eye, camera_.data(), sizeof(c.to_eye));
    c.gain = emission_;
    c.near_plane = near_plane(view_);
    c.reset = reset ? 1u : 0u;
    c.factor = factor;
    c.authored_power = authored_power_;
    c.time = static_cast<float>(static_cast<double>(++authored_frames_ % 216000u) / 60.0);
    // The found list was cleared before the picture capture (glowing surfaces are in it).
    {
        const D3D12_RESOURCE_BARRIER barrier = uav_barrier(emit_tiles_.Get());
        list->ResourceBarrier(1u, &barrier);
    }
    to_state(list, emit_list_.Get(), emit_list_state_, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    to_state(list, glow_added_.Get(), glow_added_state_, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    list->SetComputeRootSignature(emit_root_.Get());
    list->SetComputeRoot32BitConstants(kEmitConstants, sizeof(c) / 4u, &c, 0u);
    list->SetComputeRootDescriptorTable(kEmitSources, color_depth_table);
    list->SetComputeRootShaderResourceView(kEmitDraws, glow_draws != 0u ? table.gpu
                                                                         : glow_positions_->GetGPUVirtualAddress());
    list->SetComputeRootShaderResourceView(kEmitPositions, glow_positions_->GetGPUVirtualAddress());
    list->SetComputeRootShaderResourceView(kEmitAdded, glow_added_->GetGPUVirtualAddress());
    list->SetComputeRootUnorderedAccessView(kEmitTiles, emit_tiles_->GetGPUVirtualAddress());
    list->SetComputeRootUnorderedAccessView(kEmitLightsSlot, emit_lights_->GetGPUVirtualAddress());
    list->SetComputeRootUnorderedAccessView(kEmitList, emit_list_->GetGPUVirtualAddress());
    list->SetComputeRootShaderResourceView(kEmitGlowUv, glow_uv_->GetGPUVirtualAddress());
    const D3D12_RESOURCE_BARRIER all_uav = uav_barrier(nullptr);
    const int emit_timed = begin_pass(list);
    if (glow_draws != 0u) {
        const RtUpload lights = allocate(allocate_context, glow_lights_.size() * sizeof(AuthoredLight), 256u);
        if (lights.cpu != nullptr && glow_lights_.size() == glow_draws_.size()) {
            std::memcpy(lights.cpu, glow_lights_.data(), glow_lights_.size() * sizeof(AuthoredLight));
            list->SetComputeRootShaderResourceView(kEmitDraws, lights.gpu);
            dred_marker(list, L"glow_emit");
            list->SetPipelineState(glow_emit_pso_.Get());
            list->Dispatch(glow_draws, 1u, 1u);
            list->ResourceBarrier(1u, &all_uav);
        }
    }
    // Physical: the table's glow light sources into the emitters' glow segment (world space) for the next frame.
    glow_emit_count_ = 0u;
    glow_written_.clear();
    if (glow_draws != 0u && physical_active_ && !glow_emitters_.empty() && glow_emitter_pso_) {
        if (!emit_tris_ && !create_buffer(kEmitBufferBytes, D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                                          D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS, emit_tris_, L"rt emitters")) {
            emit_tris_.Reset();
        }
        // The emitters (32 bytes each; word 7 its TEV record's byte offset in the upload), then their TEV records.
        const std::size_t list_bytes = glow_emitters_.size() * sizeof(glow_emitters_[0]);
        const std::size_t tev_bytes = glow_tev_records_.empty() ? 0u : tev_block_bytes(glow_tev_records_, glow_tev_stages_);
        const RtUpload list_upload = allocate(allocate_context, list_bytes + tev_bytes, 256u);
        if (emit_tris_ && list_upload.cpu != nullptr) {
            auto* const emitters = static_cast<std::array<std::uint32_t, 8>*>(list_upload.cpu);
            std::memcpy(emitters, glow_emitters_.data(), list_bytes);
            for (std::size_t g = 0; g < glow_emitters_.size(); ++g) {
                if (emitters[g][7] != 0xFFFFFFFFu) {
                    emitters[g][7] = static_cast<std::uint32_t>(list_bytes + emitters[g][7] * sizeof(RtTevMaterial));
                }
            }
            if (tev_bytes != 0u) {
                write_tev_block(glow_tev_records_, glow_tev_stages_, static_cast<std::byte*>(list_upload.cpu) + list_bytes,
                                list_bytes);
            }
            to_state(list, emit_tris_.Get(), emit_tris_state_, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
            // The glow triangles' surfaces (their vertex normals; the geometry pass keeps them as a UAV).
            D3D12_RESOURCE_STATES surfaces_state = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
            to_state(list, glow_surfaces_.Get(), surfaces_state, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
            list->SetComputeRootShaderResourceView(kEmitDraws, list_upload.gpu);
            list->SetComputeRootShaderResourceView(kEmitAdded, glow_surfaces_->GetGPUVirtualAddress());
            list->SetComputeRootUnorderedAccessView(kEmitGlowOut, emit_tris_->GetGPUVirtualAddress());
            dred_marker(list, L"glow_emitter");
            list->SetPipelineState(glow_emitter_pso_.Get());
            list->Dispatch(static_cast<UINT>(glow_emitters_.size()), 1u, 1u);
            to_state(list, glow_surfaces_.Get(), surfaces_state, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
            list->SetComputeRootShaderResourceView(kEmitAdded, glow_added_->GetGPUVirtualAddress());
            const D3D12_RESOURCE_BARRIER glow_uav = uav_barrier(emit_tris_.Get());
            list->ResourceBarrier(1u, &glow_uav);
            to_state(list, emit_tris_.Get(), emit_tris_state_, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
            glow_emit_count_ = glow_emitter_slots_;
            for (std::size_t g = 0; g < glow_emitters_.size() && g < glow_emitter_keys_.size(); ++g) {
                if (glow_emitters_[g][2] < glow_emit_count_) {
                    glow_written_.push_back({glow_emitter_keys_[g], glow_emitters_[g][2],
                                             std::min(glow_emitters_[g][1], glow_emit_count_ - glow_emitters_[g][2])});
                }
            }
        }
    }
    // The material table's point lights: one group per draw over its triangles in the scene.
    if (!authored_.empty() && authored_emit_pso_ && positions_) {
        const RtUpload lights = allocate(allocate_context, authored_.size() * sizeof(AuthoredLight), 256u);
        if (lights.cpu != nullptr) {
            std::memcpy(lights.cpu, authored_.data(), authored_.size() * sizeof(AuthoredLight));
            to_state(list, positions_.Get(), positions_state_, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
            list->SetComputeRootShaderResourceView(kEmitDraws, lights.gpu);
            list->SetComputeRootShaderResourceView(kEmitPositions, positions_->GetGPUVirtualAddress());
            dred_marker(list, L"authored_emit");
            list->SetPipelineState(authored_emit_pso_.Get());
            list->Dispatch(static_cast<UINT>(authored_.size()), 1u, 1u);
            list->ResourceBarrier(1u, &all_uav);
        }
    }
    end_pass(list, emit_timed, kPassEmit);
    const int merge_timed = begin_pass(list);
    dred_marker(list, L"emit_merge");
    list->SetPipelineState(emit_merge_pso_.Get());
    list->Dispatch(1u, 1u, 1u);
    end_pass(list, merge_timed, kPassMerge);
    // Every 120 frames or more: a fixed frame residue would never meet the traced frames of an A/B run
    // (every other frame) when it falls on the other parity.
    if (emit_readback_ && !emit_log_pending_ && frame_index_ >= emit_log_frame_ + emit_log_interval_) {
        to_state(list, emit_list_.Get(), emit_list_state_, D3D12_RESOURCE_STATE_COPY_SOURCE);
        list->CopyBufferRegion(emit_readback_.Get(), 0u, emit_list_.Get(), 0u, kEmitLogBytes);
        const D3D12_RESOURCE_STATES sums_state = picture_sums_state_;
        to_state(list, picture_sums_.Get(), picture_sums_state_, D3D12_RESOURCE_STATE_COPY_SOURCE);
        list->CopyBufferRegion(emit_readback_.Get(), kEmitLogBytes, picture_sums_.Get(), 0u, kEmitLogSums);
        to_state(list, picture_sums_.Get(), picture_sums_state_, sums_state);
        to_state(list, emit_tiles_.Get(), emit_tiles_state_, D3D12_RESOURCE_STATE_COPY_SOURCE);
        list->CopyBufferRegion(emit_readback_.Get(), kEmitLogBytes + kEmitLogSums, emit_tiles_.Get(), 0u, kEmitFoundBytes);
        to_state(list, emit_tiles_.Get(), emit_tiles_state_, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        if (physical_readback_ && emit_tris_) {
            const D3D12_RESOURCE_STATES emit_state = emit_tris_state_;
            to_state(list, emit_tris_.Get(), emit_tris_state_, D3D12_RESOURCE_STATE_COPY_SOURCE);
            list->CopyBufferRegion(physical_readback_.Get(), 0u, emit_tris_.Get(), 0u, kEmitBufferBytes);
            to_state(list, emit_tris_.Get(), emit_tris_state_, emit_state);
            physical_log_scene_ = emit_triangles_;
            physical_log_glows_ = glow_emitter_slots_;
            physical_log_scene_materials_.assign(emit_triangles_, 0u);
            for (std::size_t r = 0; r < emit_ranges_.size() && r < emit_range_materials_.size(); ++r) {
                const auto& range = emit_ranges_[r];
                for (std::uint32_t t = 0; t < range[1] && range[3] + t < emit_triangles_; ++t) {
                    physical_log_scene_materials_[range[3] + t] = emit_range_materials_[r];
                }
            }
            physical_log_glow_materials_.assign(glow_emitter_slots_, 0u);
            for (std::size_t g = 0; g < glow_emitters_.size() && g < glow_emitter_materials_.size(); ++g) {
                const auto& emitter = glow_emitters_[g];
                for (std::uint32_t t = 0; t < emitter[1] && emitter[2] + t < glow_emitter_slots_; ++t) {
                    physical_log_glow_materials_[emitter[2] + t] = glow_emitter_materials_[g];
                }
            }
            physical_log_pending_ = true;
        }
        emit_log_authored_materials_ = authored_materials_;
        emit_log_glow_materials_ = glow_materials_;
        emit_log_outcomes_ = std::move(emitter_outcomes_);
        emitter_outcomes_.clear();
        emit_log_pending_ = true;
        emit_log_frame_ = frame_index_;
        emit_log_draws_ = {static_cast<std::uint32_t>(authored_.size()), glow_draws,
                           static_cast<std::uint32_t>(std::count_if(
                               glow_lights_.begin(), glow_lights_.end(),
                               [](const AuthoredLight& l) { return (l.flags & kGlowTableIntensity) != 0u; }))};
        // The camera's world position: -R^T t of the world-to-eye rows.
        for (unsigned i = 0; i < 3u; ++i) {
            emit_log_eye_[i] = -(camera_[0 * 4 + i] * camera_[0 * 4 + 3] + camera_[1 * 4 + i] * camera_[1 * 4 + 3] +
                                 camera_[2 * 4 + i] * camera_[2 * 4 + 3]);
        }
    }
    to_state(list, emit_list_.Get(), emit_list_state_, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    window_.glow_draws += glow_draws;
    window_.glow_triangles += glow ? glow_triangles_ : 0u;
}

void SceneRayTracer::write_light_texture(ID3D12GraphicsCommandList* list, int slot) {
    if (list == nullptr || slot < 0 || slot >= static_cast<int>(kLightSlots) || !light_tex_pso_ ||
        !light_final_[static_cast<unsigned>(slot)]) {
        return;
    }
    const LightSlot& info = light_slots_[static_cast<unsigned>(slot)];
    if (info.sample_width == 0u || info.sample_height == 0u) {
        return;
    }
    if (info.sample_width != light_tex_width_ || info.sample_height != light_tex_height_ || !light_tex_) {
        retire(light_tex_);
        D3D12_HEAP_PROPERTIES heap{};
        heap.Type = D3D12_HEAP_TYPE_DEFAULT;
        D3D12_RESOURCE_DESC desc{};
        desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        desc.Width = info.sample_width;
        desc.Height = info.sample_height;
        desc.DepthOrArraySize = 1u;
        desc.MipLevels = 1u;
        desc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
        desc.SampleDesc.Count = 1u;
        desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
        if (FAILED(device_->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc,
                                                    D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, nullptr,
                                                    IID_PPV_ARGS(&light_tex_)))) {
            light_tex_width_ = light_tex_height_ = 0u;
            return;
        }
        light_tex_->SetName(L"rt light texture");
        device_->CreateRenderTargetView(light_tex_.Get(), nullptr,
                                        light_tex_rtv_heap_->GetCPUDescriptorHandleForHeapStart());
        device_->CreateShaderResourceView(light_tex_.Get(), nullptr, light_tex_srv_);
        light_tex_state_ = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
        light_tex_width_ = info.sample_width;
        light_tex_height_ = info.sample_height;
    }
    to_state(list, light_final_[static_cast<unsigned>(slot)].Get(), light_final_state_[static_cast<unsigned>(slot)],
             kLightRead);
    to_state(list, light_tex_.Get(), light_tex_state_, D3D12_RESOURCE_STATE_RENDER_TARGET);
    const D3D12_CPU_DESCRIPTOR_HANDLE rtv = light_tex_rtv_heap_->GetCPUDescriptorHandleForHeapStart();
    list->OMSetRenderTargets(1u, &rtv, FALSE, nullptr);
    const D3D12_VIEWPORT viewport{0.0f, 0.0f, static_cast<float>(light_tex_width_),
                                  static_cast<float>(light_tex_height_), 0.0f, 1.0f};
    const D3D12_RECT scissor{0, 0, static_cast<LONG>(light_tex_width_), static_cast<LONG>(light_tex_height_)};
    list->RSSetViewports(1u, &viewport);
    list->RSSetScissorRects(1u, &scissor);
    list->SetGraphicsRootSignature(light_tex_root_.Get());
    dred_marker(list, L"light_tex");
    list->SetPipelineState(light_tex_pso_.Get());
    // The translucent draws are drawn in the game's own light: the traced light relative to the stage's full light.
    const std::array<float, 3>& full = light_slot_full_[static_cast<unsigned>(slot)];
    const std::uint32_t constants[8] = {light_tex_width_, light_tex_height_, 0u, 0u, std::bit_cast<std::uint32_t>(full[0]),
                                        std::bit_cast<std::uint32_t>(full[1]), std::bit_cast<std::uint32_t>(full[2]), 0u};
    list->SetGraphicsRoot32BitConstants(0u, 8u, constants, 0u);
    list->SetGraphicsRootShaderResourceView(1u, light_final_[static_cast<unsigned>(slot)]->GetGPUVirtualAddress());
    list->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    list->DrawInstanced(3u, 1u, 0u, 0u);
    to_state(list, light_tex_.Get(), light_tex_state_, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    light_tex_scale_ = 1.0f / static_cast<float>(std::max(info.factor, 1u));
    light_tex_ready_ = true;
}

void SceneRayTracer::composite(ID3D12GraphicsCommandList* list, D3D12_CPU_DESCRIPTOR_HANDLE target,
                               D3D12_GPU_DESCRIPTOR_HANDLE depth_srv, D3D12_GPU_DESCRIPTOR_HANDLE color_srv,
                               std::uint32_t width, std::uint32_t height, int slot, bool reproject, unsigned efb_scale) {
    if (list == nullptr || slot < 0 || slot >= static_cast<int>(kLightSlots) || !composite_pso_ ||
        depth_srv.ptr == 0u || color_srv.ptr == 0u) {
        return;
    }
    const auto index = static_cast<unsigned>(slot);
    const LightSlot& info = light_slots_[index];
    if (!light_final_[index] || info.sample_width == 0u || info.width != width || info.height != height) {
        return;
    }
    to_state(list, light_final_[index].Get(), light_final_state_[index], kLightRead);
    // Each sub-frame's evaluation list wrote the reprojection before its replay runs.
    reproject = reproject && reproject_;
    RtCompositeConstants c{};
    std::copy(info.view.projection.begin(), info.view.projection.end(), c.projection);
    c.depth[0] = info.view.z_scale;
    c.depth[1] = info.view.z_offset;
    c.depth[2] = info.view.min_depth;
    c.depth[3] = info.view.max_depth;
    std::copy(info.view.viewport.begin(), info.view.viewport.end(), c.viewport);
    c.sample_size[0] = info.sample_width;
    c.sample_size[1] = info.sample_height;
    c.factor = info.factor;
    // Physical: the sun per pixel from the slot's per-pixel normals (not the reference, whose light is its own).
    const bool per_pixel = info.pixels && light_pixels_[index];
    // The engine's result (full resolution, its own record: rt_engine.hlsl EngineFinalCS).
    const bool engine = per_pixel && engine_slot(index, width, height);
    c.flags = (reproject ? 1u : 0u) | (show_lighting_ ? 2u : 0u) | (physical_active_ ? 4u : 0u) |
              (per_pixel ? 8u : 0u) | (surface_reflections_active() ? 16u : 0u) | (engine ? 32u : 0u);
    if (engine) {
        c.sample_size[0] = width;
        c.sample_size[1] = height;
        c.factor = 1u;
        to_state(list, engine_slots_[index].Get(), engine_slot_state_[index], kLightRead);
    }
    c.target_size[0] = info.width;
    c.target_size[1] = info.height;
    c.slot = index;
    c.near_plane = info.near_plane;
    c.split = split_ > 0.0f ? info.view.viewport[0] + split_ * info.view.viewport[2] : -1.0f;
    for (unsigned i = 0; i < 3u; ++i) {
        c.sun[i] = info.sun[i];
        c.sun_light[i] = info.sun_light[i];
        c.water_sky[i] = info.water_sky[i];
    }
    std::copy(info.water.begin(), info.water.end(), c.water);
    c.sun[3] = relief_;
    c.efb_scale = static_cast<float>(std::max(efb_scale, 1u));
    const int timed = begin_pass(list);
    list->OMSetRenderTargets(1u, &target, FALSE, nullptr);
    const D3D12_VIEWPORT viewport{0.0f, 0.0f, static_cast<float>(width), static_cast<float>(height), 0.0f, 1.0f};
    const D3D12_RECT scissor{0, 0, static_cast<LONG>(width), static_cast<LONG>(height)};
    list->RSSetViewports(1u, &viewport);
    list->RSSetScissorRects(1u, &scissor);
    list->SetGraphicsRootSignature(composite_root_.Get());
    dred_marker(list, L"composite");
    list->SetPipelineState(composite_pso_.Get());
    list->SetGraphicsRoot32BitConstants(0u, sizeof(c) / 4u, &c, 0u);
    list->SetGraphicsRootShaderResourceView(1u, engine ? engine_slots_[index]->GetGPUVirtualAddress()
                                                       : light_final_[index]->GetGPUVirtualAddress());
    list->SetGraphicsRootDescriptorTable(2u, depth_srv);
    list->SetGraphicsRootShaderResourceView(3u, reproject ? reproject_->GetGPUVirtualAddress()
                                                          : light_final_[index]->GetGPUVirtualAddress());
    list->SetGraphicsRootDescriptorTable(4u, color_srv);
    if (per_pixel) {
        to_state(list, light_pixels_[index].Get(), light_pixels_state_[index], kLightRead);
    }
    list->SetGraphicsRootShaderResourceView(5u, per_pixel ? light_pixels_[index]->GetGPUVirtualAddress()
                                                          : light_final_[index]->GetGPUVirtualAddress());
    list->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    list->DrawInstanced(3u, 1u, 0u, 0u);
    end_pass(list, timed, kPassComposite);
    ++window_.composites;
    if (ab_ && !reproject) {
        const float magenta[4] = {1.0f, 0.0f, 1.0f, 1.0f};
        const D3D12_RECT mark{0, 0, 6, 6};
        list->ClearRenderTargetView(target, magenta, 1u, &mark);
        // The cycled view's number: that many white squares right of the mark.
        const float white[4] = {1.0f, 1.0f, 1.0f, 1.0f};
        for (std::uint32_t i = 0; view_cycle_ != 0u && i < show_term_; ++i) {
            const D3D12_RECT square{static_cast<LONG>(10u + 10u * i), 0, static_cast<LONG>(16u + 10u * i), 6};
            list->ClearRenderTargetView(target, white, 1u, &square);
        }
    }
    if (!show_lighting_ && !engine) {
        reflections_.composite(list, target, depth_srv, width, height, index,
                               reproject ? reproject_->GetGPUVirtualAddress() : 0u, c.split);
    }
}

std::uint32_t SceneRayTracer::tlas_view(D3D12_GPU_VIRTUAL_ADDRESS tlas) {
    if (!texture_views_usable_ || tlas == 0u || !device_ || texture_view_count_ >= kTextureViewsPerScene) {
        return 0xFFFFFFFFu;
    }
    const std::uint32_t slot = texture_view_scene_ * kTextureViewsPerScene + texture_view_count_++;
    D3D12_CPU_DESCRIPTOR_HANDLE destination = texture_view_cpu_;
    destination.ptr += static_cast<SIZE_T>(slot) * texture_view_increment_;
    D3D12_SHADER_RESOURCE_VIEW_DESC view{};
    view.ViewDimension = D3D12_SRV_DIMENSION_RAYTRACING_ACCELERATION_STRUCTURE;
    view.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    view.RaytracingAccelerationStructure.Location = tlas;
    device_->CreateShaderResourceView(nullptr, &view, destination);
    return texture_view_first_ + slot;
}

bool SceneRayTracer::trace_engine_clear(ID3D12GraphicsCommandList* list, Allocate allocate, void* allocate_context,
                                        D3D12_GPU_DESCRIPTOR_HANDLE depth_table, unsigned slot) {
    // GALAXY_RT_LOG_CLEAR=1: why a scene's clear pass did not run (its first refusals, then every 300th).
    static const bool log_clear = env_float("GALAXY_RT_LOG_CLEAR", 0.0f) != 0.0f;
    static std::uint64_t calls = 0u;
    const auto refuse = [&](const char* why) {
        if (log_clear && (calls < 12u || calls % 300u == 0u)) {
            std::fprintf(stderr, "[rt-clear-pass] slot=%u bind=%d/%u draws=%zu glass=%zd triangles=%u: %s\n", slot,
                         engine_bind_.valid ? 1 : 0, engine_bind_.slot, water_clear_.size(),
                         std::count_if(water_clear_.begin(), water_clear_.end(),
                                       [](std::uint32_t w) { return (w & 0x10000u) != 0u; }),
                         water_triangles_, why);
        }
        ++calls;
        return false;
    };
    if (list == nullptr || allocate == nullptr || !engine_clear_pso_) {
        return refuse("no pipeline");
    }
    if (!engine_bind_.valid || engine_bind_.slot != slot || slot >= kLightSlots ||
        !engine_slot(slot, engine_width_, engine_height_)) {
        return refuse("no engine trace for the slot");
    }
    const bool glass = water_tlas_ && water_triangles_ != 0u &&
                       std::any_of(water_clear_.begin(), water_clear_.end(),
                                   [](std::uint32_t w) { return (w & 0x10000u) != 0u; });
    if (!water_tlas_ || depth_table.ptr == 0u) {
        return refuse("no water geometry");
    }
    if (!glass && solid_draws_ == 0u) {
        return refuse("no glass");
    }
    // Two records a pixel (rt_engine.hlsl kEvClear).
    const std::uint64_t full = static_cast<std::uint64_t>(engine_width_) * engine_height_;
    if (!engine_clear_[slot]) {
        if (!create_buffer(full * 32u, D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                           D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS, engine_clear_[slot], L"rt engine clear")) {
            return false;
        }
        engine_clear_state_[slot] = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    }
    const std::size_t record_bytes = std::max<std::size_t>(water_clear_.size() * 4u, 16u);
    const RtUpload records = allocate(allocate_context, record_bytes, 256u);
    const RtUpload upload = allocate(allocate_context, engine_bind_.constants.size(), 256u);
    if (records.cpu == nullptr || upload.cpu == nullptr) {
        return false;
    }
    std::memcpy(records.cpu, water_clear_.data(), water_clear_.size() * 4u);
    // Without glass the water window's views are read nowhere (kClearHasGlass); they stay valid views.
    const std::uint64_t water = std::max(water_triangles_, 1u);
    std::uint32_t views[5] = {
        tlas_view(water_tlas_->GetGPUVirtualAddress()),
        buffer_view(water_positions_.Get(), 0u, water * 36u),
        buffer_view(water_surfaces_.Get(), 0u, water * 16u),
        buffer_view(records.resource, records.offset, record_bytes),
        buffer_uav(engine_clear_[slot].Get(), full * 32u),
    };
    for (const std::uint32_t view : views) {
        if (view == 0xFFFFFFFFu) {
            return refuse("no view");
        }
    }
    refuse("runs");
    std::memcpy(upload.cpu, engine_bind_.constants.data(), engine_bind_.constants.size());
    auto* const engine = reinterpret_cast<RtEngineConstants*>(static_cast<std::byte*>(upload.cpu) + engine_bind_.engine_at);
    std::copy(std::begin(views), std::end(views), engine->views + kEngineClearViews);
    // rt_engine.hlsl kClearHasGlass, kClearHasSolids.
    engine->info[1] = (glass ? 1u : 0u) | (solid_draws_ != 0u ? 2u : 0u);
    engine->info[2] = slot;
    constexpr D3D12_RESOURCE_STATES kRead = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
    to_state(list, engine_clear_[slot].Get(), engine_clear_state_[slot], D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    to_state(list, picture_.Get(), picture_state_, kRead);
    to_state(list, picture_sums_.Get(), picture_sums_state_, kRead);
    to_state(list, emit_list_.Get(), emit_list_state_, kRead);
    to_state(list, glow_base_.Get(), glow_base_state_, kRead);
    to_state(list, glow_added_.Get(), glow_added_state_, kRead);
    to_state(list, sky_sh_.Get(), sky_sh_state_, kRead);
    if (sky_probe_map_) {
        to_state(list, sky_probe_map_.Get(), sky_probe_map_state_, kRead);
    }
    if (emit_tris_) {
        to_state(list, emit_tris_.Get(), emit_tris_state_, kRead);
    }
    // The lighting's root as the trace bound it (the clear pass reads the scene, its draws, TEV records, textures,
    // emitters and sky; nothing writes the lighting's own buffers).
    const D3D12_GPU_VIRTUAL_ADDRESS spare = positions_->GetGPUVirtualAddress();
    const D3D12_GPU_VIRTUAL_ADDRESS scratch = light_raw_->GetGPUVirtualAddress();
    list->SetComputeRootSignature(light_root_.Get());
    list->SetComputeRootConstantBufferView(kLightFrame, upload.gpu);
    list->SetComputeRootShaderResourceView(kLightScene, tlas_->GetGPUVirtualAddress());
    list->SetComputeRootDescriptorTable(kLightDepth, depth_table);
    list->SetComputeRootShaderResourceView(kLightPrevZ, spare);
    list->SetComputeRootShaderResourceView(kLightPrevHistory, spare);
    list->SetComputeRootShaderResourceView(kLightHistoryIn, spare);
    list->SetComputeRootShaderResourceView(kLightPicture, picture_->GetGPUVirtualAddress());
    list->SetComputeRootShaderResourceView(kLightPictureSums, picture_sums_->GetGPUVirtualAddress());
    list->SetComputeRootShaderResourceView(kLightPositions, positions_->GetGPUVirtualAddress());
    list->SetComputeRootShaderResourceView(kLightSurfaces, surfaces_->GetGPUVirtualAddress());
    list->SetComputeRootShaderResourceView(kLightEmitters, emit_list_->GetGPUVirtualAddress());
    list->SetComputeRootShaderResourceView(kLightGlowBase, glow_base_->GetGPUVirtualAddress());
    list->SetComputeRootShaderResourceView(kLightGlowAdded, glow_added_->GetGPUVirtualAddress());
    list->SetComputeRootShaderResourceView(kLightDraws, engine_bind_.draws);
    list->SetComputeRootShaderResourceView(kLightSky, sky_sh_->GetGPUVirtualAddress());
    list->SetComputeRootUnorderedAccessView(kLightZ, scratch);
    list->SetComputeRootUnorderedAccessView(kLightNormal, scratch);
    list->SetComputeRootUnorderedAccessView(kLightRaw, scratch);
    list->SetComputeRootUnorderedAccessView(kLightHistory, scratch);
    list->SetComputeRootUnorderedAccessView(kLightFinal, scratch);
    list->SetComputeRootUnorderedAccessView(kLightPrimary, scratch);
    list->SetComputeRootShaderResourceView(kLightSurfaceUv, engine_bind_.surface_uv);
    list->SetComputeRootShaderResourceView(kLightDrawTextures, engine_bind_.draw_textures);
    list->SetComputeRootUnorderedAccessView(kLightReference, scratch);
    list->SetComputeRootShaderResourceView(kLightSkyMap, engine_bind_.sky_map);
    list->SetComputeRootShaderResourceView(kLightTev, engine_bind_.tev);
    list->SetComputeRootUnorderedAccessView(kLightPixels, scratch);
    list->SetComputeRootShaderResourceView(kLightEmitTris, engine_bind_.emit_tris);
    list->SetComputeRootShaderResourceView(kLightReservoirIn, spare);
    list->SetComputeRootUnorderedAccessView(kLightReservoirOut, scratch);
    dred_marker(list, L"engine_clear");
    list->SetPipelineState(engine_clear_pso_.Get());
    const int timed = begin_pass(list);
    list->Dispatch((engine_width_ + 7u) / 8u, (engine_height_ + 7u) / 8u, 1u);
    end_pass(list, timed, kPassEngineClear);
    const D3D12_RESOURCE_BARRIER barrier = uav_barrier(engine_clear_[slot].Get());
    list->ResourceBarrier(1u, &barrier);
    to_state(list, engine_clear_[slot].Get(), engine_clear_state_[slot],
             D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    engine_clear_valid_[slot] = true;
    return true;
}

void SceneRayTracer::composite_clear(ID3D12GraphicsCommandList* list, D3D12_CPU_DESCRIPTOR_HANDLE target,
                                     D3D12_GPU_DESCRIPTOR_HANDLE depth_srv, std::uint32_t width, std::uint32_t height,
                                     unsigned slot, bool reproject, D3D12_GPU_DESCRIPTOR_HANDLE scene_srv) {
    if (slot >= kLightSlots || !engine_clear_valid_[slot] || !engine_clear_[slot] || !clear_composite_pso_ ||
        scene_srv.ptr == 0u || width != engine_width_ || height != engine_height_) {
        static const bool log_clear = env_float("GALAXY_RT_LOG_CLEAR", 0.0f) != 0.0f;
        static std::uint64_t refused = 0u;
        if (log_clear && (refused++ < 12u || refused % 300u == 0u)) {
            std::fprintf(stderr, "[rt-clear-composite] slot=%u valid=%d pso=%d scene=%d size=%ux%u engine=%ux%u\n", slot,
                         slot < kLightSlots && engine_clear_valid_[slot] ? 1 : 0, clear_composite_pso_ ? 1 : 0,
                         scene_srv.ptr != 0u ? 1 : 0, width, height, engine_width_, engine_height_);
        }
        return;
    }
    {
        static const bool log_clear = env_float("GALAXY_RT_LOG_CLEAR", 0.0f) != 0.0f;
        static std::uint64_t drawn = 0u;
        if (log_clear && (drawn++ < 12u || drawn % 300u == 0u)) {
            std::fprintf(stderr, "[rt-clear-composite] slot=%u drawn reproject=%d\n", slot, reproject ? 1 : 0);
        }
    }
    const LightSlot& info = light_slots_[slot];
    reproject = reproject && reproject_;
    RtCompositeConstants c{};
    std::copy(info.view.projection.begin(), info.view.projection.end(), c.projection);
    c.depth[0] = info.view.z_scale;
    c.depth[1] = info.view.z_offset;
    c.depth[2] = info.view.min_depth;
    c.depth[3] = info.view.max_depth;
    std::copy(info.view.viewport.begin(), info.view.viewport.end(), c.viewport);
    c.sample_size[0] = width;
    c.sample_size[1] = height;
    c.factor = 1u;
    c.flags = (reproject ? 1u : 0u) | (engine_view_ == 5u ? 4u : 0u) | (engine_view_ == 6u ? 8u : 0u);
    c.slot = slot;
    c.near_plane = info.near_plane;
    c.target_size[0] = width;
    c.target_size[1] = height;
    list->OMSetRenderTargets(1u, &target, FALSE, nullptr);
    const D3D12_VIEWPORT viewport{0.0f, 0.0f, static_cast<float>(width), static_cast<float>(height), 0.0f, 1.0f};
    const D3D12_RECT scissor{0, 0, static_cast<LONG>(width), static_cast<LONG>(height)};
    list->RSSetViewports(1u, &viewport);
    list->RSSetScissorRects(1u, &scissor);
    list->SetGraphicsRootSignature(composite_root_.Get());
    dred_marker(list, L"clear_composite");
    list->SetPipelineState(clear_composite_pso_.Get());
    list->SetGraphicsRoot32BitConstants(0u, sizeof(c) / 4u, &c, 0u);
    list->SetGraphicsRootShaderResourceView(1u, engine_clear_[slot]->GetGPUVirtualAddress());
    list->SetGraphicsRootDescriptorTable(2u, depth_srv);
    list->SetGraphicsRootShaderResourceView(3u, reproject ? reproject_->GetGPUVirtualAddress()
                                                          : engine_clear_[slot]->GetGPUVirtualAddress());
    list->SetGraphicsRootDescriptorTable(4u, scene_srv);
    list->SetGraphicsRootShaderResourceView(5u, engine_clear_[slot]->GetGPUVirtualAddress());
    list->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    list->DrawInstanced(3u, 1u, 0u, 0u);
}

RtReflectScene SceneRayTracer::reflect_scene(unsigned slot) const {
    const LightSlot& light = light_slots_[slot];
    RtReflectScene scene{};
    scene.projection = light.view.projection;
    scene.depth = {light.view.z_scale, light.view.z_offset, light.view.min_depth, light.view.max_depth};
    scene.viewport = light.view.viewport;
    scene.sky_fill = physical_active_ ? sky_fill_ : std::array<float, 3>{};
    scene.near_plane = light.near_plane;
    scene.camera = light.camera;
    scene.camera_known = light.camera_known;
    scene.width = light.width;
    scene.height = light.height;
    scene.sample_width = light.sample_width;
    scene.sample_height = light.sample_height;
    scene.factor = light.factor;
    scene.tlas = tlas_->GetGPUVirtualAddress();
    scene.positions = positions_->GetGPUVirtualAddress();
    scene.surfaces = surfaces_->GetGPUVirtualAddress();
    scene.primary = light_primary_->GetGPUVirtualAddress();
    if (surface_uv_ && draw_texture_table_ != 0u) {
        scene.surface_uv = surface_uv_->GetGPUVirtualAddress();
        scene.draw_textures = draw_texture_table_;
        scene.texture_count = static_cast<std::uint32_t>(draw_textures_.size());
    }
    scene.physical = reflect_physical_;
    // The art's level at a hit: by day (the stage's table, else a sun) the day model's open ground, by night
    // its open art level.
    {
        const float day = stage_sun_ != nullptr && stage_sun_->day >= 0.0f ? stage_sun_->day : 1.0f;
        const float night = night_level_ * (stage_sun_ != nullptr ? stage_sun_->ambient : 1.0f);
        scene.hit = {std::clamp(day, 0.0f, 1.0f), night, day_environment_, 1.0f / 400.0f};
    }
    scene.light = light_final_[slot]->GetGPUVirtualAddress();
    scene.sums = picture_valid_ ? picture_sums_->GetGPUVirtualAddress() : 0u;
    scene.sky_distance = sky_distance_;
    // trace_reflections and trace_water left the sky map readable.
    scene.sky_map = sky_radiance_ ? sky_radiance_->GetGPUVirtualAddress() : 0u;
    scene.lights = reflect_lights_;
    // trace_lighting left the glow lights' list readable.
    scene.lights.emitters = emit_list_ ? emit_list_->GetGPUVirtualAddress() : 0u;
    return scene;
}

void SceneRayTracer::trace_reflections(ID3D12GraphicsCommandList* list, Allocate allocate, void* allocate_context,
                                       D3D12_GPU_DESCRIPTOR_HANDLE color_depth_table, std::uint32_t width,
                                       std::uint32_t height) {
    // The engine's specular takes the opaque reflections' place (the water's stay: trace_water).
    if (!surface_reflections_active() || light_slot_ < 0 || list == nullptr || allocate == nullptr || !tlas_ ||
        !positions_ || !surfaces_ || !light_primary_ ||
        (light_slot_ >= 0 && engine_slot(static_cast<unsigned>(light_slot_), width, height))) {
        return;
    }
    const auto slot = static_cast<unsigned>(light_slot_);
    const LightSlot& light = light_slots_[slot];
    if (light.width != width || light.height != height || !light_final_[slot]) {
        return;
    }
    const RtUpload upload = allocate(allocate_context, RtReflections::kConstantsBytes, 256u);
    if (upload.cpu == nullptr) {
        return;
    }
    // trace_lighting left the geometry readable and its per-sample primary hits (always UAV) written.
    to_state(list, positions_.Get(), positions_state_, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    to_state(list, surfaces_.Get(), surfaces_state_, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    if (surface_uv_) {
        to_state(list, surface_uv_.Get(), surface_uv_state_, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    }
    if (sky_radiance_) {
        to_state(list, sky_radiance_.Get(), sky_radiance_state_, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    }
    RtReflectScene scene = reflect_scene(slot);
    // The draw table of the scene just traced (the water pass, later in the frame, goes without it).
    scene.draws = draw_table_;
    scene.draw_count = static_cast<std::uint32_t>(draw_colors_.size());
    reflections_.trace(list, scene, slot, upload.cpu, upload.gpu, color_depth_table);
}

void SceneRayTracer::write_reprojection(ID3D12GraphicsCommandList* list, const std::array<float, 12>& camera_t) {
    if (list == nullptr || !lighting_active() || !reproject_ || reproject_mapped_ == nullptr) {
        return;
    }
    std::array<float, 12> inverse{};
    const bool invertible = affine_inverse(camera_t, inverse);
    float rows[kLightSlots][12]{};
    for (unsigned slot = 0; slot < kLightSlots; ++slot) {
        const LightSlot& info = light_slots_[slot];
        const std::array<float, 12> m =
            invertible && info.camera_known ? affine_multiply(info.camera, inverse) : kIdentity34;
        std::memcpy(rows[slot], m.data(), sizeof(rows[slot]));
    }
    static_assert(sizeof(rows) <= kReprojectEntryBytes);
    const UINT64 offset = static_cast<UINT64>(reproject_entry_) * kReprojectEntryBytes;
    reproject_entry_ = (reproject_entry_ + 1u) % kReprojectEntries;
    std::memcpy(reproject_mapped_ + offset, rows, sizeof(rows));
    to_state(list, reproject_.Get(), reproject_state_, D3D12_RESOURCE_STATE_COPY_DEST);
    list->CopyBufferRegion(reproject_.Get(), 0u, reproject_upload_.Get(), offset, sizeof(rows));
    to_state(list, reproject_.Get(), reproject_state_, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
}

bool SceneRayTracer::fill_constants(const RtSources& sources, Allocate allocate, void* allocate_context,
                                    std::uint32_t draws, std::uint32_t triangle_base, std::uint32_t triangles,
                                    std::uint32_t width, std::uint32_t height, std::uint32_t flags, RtUpload& frame) {
    frame = allocate(allocate_context, sizeof(RtFrameConstants), 256u);
    if (frame.cpu == nullptr) {
        return false;
    }
    RtFrameConstants constants{};
    constants.draw_count = draws;
    constants.triangle_count = triangles;
    constants.vertex_ring_limit = read_limit(sources.vertex_ring.bytes, kRtVertexStride);
    constants.resident_limit = read_limit(sources.resident.bytes, kRtVertexStride);
    constants.index_ring_limit = read_limit(sources.index_ring.bytes, 4u);
    constants.constant_ring_limit = read_limit(sources.constant_ring.bytes, kVsConstantsBytes);
    constants.matrix_ring_limit = read_limit(sources.matrix_ring.bytes, kPaletteMatrixBytes);
    constants.frame_flags = flags;
    std::copy(view_.projection.begin(), view_.projection.end(), constants.projection);
    constants.depth[0] = view_.z_scale;
    constants.depth[1] = view_.z_offset;
    constants.depth[2] = view_.min_depth;
    constants.depth[3] = view_.max_depth;
    std::copy(view_.viewport.begin(), view_.viewport.end(), constants.viewport);
    constants.target_size[0] = width;
    constants.target_size[1] = height;
    constants.near_plane = near_plane(view_);
    constants.triangle_base = triangle_base;
    std::memcpy(frame.cpu, &constants, sizeof(constants));
    return true;
}

bool SceneRayTracer::transform_draws(ID3D12GraphicsCommandList* list, const RtSources& sources, Allocate allocate,
                                     void* allocate_context, const RtDraw* draws, std::size_t count,
                                     std::uint32_t triangle_base, std::uint32_t triangles, ID3D12Resource* positions,
                                     D3D12_RESOURCE_STATES& positions_state, ID3D12Resource* surfaces,
                                     D3D12_RESOURCE_STATES& surfaces_state, ID3D12Resource* uv,
                                     D3D12_RESOURCE_STATES* uv_state) {
    const std::size_t table_bytes = count * sizeof(RtDraw);
    const RtUpload table = allocate(allocate_context, table_bytes, 256u);
    RtUpload frame{};
    if (table.cpu == nullptr || !fill_constants(sources, allocate, allocate_context, static_cast<std::uint32_t>(count),
                                                triangle_base, triangles, 0u, 0u, uv != nullptr ? 2u : 0u, frame)) {
        return false;
    }
    std::memcpy(table.cpu, draws, table_bytes);
    to_state(list, positions, positions_state, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    to_state(list, surfaces, surfaces_state, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    if (uv != nullptr && uv_state != nullptr) {
        to_state(list, uv, *uv_state, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    }
    const D3D12_GPU_VIRTUAL_ADDRESS resident = sources.resident.base != 0u ? sources.resident.base
                                                                           : sources.vertex_ring.base;
    list->SetComputeRootSignature(root_.Get());
    dred_marker(list, L"geometry");
    list->SetPipelineState(geometry_pso_.Get());
    list->SetComputeRootConstantBufferView(kRootFrame, frame.gpu);
    list->SetComputeRootShaderResourceView(kRootDraws, table.gpu);
    list->SetComputeRootShaderResourceView(kRootVertexRing, sources.vertex_ring.base);
    list->SetComputeRootShaderResourceView(kRootResident, resident);
    list->SetComputeRootShaderResourceView(kRootIndexRing, sources.index_ring.base);
    list->SetComputeRootShaderResourceView(kRootConstantRing, sources.constant_ring.base);
    list->SetComputeRootShaderResourceView(kRootMatrixRing, sources.matrix_ring.base);
    list->SetComputeRootUnorderedAccessView(kRootPositions, positions->GetGPUVirtualAddress());
    list->SetComputeRootUnorderedAccessView(kRootSurfaces, surfaces->GetGPUVirtualAddress());
    list->SetComputeRootUnorderedAccessView(kRootSurfaceUv, (uv != nullptr ? uv : surfaces)->GetGPUVirtualAddress());
    list->Dispatch((triangles + 63u) / 64u, 1u, 1u);
    const D3D12_RESOURCE_BARRIER barrier = uav_barrier(nullptr);
    list->ResourceBarrier(1u, &barrier);
    return true;
}

void SceneRayTracer::flush_geometry(ID3D12GraphicsCommandList* list, const RtSources& sources, Allocate allocate,
                                    void* allocate_context) {
    if (!ready_ || !scene_open_ || list == nullptr || allocate == nullptr || flushed_draws_ >= draws_.size()) {
        return;
    }
    const std::size_t first = flushed_draws_;
    const std::uint32_t base = flushed_triangles_;
    const std::uint32_t count = triangles_ - base;
    flushed_draws_ = draws_.size();
    flushed_triangles_ = triangles_;
    if (count == 0u || !ensure_capacity(list, triangles_, base)) {
        if (count != 0u) {
            ++window_.skipped;
            scene_incomplete_ = true;
        }
        return;
    }
    const std::size_t table_bytes = (draws_.size() - first) * sizeof(RtDraw);
    const RtUpload table = allocate(allocate_context, table_bytes, 256u);
    RtUpload frame{};
    if (table.cpu == nullptr ||
        !fill_constants(sources, allocate, allocate_context, static_cast<std::uint32_t>(draws_.size() - first), base,
                        count, 0u, 0u, surface_uv_ ? 2u : 0u, frame)) {
        ++window_.skipped;
        scene_incomplete_ = true;
        return;
    }
    std::memcpy(table.cpu, draws_.data() + first, table_bytes);
    if (positions_state_ != D3D12_RESOURCE_STATE_UNORDERED_ACCESS) {
        const D3D12_RESOURCE_BARRIER barrier =
            transition(positions_.Get(), positions_state_, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        list->ResourceBarrier(1u, &barrier);
        positions_state_ = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    }
    to_state(list, surfaces_.Get(), surfaces_state_, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    if (surface_uv_) {
        to_state(list, surface_uv_.Get(), surface_uv_state_, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    }
    const D3D12_GPU_VIRTUAL_ADDRESS resident = sources.resident.base != 0u ? sources.resident.base
                                                                           : sources.vertex_ring.base;
    const int timed = begin_pass(list);
    list->SetComputeRootSignature(root_.Get());
    dred_marker(list, L"geometry");
    list->SetPipelineState(geometry_pso_.Get());
    list->SetComputeRootConstantBufferView(kRootFrame, frame.gpu);
    list->SetComputeRootShaderResourceView(kRootDraws, table.gpu);
    list->SetComputeRootShaderResourceView(kRootVertexRing, sources.vertex_ring.base);
    list->SetComputeRootShaderResourceView(kRootResident, resident);
    list->SetComputeRootShaderResourceView(kRootIndexRing, sources.index_ring.base);
    list->SetComputeRootShaderResourceView(kRootConstantRing, sources.constant_ring.base);
    list->SetComputeRootShaderResourceView(kRootMatrixRing, sources.matrix_ring.base);
    list->SetComputeRootUnorderedAccessView(kRootPositions, positions_->GetGPUVirtualAddress());
    list->SetComputeRootUnorderedAccessView(kRootSurfaces, surfaces_->GetGPUVirtualAddress());
    list->SetComputeRootUnorderedAccessView(kRootSurfaceUv, (surface_uv_ ? surface_uv_ : surfaces_)->GetGPUVirtualAddress());
    list->Dispatch((count + 63u) / 64u, 1u, 1u);
    const D3D12_RESOURCE_BARRIER barrier = uav_barrier(nullptr);
    list->ResourceBarrier(1u, &barrier);
    end_pass(list, timed, kPassGeometry);
}

bool SceneRayTracer::ensure_late_buffers() {
    if (late_blas_) {
        return true;
    }
    D3D12_RAYTRACING_GEOMETRY_DESC geometry{};
    geometry.Type = D3D12_RAYTRACING_GEOMETRY_TYPE_TRIANGLES;
    geometry.Flags = D3D12_RAYTRACING_GEOMETRY_FLAG_OPAQUE;
    geometry.Triangles.VertexFormat = DXGI_FORMAT_R32G32B32_FLOAT;
    geometry.Triangles.VertexCount = kLateMaxTriangles * 3u;
    geometry.Triangles.VertexBuffer.StrideInBytes = 12u;
    D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS bottom{};
    bottom.Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL;
    bottom.Flags = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_BUILD;
    bottom.NumDescs = 1u;
    bottom.DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY;
    bottom.pGeometryDescs = &geometry;
    D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO info{};
    device_->GetRaytracingAccelerationStructurePrebuildInfo(&bottom, &info);
    if (info.ResultDataMaxSizeInBytes == 0u) {
        return false;
    }
    constexpr D3D12_RESOURCE_STATES kUav = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    constexpr D3D12_RESOURCE_FLAGS kUavFlag = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
    if (!create_buffer(static_cast<std::uint64_t>(kLateMaxTriangles) * 36u, D3D12_HEAP_TYPE_DEFAULT, kUav, kUavFlag,
                       late_positions_, L"rt late triangles") ||
        !create_buffer(static_cast<std::uint64_t>(kLateMaxTriangles) * 16u, D3D12_HEAP_TYPE_DEFAULT, kUav, kUavFlag,
                       late_surfaces_, L"rt late surfaces") ||
        !create_buffer(info.ResultDataMaxSizeInBytes, D3D12_HEAP_TYPE_DEFAULT,
                       D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE, kUavFlag, late_blas_, L"rt late blas") ||
        !create_buffer(std::max<std::uint64_t>(info.ScratchDataSizeInBytes, 256u), D3D12_HEAP_TYPE_DEFAULT, kUav,
                       kUavFlag, late_scratch_, L"rt late scratch")) {
        late_positions_.Reset();
        late_surfaces_.Reset();
        late_blas_.Reset();
        late_scratch_.Reset();
        return false;
    }
    late_positions_state_ = kUav;
    late_surfaces_state_ = kUav;
    std::cerr << "[rt] late triangles=" << kLateMaxTriangles << " blas-kb=" << (info.ResultDataMaxSizeInBytes >> 10u)
              << '\n';
    return true;
}

void SceneRayTracer::flush_late(ID3D12GraphicsCommandList* list, const RtSources& sources, Allocate allocate,
                                void* allocate_context) {
    if (!late_open_ || list == nullptr || allocate == nullptr || late_flushed_draws_ >= late_draws_.size()) {
        return;
    }
    const std::size_t first = late_flushed_draws_;
    const std::uint32_t base = late_flushed_triangles_;
    late_flushed_draws_ = late_draws_.size();
    late_flushed_triangles_ = late_triangles_;
    if (!ensure_late_buffers() ||
        !transform_draws(list, sources, allocate, allocate_context, late_draws_.data() + first,
                         late_draws_.size() - first, base, late_triangles_ - base, late_positions_.Get(),
                         late_positions_state_, late_surfaces_.Get(), late_surfaces_state_)) {
        late_incomplete_ = true;
    }
}

void SceneRayTracer::finish_late(ID3D12GraphicsCommandList* list, const RtSources& sources, Allocate allocate,
                                 void* allocate_context) {
    if (!late_open_) {
        return;
    }
    flush_late(list, sources, allocate, allocate_context);
    late_open_ = false;
    late_built_ = 0u;
    ComPtr<ID3D12GraphicsCommandList4> list4;
    if (list == nullptr || late_incomplete_ || late_triangles_ == 0u || !late_blas_ || !late_next_camera_known_ ||
        FAILED(list->QueryInterface(IID_PPV_ARGS(&list4)))) {
        return;
    }
    to_state(list, late_positions_.Get(), late_positions_state_, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    // This frame's traces may still read the previous late structure.
    D3D12_RESOURCE_BARRIER barrier = uav_barrier(nullptr);
    list->ResourceBarrier(1u, &barrier);
    D3D12_RAYTRACING_GEOMETRY_DESC geometry{};
    geometry.Type = D3D12_RAYTRACING_GEOMETRY_TYPE_TRIANGLES;
    geometry.Flags = D3D12_RAYTRACING_GEOMETRY_FLAG_OPAQUE;
    geometry.Triangles.VertexFormat = DXGI_FORMAT_R32G32B32_FLOAT;
    geometry.Triangles.VertexCount = late_triangles_ * 3u;
    geometry.Triangles.VertexBuffer.StartAddress = late_positions_->GetGPUVirtualAddress();
    geometry.Triangles.VertexBuffer.StrideInBytes = 12u;
    D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC build{};
    build.Inputs.Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL;
    build.Inputs.Flags = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_BUILD;
    build.Inputs.NumDescs = 1u;
    build.Inputs.DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY;
    build.Inputs.pGeometryDescs = &geometry;
    build.DestAccelerationStructureData = late_blas_->GetGPUVirtualAddress();
    build.ScratchAccelerationStructureData = late_scratch_->GetGPUVirtualAddress();
    list4->BuildRaytracingAccelerationStructure(&build, 0u, nullptr);
    barrier = uav_barrier(late_blas_.Get());
    list->ResourceBarrier(1u, &barrier);
    late_built_ = late_triangles_;
    late_scene_ = scene_serial_ - 1u;
    late_camera_ = late_next_camera_;
    late_camera_known_ = true;
    window_.late_triangles += late_triangles_;
}

void SceneRayTracer::flush_sky(ID3D12GraphicsCommandList* list, const RtSources& sources, Allocate allocate,
                               void* allocate_context) {
    if (!sky_open_ || list == nullptr || allocate == nullptr || sky_flushed_draws_ >= sky_draws_.size()) {
        return;
    }
    const std::size_t first = sky_flushed_draws_;
    const std::uint32_t base = sky_flushed_triangles_;
    sky_flushed_draws_ = sky_draws_.size();
    sky_flushed_triangles_ = sky_triangles_;
    if (!ensure_sky_buffers() ||
        !transform_draws(list, sources, allocate, allocate_context, sky_draws_.data() + first, sky_draws_.size() - first,
                         base, sky_triangles_ - base, sky_positions_.Get(), sky_positions_state_, sky_surfaces_.Get(),
                         sky_surfaces_state_, sky_uv_.Get(), &sky_uv_state_)) {
        sky_incomplete_ = true;
    }
}

bool SceneRayTracer::ensure_sky_buffers() {
    if (sky_blas_) {
        return true;
    }
    D3D12_RAYTRACING_GEOMETRY_DESC geometry{};
    geometry.Type = D3D12_RAYTRACING_GEOMETRY_TYPE_TRIANGLES;
    geometry.Triangles.VertexFormat = DXGI_FORMAT_R32G32B32_FLOAT;
    geometry.Triangles.VertexCount = kSkyMaxTriangles * 3u;
    geometry.Triangles.VertexBuffer.StrideInBytes = 12u;
    D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS bottom{};
    bottom.Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL;
    bottom.Flags = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_BUILD;
    bottom.NumDescs = 1u;
    bottom.DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY;
    bottom.pGeometryDescs = &geometry;
    D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO bottom_info{};
    device_->GetRaytracingAccelerationStructurePrebuildInfo(&bottom, &bottom_info);
    D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS top{};
    top.Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL;
    top.Flags = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_TRACE;
    top.NumDescs = 1u;
    top.DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY;
    D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO top_info{};
    device_->GetRaytracingAccelerationStructurePrebuildInfo(&top, &top_info);
    if (bottom_info.ResultDataMaxSizeInBytes == 0u || top_info.ResultDataMaxSizeInBytes == 0u) {
        return false;
    }
    constexpr D3D12_RESOURCE_STATES kUav = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    constexpr D3D12_RESOURCE_STATES kStructure = D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE;
    constexpr D3D12_RESOURCE_FLAGS kUavFlag = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
    sky_scratch_tlas_ = (std::max<std::uint64_t>(bottom_info.ScratchDataSizeInBytes, 256u) + 255u) & ~255ull;
    if (!create_buffer(static_cast<std::uint64_t>(kSkyMaxTriangles) * 36u, D3D12_HEAP_TYPE_DEFAULT, kUav, kUavFlag,
                       sky_positions_, L"rt sky triangles") ||
        !create_buffer(static_cast<std::uint64_t>(kSkyMaxTriangles) * 16u, D3D12_HEAP_TYPE_DEFAULT, kUav, kUavFlag,
                       sky_surfaces_, L"rt sky surfaces") ||
        !create_buffer(static_cast<std::uint64_t>(kSkyMaxTriangles) * kRtTriUvStride, D3D12_HEAP_TYPE_DEFAULT, kUav,
                       kUavFlag, sky_uv_, L"rt sky coordinates") ||
        !create_buffer(bottom_info.ResultDataMaxSizeInBytes, D3D12_HEAP_TYPE_DEFAULT, kStructure, kUavFlag, sky_blas_,
                       L"rt sky blas") ||
        !create_buffer(top_info.ResultDataMaxSizeInBytes, D3D12_HEAP_TYPE_DEFAULT, kStructure, kUavFlag, sky_tlas_,
                       L"rt sky tlas") ||
        !create_buffer(sky_scratch_tlas_ + std::max<std::uint64_t>(top_info.ScratchDataSizeInBytes, 256u),
                       D3D12_HEAP_TYPE_DEFAULT, kUav, kUavFlag, sky_scratch_, L"rt sky scratch") ||
        !create_buffer((static_cast<std::uint64_t>(kSkyMapSize) * kSkyMapSize + 16u) * 16u, D3D12_HEAP_TYPE_DEFAULT,
                       kUav, kUavFlag, sky_probe_map_, L"rt sky map")) {
        sky_positions_.Reset();
        sky_surfaces_.Reset();
        sky_uv_.Reset();
        sky_blas_.Reset();
        sky_tlas_.Reset();
        sky_scratch_.Reset();
        sky_probe_map_.Reset();
        return false;
    }
    sky_positions_state_ = kUav;
    sky_surfaces_state_ = kUav;
    sky_uv_state_ = kUav;
    sky_probe_map_state_ = kUav;
    std::cerr << "[rt] sky triangles=" << kSkyMaxTriangles << " blas-kb=" << (bottom_info.ResultDataMaxSizeInBytes >> 10u)
              << '\n';
    return true;
}

void SceneRayTracer::finish_sky(ID3D12GraphicsCommandList4* list, const RtSources& sources, Allocate allocate,
                                void* allocate_context) {
    if (!sky_open_) {
        return;
    }
    flush_sky(list, sources, allocate, allocate_context);
    sky_open_ = false;
    sky_built_ = 0u;
    if (list == nullptr || sky_incomplete_ || sky_triangles_ == 0u || !sky_blas_) {
        return;
    }
    const RtUpload instance_upload = allocate(allocate_context, sizeof(D3D12_RAYTRACING_INSTANCE_DESC), 256u);
    if (instance_upload.cpu == nullptr) {
        return;
    }
    constexpr D3D12_RESOURCE_STATES kRead = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
    to_state(list, sky_positions_.Get(), sky_positions_state_, kRead);
    to_state(list, sky_surfaces_.Get(), sky_surfaces_state_, kRead);
    to_state(list, sky_uv_.Get(), sky_uv_state_, kRead);
    D3D12_RESOURCE_BARRIER barrier = uav_barrier(nullptr);
    list->ResourceBarrier(1u, &barrier);
    // Every layer of the sky is a candidate (none opaque): the probe composites them in the order they were drawn.
    D3D12_RAYTRACING_GEOMETRY_DESC geometry{};
    geometry.Type = D3D12_RAYTRACING_GEOMETRY_TYPE_TRIANGLES;
    geometry.Triangles.VertexFormat = DXGI_FORMAT_R32G32B32_FLOAT;
    geometry.Triangles.VertexCount = sky_triangles_ * 3u;
    geometry.Triangles.VertexBuffer.StartAddress = sky_positions_->GetGPUVirtualAddress();
    geometry.Triangles.VertexBuffer.StrideInBytes = 12u;
    D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC build{};
    build.Inputs.Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL;
    build.Inputs.Flags = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_BUILD;
    build.Inputs.NumDescs = 1u;
    build.Inputs.DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY;
    build.Inputs.pGeometryDescs = &geometry;
    build.DestAccelerationStructureData = sky_blas_->GetGPUVirtualAddress();
    build.ScratchAccelerationStructureData = sky_scratch_->GetGPUVirtualAddress();
    list->BuildRaytracingAccelerationStructure(&build, 0u, nullptr);
    barrier = uav_barrier(sky_blas_.Get());
    list->ResourceBarrier(1u, &barrier);
    D3D12_RAYTRACING_INSTANCE_DESC instance{};
    instance.Transform[0][0] = 1.0f;
    instance.Transform[1][1] = 1.0f;
    instance.Transform[2][2] = 1.0f;
    instance.InstanceMask = 0xFFu;
    instance.Flags = D3D12_RAYTRACING_INSTANCE_FLAG_TRIANGLE_CULL_DISABLE |
                     D3D12_RAYTRACING_INSTANCE_FLAG_FORCE_NON_OPAQUE;
    instance.AccelerationStructure = sky_blas_->GetGPUVirtualAddress();
    std::memcpy(instance_upload.cpu, &instance, sizeof(instance));
    D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC top{};
    top.Inputs.Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL;
    top.Inputs.Flags = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_TRACE;
    top.Inputs.NumDescs = 1u;
    top.Inputs.DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY;
    top.Inputs.InstanceDescs = instance_upload.gpu;
    top.DestAccelerationStructureData = sky_tlas_->GetGPUVirtualAddress();
    top.ScratchAccelerationStructureData = sky_scratch_->GetGPUVirtualAddress() + sky_scratch_tlas_;
    list->BuildRaytracingAccelerationStructure(&top, 0u, nullptr);
    barrier = uav_barrier(sky_tlas_.Get());
    list->ResourceBarrier(1u, &barrier);
    sky_built_ = sky_triangles_;
}

bool SceneRayTracer::build_tlas(ID3D12GraphicsCommandList4* list, Allocate allocate, void* allocate_context) {
    // The late geometry of this scene (the water's trace) or the last one (moved to this camera: a frame late).
    std::array<float, 12> inverse{};
    const bool late = late_built_ != 0u && late_blas_ && late_camera_known_ && camera_known_ &&
                      scene_serial_ - 1u - late_scene_ <= 1u && affine_inverse(late_camera_, inverse);
    const UINT count = late ? 2u : 1u;
    const RtUpload upload = allocate(allocate_context, 2u * sizeof(D3D12_RAYTRACING_INSTANCE_DESC), 256u);
    if (upload.cpu == nullptr) {
        return false;
    }
    D3D12_RAYTRACING_INSTANCE_DESC instances[2]{};
    for (D3D12_RAYTRACING_INSTANCE_DESC& instance : instances) {
        instance.Flags = D3D12_RAYTRACING_INSTANCE_FLAG_TRIANGLE_CULL_DISABLE | D3D12_RAYTRACING_INSTANCE_FLAG_FORCE_OPAQUE;
    }
    instances[0].Transform[0][0] = 1.0f;
    instances[0].Transform[1][1] = 1.0f;
    instances[0].Transform[2][2] = 1.0f;
    instances[0].InstanceMask = kSceneMask;
    instances[0].AccelerationStructure = blas_->GetGPUVirtualAddress();
    if (late) {
        const std::array<float, 12> to_now = affine_multiply(camera_, inverse);
        for (unsigned r = 0; r < 3u; ++r) {
            for (unsigned c = 0; c < 4u; ++c) {
                instances[1].Transform[r][c] = to_now[r * 4u + c];
            }
        }
        instances[1].InstanceID = 1u;
        instances[1].InstanceMask = kLateMask;
        instances[1].AccelerationStructure = late_blas_->GetGPUVirtualAddress();
    }
    std::memcpy(upload.cpu, instances, count * sizeof(D3D12_RAYTRACING_INSTANCE_DESC));
    D3D12_RESOURCE_BARRIER barrier = uav_barrier(nullptr);
    list->ResourceBarrier(1u, &barrier);
    D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC top{};
    top.Inputs.Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL;
    top.Inputs.Flags = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_TRACE;
    top.Inputs.NumDescs = count;
    top.Inputs.DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY;
    top.Inputs.InstanceDescs = upload.gpu;
    top.DestAccelerationStructureData = tlas_->GetGPUVirtualAddress();
    top.ScratchAccelerationStructureData = scratch_->GetGPUVirtualAddress() + scratch_bytes_;
    list->BuildRaytracingAccelerationStructure(&top, 0u, nullptr);
    barrier = uav_barrier(tlas_.Get());
    list->ResourceBarrier(1u, &barrier);
    return true;
}

bool SceneRayTracer::trace_scene(ID3D12GraphicsCommandList* list, const RtSources& sources, Allocate allocate,
                                 void* allocate_context, D3D12_GPU_DESCRIPTOR_HANDLE depth_srv,
                                 std::uint32_t width, std::uint32_t height) {
    if (log_seq_) {
        std::fprintf(stderr, "[rt-seq] frame=%llu trace_scene ready=%d scene-open=%d draws=%zu triangles=%u incomplete=%d\n",
                     static_cast<unsigned long long>(frame_index_), ready_ ? 1 : 0, scene_open_ ? 1 : 0, draws_.size(),
                     triangles_, scene_incomplete_ ? 1 : 0);
    }
    if (!ready_ || !scene_open_) {
        return false;
    }
    flush_geometry(list, sources, allocate, allocate_context);
    scene_open_ = false;
    glow_open_ = emitters_active() && emit_merge_pso_ != nullptr;
    if (draws_.empty() || triangles_ == 0u || list == nullptr || allocate == nullptr || !positions_ ||
        capacity_ < triangles_ || scene_incomplete_) {
        if (log_seq_) {
            std::fprintf(stderr, "[rt-seq] frame=%llu trace_scene skipped draws=%zu triangles=%u capacity=%u incomplete=%d\n",
                         static_cast<unsigned long long>(frame_index_), draws_.size(), triangles_, capacity_,
                         scene_incomplete_ ? 1 : 0);
        }
        return false;
    }
    ++window_.scenes;
    window_.draws += draws_.size();
    window_.triangles += triangles_;
    window_.max_triangles = std::max<std::uint64_t>(window_.max_triangles, triangles_);
    ComPtr<ID3D12GraphicsCommandList4> list4;
    if (FAILED(list->QueryInterface(IID_PPV_ARGS(&list4)))) {
        ++window_.skipped;
        return false;
    }
    const std::uint64_t serial = scene_serial_++;
    const unsigned slot = static_cast<unsigned>(serial % kResultSlots);
    const bool record_results = !slots_[slot].pending;
    const bool validated = validate_ && view_known_ && depth_srv.ptr != 0u && width != 0u && height != 0u &&
                           record_results;
    bool dumping = validated && serial == dump_scene_ && dump_scene_ != 0u && !dump_pending_;
    if (dumping) {
        const std::uint64_t bytes = static_cast<std::uint64_t>(width) * height * 16u;
        dump_.Reset();
        dump_readback_.Reset();
        dumping = create_buffer(bytes, D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                                D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS, dump_, L"rt dump") &&
                  create_buffer(bytes, D3D12_HEAP_TYPE_READBACK, D3D12_RESOURCE_STATE_COPY_DEST,
                                D3D12_RESOURCE_FLAG_NONE, dump_readback_, L"rt dump readback");
    }
    RtUpload frame{};
    if (!fill_constants(sources, allocate, allocate_context, 0u, 0u, triangles_, width, height, dumping ? 1u : 0u,
                        frame)) {
        ++window_.skipped;
        if (log_seq_) std::fprintf(stderr, "[rt-seq] trace_scene failed: constants\n");
        return false;
    }
    if (record_results && timestamps_) {
        list->EndQuery(timestamps_.Get(), D3D12_QUERY_TYPE_TIMESTAMP, slot * kTimestampsPerSlot);
    }
    {
        const D3D12_RESOURCE_BARRIER barrier = transition(
            positions_.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        list->ResourceBarrier(1u, &barrier);
        positions_state_ = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
    }
    list->SetComputeRootSignature(root_.Get());
    list->SetComputeRootConstantBufferView(kRootFrame, frame.gpu);

    D3D12_RAYTRACING_GEOMETRY_DESC geometry{};
    geometry.Type = D3D12_RAYTRACING_GEOMETRY_TYPE_TRIANGLES;
    geometry.Flags = D3D12_RAYTRACING_GEOMETRY_FLAG_OPAQUE;
    geometry.Triangles.VertexFormat = DXGI_FORMAT_R32G32B32_FLOAT;
    geometry.Triangles.VertexCount = triangles_ * 3u;
    geometry.Triangles.VertexBuffer.StartAddress = positions_->GetGPUVirtualAddress();
    geometry.Triangles.VertexBuffer.StrideInBytes = 12u;
    D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC build{};
    build.Inputs.Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL;
    build.Inputs.Flags = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_BUILD;
    build.Inputs.NumDescs = 1u;
    build.Inputs.DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY;
    build.Inputs.pGeometryDescs = &geometry;
    build.DestAccelerationStructureData = blas_->GetGPUVirtualAddress();
    build.ScratchAccelerationStructureData = scratch_->GetGPUVirtualAddress();
    list4->BuildRaytracingAccelerationStructure(&build, 0u, nullptr);
    {
        const D3D12_RESOURCE_BARRIER barrier = uav_barrier(blas_.Get());
        list->ResourceBarrier(1u, &barrier);
    }
    if (!build_tlas(list4.Get(), allocate, allocate_context)) {
        ++window_.skipped;
        if (log_seq_) std::fprintf(stderr, "[rt-seq] trace_scene failed: tlas\n");
        return false;
    }
    finish_sky(list4.Get(), sources, allocate, allocate_context);
    if (record_results && timestamps_) {
        list->EndQuery(timestamps_.Get(), D3D12_QUERY_TYPE_TIMESTAMP, slot * kTimestampsPerSlot + 1u);
    }

    if (validated) {
        list->CopyBufferRegion(stats_.Get(), 0u, stats_zero_.Get(), 0u, kStatsWords * 4u);
        D3D12_RESOURCE_BARRIER barrier = transition(stats_.Get(), D3D12_RESOURCE_STATE_COPY_DEST,
                                                    D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        list->ResourceBarrier(1u, &barrier);
        dred_marker(list, L"validate");
        list->SetPipelineState(validate_pso_.Get());
        list->SetComputeRootShaderResourceView(kRootScene, tlas_->GetGPUVirtualAddress());
        list->SetComputeRootDescriptorTable(kRootDepth, depth_srv);
        list->SetComputeRootUnorderedAccessView(kRootStats, stats_->GetGPUVirtualAddress());
        list->SetComputeRootUnorderedAccessView(kRootDump, dumping ? dump_->GetGPUVirtualAddress()
                                                                   : stats_->GetGPUVirtualAddress());
        list->Dispatch((width + 7u) / 8u, (height + 7u) / 8u, 1u);
        if (dumping) {
            D3D12_RESOURCE_BARRIER dump_barrier = transition(dump_.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                                                             D3D12_RESOURCE_STATE_COPY_SOURCE);
            list->ResourceBarrier(1u, &dump_barrier);
            list->CopyBufferRegion(dump_readback_.Get(), 0u, dump_.Get(), 0u,
                                   static_cast<std::uint64_t>(width) * height * 16u);
            dump_pending_ = true;
            dump_frame_ = frame_index_;
            dump_draws_ = draws_;
            dump_infos_ = infos_;
            dump_camera_ = camera_;
            dump_camera_known_ = camera_known_;
            dump_width_ = width;
            dump_height_ = height;
        }
        barrier = transition(stats_.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_SOURCE);
        list->ResourceBarrier(1u, &barrier);
        list->CopyBufferRegion(readback_.Get(), slot * kReadbackSlotBytes, stats_.Get(), 0u, kStatsWords * 4u);
        barrier = transition(stats_.Get(), D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_COPY_DEST);
        list->ResourceBarrier(1u, &barrier);
    }
    if (record_results && timestamps_) {
        list->EndQuery(timestamps_.Get(), D3D12_QUERY_TYPE_TIMESTAMP, slot * kTimestampsPerSlot + 2u);
    }
    trace_lighting(list, allocate, allocate_context, depth_srv, width, height);
    water_open_ = reflections_active() && light_slot_ >= 0;
    if (log_seq_) {
        std::fprintf(stderr, "[rt-seq] frame=%llu traced: water window %s (reflections %d, slot %d)\n",
                     static_cast<unsigned long long>(frame_index_), water_open_ ? "open" : "closed",
                     reflections_active() ? 1 : 0, light_slot_);
    }
    late_open_ = true;
    late_next_camera_ = camera_;
    late_next_camera_known_ = camera_known_;
    if (record_results) {
        if (timestamps_) {
            list->EndQuery(timestamps_.Get(), D3D12_QUERY_TYPE_TIMESTAMP, slot * kTimestampsPerSlot + 3u);
            list->ResolveQueryData(timestamps_.Get(), D3D12_QUERY_TYPE_TIMESTAMP, slot * kTimestampsPerSlot,
                                   kTimestampsPerSlot, readback_.Get(), slot * kReadbackSlotBytes + 64u);
        }
        slots_[slot] = ResultSlot{frame_index_, true, validated, static_cast<std::uint32_t>(draws_.size()),
                                  triangles_};
    }
    return true;
}

}  // namespace galaxy::gx
