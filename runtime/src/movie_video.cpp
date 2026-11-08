// Adapted from encounter/aurora 3227d76c60e1e782ca576610bce61c9e7744d8be
// lib/dolphin/thp/THPDec.cpp and lib/internal.hpp::ByteReader.
// Copyright (c) 2022 Luke Street. MIT; full notice in THIRD-PARTY-NOTICES.md.
// Changes: bounded input and planes, validated dimensions, EOF propagation, restart streams refused, the SDK's
// quarter-IDCT behaviour, no Aurora runtime, fused multiply-adds without the C runtime (galaxy/fused_multiply_add.h),
// table-driven Huffman codes with folded AC values, word refills, DC-only blocks, the row pass in SIMD, pitched planes.
#include "galaxy/movie_video.h"
#include "galaxy/fused_multiply_add.h"
#if defined(__aarch64__) || defined(_M_ARM64)
#include <arm_neon.h>
#endif
#include <cstddef>
#include <cstring>
#include <type_traits>
using u8 = std::uint8_t;
using u16 = std::uint16_t;
using u32 = std::uint32_t;
using s16 = std::int16_t;
using s32 = std::int32_t;
using std::size_t;
namespace {
// Minimal bounded subset of Aurora's ByteReader.
class ByteReader {
public:
 explicit ByteReader(std::span<const u8> data) noexcept : data_(data) {}
 size_t offset() const noexcept { return position_; }
 template<class T> bool try_read(T& value) noexcept {
  std::span<const u8> bytes;
  if (!try_take(sizeof(T), bytes)) return false;
  value = 0;
  for (u8 byte : bytes) value = static_cast<T>((value << 8) | byte);
  return true;
 }
 bool try_take(size_t count, std::span<const u8>& bytes) noexcept {
  if (count > data_.size() - position_) return false;
  bytes = data_.subspan(position_, count);
  position_ += count;
  return true;
 }
private:
 std::span<const u8> data_;
 size_t position_ = 0;
};
}


#include <algorithm>
#include <array>

// Each multiply and add rounds on its own; madd is the only fused operation.
#if defined(__clang__)
#pragma clang fp contract(off)
#elif defined(_MSC_VER)
#pragma fp_contract(off)
#endif

