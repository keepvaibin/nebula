// SPDX-License-Identifier: GPL-3.0-only
// The shading model the programs and the tracer share (materials/shade_core.hlsli), compiled as C++ and checked
// against the physics it claims: the exact Fresnel's known values, the lobe's terms at a mirror against Schlick's,
// energy kept (white furnace), a light's highlight integrating to the lobe's own reflectance, the thin sheet's series,
// and water's medium.
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <iostream>

namespace shade {

using uint = unsigned int;

struct float2 {
    float x, y;
    float2() : x(0.0f), y(0.0f) {}
    float2(float a, float b) : x(a), y(b) {}
};
struct float3 {
    float x, y, z;
    float3() : x(0.0f), y(0.0f), z(0.0f) {}
    float3(float a, float b, float c) : x(a), y(b), z(c) {}
};

inline float2 operator+(float2 a, float2 b) { return {a.x + b.x, a.y + b.y}; }
inline float2 operator-(float2 a, float2 b) { return {a.x - b.x, a.y - b.y}; }
inline float2 operator*(float2 a, float s) { return {a.x * s, a.y * s}; }
inline float3 operator+(float3 a, float3 b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
inline float3 operator-(float3 a, float3 b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
inline float3 operator-(float3 a) { return {-a.x, -a.y, -a.z}; }
inline float3 operator*(float3 a, float3 b) { return {a.x * b.x, a.y * b.y, a.z * b.z}; }
inline float3 operator/(float3 a, float3 b) { return {a.x / b.x, a.y / b.y, a.z / b.z}; }
inline float3 operator*(float3 a, float s) { return {a.x * s, a.y * s, a.z * s}; }
inline float3 operator*(float s, float3 a) { return a * s; }
inline float3 operator/(float3 a, float s) { return {a.x / s, a.y / s, a.z / s}; }

inline float saturate(float v) { return std::clamp(v, 0.0f, 1.0f); }
inline float lerp(float a, float b, float t) { return a + (b - a) * t; }
inline float2 lerp(float2 a, float2 b, float t) { return a + (b - a) * t; }
inline float3 lerp(float3 a, float3 b, float t) { return a + (b - a) * t; }
inline float3 clamp(float3 v, float3 lo, float3 hi) {
    return {std::clamp(v.x, lo.x, hi.x), std::clamp(v.y, lo.y, hi.y), std::clamp(v.z, lo.z, hi.z)};
}
inline float max(float a, float b) { return std::max(a, b); }
inline float3 max(float3 a, float3 b) { return {std::max(a.x, b.x), std::max(a.y, b.y), std::max(a.z, b.z)}; }
inline uint min(uint a, uint b) { return std::min(a, b); }
inline float abs(float v) { return std::fabs(v); }
inline float sqrt(float v) { return std::sqrt(v); }
inline float exp(float v) { return std::exp(v); }
inline float3 exp(float3 v) { return {std::exp(v.x), std::exp(v.y), std::exp(v.z)}; }
inline float dot(float3 a, float3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
inline float3 normalize(float3 v) { return v / std::sqrt(dot(v, v)); }

#include "shade_core.hlsli"

}  // namespace shade

namespace {

using namespace shade;

int g_failed = 0;

void expect(bool condition, const char* what, double got) {
    if (!condition) {
        std::cerr << "FAILED: " << what << " (" << got << ")\n";
        ++g_failed;
    }
}

float lum(float3 c) { return 0.2126f * c.x + 0.7152f * c.y + 0.0722f * c.z; }

void fresnel() {
    // Head-on, the exact form is the F0 of the index.
    expect(std::fabs(shade_fresnel_exact(1.0f, 1.33f) - shade_f0_of_ior(1.33f)) < 1e-6f, "water head-on", shade_fresnel_exact(1.0f, 1.33f));
    expect(std::fabs(shade_f0_of_ior(1.33f) - 0.02006f) < 1e-4f, "water F0 0.0201", shade_f0_of_ior(1.33f));
    expect(std::fabs(shade_f0_of_ior(1.5f) - 0.04f) < 1e-6f, "glass F0 0.04", shade_f0_of_ior(1.5f));
    // Air to water at 60 degrees: 0.0591 (Rs 0.1139, Rp 0.0044).
    const float f60 = shade_fresnel_exact(0.5f, 1.33f);
    expect(std::fabs(f60 - 0.0591f) < 2e-4f, "water at 60 degrees", f60);
    // Water to air: total internal reflection past asin(1 / 1.33) = 48.75 degrees.
    expect(shade_fresnel_exact(std::cos(50.0f * 3.14159265f / 180.0f), 1.0f / 1.33f) == 1.0f, "TIR at 50 degrees", 0.0);
    expect(shade_fresnel_exact(std::cos(47.0f * 3.14159265f / 180.0f), 1.0f / 1.33f) < 1.0f, "no TIR at 47 degrees", 0.0);
    expect(shade_refracted_cos(std::cos(50.0f * 3.14159265f / 180.0f), 1.0f / 1.33f) < 0.0f, "no refracted ray under TIR", 0.0);
    // Snell: 30 degrees in air -> asin(sin 30 / 1.33) = 22.08 degrees in water.
    const float ct = shade_refracted_cos(std::cos(30.0f * 3.14159265f / 180.0f), 1.33f);
    expect(std::fabs(std::acos(ct) * 180.0f / 3.14159265f - 22.08f) < 0.02f, "Snell 30 -> 22.08", std::acos(ct) * 180.0 / 3.14159265);
}

void mirror_row() {
    // At roughness 0 the lobe's terms are Schlick's exactly at the nodes and within the table's interpolation between.
    double worst = 0.0;
    for (int i = 0; i <= 200; ++i) {
        const float nv = kShadeNvLeast + (1.0f - kShadeNvLeast) * float(i) / 200.0f;
        const float2 t = shade_dfg(nv, 0.0f);
        const float p = std::pow(1.0f - nv, 5.0f);
        worst = std::max(worst, double(std::max(std::fabs(t.x - (1.0f - p)), std::fabs(t.y - p))));
    }
    expect(worst < 0.006, "mirror row is Schlick's", worst);
}

void furnace() {
    // A metal of F0 1 mirrors everything at every roughness and angle (Fdez-Aguera's multiple scattering).
    double worst_metal = 0.0, worst_white = 0.0, worst_over = 0.0;
    for (int j = 0; j <= 20; ++j) {
        for (int i = 0; i <= 20; ++i) {
            const float rough = float(j) / 20.0f;
            const float nv = 0.01f + 0.99f * float(i) / 20.0f;
            const ShadeMaterial metal = shade_material(float3(0.9f, 0.9f, 0.9f), rough, 1.0f, 1.5f, float3(), kShadeBehindOpaque);
            ShadeMaterial bright = metal;
            bright.base = float3(1.0f, 1.0f, 1.0f);  // past kShadeBaseMost on purpose: the furnace's ideal white
            worst_metal = std::max(worst_metal, double(std::fabs(shade_lobes(bright, nv).mirror.x - 1.0f)));
            // A white dielectric sends back everything: what it mirrors plus what its body returns.
            ShadeMaterial white = shade_material(float3(), rough, 0.0f, 1.5f, float3(), kShadeBehindOpaque);
            white.base = float3(1.0f, 1.0f, 1.0f);
            const ShadeLobes l = shade_lobes(white, nv);
            worst_white = std::max(worst_white, double(std::fabs(l.mirror.x + l.diffuse.x - 1.0f)));
            // A real surface (base at most 0.9) never sends back more than it receives.
            const ShadeLobes r = shade_lobes(metal, nv);
            worst_over = std::max(worst_over, double(r.mirror.x + r.diffuse.x - 1.0f));
        }
    }
    expect(worst_metal < 1e-4, "white furnace, metal F0 1", worst_metal);
    expect(worst_white < 1e-4, "white furnace, white dielectric", worst_white);
    expect(worst_over <= 0.0, "nothing sends back more than it receives", worst_over);
}

// The highlight of even light of radiance 1 from every direction (a direction's light dw / pi), summed over the
// hemisphere, is what the lobe mirrors of even light (the split sum's F0 A + B, with the multiple-scattering boost): the D and Vis normalisation and
// the table agree.
void highlight_energy() {
    double worst = 0.0;
    const float roughs[] = {0.2f, 0.5f, 0.8f};
    const float nvs[] = {0.3f, 0.7f, 1.0f};
    for (float rough : roughs) {
        for (float nv : nvs) {
            const ShadeMaterial m = shade_material(float3(0.9f, 0.9f, 0.9f), rough, 1.0f, 1.5f, float3(), kShadeBehindOpaque);
            const ShadeLobes lobes = shade_lobes(m, nv);
            const float3 n(0.0f, 0.0f, 1.0f);
            const float3 v(std::sqrt(std::max(1.0f - nv * nv, 0.0f)), 0.0f, nv);
            const int rings = 1024, steps = 512;
            double sum = 0.0;
            for (int a = 0; a < rings; ++a) {
                // Rings even in cos(theta): d(omega) = d(cos) d(phi).
                const float c = (float(a) + 0.5f) / float(rings);
                const float s = std::sqrt(1.0f - c * c);
                for (int b = 0; b < steps; ++b) {
                    const float phi = 2.0f * 3.14159265f * (float(b) + 0.5f) / float(steps);
                    const float3 l(s * std::cos(phi), s * std::sin(phi), c);
                    sum += lum(shade_highlight(m, lobes, n, v, l, 0.0f));
                }
            }
            const double integral = sum * (1.0 / rings) * (2.0 * 3.14159265 / steps) / 3.14159265;
            worst = std::max(worst, std::fabs(integral - double(lum(lobes.mirror))) / double(lum(lobes.mirror)));
        }
    }
    expect(worst < 0.02, "a light's highlight over the hemisphere is the lobe's reflectance", worst);
}

void sheet() {
    const float3 clear(1.0f, 1.0f, 1.0f), black;
    expect(std::fabs(shade_sheet_through(0.0f, clear).x - 1.0f) < 1e-6f, "no reflection, clear: all through", 0.0);
    expect(std::fabs(shade_sheet_mirror(0.04f, black).x - 0.04f) < 1e-6f, "an opaque sheet mirrors its face", 0.0);
    const float r = 0.04f;
    const float sum = shade_sheet_through(r, clear).x + shade_sheet_mirror(r, clear).x;
    expect(std::fabs(sum - 1.0f) < 1e-5f, "a clear sheet keeps all the light", sum);
    // A pane of glass head-on lets through 2n / (n^2 + 1) = 0.923.
    expect(std::fabs(shade_sheet_through(r, clear).x - 0.9231f) < 1e-3f, "a pane lets through 0.923", shade_sheet_through(r, clear).x);
}

void water() {
    const ShadeMedium w = shade_water(1770.0f);
    expect(std::fabs(1.0f / w.absorb.y - 1770.0f) < 1.0f, "green's 1/e way 1770 units", 1.0 / w.absorb.y);
    const ShadeMedium muddy = shade_water(177.0f);
    expect(std::fabs(muddy.absorb.x / w.absorb.x - 10.0f) < 1e-3f, "a tenth of the clear way, ten times the absorption", muddy.absorb.x / w.absorb.x);
    const float3 sigma = shade_extinction(w);
    // A metre of clear water: red mostly gone, blue nearly all kept.
    const float3 t = shade_beer(sigma, 100.0f);
    expect(t.x < 0.75f && t.z > 0.98f && t.x < t.y && t.y < t.z, "a metre of water: red goes first", t.x);
    expect(shade_beer(sigma, 1e30f).y == 0.0f, "no floor: nothing comes through", 0.0);
    // The gathered light reaches a finite limit with no floor, and grows with the way.
    const float3 sun(1.0f, 1.0f, 1.0f), sky(0.3f, 0.3f, 0.3f);
    const float3 g10 = shade_water_gathered(w, sun, 0.9f, -0.5f, sky, 0.9f, true, 1000.0f);
    const float3 ginf = shade_water_gathered(w, sun, 0.9f, -0.5f, sky, 0.9f, true, 1e30f);
    expect(std::isfinite(ginf.z) && ginf.z > g10.z && g10.z > 0.0f, "deep water's own light is finite and the most", ginf.z);
    // Deep clear water's own light, the sun behind the eye: a remote-sensing reflectance of 0.005 to 0.03 per steradian
    // in blue (clear ocean), so 0.016 to 0.1 of a white Lambert surface's 1 / pi (blue the most).
    expect(ginf.z > ginf.x && ginf.z > 0.016f && ginf.z < 0.1f, "deep water is dark blue", ginf.z);
    // Looking up from under water: the source grows toward the surface (k < 0 in one channel is allowed).
    const float up = shade_gathered1(-1e-3f, 100.0f);
    expect(std::isfinite(up) && up > 100.0f, "a way toward the surface gathers more than its length", up);
    // Exact (against doubles) for k from far under 0 through 0 to far over it, the series' switch included.
    double worst = 0.0;
    for (int i = -400; i <= 400; ++i) {
        const double k = (i < 0 ? -1.0 : 1.0) * std::pow(10.0, -9.0 + 7.0 * std::fabs(double(i)) / 400.0) * (i == 0 ? 0.0 : 1.0);
        const double d = 50.0;
        const double ref = std::fabs(k * d) < 1e-12 ? d : (1.0 - std::exp(-k * d)) / k;
        worst = std::max(worst, std::fabs(double(shade_gathered1(float(k), float(d))) - ref) / ref);
    }
    expect(worst < 1e-5, "the gathered integral exact across k = 0", worst);
    // Forward scattering: the particles' phase is far stronger ahead than behind.
    expect(shade_phase_hg(0.92f, 1.0f) > 100.0f * shade_phase_hg(0.92f, -1.0f), "Petzold's forward peak", 0.0);
}

}  // namespace

int main() {
    fresnel();
    mirror_row();
    furnace();
    highlight_energy();
    sheet();
    water();
    if (g_failed != 0) {
        std::cerr << g_failed << " failed\n";
        return 1;
    }
    std::cout << "shade_core: all passed\n";
    return 0;
}
