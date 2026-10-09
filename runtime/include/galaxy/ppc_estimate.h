// Copyright 2018 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

// Broadway estimate tables/interpolation adapted from Dolphin FloatUtils.cpp,
// revision e6f3ae17627e4344da95b13424af5baf4c892b08. The same tables are used by
// WiiCompiled. Operate on bits: no host division, square root, FP environment
// change, or rounding-dependent NaN conversion is needed for these estimates.
#include <array>
#include <cstdint>

namespace galaxy::estimate_detail {
struct Entry { std::int32_t base; std::int32_t decrement; };
inline constexpr std::uint64_t kSign = 0x8000000000000000ull;
inline constexpr std::uint64_t kExponent = 0x7FF0000000000000ull;
inline constexpr std::uint64_t kFraction = 0x000FFFFFFFFFFFFFull;
inline constexpr std::uint64_t kQuiet = 0x0008000000000000ull;
inline constexpr std::array<Entry, 32> kReciprocal = {{

    {0x7ff800, 0x3e1}, {0x783800, 0x3a7}, {0x70ea00, 0x371}, {0x6a0800, 0x340}, {0x638800, 0x313},
    {0x5d6200, 0x2ea}, {0x579000, 0x2c4}, {0x520800, 0x2a0}, {0x4cc800, 0x27f}, {0x47ca00, 0x261},
    {0x430800, 0x245}, {0x3e8000, 0x22a}, {0x3a2c00, 0x212}, {0x360800, 0x1fb}, {0x321400, 0x1e5},
    {0x2e4a00, 0x1d1}, {0x2aa800, 0x1be}, {0x272c00, 0x1ac}, {0x23d600, 0x19b}, {0x209e00, 0x18b},
    {0x1d8800, 0x17c}, {0x1a9000, 0x16e}, {0x17ae00, 0x15b}, {0x14f800, 0x15b}, {0x124400, 0x143},
    {0x0fbe00, 0x143}, {0x0d3800, 0x12d}, {0x0ade00, 0x12d}, {0x088400, 0x11a}, {0x065000, 0x11a},
    {0x041c00, 0x108}, {0x020c00, 0x106},
}};
inline constexpr std::array<Entry, 32> kReciprocalRoot = {{

    {0x1a7e800, -0x568}, {0x17cb800, -0x4f3}, {0x1552800, -0x48d}, {0x130c000, -0x435},
    {0x10f2000, -0x3e7}, {0x0eff000, -0x3a2}, {0x0d2e000, -0x365}, {0x0b7c000, -0x32e},
    {0x09e5000, -0x2fc}, {0x0867000, -0x2d0}, {0x06ff000, -0x2a8}, {0x05ab800, -0x283},
    {0x046a000, -0x261}, {0x0339800, -0x243}, {0x0218800, -0x226}, {0x0105800, -0x20b},
    {0x3ffa000, -0x7a4}, {0x3c29000, -0x700}, {0x38aa000, -0x670}, {0x3572000, -0x5f2},
    {0x3279000, -0x584}, {0x2fb7000, -0x524}, {0x2d26000, -0x4cc}, {0x2ac0000, -0x47e},
    {0x2881000, -0x43a}, {0x2665000, -0x3fa}, {0x2468000, -0x3c2}, {0x2287000, -0x38e},
    {0x20c1000, -0x35e}, {0x1f12000, -0x332}, {0x1d79000, -0x30a}, {0x1bf4000, -0x2e6},
}};

inline std::uint64_t reciprocal_bits(std::uint64_t input) {
    const auto mantissa = input & kFraction;
    const auto sign = input & kSign;
    auto exponent = input & kExponent;
    if (mantissa == 0u && exponent == 0u) return sign | kExponent;
    if (exponent == kExponent) return mantissa == 0u ? sign : input | kQuiet;
    if (exponent < (std::uint64_t{895} << 52u)) return sign | 0x47EFFFFFE0000000ull;
    if (exponent >= (std::uint64_t{1149} << 52u)) return sign;
    exponent = (std::uint64_t{0x7FD} << 52u) - exponent;
    const auto index = static_cast<std::uint32_t>(mantissa >> 37u);
    const auto& entry = kReciprocal[index / 1024u];
    const auto estimate = entry.base - (entry.decrement * (index % 1024u) + 1u) / 2u;
    return sign | exponent | (static_cast<std::uint64_t>(estimate) << 29u);
}

inline std::uint64_t reciprocal_root_bits(std::uint64_t input) {
    auto mantissa = input & kFraction;
    const auto sign = input & kSign;
    auto exponent = static_cast<std::int64_t>(input & kExponent);
    if (mantissa == 0u && exponent == 0) return sign | kExponent;
    if (static_cast<std::uint64_t>(exponent) == kExponent) {
        if (mantissa != 0u) return input | kQuiet;
        return sign != 0u ? kExponent | kQuiet : 0u;
    }
    if (sign != 0u) return kExponent | kQuiet;
    if (exponent == 0) {
        do {
            exponent -= std::int64_t{1} << 52u;
            mantissa <<= 1u;
        } while ((mantissa & (std::uint64_t{1} << 52u)) == 0u);
        mantissa &= kFraction;
        exponent += std::int64_t{1} << 52u;
    }
    const auto parity = exponent & (std::int64_t{1} << 52u);
    exponent = ((std::int64_t{0x3FF} << 52u) -
        ((exponent - (std::int64_t{0x3FE} << 52u)) / 2)) &
        static_cast<std::int64_t>(kExponent);
    const auto index = static_cast<std::uint32_t>(
        (static_cast<std::uint64_t>(parity) | mantissa) >> 37u);
    const auto& entry = kReciprocalRoot[index / 2048u];
    const auto estimate = entry.base + entry.decrement * static_cast<std::int32_t>(index % 2048u);
    return static_cast<std::uint64_t>(exponent) |
        (static_cast<std::uint64_t>(estimate) << 26u);
}
}  // namespace galaxy::estimate_detail