namespace {
constexpr s32 kBadSyntax = 3;
constexpr s32 kBadPrecision = 10;
constexpr s32 kUnsupportedMarker = 11;
constexpr s32 kBadComponentCount = 12;
constexpr s32 kMissingHuffmanTable = 15;
constexpr s32 kBadSampling = 19;
constexpr s32 kNoInput = 25;
constexpr s32 kNoOutput = 27;

constexpr std::array<u8, 64> kNaturalOrder{0,  1,  8,  16, 9,  2,  3,  10, 17, 24, 32, 25, 18, 11, 4,  5,
                                           12, 19, 26, 33, 40, 48, 41, 34, 27, 20, 13, 6,  7,  14, 21, 28,
                                           35, 42, 49, 56, 57, 50, 43, 36, 29, 22, 15, 23, 30, 37, 44, 51,
                                           58, 59, 52, 45, 38, 31, 39, 46, 53, 60, 61, 54, 47, 55, 62, 63};

constexpr std::array<double, 8> kAanScale{1.0f, 1.387039845f, 1.306562965f, 1.175875602f,
                                          1.0f, 0.785694958f, 0.541196100f, 0.275899379f};

// AAN butterfly constants
constexpr float kSqrt2 = 1.414213562f;
constexpr float kC2 = 1.847759065f;        // 2 cos(pi/8)
constexpr float kC2MinusC6 = 1.082392200f; // 2 cos(pi/8) - 2 cos(3pi/8)
constexpr float kC2PlusC6 = 2.613125930f;  // 2 cos(pi/8) + 2 cos(3pi/8)
constexpr float kC6 = kC2 - kC2MinusC6;

constexpr float kOutputBias = 128.0f * 8.0f;

// Huffman codes up to this many bits decode by one table lookup.
constexpr unsigned kFastBits = 9;
constexpr unsigned kFastSize = 1u << kFastBits;

// Eight bytes of the stream as one word, the first byte highest (compilers make it one load and a byte reverse).
std::uint64_t load_word(const u8* bytes) noexcept {
  std::uint64_t value = 0;
  for (size_t i = 0; i < 8; ++i) {
    value = (value << 8) | bytes[i];
  }
  return value;
}

s32 extend_value(u32 value, u8 bitCount) noexcept {
  if (bitCount != 0 && value < (u32{1} << (bitCount - 1))) {
    return static_cast<s32>(value) - static_cast<s32>(u32{1} << bitCount) + 1;
  }
  return static_cast<s32>(value);
}

// The SDK's THP _quarterIDCT swaps columns 3 and 4; the decoder does the same.
constexpr bool kBuggyQuarterIdct = true;

bool read_segment(ByteReader& reader, const u8*& data, size_t& size) noexcept {
  u16 encodedSize = 0;
  if (!reader.try_read(encodedSize) || encodedSize < 2) {
    return false;
  }

  std::span<const u8> bytes;
  if (!reader.try_take(encodedSize - 2, bytes)) {
    return false;
  }
  data = bytes.data();
  size = bytes.size();
  return true;
}

struct QuantizationTable {
  std::array<float, 64> values{};
  std::array<float, 64> transposed{};  // values[row * 8 + column] at [column * 8 + row], for the row pass in SIMD
  bool valid = false;
};

struct HuffmanTable {
  std::array<u8, 17> counts{};
  std::array<u16, 17> firstCodes{};
  std::array<u16, 17> symbolOffsets{};
  std::array<u8, 256> symbols{};
  // By the next kFastBits bits: the code's length << 8 | its symbol, 0 for a longer code.
  std::array<u16, kFastSize> fast{};
  // AC tables: a code whose value bits fit in kFastBits too, as value << 16 | run << 8 | code and value bits; else 0.
  std::array<s32, kFastSize> fastAc{};
  bool valid = false;
};

struct Component {
  u8 quantizationTable = 0;
  u8 dcTable = 0;
  u8 acTable = 0;
  s32 predictedDc = 0;
};

struct DecodeContext {
  std::array<QuantizationTable, 3> quantizationTables{};
  std::array<HuffmanTable, 4> huffmanTables{};
  std::array<Component, 3> components{};
  u16 width = 0;
  u16 height = 0;
  u16 restartInterval = 0;
  size_t scanOffset = 0;
};

s32 parse_quantization_tables(const u8* data, size_t size, DecodeContext& context) noexcept {
  size_t position = 0;
  while (position < size) {
    if (size - position < 65) {
      return kBadSyntax;
    }
    const u8 descriptor = data[position++];
    const u8 precision = descriptor >> 4;
    const u8 id = descriptor & 15;
    if (precision != 0 || id >= context.quantizationTables.size()) {
      return kBadSyntax;
    }

    std::array<float, 64> natural{};
    for (size_t i = 0; i < 64; ++i) {
      natural[kNaturalOrder[i]] = static_cast<float>(data[position++]);
    }
    QuantizationTable& table = context.quantizationTables[id];
    for (size_t row = 0; row < 8; ++row) {
      for (size_t column = 0; column < 8; ++column) {
        const size_t index = row * 8 + column;
        table.values[index] =
            static_cast<float>(static_cast<double>(natural[index]) * kAanScale[row] * kAanScale[column]);
        table.transposed[column * 8 + row] = table.values[index];
      }
    }
    table.valid = true;
  }
  return 0;
}

s32 parse_frame_header(const u8* data, size_t size, DecodeContext& context) noexcept {
  if (size < 6) {
    return kBadSyntax;
  }
  if (data[0] != 8) {
    return kBadPrecision;
  }
  context.height = static_cast<u16>((static_cast<u16>(data[1]) << 8) | data[2]);
  context.width = static_cast<u16>((static_cast<u16>(data[3]) << 8) | data[4]);
  if (data[5] != 3) {
    return kBadComponentCount;
  }
  if (size < 15 || context.width == 0 || context.height == 0) {
    return kBadSyntax;
  }

  for (size_t i = 0; i < 3; ++i) {
    const u8 sampling = data[7 + i * 3];
    if ((i == 0 && sampling != 0x22) || (i != 0 && sampling != 0x11)) {
      return kBadSampling;
    }
    const u8 table = data[8 + i * 3];
    if (table >= context.quantizationTables.size()) {
      return kBadSyntax;
    }
    context.components[i].quantizationTable = table;
  }
  return 0;
}

s32 parse_huffman_tables(const u8* data, size_t size, DecodeContext& context) noexcept {
  size_t position = 0;
  while (position < size) {
    if (size - position < 17) {
      return kBadSyntax;
    }
    const u8 descriptor = data[position++];
    const u8 tableClass = descriptor >> 4;
    const u8 id = descriptor & 15;
    if (tableClass > 1 || id > 1) {
      return kBadSyntax;
    }

    HuffmanTable table{};
    size_t symbolCount = 0;
    for (size_t length = 1; length <= 16; ++length) {
      table.counts[length] = data[position++];
      symbolCount += table.counts[length];
    }
    if (symbolCount > table.symbols.size() || symbolCount > size - position) {
      return kBadSyntax;
    }
    std::copy_n(data + position, symbolCount, table.symbols.begin());
    position += symbolCount;

    u32 code = 0;
    u16 symbolOffset = 0;
    for (size_t length = 1; length <= 16; ++length) {
      const u32 count = table.counts[length];
      if (code + count > (u32{1} << length)) {
        return kBadSyntax;
      }
      table.firstCodes[length] = static_cast<u16>(code);
      table.symbolOffsets[length] = symbolOffset;
      if (length <= kFastBits) {
        const unsigned suffixBits = kFastBits - static_cast<unsigned>(length);
        for (unsigned i = 0; i < count; ++i) {
          const unsigned first = (code + i) << suffixBits;
          const unsigned end = std::min(kFastSize, first + (1u << suffixBits));
          for (unsigned prefix = first; prefix < end; ++prefix) {
            // The canonical-code bound above rejects oversubscribed tables;
            // shorter valid codes and their suffix ranges cannot overlap.
            table.fast[prefix] = static_cast<u16>((length << 8) | table.symbols[symbolOffset + i]);
          }
        }
      }
      code = (code + count) << 1;
      symbolOffset = static_cast<u16>(symbolOffset + count);
    }
    if (tableClass == 1) {
      // End of block and the 16-zero run carry no value bits: they stay with the plain lookup.
      for (unsigned prefix = 0; prefix < kFastSize; ++prefix) {
        const unsigned length = table.fast[prefix] >> 8;
        const unsigned symbol = table.fast[prefix] & 0xFFu;
        const unsigned valueBits = symbol & 15u;
        if (length == 0 || valueBits == 0 || length + valueBits > kFastBits) {
          continue;
        }
        const u32 bits = (prefix >> (kFastBits - length - valueBits)) & ((1u << valueBits) - 1u);
        const s32 value = extend_value(bits, static_cast<u8>(valueBits));
        table.fastAc[prefix] = static_cast<s32>((static_cast<u32>(static_cast<u16>(value)) << 16) |
                                                ((symbol >> 4) << 8) | (length + valueBits));
      }
    }
    table.valid = true;
    context.huffmanTables[id * 2 + tableClass] = table;
  }
  return 0;
}

s32 parse_scan_header(const u8* data, size_t size, DecodeContext& context) noexcept {
  if (size < 1) {
    return kBadSyntax;
  }
  if (data[0] != 3) {
    return kBadComponentCount;
  }
  if (size < 10) {
    return kBadSyntax;
  }

  for (size_t i = 0; i < 3; ++i) {
    const u8 selectors = data[2 + i * 2];
    const u8 dcTable = selectors >> 4;
    const u8 acTable = selectors & 15;
    if (dcTable > 1 || acTable > 1 || !context.huffmanTables[dcTable * 2].valid ||
        !context.huffmanTables[acTable * 2 + 1].valid) {
      return kMissingHuffmanTable;
    }
    context.components[i].dcTable = dcTable;
    context.components[i].acTable = acTable;
    context.components[i].predictedDc = 0;
  }
  if (data[7] != 0 || data[8] != 63 || data[9] != 0) {
    return kBadSyntax;
  }
  return 0;
}

s32 parse_headers(std::span<const u8> file, DecodeContext& context) noexcept {
  ByteReader reader{file};
  while (true) {
    u8 prefix = 0;
    if (!reader.try_read(prefix) || prefix != 0xFF) {
      return kBadSyntax;
    }

    u8 marker = 0;
    do {
      if (!reader.try_read(marker)) {
        return kBadSyntax;
      }
    } while (marker == 0xFF);

    if (marker == 0xD8) {
      continue;
    }
    if ((marker >= 0xE0 && marker <= 0xEF) || marker == 0xFE) {
      const u8* ignored = nullptr;
      size_t ignoredSize = 0;
      if (!read_segment(reader, ignored, ignoredSize)) {
        return kBadSyntax;
      }
      continue;
    }

    const u8* segment = nullptr;
    size_t segmentSize = 0;
    switch (marker) {
    case 0xC0: {
      if (!read_segment(reader, segment, segmentSize)) {
        return kBadSyntax;
      }
      if (const s32 result = parse_frame_header(segment, segmentSize, context); result != 0) {
        return result;
      }
      break;
    }
    case 0xC4: {
      if (!read_segment(reader, segment, segmentSize)) {
        return kBadSyntax;
      }
      if (const s32 result = parse_huffman_tables(segment, segmentSize, context); result != 0) {
        return result;
      }
      break;
    }
    case 0xDA: {
      if (!read_segment(reader, segment, segmentSize)) {
        return kBadSyntax;
      }
      if (const s32 result = parse_scan_header(segment, segmentSize, context); result != 0) {
        return result;
      }
      context.scanOffset = reader.offset();
      return 0;
    }
    case 0xDB: {
      if (!read_segment(reader, segment, segmentSize)) {
        return kBadSyntax;
      }
      const s32 result = parse_quantization_tables(segment, segmentSize, context);
      if (result != 0) {
        return result;
      }
      break;
    }
    case 0xDD: {
      if (!read_segment(reader, segment, segmentSize) || segmentSize != 2) {
        return kBadSyntax;
      }
      context.restartInterval = static_cast<u16>((static_cast<u16>(segment[0]) << 8) | segment[1]);
      break;
    }
    default:
      return kUnsupportedMarker;
    }
  }
}

// Bit reader with a 64-bit cache. Bytes past the end read as zero; the reader turns invalid once more bits than
// the stream holds have been consumed. The cache's bits below its count are the stream's next bits or zero, so a
// refill may lay a whole word over them.
class BitReader {
public:
  BitReader(std::span<const u8> data, size_t byteOffset) noexcept
      : mData{data}, mBytePosition{byteOffset},
        mAvailableBits{byteOffset <= data.size() ? (data.size() - byteOffset) * 8 : 0} {}

