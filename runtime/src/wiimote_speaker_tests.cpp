// The speaker's sound into the game's output (wiimote_speaker.h): the game's 6 kHz blocks pushed, mixed at 32 kHz.

#include "galaxy/wiimote_speaker.h"

#include <cmath>
#include <cstdint>
#include <iostream>
#include <numbers>
#include <vector>

namespace {

bool expect(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
    }
    return condition;
}

std::int16_t left(const std::vector<std::byte>& out, std::size_t frame) {
    return static_cast<std::int16_t>(std::to_integer<std::uint16_t>(out[frame * 4]) |
                                     std::to_integer<std::uint16_t>(out[frame * 4 + 1]) << 8u);
}

// A 600 Hz tone at 6 kHz (the game's speaker test tone), `blocks` blocks of 40 samples.
void push_tone(galaxy::input::speaker::Speaker& speaker, int blocks) {
    std::vector<std::int16_t> block(40);
    for (int b = 0; b < blocks; ++b) {
        for (int i = 0; i < 40; ++i) {
            block[static_cast<std::size_t>(i)] = static_cast<std::int16_t>(
                std::lrint(8000.0 * std::sin(2.0 * std::numbers::pi * 600.0 * (b * 40 + i) / 6000.0)));
        }
        speaker.push(block, 6000);
    }
}

}  // namespace

int main() {
    bool ok = true;
    using galaxy::input::speaker::Speaker;
    {
        // Silent at volume 0: nothing kept, nothing added.
        Speaker speaker;
        std::vector<std::byte> out(3200 * 4);
        speaker.mix(out, 32000, 0.0f);
        push_tone(speaker, 30);
        speaker.mix(out, 32000, 0.0f);
        bool silent = true;
        for (std::size_t f = 0; f < 3200; ++f) silent &= left(out, f) == 0;
        ok &= expect(silent, "volume 0 adds nothing");
    }
    {
        Speaker speaker;
        std::vector<std::byte> out(3200 * 4);
        speaker.mix(out, 32000, 1.0f);  // starts listening
        push_tone(speaker, 30);         // 0.2 s
        speaker.mix(out, 32000, 1.0f);  // 0.1 s at 32 kHz
        double sum = 0.0;
        int crossings = 0;
        for (std::size_t f = 1000; f < 3200; ++f) {
            const double v = left(out, f);
            sum += v * v;
            crossings += (left(out, f - 1) < 0) != (v < 0) ? 1 : 0;
        }
        const double rms = std::sqrt(sum / 2200.0);
        ok &= expect(rms > 8000.0 / std::sqrt(2.0) * 0.9 && rms < 8000.0 / std::sqrt(2.0) * 1.1, "the tone's level");
        ok &= expect(crossings >= 80 && crossings <= 85, "the tone's pitch (600 Hz: 82.5 crossings in 2200 frames)");
        bool both = true;
        for (std::size_t f = 0; f < 3200; ++f) {
            both &= out[f * 4] == out[f * 4 + 2] && out[f * 4 + 1] == out[f * 4 + 3];
        }
        ok &= expect(both, "both channels");
    }
    std::cout << (ok ? "speaker tests passed" : "speaker tests FAILED") << '\n';
    return ok ? 0 : 1;
}
