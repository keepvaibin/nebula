#include "galaxy/native_thp_video.h"
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
    if (galaxy::thp::decode_video(input, 16, 16, y, u, v) != 0 ||
        !std::all_of(y.begin(), y.end(), [](auto p) { return p == 128; }) ||
        !std::all_of(u.begin(), u.end(), [](auto p) { return p == 128; }) ||
        !std::all_of(v.begin(), v.end(), [](auto p) { return p == 128; })) return 1;
    for (std::size_t length = 0; length < input.size(); ++length) {
        if (galaxy::thp::decode_video({input.data(), length}, 16, 16, y, u, v) == 0) return 2;
    }
    if (galaxy::thp::decode_video(input, 32, 16, y, u, v) == 0 ||
        galaxy::thp::decode_video(input, 16, 16, std::span(y).first(255), u, v) == 0 ||
        galaxy::thp::decode_video(input, 16, 16, y, u, std::span(v).first(63)) == 0) return 3;
    std::cout << "THP bounds, EOF, tiled constant image and dimension checks passed\n";

    // Every coefficient set, random quantizers: the exact SSE2 and FMA3 kernels give identical planes. DC code "0"
    // is size 4; AC code "1" is run 0 size 4, so each block is 0 + 4 bits, then 63 x (1 + 4 bits).
    std::mt19937 random(0x7468'7031u);
    const bool fma3 = galaxy::host_has_fma3();
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
        if (galaxy::thp::decode_video(image, 16, 16, exact_y, exact_u, exact_v, galaxy::thp::VideoKernel::ExactSse2) != 0) return 18;
        const auto fused = galaxy::thp::decode_video(image, 16, 16, fused_y, fused_u, fused_v, galaxy::thp::VideoKernel::Fma3);
        if (!fma3) {
            if (fused == 0) return 19;
            continue;
        }
        if (fused != 0 || exact_y != fused_y || exact_u != fused_u || exact_v != fused_v) return 20;
        if (std::all_of(exact_y.begin(), exact_y.end(), [&](auto p) { return p == exact_y[0]; })) return 21;
    }
    std::cout << "THP exact SSE2 and FMA3 kernels agree" << (fma3 ? "" : " (no FMA3 here: SSE2 only)") << '\n';
}
