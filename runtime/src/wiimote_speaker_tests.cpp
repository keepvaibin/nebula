#include "galaxy/wiimote_speaker.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <numbers>
#include <span>
#include <vector>

namespace {

using galaxy::input::speaker::AdpcmState;
using galaxy::input::speaker::Config;
using galaxy::input::speaker::Format;
using galaxy::input::speaker::Speaker;

int g_failures = 0;

void expect(bool condition, const char* what, double actual = 0.0) {
    if (!condition) {
        std::fprintf(stderr, "FAIL: %s (actual %g)\n", what, actual);
        ++g_failures;
    }
}

std::int32_t sample_at(const std::vector<std::byte>& stereo, std::size_t index) {
    return static_cast<std::int16_t>(std::to_integer<std::uint16_t>(stereo[index * 2u]) |
                                     (std::to_integer<std::uint16_t>(stereo[index * 2u + 1u]) << 8u));
}

// The game's encoder, WENCGetEncodeData (0x80454E84): high nibble first; `model` receives its decoder model.
std::vector<std::uint8_t> encode(std::span<const std::int16_t> pcm, std::vector<std::int16_t>& model) {
    constexpr std::array<double, 8> kScale{230.0, 230.0, 230.0, 230.0, 307.0, 409.0, 512.0, 614.0};
    std::vector<std::uint8_t> bytes((pcm.size() + 1u) / 2u, 0u);
    std::int32_t predictor = 0;
    std::int32_t step = 127;
    for (std::size_t i = 0; i < pcm.size(); ++i) {
        const bool negative = pcm[i] < predictor;
        std::int32_t left = std::abs(pcm[i] - predictor);
        const std::int32_t half = step / 2;
        const std::int32_t quarter = half / 2;
        unsigned magnitude = 0u;
        if (left >= step) { magnitude |= 4u; left -= step; }
        if (left >= half) { magnitude |= 2u; left -= half; }
        if (left >= quarter) { magnitude |= 1u; left -= quarter; }
        const std::int32_t delta = ((magnitude & 4u) != 0u ? step : 0) + ((magnitude & 2u) != 0u ? half : 0) +
            ((magnitude & 1u) != 0u ? quarter : 0) + quarter / 2;
        predictor = std::clamp(predictor + (negative ? -delta : delta), -32768, 32767);
        step = std::clamp(static_cast<std::int32_t>(step * (kScale[magnitude] / 256.0)), 127, 0x6000);
        bytes[i / 2u] |= static_cast<std::uint8_t>(((negative ? 8u : 0u) | magnitude) << ((i & 1u) != 0u ? 0 : 4));
        model.push_back(static_cast<std::int16_t>(predictor));
    }
    return bytes;
}

std::vector<std::int16_t> sine(double hz, double amplitude, std::size_t count) {
    std::vector<std::int16_t> pcm(count);
    for (std::size_t i = 0; i < count; ++i) {
        pcm[i] = static_cast<std::int16_t>(std::lround(amplitude * std::sin(2.0 * std::numbers::pi * hz * i / 6000.0)));
    }
    return pcm;
}

struct ToneFit {
    double amplitude = 0.0;
    double snr_db = 0.0;
};

// Least-squares fit of a sine of known frequency to out[from, from + count): its amplitude and the ratio of the fit's
// energy to the residual's. Gaps, clicks, offsets and wrong pitch all land in the residual.
ToneFit fit_tone(const std::vector<double>& out, std::size_t from, std::size_t count, double hz, double rate) {
    const double w = 2.0 * std::numbers::pi * hz / rate;
    double ss = 0.0, cc = 0.0, sc = 0.0, ys = 0.0, yc = 0.0;
    for (std::size_t i = from; i < from + count; ++i) {
        const double s = std::sin(w * i);
        const double c = std::cos(w * i);
        ss += s * s; cc += c * c; sc += s * c; ys += out[i] * s; yc += out[i] * c;
    }
    const double det = std::max(ss * cc - sc * sc, 1e-12);
    const double a = (ys * cc - yc * sc) / det;
    const double b = (yc * ss - ys * sc) / det;
    double fit = 0.0, residual = 0.0;
    for (std::size_t i = from; i < from + count; ++i) {
        const double model = a * std::sin(w * i) + b * std::cos(w * i);
        fit += model * model;
        residual += (out[i] - model) * (out[i] - model);
    }
    return {std::hypot(a, b), 10.0 * std::log10(fit / std::max(residual, 1e-9))};
}

// The game's sequence: enable, mute, 0x01 at 0xA20009, 0x08 at 0xA20001, the configuration, play, unmute.
void configure(Speaker& speaker, std::array<std::uint8_t, 7> config) {
    speaker.set_enabled(true);
    speaker.set_muted(true);
    speaker.write_registers(0x09u, std::array<std::uint8_t, 1>{0x01u});
    speaker.write_registers(0x01u, std::array<std::uint8_t, 1>{0x08u});
    speaker.write_registers(0x01u, config);
    speaker.write_registers(0x08u, std::array<std::uint8_t, 1>{0x01u});
    speaker.set_muted(false);
}

// The game's configuration at unity volume.
constexpr std::array<std::uint8_t, 7> kGameConfig{0x00u, 0x00u, 0xD0u, 0x07u, 0x40u, 0x00u, 0x00u};

std::vector<double> run_output(Speaker& speaker, std::size_t frames, float gain) {
    std::vector<std::byte> output(frames * 4u);
    speaker.mix(output, 32000u, gain);
    std::vector<double> mono(frames);
    bool same = true;
    for (std::size_t i = 0; i < frames; ++i) {
        mono[i] = sample_at(output, i * 2u);
        same &= sample_at(output, i * 2u + 1u) == mono[i];
    }
    expect(same, "both channels carry the same sample");
    return mono;
}

// Paced like the game: one 20-byte report per 6.667 ms, the output in 17.5 ms buffers (560 frames at 32 kHz).
// `gain_at(buffer)` picks the volume of each buffer.
template <typename Gain>
std::vector<double> play_paced(Speaker& speaker, std::span<const std::uint8_t> bytes, int buffers, Gain gain_at) {
    std::vector<double> out;
    std::size_t sent = 0;
    for (int buffer = 0; buffer < buffers; ++buffer) {
        const auto reports = static_cast<std::size_t>((buffer + 1) * 17.5 / (20.0 / 3.0));
        const std::size_t due = std::min(bytes.size(), reports * 20u);
        for (; sent < due; sent += 20u) speaker.receive(bytes.subspan(sent, std::min<std::size_t>(20u, due - sent)));
        const std::vector<double> part = run_output(speaker, 560u, gain_at(buffer));
        out.insert(out.end(), part.begin(), part.end());
    }
    return out;
}

void test_config() {
    const Config adpcm = galaxy::input::speaker::parse_config(kGameConfig);
    expect(adpcm.format == Format::Adpcm4 && adpcm.sample_rate == 6000u && adpcm.volume == 0x40u,
           "ADPCM with divider 2000 is 6000 Hz, the rate the game streams at", adpcm.sample_rate);
    const Config pcm = galaxy::input::speaker::parse_config(
        std::array<std::uint8_t, 7>{0x00u, 0x40u, 0x70u, 0x17u, 0x60u, 0x00u, 0x00u});
    expect(pcm.format == Format::Pcm8 && pcm.sample_rate == 2000u && pcm.volume == 0x60u,
           "PCM with divider 6000 is 2000 Hz", pcm.sample_rate);
    const Config stopped = galaxy::input::speaker::parse_config(std::array<std::uint8_t, 7>{});
    expect(stopped.sample_rate == 0u, "a zero divider has no rate");
}

void test_adpcm() {
    AdpcmState state;
    expect(galaxy::input::speaker::decode_adpcm_nibble(state, 0x7u) == 236 && state.step == 304,
           "nibble 7 from rest: 127 + 63 + 31 + 15, step x2.4", state.predictor);
    expect(galaxy::input::speaker::decode_adpcm_nibble(state, 0xFu) == -334 && state.step == 729,
           "nibble 0xF: -(304 + 152 + 76 + 38), step x2.4", state.predictor);
    expect(galaxy::input::speaker::decode_adpcm_nibble(state, 0x0u) == -243 && state.step == 654,
           "nibble 0: +729 / 8, step x0.9", state.predictor);
    for (int i = 0; i < 64; ++i) (void)galaxy::input::speaker::decode_adpcm_nibble(state, 0x7u);
    expect(state.predictor == 32767 && state.step == 24576, "the sample and step saturate", state.step);
    for (int i = 0; i < 128; ++i) (void)galaxy::input::speaker::decode_adpcm_nibble(state, 0x8u);
    expect(state.step == 127, "small nibbles return the step to its minimum", state.step);

    // A full-scale chirp from 100 Hz to 2900 Hz exercises every magnitude and both clamps.
    std::vector<std::int16_t> pcm(12000);
    for (std::size_t i = 0; i < pcm.size(); ++i) {
        const double t = static_cast<double>(i) / 6000.0;
        pcm[i] = static_cast<std::int16_t>(
            std::lround(32767.0 * std::sin(2.0 * std::numbers::pi * (100.0 + 700.0 * t) * t)));
    }
    std::vector<std::int16_t> model;
    const std::vector<std::uint8_t> bytes = encode(pcm, model);
    AdpcmState decoder;
    std::size_t mismatches = 0;
    for (std::size_t i = 0; i < pcm.size(); ++i) {
        const std::uint8_t byte = bytes[i / 2u];
        const auto nibble = static_cast<std::uint8_t>((i & 1u) != 0u ? byte & 0x0Fu : byte >> 4u);
        mismatches += galaxy::input::speaker::decode_adpcm_nibble(decoder, nibble) != model[i] ? 1u : 0u;
    }
    expect(mismatches == 0u, "the decoder reproduces the game encoder's model sample for sample",
           static_cast<double>(mismatches));
}

void test_paced_stream() {
    // A tone streamed at the game's pace comes out as one continuous tone at the same pitch and amplitude. 200 Hz
    // keeps the ADPCM error itself 35 dB down.
    auto speaker = std::make_unique<Speaker>();
    configure(*speaker, kGameConfig);
    std::vector<std::int16_t> model;
    const std::vector<std::uint8_t> bytes = encode(sine(200.0, 16000.0, 6000), model);
    (void)run_output(*speaker, 560u, 1.0f);
    const std::vector<double> out = play_paced(*speaker, bytes, 64, [](int) { return 1.0f; });
    const auto first = static_cast<std::size_t>(
        std::find_if(out.begin(), out.end(), [](double v) { return std::abs(v) > 200.0; }) - out.begin());
    expect(first <= 32000u * 40u / 1000u, "the tone starts within 40 ms", static_cast<double>(first));
    const ToneFit tone = fit_tone(out, first + 320u, 32000u - 640u, 200.0, 32000.0);
    expect(std::abs(tone.amplitude - 16000.0) < 320.0, "the tone keeps its amplitude", tone.amplitude);
    expect(tone.snr_db > 30.0, "the tone has no gaps, clicks or images (dB)", tone.snr_db);
    const std::size_t end = first + 32000u;
    expect(std::all_of(out.begin() + static_cast<std::ptrdiff_t>(end + 320u), out.end(),
                       [](double v) { return v == 0.0; }),
           "the output is silent once the stream has played out");
    expect(speaker->buffered() == 0u, "everything sent was played", speaker->buffered());
}

void test_listening() {
    // At 0% nothing is buffered, but the decoder follows the stream, so raising the volume mid-sound plays the rest
    // of the sound exactly.
    auto speaker = std::make_unique<Speaker>();
    configure(*speaker, kGameConfig);
    std::vector<std::int16_t> model;
    const std::vector<std::uint8_t> bytes = encode(sine(220.0, 20000.0, 6000), model);
    const std::vector<double> out =
        play_paced(*speaker, bytes, 64, [](int buffer) { return buffer < 21 ? 0.0f : 0.5f; });
    expect(std::all_of(out.begin(), out.begin() + 22 * 560, [](double v) { return v == 0.0; }),
           "nothing plays at 0%");
    const ToneFit tone = fit_tone(out, 26u * 560u, 20u * 560u, 220.0, 32000.0);
    expect(std::abs(tone.amplitude - 10000.0) < 200.0, "the sound continues at the chosen volume", tone.amplitude);
    expect(tone.snr_db > 28.0, "the decoder stayed in step while nothing was heard (dB)", tone.snr_db);

    auto muted = std::make_unique<Speaker>();
    configure(*muted, kGameConfig);
    (void)run_output(*muted, 64u, 1.0f);
    muted->set_muted(true);
    for (std::size_t at = 0; at < 200u; at += 20u) muted->receive(std::span<const std::uint8_t>(bytes).subspan(at, 20u));
    expect(muted->buffered() == 0u, "a muted speaker adds nothing", muted->buffered());
    muted->set_muted(false);
    muted->receive(std::span<const std::uint8_t>(bytes).subspan(200u, 20u));
    expect(muted->buffered() == 40u, "an unmuted report adds its 40 samples", muted->buffered());
    (void)run_output(*muted, 64u, 0.0f);
    muted->receive(std::span<const std::uint8_t>(bytes).subspan(220u, 20u));
    expect(muted->buffered() == 40u, "turning the volume to 0 stops buffering", muted->buffered());
}

void test_limits() {
    auto speaker = std::make_unique<Speaker>();
    configure(*speaker, {0x00u, 0x40u, 0xD0u, 0x07u, 0xFFu, 0x00u, 0x00u});
    (void)run_output(*speaker, 64u, 1.0f);
    std::array<std::uint8_t, 20> loud{};
    loud.fill(0x7Fu);
    for (int i = 0; i < 200; ++i) speaker->receive(loud);
    std::vector<std::byte> output(560u * 4u);
    for (std::size_t i = 0; i < output.size(); i += 2u) {
        output[i] = std::byte{0xF0u};
        output[i + 1u] = std::byte{0x7Fu};
    }
    speaker->mix(output, 32000u, 2.0f);
    expect(sample_at(output, 1001) == 32767, "mixing saturates instead of wrapping", sample_at(output, 1001));
    expect(speaker->buffered() <= 6000u * 200u / 1000u, "a backlog is cut to 200 ms", speaker->buffered());

    auto fast = std::make_unique<Speaker>();
    configure(*fast, {0x00u, 0x40u, 0x01u, 0x00u, 0xFFu, 0x00u, 0x00u});
    (void)run_output(*fast, 64u, 1.0f);
    fast->receive(loud);
    expect(fast->buffered() == 0u, "a rate the output cannot play is ignored", fast->buffered());
}

}  // namespace

int main() {
    test_config();
    test_adpcm();
    test_paced_stream();
    test_listening();
    test_limits();
    if (g_failures != 0) {
        std::fprintf(stderr, "%d failure(s)\n", g_failures);
        return EXIT_FAILURE;
    }
    std::puts("wiimote speaker tests passed");
    return EXIT_SUCCESS;
}
