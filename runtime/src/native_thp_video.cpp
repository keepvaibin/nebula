// Adapted from encounter/aurora 3227d76c60e1e782ca576610bce61c9e7744d8be
// lib/dolphin/thp/THPDec.cpp and lib/internal.hpp::ByteReader.
// Copyright (c) 2022 Luke Street. MIT; full notice in THIRD-PARTY-NOTICES.md.
// Galaxy changes: bounded input/planes, validated dimensions, EOF propagation,
// reject restart streams, SDK quarter-IDCT policy, no Aurora runtime dependency.
#include "galaxy/native_thp_video.h"
#include <cstddef>
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
#include <cmath>

namespace {
// Error codes
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

// Whether to reproduce a bug from SDK THP that swaps columns 3 and 4 in _quarterIDCT
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

class BitReader {
public:
  BitReader(std::span<const u8> data, size_t byteOffset) noexcept : mData{data}, mBitPosition{byteOffset * 8} {}
  bool valid() const noexcept { return mValid; }

  u32 read(u8 count) noexcept {
    u32 value = 0;
    for (u8 i = 0; i < count; ++i) {
      const size_t bytePosition = mBitPosition >> 3;
      if (bytePosition >= mData.size()) { mValid = false; return 0; }
      value = (value << 1) | ((mData[bytePosition] >> (7 - (mBitPosition & 7))) & 1);
      ++mBitPosition;
    }
    return value;
  }

