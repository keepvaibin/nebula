// The inverse DCT and scan loop of movie_video.cpp, which includes this file once per fused multiply-add:
// the including namespace declares `madd`; every other name comes from movie_video.cpp.

using FloatRow = std::array<float, 8>;

struct EvenHalf {
  float out0, out1, out2, out3;
};

struct OddHalf {
  float out7, out6, out5, out4;
};

[[maybe_unused]] EvenHalf aan_even(float sum04, float dif04, float sum26, float dif26) noexcept {
  const float rotated = madd(dif26, kSqrt2, -sum26);
  return {sum04 + sum26, dif04 + rotated, dif04 - rotated, sum04 - sum26};
}

[[maybe_unused]] OddHalf aan_odd(float z10, float z11, float z12, float z13) noexcept {
  const float out7 = z11 + z13;
  const float z5 = (z10 + z12) * kC2;
  const float out6 = madd(-z10, kC2PlusC6, z5) - out7;
  const float out5 = madd(z11 - z13, kSqrt2, -out6);
  const float out4 = madd(-z12, kC2MinusC6, z5) - out5;
  return {out7, out6, out5, out4};
}

[[maybe_unused]] FloatRow combine(const EvenHalf& even, const OddHalf& odd) noexcept {
  return {
      even.out0 + odd.out7, even.out1 + odd.out6, even.out2 + odd.out5, even.out3 + odd.out4,
      even.out3 - odd.out4, even.out2 - odd.out5, even.out1 - odd.out6, even.out0 - odd.out7,
  };
}

[[maybe_unused]] FloatRow transform_row(const s16* coefficients, const float* quant) noexcept {
  const float x0 = static_cast<float>(coefficients[0]) * quant[0];
  const float x1 = static_cast<float>(coefficients[1]) * quant[1];
  const float x2 = static_cast<float>(coefficients[2]) * quant[2];
  const float x3 = static_cast<float>(coefficients[3]) * quant[3];
  const auto c4 = static_cast<float>(coefficients[4]);
  const auto c5 = static_cast<float>(coefficients[5]);
  const auto c6 = static_cast<float>(coefficients[6]);
  const auto c7 = static_cast<float>(coefficients[7]);
  const EvenHalf even = aan_even(madd(c4, quant[4], x0), madd(-c4, quant[4], x0), madd(c6, quant[6], x2),
                                 madd(-c6, quant[6], x2));
  const OddHalf odd = aan_odd(madd(c5, quant[5], -x3), madd(c7, quant[7], x1), madd(-c7, quant[7], x1),
                              madd(c5, quant[5], x3));
  return combine(even, odd);
}

[[maybe_unused]] FloatRow transform_row_low4(const FloatRow& x) noexcept {
  const float sum02 = x[0] + x[2];
  const float dif02 = x[0] - x[2];
  const EvenHalf even{sum02, madd(x[2], kSqrt2, dif02), madd(-x[2], kSqrt2, sum02), dif02};
  const OddHalf odd = aan_odd(-x[3], x[1], x[1], x[3]);
  return combine(even, odd);
}

[[maybe_unused]] FloatRow transform_row_dc_ac(float dc, float ac) noexcept {
  const float a1 = madd(ac, kC2, -ac);
  const float a2 = madd(ac, kSqrt2, -a1);
  const float a3 = madd(-ac, kC6, a2);
  if constexpr (kBuggyQuarterIdct) {
    return {dc + ac, dc + a1, dc + a2, dc + a3, dc - a3, dc - a2, dc - a1, dc - ac};
  } else {
    return {dc + ac, dc + a1, dc + a2, dc - a3, dc + a3, dc - a2, dc - a1, dc - ac};
  }
}

[[maybe_unused]] FloatRow transform_column(const FloatRow& x) noexcept {
  const EvenHalf even = aan_even((x[0] + x[4]) + kOutputBias, (x[0] - x[4]) + kOutputBias, x[2] + x[6], x[2] - x[6]);
  const OddHalf odd = aan_odd(x[5] - x[3], x[1] + x[7], x[1] - x[7], x[5] + x[3]);
  return combine(even, odd);
}

[[maybe_unused]] size_t row_extent(const s16* coefficients) noexcept {
  size_t extent = 8;
  while (extent > 0 && coefficients[extent - 1] == 0) {
    --extent;
  }
  return extent;
}

