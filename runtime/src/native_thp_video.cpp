// Adapted from encounter/aurora 3227d76c60e1e782ca576610bce61c9e7744d8be
// lib/dolphin/thp/THPDec.cpp and lib/internal.hpp::ByteReader.
// Copyright (c) 2022 Luke Street. MIT; full notice in THIRD-PARTY-NOTICES.md.
// Changes: bounded input and planes, validated dimensions, EOF propagation, restart streams refused, the SDK's
// quarter-IDCT behaviour, no Aurora runtime, fused multiply-adds without the C runtime (galaxy/fused_multiply_add.h).
#include "galaxy/native_thp_video.h"
#include "galaxy/fused_multiply_add.h"
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
  bool valid = false;
};

struct HuffmanTable {
  std::array<u8, 17> counts{};
  std::array<u16, 17> firstCodes{};
  std::array<u16, 17> symbolOffsets{};
  std::array<u8, 256> symbols{};
  std::array<u8, 256> prefixSymbols{};
  std::array<u8, 256> prefixLengths{};
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
      if (length <= 8) {
        const unsigned suffixBits = 8u - static_cast<unsigned>(length);
        for (unsigned i = 0; i < count; ++i) {
          const unsigned first = (code + i) << suffixBits;
          const unsigned end = std::min(256u, first + (1u << suffixBits));
          for (unsigned prefix = first; prefix < end; ++prefix) {
            // The canonical-code bound above rejects oversubscribed tables;
            // shorter valid codes and their suffix ranges cannot overlap.
            table.prefixLengths[prefix] = static_cast<u8>(length);
            table.prefixSymbols[prefix] = table.symbols[symbolOffset + i];
          }
        }
      }
      code = (code + count) << 1;
      symbolOffset = static_cast<u16>(symbolOffset + count);
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
// the stream holds have been consumed.
class BitReader {
public:
  BitReader(std::span<const u8> data, size_t byteOffset) noexcept
      : mData{data}, mBytePosition{byteOffset},
        mAvailableBits{byteOffset <= data.size() ? (data.size() - byteOffset) * 8 : 0} {}

  bool valid() const noexcept { return mConsumedBits <= mAvailableBits; }

  u32 read(u8 count) noexcept {
    if (count == 0) return 0;
    if (mBitCount < count) refill();
    const u32 value = static_cast<u32>(mCache >> (64u - count));
    mCache <<= count;
    mBitCount -= count;
    mConsumedBits += count;
    return value;
  }

  u32 peek(u8 count) noexcept {
    if (mBitCount < count) refill();
    return static_cast<u32>(mCache >> (64u - count));
  }

  void skip(u8 count) noexcept {
    mCache <<= count;
    mBitCount -= count;
    mConsumedBits += count;
  }