  bool valid() const noexcept { return mConsumedBits <= mAvailableBits; }

  // The next `count` bits (1 to 32).
  u32 peek(unsigned count) noexcept {
    if (mBitCount < count) refill();
    return static_cast<u32>(mCache >> (64u - count));
  }

  // Up to the bits last peeked.
  void skip(unsigned count) noexcept {
    mCache <<= count;
    mBitCount -= count;
    mConsumedBits += count;
  }

  u32 read(unsigned count) noexcept {
    if (count == 0) return 0;
    const u32 value = peek(count);
    skip(count);
    return value;
  }

private:
  void refill() noexcept {
    if (mBytePosition <= mData.size() && mData.size() - mBytePosition >= 8) {
      mCache |= load_word(mData.data() + mBytePosition) >> mBitCount;
      const unsigned bytes = (63u - mBitCount) >> 3;
      mBytePosition += bytes;
      mBitCount += bytes * 8u;
      return;
    }
    while (mBitCount <= 56) {
      const std::uint64_t byte = mBytePosition < mData.size() ? mData[mBytePosition] : 0u;
      ++mBytePosition;
      mCache |= byte << (56u - mBitCount);
      mBitCount += 8;
    }
  }

  std::span<const u8> mData;
  size_t mBytePosition;
  size_t mAvailableBits;
  size_t mConsumedBits = 0;
  std::uint64_t mCache = 0;
  unsigned mBitCount = 0;
};

bool decode_huffman(BitReader& reader, const HuffmanTable& table, u8& symbol) noexcept {
  const unsigned fast = table.fast[reader.peek(kFastBits)];
  if (fast != 0) {
    reader.skip(fast >> 8);
    symbol = static_cast<u8>(fast);
    return true;
  }
  // Every code of up to kFastBits has its entry: this one is longer.
  for (unsigned length = kFastBits + 1; length <= 16; ++length) {
    const u32 code = reader.peek(length);
    const u32 firstCode = table.firstCodes[length];
    const u32 count = table.counts[length];
    if (code >= firstCode && code - firstCode < count) {
      reader.skip(length);
      if (!reader.valid()) return false;
      symbol = table.symbols[table.symbolOffsets[length] + code - firstCode];
      return true;
    }
  }
  return false;
}

// `last`: the zigzag index of the block's last coefficient (0: the DC alone).
bool decode_block(BitReader& reader, DecodeContext& context, size_t componentIndex,
                  std::array<s16, 64>& block, size_t& last) noexcept {
  block.fill(0);
  Component& component = context.components[componentIndex];
  const HuffmanTable& dcTable = context.huffmanTables[component.dcTable * 2];
  const HuffmanTable& acTable = context.huffmanTables[component.acTable * 2 + 1];

  u8 bitCount = 0;
  if (!decode_huffman(reader, dcTable, bitCount) || bitCount > 11) {
    return false;
  }
  const u32 encodedDifference = reader.read(bitCount);
  const auto dc = static_cast<std::int64_t>(component.predictedDc) + extend_value(encodedDifference, bitCount);
  if (dc < -32768 || dc > 32767) return false;
  component.predictedDc = static_cast<s32>(dc);
  block[0] = static_cast<s16>(component.predictedDc);

  last = 0;
  size_t coefficient = 1;
  while (coefficient < 64) {
    const s32 fast = acTable.fastAc[reader.peek(kFastBits)];
    if (fast != 0) {
      reader.skip(static_cast<unsigned>(fast) & 15u);
      coefficient += (static_cast<unsigned>(fast) >> 8) & 15u;
      if (coefficient >= 64) {
        return false;
      }
      block[kNaturalOrder[coefficient]] = static_cast<s16>(fast >> 16);
      last = coefficient;
      ++coefficient;
      continue;
    }
    u8 runAndSize = 0;
    if (!decode_huffman(reader, acTable, runAndSize)) {
      return false;
    }
    const u8 run = runAndSize >> 4;
    bitCount = runAndSize & 15;
    if (bitCount > 10) return false;
    if (bitCount == 0) {
      if (run == 15) {
        coefficient += 16;
        continue;
      }
      break;
    }

    coefficient += run;
    if (coefficient >= 64) {
      return false;
    }
    const u32 encodedValue = reader.read(bitCount);
    block[kNaturalOrder[coefficient]] = static_cast<s16>(extend_value(encodedValue, bitCount));
    last = coefficient;
    ++coefficient;
  }
  return reader.valid();
}

// The part of an 8x8 block inside a plane (`width` x `height` texels, rows from the top `pitch` bytes apart).
void write_block(u8* output, size_t pitch, u16 width, u16 height, u16 blockX, u16 blockY,
                 const std::array<u8, 64>& pixels) noexcept {
  const u16 columns = static_cast<u16>(std::min<int>(8, width - blockX));
  for (u16 row = 0; row < 8 && blockY + row < height; ++row) {
    std::memcpy(output + static_cast<size_t>(blockY + row) * pitch + blockX, &pixels[row * 8], columns);
  }
}

// One instantiation of movie_video_idct.inl per fused multiply-add, both
// bit-identical to the reference decoder. AArch64 and an x86 CPU
// with FMA3 run the instruction; any other runs the exact SSE2 sequence.
// Neither calls the C runtime's fma, which is a soft-float routine on x86 CPUs
// without FMA3.
namespace exact_sse2 {
[[nodiscard]] inline float madd(float a, float b, float c) noexcept { return galaxy::fused_multiply_add_sse2(a, b, c); }
#include "movie_video_idct.inl"
} // namespace exact_sse2

// Both passes of the inverse DCT, every row or column at once: the same operations in the same order as the scalar
// passes, so the same planes (the kernels test). The vector types and their operations come from each platform.
#if defined(__clang__) && !GALAXY_FUSED_MULTIPLY_ADD_IS_BASE
#pragma clang attribute push(__attribute__((target("avx,fma"))), apply_to = function)
#endif
template <class Vec, class Ops>
inline void aan_odd_lanes(Vec z10, Vec z11, Vec z12, Vec z13, Vec& out7, Vec& out6, Vec& out5, Vec& out4) noexcept {
  out7 = Ops::add(z11, z13);
  const Vec z5 = Ops::mul(Ops::add(z10, z12), Ops::set(kC2));
  out6 = Ops::sub(Ops::madd(Ops::neg(z10), Ops::set(kC2PlusC6), z5), out7);
  out5 = Ops::madd(Ops::sub(z11, z13), Ops::set(kSqrt2), Ops::neg(out6));
  out4 = Ops::sub(Ops::madd(Ops::neg(z12), Ops::set(kC2MinusC6), z5), out5);
}

template <class Vec, class Ops>
inline void combine_lanes(Vec e0, Vec e1, Vec e2, Vec e3, Vec o7, Vec o6, Vec o5, Vec o4, Vec* out) noexcept {
  out[0] = Ops::add(e0, o7);
  out[1] = Ops::add(e1, o6);
  out[2] = Ops::add(e2, o5);
  out[3] = Ops::add(e3, o4);
  out[4] = Ops::sub(e3, o4);
  out[5] = Ops::sub(e2, o5);
  out[6] = Ops::sub(e1, o6);
  out[7] = Ops::sub(e0, o7);
}

// The row pass, a lane per row. Every case of the scalar pass (inverse_dct's switch on the row's extent) runs with its
// own operations, then each lane takes the one its row's extent picks: the numbers transform_row, transform_row_low4,
// transform_row_dc_ac or the fill give that row. c[k], q[k]: each row's coefficient k and its quantizer; at2, at3,
// at5: the lanes whose row reaches coefficient 1, 2 or 4 (extent 2, 3, 5 or more). w[k]: each row's output k.
template <class Vec, class Ops>
inline void transform_rows(const Vec* c, const Vec* q, Vec at2, Vec at3, Vec at5, Vec* w) noexcept {
  const Vec x0 = Ops::mul(c[0], q[0]);
  const Vec x1 = Ops::mul(c[1], q[1]);
  const Vec x2 = Ops::mul(c[2], q[2]);
  const Vec x3 = Ops::mul(c[3], q[3]);
  const Vec sqrt2 = Ops::set(kSqrt2);

  // Extent 5 to 8: transform_row.
  Vec full[8];
  {
    const Vec sum04 = Ops::madd(c[4], q[4], x0);
    const Vec dif04 = Ops::madd(Ops::neg(c[4]), q[4], x0);
    const Vec sum26 = Ops::madd(c[6], q[6], x2);
    const Vec dif26 = Ops::madd(Ops::neg(c[6]), q[6], x2);
    const Vec rotated = Ops::madd(dif26, sqrt2, Ops::neg(sum26));
    Vec o7, o6, o5, o4;
    aan_odd_lanes<Vec, Ops>(Ops::madd(c[5], q[5], Ops::neg(x3)), Ops::madd(c[7], q[7], x1),
                            Ops::madd(Ops::neg(c[7]), q[7], x1), Ops::madd(c[5], q[5], x3), o7, o6, o5, o4);
    combine_lanes<Vec, Ops>(Ops::add(sum04, sum26), Ops::add(dif04, rotated), Ops::sub(dif04, rotated),
                            Ops::sub(sum04, sum26), o7, o6, o5, o4, full);
  }

  // Extent 3 or 4: transform_row_low4.
  Vec low[8];
  {
    const Vec sum02 = Ops::add(x0, x2);
    const Vec dif02 = Ops::sub(x0, x2);
    Vec o7, o6, o5, o4;
    aan_odd_lanes<Vec, Ops>(Ops::neg(x3), x1, x1, x3, o7, o6, o5, o4);
    combine_lanes<Vec, Ops>(sum02, Ops::madd(x2, sqrt2, dif02), Ops::madd(Ops::neg(x2), sqrt2, sum02), dif02, o7, o6,
                            o5, o4, low);
  }

  // Extent 2: transform_row_dc_ac.
  Vec pair[8];
  {
    const Vec a1 = Ops::madd(x1, Ops::set(kC2), Ops::neg(x1));
    const Vec a2 = Ops::madd(x1, sqrt2, Ops::neg(a1));
    const Vec a3 = Ops::madd(Ops::neg(x1), Ops::set(kC6), a2);
    pair[0] = Ops::add(x0, x1);
    pair[1] = Ops::add(x0, a1);
    pair[2] = Ops::add(x0, a2);
    if constexpr (kBuggyQuarterIdct) {
      pair[3] = Ops::add(x0, a3);
      pair[4] = Ops::sub(x0, a3);
    } else {
      pair[3] = Ops::sub(x0, a3);
      pair[4] = Ops::add(x0, a3);
    }
    pair[5] = Ops::sub(x0, a2);
    pair[6] = Ops::sub(x0, a1);
    pair[7] = Ops::sub(x0, x1);
  }

  // Extent 0 or 1: the dequantized DC across.
  for (size_t k = 0; k < 8; ++k) {
    w[k] = Ops::select(at5, full[k], Ops::select(at3, low[k], Ops::select(at2, pair[k], x0)));
  }
}

// The column pass: x[row] holds that row's eight columns.
template <class Vec, class Ops>
inline void transform_columns(const Vec* x, Vec* out) noexcept {
  const Vec bias = Ops::set(kOutputBias);
  const Vec sum04 = Ops::add(Ops::add(x[0], x[4]), bias);
  const Vec dif04 = Ops::add(Ops::sub(x[0], x[4]), bias);
  const Vec sum26 = Ops::add(x[2], x[6]);
  const Vec dif26 = Ops::sub(x[2], x[6]);
  const Vec rotated = Ops::madd(dif26, Ops::set(kSqrt2), Ops::neg(sum26));
  Vec o7, o6, o5, o4;
  aan_odd_lanes<Vec, Ops>(Ops::sub(x[5], x[3]), Ops::add(x[1], x[7]), Ops::sub(x[1], x[7]), Ops::add(x[5], x[3]), o7,
                          o6, o5, o4);
  combine_lanes<Vec, Ops>(Ops::add(sum04, sum26), Ops::add(dif04, rotated), Ops::sub(dif04, rotated),
                          Ops::sub(sum04, sum26), o7, o6, o5, o4, out);
}
#if defined(__clang__) && !GALAXY_FUSED_MULTIPLY_ADD_IS_BASE
#pragma clang attribute pop
#endif

#define MOVIE_VIDEO_IDCT_SIMD 1
#if GALAXY_FUSED_MULTIPLY_ADD_IS_BASE
// Eight lanes as two NEON registers: lo the first four.
struct Lanes8 {
  float32x4_t lo;
  float32x4_t hi;
};
struct NeonOps {
  static Lanes8 set(float v) noexcept {
    const float32x4_t s = vdupq_n_f32(v);
    return {s, s};
  }
  static Lanes8 add(Lanes8 a, Lanes8 b) noexcept { return {vaddq_f32(a.lo, b.lo), vaddq_f32(a.hi, b.hi)}; }
  static Lanes8 sub(Lanes8 a, Lanes8 b) noexcept { return {vsubq_f32(a.lo, b.lo), vsubq_f32(a.hi, b.hi)}; }
  static Lanes8 mul(Lanes8 a, Lanes8 b) noexcept { return {vmulq_f32(a.lo, b.lo), vmulq_f32(a.hi, b.hi)}; }
  static Lanes8 neg(Lanes8 a) noexcept { return {vnegq_f32(a.lo), vnegq_f32(a.hi)}; }
  static Lanes8 madd(Lanes8 a, Lanes8 b, Lanes8 c) noexcept {
    return {vfmaq_f32(c.lo, a.lo, b.lo), vfmaq_f32(c.hi, a.hi, b.hi)};
  }
  // a where the mask's lane is all ones, else b.
  static Lanes8 select(Lanes8 mask, Lanes8 a, Lanes8 b) noexcept {
    return {vbslq_f32(vreinterpretq_u32_f32(mask.lo), a.lo, b.lo), vbslq_f32(vreinterpretq_u32_f32(mask.hi), a.hi, b.hi)};
  }
};
namespace fma3 {
[[nodiscard]] inline float madd(float a, float b, float c) noexcept { return galaxy::fused_multiply_add(a, b, c); }

// A row-major 8x8 block's columns: t[k] lane r is row r's coefficient k.
inline void transpose_block(const s16* in, int16x8_t* t) noexcept {
  int16x8_t r[8];
  for (size_t row = 0; row < 8; ++row) r[row] = vld1q_s16(in + row * 8);
  const int16x8x2_t a01 = vtrnq_s16(r[0], r[1]);
  const int16x8x2_t a23 = vtrnq_s16(r[2], r[3]);
  const int16x8x2_t a45 = vtrnq_s16(r[4], r[5]);
  const int16x8x2_t a67 = vtrnq_s16(r[6], r[7]);
  const int32x4x2_t b02 = vtrnq_s32(vreinterpretq_s32_s16(a01.val[0]), vreinterpretq_s32_s16(a23.val[0]));
  const int32x4x2_t b13 = vtrnq_s32(vreinterpretq_s32_s16(a01.val[1]), vreinterpretq_s32_s16(a23.val[1]));
  const int32x4x2_t b46 = vtrnq_s32(vreinterpretq_s32_s16(a45.val[0]), vreinterpretq_s32_s16(a67.val[0]));
  const int32x4x2_t b57 = vtrnq_s32(vreinterpretq_s32_s16(a45.val[1]), vreinterpretq_s32_s16(a67.val[1]));
  const auto join_low = [](int32x4_t top, int32x4_t bottom) {
    return vcombine_s16(vget_low_s16(vreinterpretq_s16_s32(top)), vget_low_s16(vreinterpretq_s16_s32(bottom)));
  };
  const auto join_high = [](int32x4_t top, int32x4_t bottom) {
    return vcombine_s16(vget_high_s16(vreinterpretq_s16_s32(top)), vget_high_s16(vreinterpretq_s16_s32(bottom)));
  };
  t[0] = join_low(b02.val[0], b46.val[0]);
  t[1] = join_low(b13.val[0], b57.val[0]);
  t[2] = join_low(b02.val[1], b46.val[1]);
  t[3] = join_low(b13.val[1], b57.val[1]);
  t[4] = join_high(b02.val[0], b46.val[0]);
  t[5] = join_high(b13.val[0], b57.val[0]);
  t[6] = join_high(b02.val[1], b46.val[1]);
  t[7] = join_high(b13.val[1], b57.val[1]);
}

// The lanes of a 16-bit mask (each all ones or zero) as 32-bit masks.
inline Lanes8 widen_mask(uint16x8_t mask) noexcept {
  const int16x8_t s = vreinterpretq_s16_u16(mask);
  return {vreinterpretq_f32_s32(vmovl_s16(vget_low_s16(s))), vreinterpretq_f32_s32(vmovl_s16(vget_high_s16(s)))};
}

inline Lanes8 nonzero(int16x8_t v) noexcept { return widen_mask(vmvnq_u16(vceqq_s16(v, vdupq_n_s16(0)))); }

inline Lanes8 to_float(int16x8_t v) noexcept {
  return {vcvtq_f32_s32(vmovl_s16(vget_low_s16(v))), vcvtq_f32_s32(vmovl_s16(vget_high_s16(v)))};
}

inline void transpose4(float32x4_t a, float32x4_t b, float32x4_t c, float32x4_t d, float32x4_t* out) noexcept {
  const float32x4x2_t ab = vtrnq_f32(a, b);
  const float32x4x2_t cd = vtrnq_f32(c, d);
  out[0] = vcombine_f32(vget_low_f32(ab.val[0]), vget_low_f32(cd.val[0]));
  out[1] = vcombine_f32(vget_low_f32(ab.val[1]), vget_low_f32(cd.val[1]));
  out[2] = vcombine_f32(vget_high_f32(ab.val[0]), vget_high_f32(cd.val[0]));
  out[3] = vcombine_f32(vget_high_f32(ab.val[1]), vget_high_f32(cd.val[1]));
}

// w[k] lane r (row r's output k) to rows[r] lane k.
inline void rows_of(const Lanes8* w, Lanes8* rows) noexcept {
  float32x4_t q[4];
  transpose4(w[0].lo, w[1].lo, w[2].lo, w[3].lo, q);
  for (size_t r = 0; r < 4; ++r) rows[r].lo = q[r];
  transpose4(w[4].lo, w[5].lo, w[6].lo, w[7].lo, q);
  for (size_t r = 0; r < 4; ++r) rows[r].hi = q[r];
  transpose4(w[0].hi, w[1].hi, w[2].hi, w[3].hi, q);
  for (size_t r = 0; r < 4; ++r) rows[4 + r].lo = q[r];
  transpose4(w[4].hi, w[5].hi, w[6].hi, w[7].hi, q);
  for (size_t r = 0; r < 4; ++r) rows[4 + r].hi = q[r];
}

inline void idct_simd(const s16* coefficients, const float* quantization, u8* out, size_t pitch) noexcept {
  int16x8_t t[8];
  transpose_block(coefficients, t);
  const int16x8_t any5 = vorrq_s16(vorrq_s16(t[4], t[5]), vorrq_s16(t[6], t[7]));
  const int16x8_t any3 = vorrq_s16(any5, vorrq_s16(t[2], t[3]));
  const int16x8_t any2 = vorrq_s16(any3, t[1]);
  Lanes8 c[8];
  Lanes8 q[8];
  for (size_t k = 0; k < 8; ++k) {
    c[k] = to_float(t[k]);
    q[k] = {vld1q_f32(quantization + k * 8), vld1q_f32(quantization + k * 8 + 4)};
  }
  Lanes8 w[8];
  transform_rows<Lanes8, NeonOps>(c, q, nonzero(any2), nonzero(any3), nonzero(any5), w);
  Lanes8 rows[8];
  rows_of(w, rows);
  Lanes8 x[8];
  transform_columns<Lanes8, NeonOps>(rows, x);
  const float32x4_t zero = vdupq_n_f32(0.0f);
  const float32x4_t top = vdupq_n_f32(255.0f);
  for (size_t row = 0; row < 8; ++row) {
    const float32x4_t lo = vminq_f32(vmaxq_f32(vmulq_n_f32(x[row].lo, 0.125f), zero), top);
    const float32x4_t hi = vminq_f32(vmaxq_f32(vmulq_n_f32(x[row].hi, 0.125f), zero), top);
    const uint16x8_t words = vcombine_u16(vmovn_u32(vcvtq_u32_f32(lo)), vmovn_u32(vcvtq_u32_f32(hi)));
    vst1_u8(out + row * pitch, vmovn_u16(words));
  }
}
#include "movie_video_idct.inl"
} // namespace fma3
#else
#if defined(__clang__)
#pragma clang attribute push(__attribute__((target("avx,fma"))), apply_to = function)
#endif
struct AvxOps {
  static __m256 set(float v) noexcept { return _mm256_set1_ps(v); }
  static __m256 add(__m256 a, __m256 b) noexcept { return _mm256_add_ps(a, b); }
  static __m256 sub(__m256 a, __m256 b) noexcept { return _mm256_sub_ps(a, b); }
  static __m256 mul(__m256 a, __m256 b) noexcept { return _mm256_mul_ps(a, b); }
  static __m256 neg(__m256 a) noexcept { return _mm256_xor_ps(a, _mm256_set1_ps(-0.0f)); }
  static __m256 madd(__m256 a, __m256 b, __m256 c) noexcept { return _mm256_fmadd_ps(a, b, c); }
  // a where the mask's lane is all ones, else b.
  static __m256 select(__m256 mask, __m256 a, __m256 b) noexcept { return _mm256_blendv_ps(b, a, mask); }
};
namespace fma3 {
[[nodiscard]] inline float madd(float a, float b, float c) noexcept { return galaxy::fused_multiply_add_fma3(a, b, c); }

// A row-major 8x8 block's columns: t[k] lane r is row r's coefficient k.
inline void transpose_block(const s16* in, __m128i* t) noexcept {
  __m128i r[8];
  for (size_t row = 0; row < 8; ++row) r[row] = _mm_loadu_si128(reinterpret_cast<const __m128i*>(in + row * 8));
  const __m128i a0 = _mm_unpacklo_epi16(r[0], r[1]);
  const __m128i a1 = _mm_unpackhi_epi16(r[0], r[1]);
  const __m128i a2 = _mm_unpacklo_epi16(r[2], r[3]);
  const __m128i a3 = _mm_unpackhi_epi16(r[2], r[3]);
  const __m128i a4 = _mm_unpacklo_epi16(r[4], r[5]);
  const __m128i a5 = _mm_unpackhi_epi16(r[4], r[5]);
  const __m128i a6 = _mm_unpacklo_epi16(r[6], r[7]);
  const __m128i a7 = _mm_unpackhi_epi16(r[6], r[7]);
  const __m128i b0 = _mm_unpacklo_epi32(a0, a2);
  const __m128i b1 = _mm_unpackhi_epi32(a0, a2);
  const __m128i b2 = _mm_unpacklo_epi32(a1, a3);
  const __m128i b3 = _mm_unpackhi_epi32(a1, a3);
  const __m128i b4 = _mm_unpacklo_epi32(a4, a6);
  const __m128i b5 = _mm_unpackhi_epi32(a4, a6);
  const __m128i b6 = _mm_unpacklo_epi32(a5, a7);
  const __m128i b7 = _mm_unpackhi_epi32(a5, a7);
  t[0] = _mm_unpacklo_epi64(b0, b4);
  t[1] = _mm_unpackhi_epi64(b0, b4);
  t[2] = _mm_unpacklo_epi64(b1, b5);
  t[3] = _mm_unpackhi_epi64(b1, b5);
  t[4] = _mm_unpacklo_epi64(b2, b6);
  t[5] = _mm_unpackhi_epi64(b2, b6);
  t[6] = _mm_unpacklo_epi64(b3, b7);
  t[7] = _mm_unpackhi_epi64(b3, b7);
}

inline __m256 join(__m128i lo, __m128i hi) noexcept {
  return _mm256_castsi256_ps(_mm256_insertf128_si256(_mm256_castsi128_si256(lo), hi, 1));
}

// The lanes whose 16-bit value is not zero, as 32-bit masks.
inline __m256 nonzero(__m128i v) noexcept {
  const __m128i mask = _mm_xor_si128(_mm_cmpeq_epi16(v, _mm_setzero_si128()), _mm_set1_epi32(-1));
  return join(_mm_unpacklo_epi16(mask, mask), _mm_unpackhi_epi16(mask, mask));
}

inline __m256 to_float(__m128i v) noexcept {
  return _mm256_cvtepi32_ps(_mm256_castps_si256(
      join(_mm_srai_epi32(_mm_unpacklo_epi16(v, v), 16), _mm_srai_epi32(_mm_unpackhi_epi16(v, v), 16))));
}

// w[k] lane r (row r's output k) to rows[r] lane k.
inline void rows_of(const __m256* w, __m256* rows) noexcept {
  const __m256 t0 = _mm256_unpacklo_ps(w[0], w[1]);
  const __m256 t1 = _mm256_unpackhi_ps(w[0], w[1]);
  const __m256 t2 = _mm256_unpacklo_ps(w[2], w[3]);
  const __m256 t3 = _mm256_unpackhi_ps(w[2], w[3]);
  const __m256 t4 = _mm256_unpacklo_ps(w[4], w[5]);
  const __m256 t5 = _mm256_unpackhi_ps(w[4], w[5]);
  const __m256 t6 = _mm256_unpacklo_ps(w[6], w[7]);
  const __m256 t7 = _mm256_unpackhi_ps(w[6], w[7]);
  const __m256 s0 = _mm256_shuffle_ps(t0, t2, _MM_SHUFFLE(1, 0, 1, 0));
  const __m256 s1 = _mm256_shuffle_ps(t0, t2, _MM_SHUFFLE(3, 2, 3, 2));
  const __m256 s2 = _mm256_shuffle_ps(t1, t3, _MM_SHUFFLE(1, 0, 1, 0));
  const __m256 s3 = _mm256_shuffle_ps(t1, t3, _MM_SHUFFLE(3, 2, 3, 2));
  const __m256 s4 = _mm256_shuffle_ps(t4, t6, _MM_SHUFFLE(1, 0, 1, 0));
  const __m256 s5 = _mm256_shuffle_ps(t4, t6, _MM_SHUFFLE(3, 2, 3, 2));
  const __m256 s6 = _mm256_shuffle_ps(t5, t7, _MM_SHUFFLE(1, 0, 1, 0));
  const __m256 s7 = _mm256_shuffle_ps(t5, t7, _MM_SHUFFLE(3, 2, 3, 2));
  rows[0] = _mm256_permute2f128_ps(s0, s4, 0x20);
  rows[1] = _mm256_permute2f128_ps(s1, s5, 0x20);
  rows[2] = _mm256_permute2f128_ps(s2, s6, 0x20);
  rows[3] = _mm256_permute2f128_ps(s3, s7, 0x20);
  rows[4] = _mm256_permute2f128_ps(s0, s4, 0x31);
  rows[5] = _mm256_permute2f128_ps(s1, s5, 0x31);
  rows[6] = _mm256_permute2f128_ps(s2, s6, 0x31);
  rows[7] = _mm256_permute2f128_ps(s3, s7, 0x31);
}

inline void idct_simd(const s16* coefficients, const float* quantization, u8* out, size_t pitch) noexcept {
  __m128i t[8];
  transpose_block(coefficients, t);
  const __m128i any5 = _mm_or_si128(_mm_or_si128(t[4], t[5]), _mm_or_si128(t[6], t[7]));
  const __m128i any3 = _mm_or_si128(any5, _mm_or_si128(t[2], t[3]));
  const __m128i any2 = _mm_or_si128(any3, t[1]);
  __m256 c[8];
  __m256 q[8];
  for (size_t k = 0; k < 8; ++k) {
    c[k] = to_float(t[k]);
    q[k] = _mm256_loadu_ps(quantization + k * 8);
  }
  __m256 w[8];
  transform_rows<__m256, AvxOps>(c, q, nonzero(any2), nonzero(any3), nonzero(any5), w);
  __m256 rows[8];
  rows_of(w, rows);
  __m256 x[8];
  transform_columns<__m256, AvxOps>(rows, x);
  for (size_t row = 0; row < 8; ++row) {
    const __m256 scaled = _mm256_min_ps(_mm256_max_ps(_mm256_mul_ps(x[row], _mm256_set1_ps(0.125f)), _mm256_setzero_ps()), _mm256_set1_ps(255.0f));
    const __m256i words = _mm256_cvttps_epi32(scaled);
    const __m128i packed = _mm_packs_epi32(_mm256_castsi256_si128(words), _mm256_extractf128_si256(words, 1));
    _mm_storel_epi64(reinterpret_cast<__m128i*>(out + row * pitch), _mm_packus_epi16(packed, packed));
  }
  _mm256_zeroupper();
}
#include "movie_video_idct.inl"
} // namespace fma3
#if defined(__clang__)
#pragma clang attribute pop
#endif
#endif
#undef MOVIE_VIDEO_IDCT_SIMD
} // namespace

