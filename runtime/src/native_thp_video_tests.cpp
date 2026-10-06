#include "galaxy/native_thp_video.h"
#include "galaxy/native_thp_boundary_probe.h"
#include <algorithm>
#include <array>
#include <iostream>
#include <vector>

int main() {
    using Probe = galaxy::diagnostics::NativeThpBoundaryProbe;
    using Reason = galaxy::diagnostics::NativeThpBoundaryReason;
    Probe probe{};
    if (probe.begin(false, 2u, 1u, 2u, 3u) != nullptr) return 4;
    probe.complete(false, Reason::Metadata);
    if (probe.attempts != 0u || probe.has_first ||
        probe.first.checked_fields != 0u ||
        std::any_of(probe.reasons.begin(), probe.reasons.end(),
            [](auto count) { return count != 0u; })) return 5;
    auto* first = probe.begin(true, 2u, 83'264u, 0x813399B0u, 0x90000010u);
    if (first == nullptr || first->checked_fields != 1u) return 6;
    first->width = 640u; first->height = 368u;
    first->checked_fields |= 4u;
    probe.complete(true, Reason::Metadata);
    if (!probe.has_first || probe.first.reason != Reason::Metadata ||
        probe.first.components != 0u ||
        (probe.first.checked_fields & 8u) != 0u) return 7;
    if (probe.begin(true, 1u, 90'000u, 4u, 5u) != nullptr) return 8;
    probe.complete(true, Reason::AdmittedNative);
    probe.begin(true, 2u, 90'001u, 6u, 7u);
    probe.complete(true, Reason::AdmittedCompare);
    if (probe.attempts != 3u || probe.first.mode != 2u ||
        probe.first.vi != 83'264u || probe.first.wrapper != 0x813399B0u ||
        probe.reasons[static_cast<std::size_t>(Reason::Metadata)] != 1u ||
        probe.reasons[static_cast<std::size_t>(Reason::AdmittedNative)] != 1u ||
        probe.reasons[static_cast<std::size_t>(Reason::AdmittedCompare)] != 1u)
        return 9;
    probe.begin(true, 2u, 90'002u, 8u, 9u);
    std::uint64_t classified = 0u;
    for (const auto count : probe.reasons) classified += count;
    if (probe.attempts - classified != 1u) return 10;
    std::cout << "THP boundary disabled, first snapshot, reason and unresolved checks passed\n";

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
}
