#include "galaxy/movie_video.h"
#include "galaxy/fused_multiply_add.h"
#include <algorithm>
#include <array>
#include <iostream>
#include <random>
#include <vector>

int main() {
    // Baseline 4:2:0 THP image: six blocks with zero DC and EOB, hence 128.
    std::vector<std::uint8_t> input{0xFF, 0xD8};
    const auto segment = [&input](std::uint8_t marker, std::vector<std::uint8_t> bytes) {
        const auto length = bytes.size() + 2;
        input.insert(input.end(), {0xFF, marker, static_cast<std::uint8_t>(length >> 8), static_cast<std::uint8_t>(length)});
        input.insert(input.end(), bytes.begin(), bytes.end());
    };
    std::vector<std::uint8_t> quant(65, 1); quant[0] = 0;
    segment(0xDB, quant);
    segment(0xC0, {8, 0, 16, 0, 16, 3, 1, 0x22, 0, 2, 0x11, 0, 3, 0x11, 0});
    for (const auto descriptor : {0, 0x10}) {
        std::vector<std::uint8_t> huffman(18, 0);
        huffman[0] = static_cast<std::uint8_t>(descriptor); huffman[1] = 1;
        segment(0xC4, huffman);
    }
    segment(0xDA, {3, 1, 0, 2, 0, 3, 0, 0, 63, 0});
    input.insert(input.end(), {0, 0});
    std::array<std::uint8_t, 256> y{};
    std::array<std::uint8_t, 64> u{}, v{};
    if (galaxy::movie::decode_video(input, 16, 16, y, u, v) != 0 ||
        !std::all_of(y.begin(), y.end(), [](auto p) { return p == 128; }) ||
        !std::all_of(u.begin(), u.end(), [](auto p) { return p == 128; }) ||
        !std::all_of(v.begin(), v.end(), [](auto p) { return p == 128; })) return 1;
    for (std::size_t length = 0; length < input.size(); ++length) {
        if (galaxy::movie::decode_video({input.data(), length}, 16, 16, y, u, v) == 0) return 2;
    }
    if (galaxy::movie::decode_video(input, 32, 16, y, u, v) == 0 ||
        galaxy::movie::decode_video(input, 16, 16, std::span(y).first(255), u, v) == 0 ||
        galaxy::movie::decode_video(input, 16, 16, y, u, std::span(v).first(63)) == 0) return 3;
    std::cout << "THP bounds, EOF, constant image and dimension checks passed\n";

    // Every coefficient set, random quantizers: the exact SSE2 and FMA3 kernels give identical planes. DC code "0"
    // is size 4; AC code "1" is run 0 size 4, so each block is 0 + 4 bits, then 63 x (1 + 4 bits).
    std::mt19937 random(0x7468'7031u);
    // The fused kernel runs where the instruction is: AArch64 always (fmadd), x86 with FMA3.
    const bool fma3 = GALAXY_FUSED_MULTIPLY_ADD_IS_BASE || galaxy::host_has_fma3();
    for (unsigned frame = 0; frame < 200; ++frame) {
        std::vector<std::uint8_t> image{0xFF, 0xD8};
        const auto put = [&image](std::uint8_t marker, std::vector<std::uint8_t> bytes) {
            const auto length = bytes.size() + 2;
            image.insert(image.end(), {0xFF, marker, static_cast<std::uint8_t>(length >> 8), static_cast<std::uint8_t>(length)});
            image.insert(image.end(), bytes.begin(), bytes.end());
        };
        std::vector<std::uint8_t> table(65);
        for (std::size_t i = 1; i < table.size(); ++i) table[i] = static_cast<std::uint8_t>(1 + random() % 64);
        put(0xDB, table);
        put(0xC0, {8, 0, 16, 0, 16, 3, 1, 0x22, 0, 2, 0x11, 0, 3, 0x11, 0});
        std::vector<std::uint8_t> dc(17, 0), ac(17, 0);
        dc[0] = 0x00; dc[1] = 1; dc.push_back(0x04);
        ac[0] = 0x10; ac[1] = 2; ac.push_back(0x00); ac.push_back(0x04);
        put(0xC4, dc);
        put(0xC4, ac);
        put(0xDA, {3, 1, 0, 2, 0, 3, 0, 0, 63, 0});
        std::vector<bool> bits;
        const auto value = [&] { for (int i = 0; i < 4; ++i) bits.push_back((random() & 1u) != 0u); };
        for (int block = 0; block < 6; ++block) {
            bits.push_back(false);
            value();
            for (int coefficient = 1; coefficient < 64; ++coefficient) { bits.push_back(true); value(); }
        }
        for (std::size_t i = 0; i < bits.size(); i += 8) {
            std::uint8_t byte = 0;
            for (std::size_t j = 0; j < 8; ++j) byte = static_cast<std::uint8_t>((byte << 1) | (i + j < bits.size() && bits[i + j] ? 1 : 0));
            image.push_back(byte);
        }
        std::array<std::uint8_t, 256> exact_y{}, fused_y{};
        std::array<std::uint8_t, 64> exact_u{}, exact_v{}, fused_u{}, fused_v{};
        if (galaxy::movie::decode_video(image, 16, 16, exact_y, exact_u, exact_v, galaxy::movie::VideoKernel::ExactSse2) != 0) return 18;
        const auto fused = galaxy::movie::decode_video(image, 16, 16, fused_y, fused_u, fused_v, galaxy::movie::VideoKernel::Fma3);
        if (!fma3) {
            if (fused == 0) return 19;
            continue;
        }
        if (fused != 0 || exact_y != fused_y || exact_u != fused_u || exact_v != fused_v) return 20;
        if (std::all_of(exact_y.begin(), exact_y.end(), [&](auto p) { return p == exact_y[0]; })) return 21;
    }
    std::cout << "THP exact SSE2 and FMA3 kernels agree" << (fma3 ? "" : " (no FMA3 here: SSE2 only)") << '\n';

    // Sparse blocks, as movies have them: the DC alone, short rows, zero runs, and full rows, in 32x32 pictures (four
    // MCUs). AC codes, two bits each: 00 end of block, 01 run 0 size 4, 10 run 3 size 4, 11 sixteen zeros. Both kernels
    // (the row pass in SIMD and the scalar one) and packed and pitched planes give the same texels.
    for (unsigned frame = 0; frame < 400; ++frame) {
        std::vector<std::uint8_t> image{0xFF, 0xD8};
        const auto put = [&image](std::uint8_t marker, std::vector<std::uint8_t> bytes) {
            const auto length = bytes.size() + 2;
            image.insert(image.end(), {0xFF, marker, static_cast<std::uint8_t>(length >> 8), static_cast<std::uint8_t>(length)});
            image.insert(image.end(), bytes.begin(), bytes.end());
        };
        std::vector<std::uint8_t> table(65);
        for (std::size_t i = 1; i < table.size(); ++i) table[i] = static_cast<std::uint8_t>(1 + random() % 32);
        put(0xDB, table);
        put(0xC0, {8, 0, 32, 0, 32, 3, 1, 0x22, 0, 2, 0x11, 0, 3, 0x11, 0});
        std::vector<std::uint8_t> dc(17, 0), ac(17, 0);
        dc[0] = 0x00; dc[1] = 1; dc.push_back(0x04);
        ac[0] = 0x10; ac[2] = 4; ac.push_back(0x00); ac.push_back(0x04); ac.push_back(0x34); ac.push_back(0xF0);
        put(0xC4, dc);
        put(0xC4, ac);
        put(0xDA, {3, 1, 0, 2, 0, 3, 0, 0, 63, 0});
        std::vector<bool> bits;
        const auto value = [&] { for (int i = 0; i < 4; ++i) bits.push_back((random() & 1u) != 0u); };
        const auto code = [&](unsigned c) { bits.push_back((c & 2u) != 0u); bits.push_back((c & 1u) != 0u); };
        for (int block = 0; block < 6 * 4; ++block) {
            bits.push_back(false);
            value();
            // How far this block reaches: often the DC alone, else up to all 63.
            const unsigned shape = random() % 4u;
            const unsigned reach = shape == 0u ? 0u : shape == 1u ? 1u + random() % 9u : shape == 2u ? 1u + random() % 27u : 63u;
            unsigned at = 1;
            while (at <= reach && at < 64) {
                const unsigned pick = random() % 8u;
                if (pick == 0u && at + 16u <= reach && at + 16u < 64u) {
                    code(3);
                    at += 16;
                } else if (pick < 3u && at + 3u <= reach && at + 3u < 64u) {
                    code(2);
                    value();
                    at += 4;
                } else {
                    code(1);
                    value();
                    at += 1;
                }
            }
            if (at < 64) code(0);
        }
        for (std::size_t i = 0; i < bits.size(); i += 8) {
            std::uint8_t byte = 0;
            for (std::size_t j = 0; j < 8; ++j) byte = static_cast<std::uint8_t>((byte << 1) | (i + j < bits.size() && bits[i + j] ? 1 : 0));
            image.push_back(byte);
        }
        std::array<std::uint8_t, 1024> exact_y{}, fused_y{};
        std::array<std::uint8_t, 256> exact_u{}, exact_v{}, fused_u{}, fused_v{};
        if (galaxy::movie::decode_video(image, 32, 32, exact_y, exact_u, exact_v, galaxy::movie::VideoKernel::ExactSse2) != 0) return 22;
        if (fma3) {
            if (galaxy::movie::decode_video(image, 32, 32, fused_y, fused_u, fused_v, galaxy::movie::VideoKernel::Fma3) != 0 ||
                exact_y != fused_y || exact_u != fused_u || exact_v != fused_v) return 23;
        }
        // Pitched: rows 48 and 24 bytes apart; the bytes past each row are left alone.
        std::vector<std::uint8_t> py(48 * 32, 0xA5), pu(24 * 16, 0xA5), pv(24 * 16, 0xA5);
        if (galaxy::movie::decode_video(image, 32, 32, py, pu, pv, galaxy::movie::VideoKernel::Automatic, {48, 24, 24}) != 0) return 24;
        for (std::size_t row = 0; row < 32; ++row) {
            if (!std::equal(exact_y.begin() + row * 32, exact_y.begin() + row * 32 + 32, py.begin() + row * 48)) return 25;
            if (!std::all_of(py.begin() + row * 48 + 32, py.begin() + row * 48 + 48, [](auto b) { return b == 0xA5; })) return 26;
        }
        for (std::size_t row = 0; row < 16; ++row) {
            if (!std::equal(exact_u.begin() + row * 16, exact_u.begin() + row * 16 + 16, pu.begin() + row * 24) ||
                !std::equal(exact_v.begin() + row * 16, exact_v.begin() + row * 16 + 16, pv.begin() + row * 24)) return 27;
        }
        if (galaxy::movie::decode_video(image, 32, 32, std::span(py).first(48 * 31 + 31), pu, pv,
                                        galaxy::movie::VideoKernel::Automatic, {48, 24, 24}) == 0) return 28;
    }
    std::cout << "THP sparse blocks: kernels and pitched planes agree\n";
}