// One block's inverse DCT into 8 rows of 8 texels, `pitch` bytes apart. `last`: the zigzag index of the block's last
// coefficient.
void inverse_dct(const std::array<s16, 64>& coefficients, const QuantizationTable& quantization, size_t last, u8* out,
                 size_t pitch) noexcept {
  if (last == 0) {
    // The DC alone: every other term of both passes adds or subtracts +0, so the 64 texels are one value.
    const float dc = static_cast<float>(coefficients[0]) * quantization.values[0];
    const float scaled = (dc + kOutputBias) * 0.125f;
    const u8 value = scaled <= 0.0f ? 0 : scaled >= 255.0f ? 255 : static_cast<u8>(scaled);
    for (size_t row = 0; row < 8; ++row) {
      std::memset(out + row * pitch, value, 8);
    }
    return;
  }
#if defined(MOVIE_VIDEO_IDCT_SIMD)
  idct_simd(coefficients.data(), quantization.transposed.data(), out, pitch);
#else
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

  for (size_t column = 0; column < 8; ++column) {
    FloatRow input{};
    for (size_t row = 0; row < 8; ++row) {
      input[row] = workspace[row * 8 + column];
    }
    const FloatRow transformed = transform_column(input);
    for (size_t row = 0; row < 8; ++row) {
      const float scaled = transformed[row] * 0.125f;
      out[row * pitch + column] = scaled <= 0.0f ? 0 : scaled >= 255.0f ? 255 : static_cast<u8>(scaled);
    }
  }
#endif
}

bool decode_and_write_block(BitReader& reader, DecodeContext& context, size_t component, u8* plane, size_t pitch,
                            u16 width, u16 height, u16 x, u16 y) noexcept {
  std::array<s16, 64> coefficients;
  size_t last = 0;
  if (!decode_block(reader, context, component, coefficients, last)) {
    return false;
  }
  const QuantizationTable& quantization = context.quantizationTables[context.components[component].quantizationTable];
  if (!quantization.valid) {
    return false;
  }
  if (x + 8 <= width && y + 8 <= height) {
    inverse_dct(coefficients, quantization, last, plane + static_cast<size_t>(y) * pitch + x, pitch);
    return true;
  }
  // A block over the plane's edge: only its part inside.
  std::array<u8, 64> pixels;
  inverse_dct(coefficients, quantization, last, pixels.data(), 8);
  write_block(plane, pitch, width, height, x, y, pixels);
  return true;
}

s32 decode_scan(std::span<const u8> file, DecodeContext& context, u8* planeY, u8* planeU, u8* planeV,
                const std::array<size_t, 3>& pitches) noexcept {
  const u16 chromaWidth = static_cast<u16>((context.width + 1) / 2);
  const u16 chromaHeight = static_cast<u16>((context.height + 1) / 2);
  BitReader bits{file, context.scanOffset};
  const u16 mcuColumns = static_cast<u16>((context.width + 15) / 16);
  const u16 mcuRows = static_cast<u16>((context.height + 15) / 16);
  const u16 width = context.width;
  const u16 height = context.height;
  for (u16 mcuY = 0; mcuY < mcuRows; ++mcuY) {
    for (u16 mcuX = 0; mcuX < mcuColumns; ++mcuX) {
      const u16 lumaX = static_cast<u16>(mcuX * 16);
      const u16 lumaY = static_cast<u16>(mcuY * 16);
      const u16 chromaX = static_cast<u16>(mcuX * 8);
      const u16 chromaY = static_cast<u16>(mcuY * 8);
      if (!decode_and_write_block(bits, context, 0, planeY, pitches[0], width, height, lumaX, lumaY) ||
          !decode_and_write_block(bits, context, 0, planeY, pitches[0], width, height, static_cast<u16>(lumaX + 8),
                                  lumaY) ||
          !decode_and_write_block(bits, context, 0, planeY, pitches[0], width, height, lumaX,
                                  static_cast<u16>(lumaY + 8)) ||
          !decode_and_write_block(bits, context, 0, planeY, pitches[0], width, height, static_cast<u16>(lumaX + 8),
                                  static_cast<u16>(lumaY + 8)) ||
          !decode_and_write_block(bits, context, 1, planeU, pitches[1], chromaWidth, chromaHeight, chromaX, chromaY) ||
          !decode_and_write_block(bits, context, 2, planeV, pitches[2], chromaWidth, chromaHeight, chromaX, chromaY)) {
        return kBadSyntax;
      }
    }
  }
  return 0;
}
