// galaxy::fused_multiply_add against std::fma (the C runtime's correctly
// rounded result, the reference here and nowhere else) and, on a CPU with
// FMA3, against the instruction. Raw bit patterns, typical magnitudes with
// exponent gaps, cancellation, binary32 subnormal results, and constructed
// double-rounding midpoints, which a binary64 multiply-add then narrowing
// gets wrong.
#include "galaxy/fused_multiply_add.h"

#include <algorithm>
#include <cstdio>
#include <random>

namespace {
std::uint64_t g_failures = 0;
std::uint64_t g_cases = 0;
std::uint64_t g_naive_wrong = 0;
bool g_fma3 = false;

bool same(float x, float y) {
    return (std::isnan(x) && std::isnan(y)) || std::bit_cast<std::uint32_t>(x) == std::bit_cast<std::uint32_t>(y);
}
bool same(double x, double y) {
    return (std::isnan(x) && std::isnan(y)) || std::bit_cast<std::uint64_t>(x) == std::bit_cast<std::uint64_t>(y);
}

template <class T>
void check(T a, T b, T c) {
    ++g_cases;
    const T want = std::fma(a, b, c);
    const T sse2 = galaxy::fused_multiply_add_sse2(a, b, c);
    const T selected = galaxy::fused_multiply_add(a, b, c);
    bool ok = same(want, sse2) && same(want, selected);
    if (g_fma3) ok = ok && same(want, galaxy::fused_multiply_add_fma3(a, b, c));
    if (!ok && g_failures++ < 8)
        std::printf("FAIL a=%a b=%a c=%a want=%a sse2=%a\n", static_cast<double>(a), static_cast<double>(b),
            static_cast<double>(c), static_cast<double>(want), static_cast<double>(sse2));
    if constexpr (sizeof(T) == 4) g_naive_wrong += !same(want, static_cast<float>(static_cast<double>(a) * b + c));
}
} // namespace

int main() {
    g_fma3 = galaxy::host_has_fma3();
    std::mt19937_64 rng(0x6e6562756c61ull);
    const auto pick = [&](int lo, int hi) { return lo + static_cast<int>(rng() % static_cast<unsigned>(hi - lo + 1)); };
    const auto f32 = [&](int lo, int hi) {
        const float m = 1.0f + static_cast<float>(rng() & 0x7FFFFFu) * 0x1p-23f;
        return std::ldexp((rng() & 1u) ? -m : m, pick(lo, hi));
    };
    const auto f64 = [&](int lo, int hi) {
        const double m = 1.0 + static_cast<double>(rng() & 0xFFFFFFFFFFFFFull) * 0x1p-52;
        return std::ldexp((rng() & 1u) ? -m : m, pick(lo, hi));
    };
    const float special32[] = {0.0f, -0.0f, 1.0f, -1.0f, 0x1p-149f, 0x1p-126f, 0x1.fffffep127f,
        INFINITY, -INFINITY, NAN};
    const double special64[] = {0.0, -0.0, 1.0, -1.0, 0x1p-1074, 0x1p-1022, 0x1.fffffffffffffp1023,
        0x1p-401, 0x1p400, 0x1p-801, 0x1p800, INFINITY, -INFINITY, NAN};
    for (float a : special32) for (float b : special32) for (float c : special32) check(a, b, c);
    for (double a : special64) for (double b : special64) for (double c : special64) check(a, b, c);

    for (int i = 0; i < 400000; ++i) {
        check(std::bit_cast<float>(static_cast<std::uint32_t>(rng())), std::bit_cast<float>(static_cast<std::uint32_t>(rng())),
              std::bit_cast<float>(static_cast<std::uint32_t>(rng())));
        const float a = f32(-20, 20), b = f32(-20, 20);
        const int product_exponent = std::ilogb(a) + std::ilogb(b);
        check(a, b, f32(product_exponent - 60, product_exponent + 60));
        check(a, b, -(a * b));
        check(a, b, -std::nextafter(a * b, (rng() & 1u) ? INFINITY : -INFINITY));
        check(f32(-80, -60), f32(-80, -60), f32(-150, -120));
        // c + h - h*k^2: just below (or above) the binary32 midpoint above c.
        const float c = f32(-60, 60);
        const float h = std::ldexp(1.0f, std::ilogb(c) - 24);
        const float k = static_cast<float>(pick(1, 1000)) * 0x1p-20f;
        check(h * (1.0f + k), 1.0f - k, c);
        check(-h * (1.0f + k), 1.0f + k, c);

        check(std::bit_cast<double>(rng()), std::bit_cast<double>(rng()), std::bit_cast<double>(rng()));
        const double x = f64(-390, 390), y = f64(-390, 390);
        const int xy_exponent = std::ilogb(x) + std::ilogb(y);
        check(x, y, f64(std::max(-790, xy_exponent - 120), std::min(790, xy_exponent + 120)));
        check(x, y, -(x * y));
        check(x, y, -std::nextafter(x * y, (rng() & 1u) ? INFINITY : -INFINITY));
        check(x, y, 0.0);
        check(0.0, y, f64(-90, 90));
        check(f64(-1000, 1000), f64(-1000, 1000), f64(-1070, 1020));
    }
    std::printf("fused multiply-add: %llu cases, %llu mismatches (fma3=%d; binary64-then-narrow wrong in %llu)\n",
        static_cast<unsigned long long>(g_cases), static_cast<unsigned long long>(g_failures), g_fma3 ? 1 : 0,
        static_cast<unsigned long long>(g_naive_wrong));
    // Without midpoint cases the binary32 check proves little.
    return g_failures == 0 && g_naive_wrong > 1000 ? 0 : 1;
}