  void byte_align() noexcept { mBitPosition = (mBitPosition + 7) & ~size_t{7}; }

private:
  std::span<const u8> mData;
  bool mValid = true;
  size_t mBitPosition;
};

bool decode_huffman(BitReader& reader, const HuffmanTable& table, u8& symbol) noexcept {
  u32 code = 0;
  for (size_t length = 1; length <= 16; ++length) {
    code = (code << 1) | reader.read(1);
    const u32 firstCode = table.firstCodes[length];
    const u32 count = table.counts[length];
    if (reader.valid() && code >= firstCode && code - firstCode < count) {
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

using FloatRow = std::array<float, 8>;

struct EvenHalf {
  float out0, out1, out2, out3;
};

struct OddHalf {
  float out7, out6, out5, out4;
};

EvenHalf aan_even(float sum04, float dif04, float sum26, float dif26) noexcept {
  const float rotated = std::fma(dif26, kSqrt2, -sum26);
  return {sum04 + sum26, dif04 + rotated, dif04 - rotated, sum04 - sum26};
}

OddHalf aan_odd(float z10, float z11, float z12, float z13) noexcept {
  const float out7 = z11 + z13;
  const float z5 = (z10 + z12) * kC2;
  const float out6 = std::fma(-z10, kC2PlusC6, z5) - out7;
  const float out5 = std::fma(z11 - z13, kSqrt2, -out6);
  const float out4 = std::fma(-z12, kC2MinusC6, z5) - out5;
  return {out7, out6, out5, out4};
}

FloatRow combine(const EvenHalf& even, const OddHalf& odd) noexcept {
  return {
      even.out0 + odd.out7, even.out1 + odd.out6, even.out2 + odd.out5, even.out3 + odd.out4,
      even.out3 - odd.out4, even.out2 - odd.out5, even.out1 - odd.out6, even.out0 - odd.out7,
  };
}

FloatRow transform_row(const s16* coefficients, const float* quant) noexcept {
  const float x0 = static_cast<float>(coefficients[0]) * quant[0];
  const float x1 = static_cast<float>(coefficients[1]) * quant[1];
  const float x2 = static_cast<float>(coefficients[2]) * quant[2];
  const float x3 = static_cast<float>(coefficients[3]) * quant[3];
  const auto c4 = static_cast<float>(coefficients[4]);
  const auto c5 = static_cast<float>(coefficients[5]);
  const auto c6 = static_cast<float>(coefficients[6]);
  const auto c7 = static_cast<float>(coefficients[7]);
  const EvenHalf even = aan_even(std::fma(c4, quant[4], x0), std::fma(-c4, quant[4], x0), std::fma(c6, quant[6], x2),
                                 std::fma(-c6, quant[6], x2));
  const OddHalf odd = aan_odd(std::fma(c5, quant[5], -x3), std::fma(c7, quant[7], x1), std::fma(-c7, quant[7], x1),
                              std::fma(c5, quant[5], x3));
  return combine(even, odd);
}

FloatRow transform_row_low4(const FloatRow& x) noexcept {
  const float sum02 = x[0] + x[2];
  const float dif02 = x[0] - x[2];
  const EvenHalf even{sum02, std::fma(x[2], kSqrt2, dif02), std::fma(-x[2], kSqrt2, sum02), dif02};
  const OddHalf odd = aan_odd(-x[3], x[1], x[1], x[3]);
  return combine(even, odd);
}

FloatRow transform_row_dc_ac(float dc, float ac) noexcept {
  const float a1 = std::fma(ac, kC2, -ac);
  const float a2 = std::fma(ac, kSqrt2, -a1);
  const float a3 = std::fma(-ac, kC6, a2);
  if constexpr (kBuggyQuarterIdct) {
    return {dc + ac, dc + a1, dc + a2, dc + a3, dc - a3, dc - a2, dc - a1, dc - ac};
  }
  return {dc + ac, dc + a1, dc + a2, dc - a3, dc + a3, dc - a2, dc - a1, dc - ac};
}

FloatRow transform_column(const FloatRow& x) noexcept {
  const EvenHalf even = aan_even((x[0] + x[4]) + kOutputBias, (x[0] - x[4]) + kOutputBias, x[2] + x[6], x[2] - x[6]);
  const OddHalf odd = aan_odd(x[5] - x[3], x[1] + x[7], x[1] - x[7], x[5] + x[3]);
  return combine(even, odd);
}

size_t row_extent(const s16* coefficients) noexcept {
  size_t extent = 8;
  while (extent > 0 && coefficients[extent - 1] == 0) {
    --extent;
  }
  return extent;
}

std::array<u8, 64> inverse_dct(const std::array<s16, 64>& coefficients,
                               const QuantizationTable& quantization) noexcept {
  std::array<float, 64> workspace{};
  for (size_t row = 0; row < 8; ++row) {
    const s16* rowCoefficients = &coefficients[row * 8];
    const float* rowQuant = &quantization.values[row * 8];
    FloatRow transformed{};
    switch (row_extent(rowCoefficients)) {
    case 0:
    case 1:
      transformed.fill(static_cast<float>(rowCoefficients[0]) * rowQuant[0]);
      break;
    case 2:
      transformed = transform_row_dc_ac(static_cast<float>(rowCoefficients[0]) * rowQuant[0],
                                        static_cast<float>(rowCoefficients[1]) * rowQuant[1]);
      break;
    case 3:
    case 4: {
      FloatRow dequantized{};
      for (size_t column = 0; column < 4; ++column) {
        dequantized[column] = static_cast<float>(rowCoefficients[column]) * rowQuant[column];
      }
      transformed = transform_row_low4(dequantized);
      break;
    }
    default:
      transformed = transform_row(rowCoefficients, rowQuant);
      break;
    }
    std::copy(transformed.begin(), transformed.end(), workspace.begin() + row * 8);
  }

  std::array<u8, 64> output{};
  for (size_t column = 0; column < 8; ++column) {
    FloatRow input{};
    for (size_t row = 0; row < 8; ++row) {
      input[row] = workspace[row * 8 + column];
    }
    const FloatRow transformed = transform_column(input);
    for (size_t row = 0; row < 8; ++row) {
      const float scaled = transformed[row] * 0.125f;
      output[row * 8 + column] = scaled <= 0.0f ? 0 : scaled >= 255.0f ? 255 : static_cast<u8>(scaled);
    }
  }
  return output;
}

void write_block(u8* output, u16 width, u16 height, u16 blockX, u16 blockY, const std::array<u8, 64>& pixels) noexcept {
  const size_t tilesPerRow = (width + 7) / 8;
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

bool decode_and_write_block(BitReader& reader, DecodeContext& context, size_t component, u8* output, u16 width,
                            u16 height, u16 x, u16 y) noexcept {
  std::array<s16, 64> coefficients{};
  if (!decode_block(reader, context, component, coefficients)) {
    return false;
  }
  const QuantizationTable& quantization = context.quantizationTables[context.components[component].quantizationTable];
  if (!quantization.valid) {
    return false;
  }
  write_block(output, width, height, x, y, inverse_dct(coefficients, quantization));
  return true;
}
} // namespace

namespace galaxy::thp {
s32 decode_video(std::span<const u8> file, u16 width, u16 height, std::span<u8> tileY, std::span<u8> tileU, std::span<u8> tileV) {
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
  const u16 chromaWidth = static_cast<u16>((context.width + 1) / 2);
  const u16 chromaHeight = static_cast<u16>((context.height + 1) / 2);

  BitReader bits{file, context.scanOffset};
  const u16 mcuColumns = static_cast<u16>((context.width + 15) / 16);
  const u16 mcuRows = static_cast<u16>((context.height + 15) / 16);
  u32 restartCount = 0;
  for (u16 mcuY = 0; mcuY < mcuRows; ++mcuY) {
    for (u16 mcuX = 0; mcuX < mcuColumns; ++mcuX) {
      const u16 lumaX = static_cast<u16>(mcuX * 16);
      const u16 lumaY = static_cast<u16>(mcuY * 16);
      if (!decode_and_write_block(bits, context, 0, tileY.data(), context.width, context.height, lumaX,
                                  lumaY) ||
          !decode_and_write_block(bits, context, 0, tileY.data(), context.width, context.height,
                                  static_cast<u16>(lumaX + 8), lumaY) ||
          !decode_and_write_block(bits, context, 0, tileY.data(), context.width, context.height, lumaX,
                                  static_cast<u16>(lumaY + 8)) ||
          !decode_and_write_block(bits, context, 0, tileY.data(), context.width, context.height,
                                  static_cast<u16>(lumaX + 8), static_cast<u16>(lumaY + 8)) ||
          !decode_and_write_block(bits, context, 1, tileU.data(), chromaWidth, chromaHeight,
                                  static_cast<u16>(mcuX * 8), static_cast<u16>(mcuY * 8)) ||
          !decode_and_write_block(bits, context, 2, tileV.data(), chromaWidth, chromaHeight,
                                  static_cast<u16>(mcuX * 8), static_cast<u16>(mcuY * 8))) {
        return kBadSyntax;
      }

      if (context.restartInterval != 0 && ++restartCount == context.restartInterval) {
        bits.byte_align();
        restartCount = 0;
        for (Component& component : context.components) {
          component.predictedDc = 0;
        }
      }
    }
  }
  return 0;
}
}
