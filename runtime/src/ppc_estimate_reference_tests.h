// Copyright 2018, 2021 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

// Independent, pinned Dolphin reference used ONLY by public arithmetic tests.
// Keep this double-valued reference separate from the runtime's bit implementation.
// FloatUtils.cpp, FloatUtilsTest.cpp and TestValues.h at
// e6f3ae17627e4344da95b13424af5baf4c892b08; vectors are upstream expectations,
// not new measurements from a Wii or a claim of whole-game conformance.
#include <array>
#include <bit>
#include <cmath>
#include <cstdint>
#include <limits>
namespace estimate_reference {
using s64 = std::int64_t;
using u64 = std::uint64_t;
struct BaseAndDec { std::int32_t m_base; std::int32_t m_dec; };
inline double MakeQuiet(double value) {
    return std::bit_cast<double>(std::bit_cast<u64>(value) | 0x0008000000000000ull);
}
inline constexpr std::array<BaseAndDec, 32> frsqrte_expected = {{
    {0x1a7e800, -0x568}, {0x17cb800, -0x4f3}, {0x1552800, -0x48d}, {0x130c000, -0x435},
    {0x10f2000, -0x3e7}, {0x0eff000, -0x3a2}, {0x0d2e000, -0x365}, {0x0b7c000, -0x32e},
    {0x09e5000, -0x2fc}, {0x0867000, -0x2d0}, {0x06ff000, -0x2a8}, {0x05ab800, -0x283},
    {0x046a000, -0x261}, {0x0339800, -0x243}, {0x0218800, -0x226}, {0x0105800, -0x20b},
    {0x3ffa000, -0x7a4}, {0x3c29000, -0x700}, {0x38aa000, -0x670}, {0x3572000, -0x5f2},
    {0x3279000, -0x584}, {0x2fb7000, -0x524}, {0x2d26000, -0x4cc}, {0x2ac0000, -0x47e},
    {0x2881000, -0x43a}, {0x2665000, -0x3fa}, {0x2468000, -0x3c2}, {0x2287000, -0x38e},
    {0x20c1000, -0x35e}, {0x1f12000, -0x332}, {0x1d79000, -0x30a}, {0x1bf4000, -0x2e6},
}};

inline double ApproximateReciprocalSquareRoot(double val)
{
  s64 integral = std::bit_cast<s64>(val);
  s64 mantissa = integral & ((1LL << 52) - 1);
  const s64 sign = integral & (1ULL << 63);
  s64 exponent = integral & (0x7FFLL << 52);

  // Special case 0
  if (mantissa == 0 && exponent == 0)
  {
    return sign ? -std::numeric_limits<double>::infinity() :
                  std::numeric_limits<double>::infinity();
  }

  // Special case NaN-ish numbers
  if (exponent == (0x7FFLL << 52))
  {
    if (mantissa == 0)
    {
      if (sign)
        return std::numeric_limits<double>::quiet_NaN();

      return 0.0;
    }

    return MakeQuiet(val);
  }

  // Negative numbers return NaN
  if (sign)
    return std::numeric_limits<double>::quiet_NaN();

  if (!exponent)
  {
    // "Normalize" denormal values
    do
    {
      exponent -= 1LL << 52;
      mantissa <<= 1;
    } while (!(mantissa & (1LL << 52)));
    mantissa &= (1LL << 52) - 1;
    exponent += 1LL << 52;
  }

  const s64 exponent_lsb = exponent & (1LL << 52);
  exponent = ((0x3FFLL << 52) - ((exponent - (0x3FELL << 52)) / 2)) & (0x7FFLL << 52);
  integral = sign | exponent;

  const int i = static_cast<int>((exponent_lsb | mantissa) >> 37);
  const auto& entry = frsqrte_expected[i / 2048];
  integral |= static_cast<s64>(entry.m_base + entry.m_dec * (i % 2048)) << 26;

  return std::bit_cast<double>(integral);
}

inline constexpr std::array<BaseAndDec, 32> fres_expected = {{
    {0x7ff800, 0x3e1}, {0x783800, 0x3a7}, {0x70ea00, 0x371}, {0x6a0800, 0x340}, {0x638800, 0x313},
    {0x5d6200, 0x2ea}, {0x579000, 0x2c4}, {0x520800, 0x2a0}, {0x4cc800, 0x27f}, {0x47ca00, 0x261},
    {0x430800, 0x245}, {0x3e8000, 0x22a}, {0x3a2c00, 0x212}, {0x360800, 0x1fb}, {0x321400, 0x1e5},
    {0x2e4a00, 0x1d1}, {0x2aa800, 0x1be}, {0x272c00, 0x1ac}, {0x23d600, 0x19b}, {0x209e00, 0x18b},
    {0x1d8800, 0x17c}, {0x1a9000, 0x16e}, {0x17ae00, 0x15b}, {0x14f800, 0x15b}, {0x124400, 0x143},
    {0x0fbe00, 0x143}, {0x0d3800, 0x12d}, {0x0ade00, 0x12d}, {0x088400, 0x11a}, {0x065000, 0x11a},
    {0x041c00, 0x108}, {0x020c00, 0x106},
}};

// Used by fres and ps_res.
inline double ApproximateReciprocal(double val)
{
  s64 integral = std::bit_cast<s64>(val);
  const s64 mantissa = integral & ((1LL << 52) - 1);
  const s64 sign = integral & (1ULL << 63);
  s64 exponent = integral & (0x7FFLL << 52);

  // Special case 0
  if (mantissa == 0 && exponent == 0)
    return std::copysign(std::numeric_limits<double>::infinity(), val);

  // Special case NaN-ish numbers
  if (exponent == (0x7FFLL << 52))
  {
    if (mantissa == 0)
      return std::copysign(0.0, val);
    return MakeQuiet(val);
  }

  // Special case small inputs
  if (exponent < (895LL << 52))
    return std::copysign(std::numeric_limits<float>::max(), val);

  // Special case large inputs
  if (exponent >= (1149LL << 52))
    return std::copysign(0.0, val);

  exponent = (0x7FDLL << 52) - exponent;

  const int i = static_cast<int>(mantissa >> 37);
  const auto& entry = fres_expected[i / 1024];
  integral = sign | exponent;
  integral |= static_cast<s64>(entry.m_base - (entry.m_dec * (i % 1024) + 1) / 2) << 29;

  return std::bit_cast<double>(integral);
}

inline u64 reciprocal(u64 bits) {
    return std::bit_cast<u64>(ApproximateReciprocal(std::bit_cast<double>(bits)));
}
inline u64 root(u64 bits) {
    return std::bit_cast<u64>(ApproximateReciprocalSquareRoot(std::bit_cast<double>(bits)));
}
inline constexpr std::array<u64, 57> kInputs = {

    // Special values
    0x0000'0000'0000'0000,  // positive zero
    0x0000'0000'0000'0001,  // smallest positive denormal
    0x0000'0000'0100'0000,
    0x000F'FFFF'FFFF'FFFF,  // largest positive denormal
    0x0010'0000'0000'0000,  // smallest positive normal
    0x0010'0000'0000'0002,
    0x3FF0'0000'0000'0000,  // 1.0
    0x7FEF'FFFF'FFFF'FFFF,  // largest positive normal
    0x7FF0'0000'0000'0000,  // positive infinity
    0x7FF0'0000'0000'0001,  // first positive SNaN
    0x7FF7'FFFF'FFFF'FFFF,  // last positive SNaN
    0x7FF8'0000'0000'0000,  // first positive QNaN
    0x7FFF'FFFF'FFFF'FFFF,  // last positive QNaN
    0x8000'0000'0000'0000,  // negative zero
    0x8000'0000'0000'0001,  // smallest negative denormal
    0x8000'0000'0100'0000,
    0x800F'FFFF'FFFF'FFFF,  // largest negative denormal
    0x8010'0000'0000'0000,  // smallest negative normal
    0x8010'0000'0000'0002,
    0xBFF0'0000'0000'0000,  // -1.0
    0xFFEF'FFFF'FFFF'FFFF,  // largest negative normal
    0xFFF0'0000'0000'0000,  // negative infinity
    0xFFF0'0000'0000'0001,  // first negative SNaN
    0xFFF7'FFFF'FFFF'FFFF,  // last negative SNaN
    0xFFF8'0000'0000'0000,  // first negative QNaN
    0xFFFF'FFFF'FFFF'FFFF,  // last negative QNaN

    // (exp > 896) Boundary case for converting to single
    0x3800'0000'0000'0000,  // 2^(-127) = Denormal in single-prec
    0x3810'0000'0000'0000,  // 2^(-126) = Smallest single-prec normal
    0xB800'0000'0000'0000,  // -2^(-127) = Denormal in single-prec
    0xB810'0000'0000'0000,  // -2^(-126) = Smallest single-prec normal
    0x3800'1234'5678'9ABC, 0x3810'1234'5678'9ABC, 0xB800'1234'5678'9ABC, 0xB810'1234'5678'9ABC,

    // (exp >= 874) Boundary case for converting to single
    0x3680'0000'0000'0000,  // 2^(-150) = Unrepresentable in single-prec
    0x36A0'0000'0000'0000,  // 2^(-149) = Smallest single-prec denormal
    0x36B0'0000'0000'0000,  // 2^(-148) = Single-prec denormal
    0xB680'0000'0000'0000,  // -2^(-150) = Unrepresentable in single-prec
    0xB6A0'0000'0000'0000,  // -2^(-149) = Smallest single-prec denormal
    0xB6B0'0000'0000'0000,  // -2^(-148) = Single-prec denormal
    0x3680'1234'5678'9ABC, 0x36A0'1234'5678'9ABC, 0x36B0'1234'5678'9ABC, 0xB680'1234'5678'9ABC,
    0xB6A0'1234'5678'9ABC, 0xB6B0'1234'5678'9ABC,

    // (exp > 1148) Boundary case for fres
    0x47C0'0000'0000'0000,  // 2^125 = fres result is non-zero
    0x47D0'0000'0000'0000,  // 2^126 = fres result is zero
    0xC7C0'0000'0000'0000,  // -2^125 = fres result is non-zero
    0xC7D0'0000'0000'0000,  // -2^126 = fres result is zero

    // (exp < 895) Boundary case for fres
    0x37F0'0000'0000'0000,  // 2^(-128) = fres result is non-max
    0x37E0'0000'0000'0000,  // 2^(-129) = fres result is max
    0xB7F0'0000'0000'0000,  // -2^(-128) = fres result is non-max
    0xB7E0'0000'0000'0000,  // -2^(-129) = fres result is max

    // Some typical numbers
    0x3FF8'0000'0000'0000,  // 1.5
    0x408F'4000'0000'0000,  // 1000
    0xC008'0000'0000'0000,  // -3
};
inline constexpr std::array<u64, 57> kRootOutputs = {

      0x7FF0'0000'0000'0000, 0x617F'FE80'0000'0000, 0x60BF'FE80'0000'0000, 0x5FE0'0008'2C00'0000,
      0x5FDF'FE80'0000'0000, 0x5FDF'FE80'0000'0000, 0x3FEF'FE80'0000'0000, 0x1FF0'0008'2C00'0000,
      0x0000'0000'0000'0000, 0x7FF8'0000'0000'0001, 0x7FFF'FFFF'FFFF'FFFF, 0x7FF8'0000'0000'0000,
      0x7FFF'FFFF'FFFF'FFFF, 0xFFF0'0000'0000'0000, 0x7FF8'0000'0000'0000, 0x7FF8'0000'0000'0000,
      0x7FF8'0000'0000'0000, 0x7FF8'0000'0000'0000, 0x7FF8'0000'0000'0000, 0x7FF8'0000'0000'0000,
      0x7FF8'0000'0000'0000, 0x7FF8'0000'0000'0000, 0xFFF8'0000'0000'0001, 0xFFFF'FFFF'FFFF'FFFF,
      0xFFF8'0000'0000'0000, 0xFFFF'FFFF'FFFF'FFFF, 0x43E6'9FA0'0000'0000, 0x43DF'FE80'0000'0000,
      0x7FF8'0000'0000'0000, 0x7FF8'0000'0000'0000, 0x43E6'9360'6000'0000, 0x43DF'ED30'7000'0000,
      0x7FF8'0000'0000'0000, 0x7FF8'0000'0000'0000, 0x44A6'9FA0'0000'0000, 0x4496'9FA0'0000'0000,
      0x448F'FE80'0000'0000, 0x7FF8'0000'0000'0000, 0x7FF8'0000'0000'0000, 0x7FF8'0000'0000'0000,
      0x44A6'9360'6000'0000, 0x4496'9360'6000'0000, 0x448F'ED30'7000'0000, 0x7FF8'0000'0000'0000,
      0x7FF8'0000'0000'0000, 0x7FF8'0000'0000'0000, 0x3C06'9FA0'0000'0000, 0x3BFF'FE80'0000'0000,
      0x7FF8'0000'0000'0000, 0x7FF8'0000'0000'0000, 0x43EF'FE80'0000'0000, 0x43F6'9FA0'0000'0000,
      0x7FF8'0000'0000'0000, 0x7FF8'0000'0000'0000, 0x3FEA'2040'0000'0000, 0x3FA0'3108'0000'0000,
      0x7FF8'0000'0000'0000};
}  // namespace estimate_reference