namespace galaxy::movie {
s32 decode_video(std::span<const u8> file, u16 width, u16 height, std::span<u8> planeY, std::span<u8> planeU, std::span<u8> planeV,
                 VideoKernel kernel, std::array<std::uint32_t, 3> pitches) {
  if (file.empty()) {
    return kNoInput;
  }
  if (planeY.empty() || planeU.empty() || planeV.empty()) {
    return kNoOutput;
  }

  DecodeContext context{};
  const s32 headerResult = parse_headers(file, context);
  if (headerResult != 0) {
    return headerResult;
  }

  if (context.width != width || context.height != height || width > 4096 || height > 4096 || context.restartInterval != 0) return kBadSyntax;
  const size_t chromaWidth = size_t(width + 1) / 2;
  const size_t chromaHeight = size_t(height + 1) / 2;
  const std::array<size_t, 3> pitch{pitches[0] != 0 ? pitches[0] : width, pitches[1] != 0 ? pitches[1] : chromaWidth,
                                    pitches[2] != 0 ? pitches[2] : chromaWidth};
  const auto holds = [](std::span<u8> plane, size_t rowPitch, size_t columns, size_t rows) {
    return rowPitch >= columns && plane.size() >= rowPitch * (rows - 1) + columns;
  };
  if (!holds(planeY, pitch[0], width, height) || !holds(planeU, pitch[1], chromaWidth, chromaHeight) ||
      !holds(planeV, pitch[2], chromaWidth, chromaHeight)) return kNoOutput;

  const bool instruction = GALAXY_FUSED_MULTIPLY_ADD_IS_BASE || galaxy::host_has_fma3();
  const bool fma = kernel == VideoKernel::Fma3 || (kernel == VideoKernel::Automatic && instruction);
  if (fma && !instruction) return kNoOutput;
  return fma ? fma3::decode_scan(file, context, planeY.data(), planeU.data(), planeV.data(), pitch)
             : exact_sse2::decode_scan(file, context, planeY.data(), planeU.data(), planeV.data(), pitch);
}
}