  bool peek_byte(u8& value) noexcept {
    if (!valid() || mAvailableBits - mConsumedBits < 8) return false;
    value = static_cast<u8>(peek(8));
    return true;
  }

private:
  void refill() noexcept {
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
  u8 prefix = 0;
  if (reader.peek_byte(prefix) && table.prefixLengths[prefix] != 0) {
    reader.skip(table.prefixLengths[prefix]);
    symbol = table.prefixSymbols[prefix];
    return true;
  }
  for (u8 length = 1; length <= 16; ++length) {
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

s32 extend_value(u32 value, u8 bitCount) noexcept {
  if (bitCount != 0 && value < (u32{1} << (bitCount - 1))) {
    return static_cast<s32>(value) - static_cast<s32>(u32{1} << bitCount) + 1;
  }
  return static_cast<s32>(value);
}

bool decode_block(BitReader& reader, DecodeContext& context, size_t componentIndex,
                  std::array<s16, 64>& block) noexcept {
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

  size_t coefficient = 1;
  while (coefficient < 64) {
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
    ++coefficient;
  }
  return reader.valid();
}

void write_block(u8* output, u16 width, u16 height, u16 blockX, u16 blockY, const std::array<u8, 64>& pixels) noexcept {
  const size_t tilesPerRow = (width + 7) / 8;
  if ((blockX & 7u) == 0u && blockX + 8u <= width && blockY + 8u <= height) {
    // Whole block: each 8-pixel row is 8 contiguous bytes of one 8x4 tile.
    for (u16 row = 0; row < 8; ++row) {
      const u16 y = static_cast<u16>(blockY + row);
      const size_t tile = static_cast<size_t>(y / 4) * tilesPerRow + blockX / 8;
      std::memcpy(output + tile * 32 + static_cast<size_t>(y & 3) * 8, &pixels[row * 8], 8);
    }
    return;
  }
  for (u16 row = 0; row < 8 && blockY + row < height; ++row) {
    for (u16 column = 0; column < 8 && blockX + column < width; ++column) {
      const u16 x = blockX + column;
      const u16 y = blockY + row;
      const size_t tile = static_cast<size_t>(y / 4) * tilesPerRow + x / 8;
      const size_t offset = tile * 32 + static_cast<size_t>(y & 3) * 8 + (x & 7);
      output[offset] = pixels[row * 8 + column];
    }
  }
}

// One instantiation of native_thp_video_idct.inl per fused multiply-add, both
// bit-identical to the console's paired-single decoder. AArch64 and an x86 CPU
// with FMA3 run the instruction; any other runs the exact SSE2 sequence.
// Neither calls the C runtime's fma, which is a soft-float routine on x86 CPUs
// without FMA3.
namespace exact_sse2 {
[[nodiscard]] inline float madd(float a, float b, float c) noexcept { return galaxy::fused_multiply_add_sse2(a, b, c); }
#include "native_thp_video_idct.inl"
} // namespace exact_sse2

#if GALAXY_FUSED_MULTIPLY_ADD_IS_BASE
namespace fma3 {
[[nodiscard]] inline float madd(float a, float b, float c) noexcept { return galaxy::fused_multiply_add(a, b, c); }
#include "native_thp_video_idct.inl"
} // namespace fma3
#else
#if defined(__clang__)
#pragma clang attribute push(__attribute__((target("fma"))), apply_to = function)
#endif
namespace fma3 {
[[nodiscard]] inline float madd(float a, float b, float c) noexcept { return galaxy::fused_multiply_add_fma3(a, b, c); }
#include "native_thp_video_idct.inl"
} // namespace fma3
#if defined(__clang__)
#pragma clang attribute pop
#endif
#endif
} // namespace

namespace galaxy::thp {
s32 decode_video(std::span<const u8> file, u16 width, u16 height, std::span<u8> tileY, std::span<u8> tileU, std::span<u8> tileV,
                 VideoKernel kernel) {
  if (file.empty()) {
    return kNoInput;
  }
  if (tileY.empty() || tileU.empty() || tileV.empty()) {
    return kNoOutput;
  }

  DecodeContext context{};
  const s32 headerResult = parse_headers(file, context);
  if (headerResult != 0) {
    return headerResult;
  }

  if (context.width != width || context.height != height || width > 4096 || height > 4096 || context.restartInterval != 0) return kBadSyntax;
  auto plane_size = [](u16 w, u16 h) { return ((size_t(w) + 7) / 8) * ((size_t(h) + 3) / 4) * 32; };
  const auto chromaSize = plane_size(static_cast<u16>((width + 1) / 2), static_cast<u16>((height + 1) / 2));
  if (tileY.size() < plane_size(width, height) || tileU.size() < chromaSize || tileV.size() < chromaSize) return kNoOutput;

  const bool instruction = GALAXY_FUSED_MULTIPLY_ADD_IS_BASE || galaxy::host_has_fma3();
  const bool fma = kernel == VideoKernel::Fma3 || (kernel == VideoKernel::Automatic && instruction);
  if (fma && !instruction) return kNoOutput;
  return fma ? fma3::decode_scan(file, context, tileY.data(), tileU.data(), tileV.data())
             : exact_sse2::decode_scan(file, context, tileY.data(), tileU.data(), tileV.data());
}
}
