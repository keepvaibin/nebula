#include "galaxy/jaudio_host_mixer.h"

#include <array>
#include <cstdio>
#include <cstdlib>
#include <vector>

namespace {

using galaxy::host::JAudioGuestMemory;
using galaxy::host::JAudioHostMixer;

int g_failures = 0;

void expect(bool condition, const char* what, long long actual = 0) {
    if (!condition) {
        std::fprintf(stderr, "FAIL: %s (actual %lld)\n", what, actual);
        ++g_failures;
    }
}

// Flat MEM1 image at 0x80000000.
struct Guest {
    // Up to 0x80700000: JASDSPChannel::sDspChannels is at 0x806A2BD0.
    std::vector<std::byte> bytes = std::vector<std::byte>(0x700000);

    static std::byte* resolve(void* user, std::uint32_t address, std::uint32_t size) {
        auto& guest = *static_cast<Guest*>(user);
        const std::uint32_t offset = address - 0x80000000u;
        if (address < 0x80000000u || offset + size > guest.bytes.size()) {
            return nullptr;
        }
        return guest.bytes.data() + offset;
    }
    [[nodiscard]] JAudioGuestMemory memory() { return {this, &Guest::resolve}; }
    void put16(std::uint32_t address, std::uint32_t value) {
        std::byte* p = resolve(this, address, 2u);
        p[0] = static_cast<std::byte>((value >> 8u) & 0xFFu);
        p[1] = static_cast<std::byte>(value & 0xFFu);
    }
    void put32(std::uint32_t address, std::uint32_t value) {
        put16(address, value >> 16u);
        put16(address + 2u, value & 0xFFFFu);
    }
    void put8(std::uint32_t address, std::uint32_t value) {
        *resolve(this, address, 1u) = static_cast<std::byte>(value & 0xFFu);
    }
    [[nodiscard]] std::uint32_t get16(std::uint32_t address) {
        const std::byte* p = resolve(this, address, 2u);
        return (static_cast<std::uint32_t>(p[0]) << 8u) | static_cast<std::uint32_t>(p[1]);
    }
    [[nodiscard]] std::int16_t gets16(std::uint32_t address) {
        return static_cast<std::int16_t>(get16(address));
    }
    [[nodiscard]] std::uint32_t get32(std::uint32_t address) {
        return (get16(address) << 16u) | get16(address + 2u);
    }
};

constexpr std::uint32_t kChannels = 0x80010000u;
constexpr std::uint32_t kResampler = 0x80020000u;
constexpr std::uint32_t kAdpcm = 0x80021000u;
constexpr std::uint32_t kFx = 0x80022000u;
constexpr std::uint32_t kLeft = 0x80030000u;
constexpr std::uint32_t kRight = 0x80031000u;
constexpr std::uint32_t kWave = 0x00040000u;  // VARAM-relative
constexpr std::uint32_t kFxBuffer = 0x80060000u;

// Every resampler row passes the second tap through, so the output at
// position p is x[p + 1] of (history[4], decoded...); the pan table rises
// linearly.
void build_tables(Guest& guest) {
    for (std::uint32_t row = 0; row < 64u; ++row) {
        guest.put16(kResampler + (row * 4u + 1u) * 2u, 0x7FFF);
    }
    for (std::uint32_t i = 0; i < 128u; ++i) {
        guest.put16(kResampler + (0x200u + i) * 2u, i * 0x102u);
    }
    // ADPCM coefficient pair 1 = {0x0800, 0}: s = n<<scale + s1.
    guest.put16(kAdpcm + 2u * 2u, 0x0800);
}

std::uint32_t record(std::uint32_t channel) { return kChannels + channel * 0x180u; }

void start(JAudioHostMixer& mixer, Guest& guest) {
    build_tables(guest);
    auto memory = guest.memory();
    mixer.setup(0x81000040u, kChannels, kResampler, kAdpcm, kFx, memory);
    mixer.set_varam(0x00000000u);
}

void frame(JAudioHostMixer& mixer, Guest& guest, std::uint32_t level = 0x4000u) {
    mixer.sync_frame(0x82070000u | level, kLeft, kRight, 0u, 0u, guest.memory());
}

bool subframe(JAudioHostMixer& mixer, Guest& guest, std::uint16_t map0) {
    auto memory = guest.memory();
    mixer.release_halt((0u << 16u) | map0, memory);
    mixer.release_halt(1u << 16u, memory);
    mixer.release_halt(2u << 16u, memory);
    return mixer.release_halt(3u << 16u, memory);
}

void pcm16_channel(Guest& guest, std::uint32_t channel, std::int16_t value,
                   std::uint32_t samples) {
    const std::uint32_t r = record(channel);
    guest.put16(r + 0x000u, 1u);           // active
    guest.put16(r + 0x004u, 0x1000u);      // pitch 1.0
    guest.put16(r + 0x008u, 1u);           // reset
    guest.put16(r + 0x064u, 1u);           // samples per block
    guest.put16(r + 0x100u, 0x10u);        // PCM16
    guest.put32(r + 0x114u, samples);      // end
    guest.put32(r + 0x118u, kWave);        // source
    for (std::uint32_t i = 0; i < samples; ++i) {
        guest.put16(0x80000000u + kWave + i * 2u, static_cast<std::uint16_t>(value));
    }
}

void test_gain_chain() {
    Guest guest;
    JAudioHostMixer mixer;
    start(mixer, guest);
    pcm16_channel(guest, 0, 0x4000, 4096);
    const std::uint32_t r = record(0);
    guest.put16(r + 0x010u, 0x0D00u);  // bus 0 -> L
    guest.put16(r + 0x012u, 0x4000u);  // target
    guest.put16(r + 0x014u, 0x4000u);  // current
    frame(mixer, guest);
    expect(subframe(mixer, guest, 0x8000u), "group 3 completes the subframe");
    // Steady state: bus = 0x4000 * 0x4000 >> 16 = 0x1000; output = bus *
    // 0x4000 / 4096 = 0x4000.
    expect(guest.gets16(kLeft + 79u * 2u) == 0x4000, "level chain", guest.gets16(kLeft + 79u * 2u));
    expect(guest.gets16(kRight + 79u * 2u) == 0, "right untouched", guest.gets16(kRight + 79u * 2u));
    expect(guest.get16(r + 0x008u) == 0u, "reset flag cleared");
    expect(guest.get32(r + 0x074u) == 4096u - 80u, "PCM samples left", guest.get32(r + 0x074u));
    expect(guest.get32(r + 0x070u) == kWave + 160u, "PCM next address", guest.get32(r + 0x070u));
    // Not in the map: untouched.
    guest.put16(r + 0x014u, 0x1234u);
    subframe(mixer, guest, 0x0000u);
    expect(guest.get16(r + 0x014u) == 0x1234u, "unmapped channel skipped");
}

void test_ramp() {
    Guest guest;
    JAudioHostMixer mixer;
    start(mixer, guest);
    pcm16_channel(guest, 0, 0x4000, 4096);
    const std::uint32_t r = record(0);
    guest.put16(r + 0x010u, 0x0D60u);  // bus 0 -> R
    guest.put16(r + 0x012u, 0x2000u);  // target
    guest.put16(r + 0x014u, 0x0000u);  // current
    frame(mixer, guest);
    subframe(mixer, guest, 0x8000u);
    expect(guest.get16(r + 0x014u) == 0x2000u, "current reaches target", guest.get16(r + 0x014u));
    // From sample 65 on the gain is the target: 0x4000*0x2000>>16 * 4 = 0x2000.
    expect(guest.gets16(kRight + 70u * 2u) == 0x2000, "ramp end level", guest.gets16(kRight + 70u * 2u));
    const std::int16_t mid = guest.gets16(kRight + 33u * 2u);
    expect(mid > 0 && mid < 0x2000, "ramp midpoint", mid);
}

void test_adpcm_bookkeeping() {
    Guest guest;
    JAudioHostMixer mixer;
    start(mixer, guest);
    const std::uint32_t r = record(0);
    guest.put16(r + 0x000u, 1u);
    guest.put16(r + 0x004u, 0x1000u);
    guest.put16(r + 0x008u, 1u);
    guest.put16(r + 0x100u, 9u);
    guest.put32(r + 0x114u, 160u);    // 10 blocks
    guest.put32(r + 0x118u, kWave);
    // Each block: scale 0, coefficient pair 1, every nibble +1: a ramp.
    for (std::uint32_t b = 0; b < 10u; ++b) {
        guest.put8(0x80000000u + kWave + b * 9u, 0x01u);
        for (std::uint32_t k = 1; k < 9u; ++k) {
            guest.put8(0x80000000u + kWave + b * 9u + k, 0x11u);
        }
    }
    frame(mixer, guest);
    subframe(mixer, guest, 0x8000u);
    // 80 samples decoded = 5 blocks exactly: nothing left over.
    expect(guest.get16(r + 0x064u) == 0u, "no leftover", guest.get16(r + 0x064u));
    expect(guest.get32(r + 0x074u) == 80u, "samples left", guest.get32(r + 0x074u));
    expect(guest.get32(r + 0x070u) == kWave + 45u, "next block", guest.get32(r + 0x070u));
    expect(guest.gets16(r + 0x0CEu) == 80, "predictor s1", guest.gets16(r + 0x0CEu));
    expect(guest.gets16(r + 0x07Eu) == 80, "resampler history tail", guest.gets16(r + 0x07Eu));
    // Pitch 1.5: 120 samples needed; only 80 remain and there is no loop.
    guest.put16(r + 0x004u, 0x1800u);
    subframe(mixer, guest, 0x8000u);
    expect(guest.get16(r + 0x002u) == 1u, "finished at end without loop");
    // Finished channels are not rendered again.
    guest.put16(r + 0x060u, 0x0ABCu);
    subframe(mixer, guest, 0x8000u);
    expect(guest.get16(r + 0x060u) == 0x0ABCu, "finished channel skipped");
}

void test_adpcm_loop() {
    Guest guest;
    JAudioHostMixer mixer;
    start(mixer, guest);
    const std::uint32_t r = record(0);
    guest.put16(r + 0x000u, 1u);
    guest.put16(r + 0x004u, 0x1000u);
    guest.put16(r + 0x008u, 1u);
    guest.put16(r + 0x100u, 9u);
    guest.put16(r + 0x102u, 1u);       // loop
    guest.put32(r + 0x110u, 20u);      // loop start (block 1, skip 4)
    guest.put32(r + 0x114u, 48u);      // end (3 blocks)
    guest.put32(r + 0x118u, kWave);
    for (std::uint32_t b = 0; b < 3u; ++b) {
        guest.put8(0x80000000u + kWave + b * 9u, 0x01u);
        for (std::uint32_t k = 1; k < 9u; ++k) {
            guest.put8(0x80000000u + kWave + b * 9u + k, 0x11u);
        }
    }
    frame(mixer, guest);
    subframe(mixer, guest, 0x8000u);
    expect(guest.get16(r + 0x002u) == 0u, "looping channel keeps playing");
    const std::uint32_t left = guest.get32(r + 0x074u) + guest.get16(r + 0x064u);
    expect(left <= 28u, "stream position inside loop", left);
}

void test_fx_delay() {
    Guest guest;
    JAudioHostMixer mixer;
    start(mixer, guest);
    pcm16_channel(guest, 0, 0x4000, 4096);
    const std::uint32_t r = record(0);
    guest.put16(r + 0x010u, 0x0DC0u);  // bus 0 -> FX line 0 input
    guest.put16(r + 0x012u, 0x4000u);
    guest.put16(r + 0x014u, 0x4000u);
    // FX line 0: two subframes of delay, output to L at full volume.
    guest.put16(kFx + 0x00u, 1u);
    guest.put16(kFx + 0x02u, 2u);
    guest.put32(kFx + 0x04u, kFxBuffer);
    guest.put16(kFx + 0x08u, 0x0D00u);
    guest.put16(kFx + 0x0Au, 0x7FFFu);
    // Identity FIR: x[n + 7] (newest tap).
    guest.put16(kFx + 0x10u + 7u * 2u, 0x7FFFu);
    frame(mixer, guest);
    subframe(mixer, guest, 0x8000u);
    expect(guest.gets16(kLeft + 40u * 2u) == 0, "no echo before the delay", guest.gets16(kLeft + 40u * 2u));
    subframe(mixer, guest, 0x8000u);
    expect(guest.gets16(kLeft + 80u * 2u + 40u * 2u) == 0, "no echo after one subframe", guest.gets16(kLeft + 80u * 2u + 40u * 2u));
    subframe(mixer, guest, 0x8000u);
    const std::int16_t echo = guest.gets16(kLeft + 160u * 2u + 40u * 2u);
    expect(echo > 0x3000, "echo after two subframes", echo);
    expect(mixer.stats().fx_line_subframes == 3u, "fx line ran each subframe");
}

void test_auto_mixer_pan() {
    Guest guest;
    JAudioHostMixer mixer;
    start(mixer, guest);
    pcm16_channel(guest, 0, 0x4000, 4096);
    const std::uint32_t r = record(0);
    guest.put16(r + 0x058u, 1u);        // auto mixer
    guest.put16(r + 0x050u, 0x0000u);   // pan fully left, front
    guest.put16(r + 0x054u, 0x7FFFu);
    guest.put16(r + 0x056u, 0x7FFFu);
    frame(mixer, guest);
    subframe(mixer, guest, 0x8000u);
    expect(guest.gets16(kLeft + 79u * 2u) > 0x1000, "left gets the signal", guest.gets16(kLeft + 79u * 2u));
    expect(guest.gets16(kRight + 79u * 2u) == 0, "right is silent", guest.gets16(kRight + 79u * 2u));
}

// A FIR whose only tap is the newest of T delays the signal by one sample.
void test_fir_delay() {
    Guest guest;
    JAudioHostMixer mixer;
    start(mixer, guest);
    pcm16_channel(guest, 0, 0x4000, 4096);
    const std::uint32_t r = record(0);
    guest.put16(r + 0x010u, 0x0D00u);
    guest.put16(r + 0x012u, 0x4000u);
    guest.put16(r + 0x014u, 0x4000u);
    guest.put16(r + 0x108u, 4u);                   // 4 taps
    guest.put16(r + 0x120u + 3u * 2u, 0x7FFFu);    // c[3]: newest input
    frame(mixer, guest);
    subframe(mixer, guest, 0x8000u);
    // Resampler warm-up gives zeros for outputs 0..2; the FIR shifts that by one.
    expect(guest.gets16(kLeft + 3u * 2u) == 0, "fir delays by one sample", guest.gets16(kLeft + 3u * 2u));
    expect(guest.gets16(kLeft + 4u * 2u) == 0x4000, "fir passes the signal", guest.gets16(kLeft + 4u * 2u));
    expect(guest.gets16(r + 0x080u + 3u * 2u) == 0x4000, "fir history saved", guest.gets16(r + 0x080u + 3u * 2u));
    expect(mixer.stats().fir_renders == 1u, "fir counted");
}

// y[n] = x[n]/2 + x[n-1]/2, output one sample late.
void test_iir_average() {
    Guest guest;
    JAudioHostMixer mixer;
    start(mixer, guest);
    pcm16_channel(guest, 0, 0x4000, 4096);
    const std::uint32_t r = record(0);
    guest.put16(r + 0x010u, 0x0D00u);
    guest.put16(r + 0x012u, 0x4000u);
    guest.put16(r + 0x014u, 0x4000u);
    guest.put16(r + 0x108u, 0x20u);
    guest.put16(r + 0x148u, 0x4000u);
    guest.put16(r + 0x14Au, 0x4000u);
    frame(mixer, guest);
    subframe(mixer, guest, 0x8000u);
    // Input: 0,0,0,0x4000... -> y: 0,0,0,0x2000,0x4000... -> output y[n-1].
    expect(guest.gets16(kLeft + 4u * 2u) == 0x2000, "iir half step", guest.gets16(kLeft + 4u * 2u));
    expect(guest.gets16(kLeft + 5u * 2u) == 0x4000, "iir settles", guest.gets16(kLeft + 5u * 2u));
    expect(guest.gets16(r + 0x0A8u) == 0x4000, "iir state x[-1]", guest.gets16(r + 0x0A8u));
    // Identity coefficients bypass the filter.
    guest.put16(r + 0x148u, 0x7FFFu);
    guest.put16(r + 0x14Au, 0u);
    subframe(mixer, guest, 0x8000u);
    expect(mixer.stats().iir_renders == 1u, "identity iir bypassed", static_cast<long long>(mixer.stats().iir_renders));
}

void test_square_oscillator() {
    Guest guest;
    JAudioHostMixer mixer;
    start(mixer, guest);
    const std::uint32_t r = record(0);
    guest.put16(r + 0x000u, 1u);
    guest.put16(r + 0x004u, 0x1000u);   // phase += 0x800 per sample
    guest.put16(r + 0x100u, 0u);        // oscillator type 0: square
    guest.put16(r + 0x010u, 0x0D00u);
    guest.put16(r + 0x012u, 0x4000u);
    guest.put16(r + 0x014u, 0x4000u);
    frame(mixer, guest);
    subframe(mixer, guest, 0x8000u);
    // 0x8000 / 0x800 = 16 samples per half period.
    const std::int16_t high = guest.gets16(kLeft + 0u);
    const std::int16_t low = guest.gets16(kLeft + 16u * 2u);
    expect(high > 0 && low < 0 && high == -low, "square wave halves", high);
    expect(guest.gets16(kLeft + 15u * 2u) == high, "square holds for 16 samples");
    expect(guest.get16(r + 0x060u) == ((80u * 0x800u) & 0xFFFFu), "oscillator phase saved", guest.get16(r + 0x060u));
}

// Volume categories: a sound's category is decided at its first render, so
// its gain applies from its first sample; a gain change ramps; a new sound on
// the channel takes its own category at once, without a ramp from the previous
// owner.
void test_bus_categories() {
    using galaxy::audio::kJasDspChannelsPointer;
    Guest guest;
    JAudioHostMixer mixer;
    start(mixer, guest);
    // DSP channel 0 belongs to a JAISe whose sound ID is a PLAYER_VOICE cue;
    // channel 1 has no owner and plays PCM16 (music by format).
    constexpr std::uint32_t kDspChannels = 0x80100000u;
    constexpr std::uint32_t kJasChannel = 0x80101000u;
    constexpr std::uint32_t kTrack = 0x80102000u;
    constexpr std::uint32_t kStream = 0x80104000u;
    guest.put32(kJasDspChannelsPointer, kDspChannels);
    guest.put32(kDspChannels + 0x14u, kJasChannel);
    guest.put32(kJasChannel + 0x08u, kDspChannels);
    guest.put32(kJasChannel + 0x10u, kTrack + 0x180u);  // TChannelMgr in the track
    guest.put32(kTrack + 0x180u + 0x4Cu, kTrack);
    guest.put32(kTrack - 0xC8u, kTrack - 0xC8u);        // JAISe JSULink
    guest.put32(kTrack - 0xC8u + 0x28u, 0x00010005u);   // section 0, PLAYER_VOICE
    mixer.set_bus_gains({1.0f, 0.0f, 0.5f, 1.0f, 1.0f, 1.0f});
    pcm16_channel(guest, 0, 0x4000, 4096);
    pcm16_channel(guest, 1, 0x4000, 4096);
    guest.put16(record(0) + 0x010u, 0x0D00u);  // voice channel -> L
    guest.put16(record(0) + 0x012u, 0x4000u);
    guest.put16(record(0) + 0x014u, 0x4000u);
    guest.put16(record(1) + 0x010u, 0x0D60u);  // music channel -> R
    guest.put16(record(1) + 0x012u, 0x4000u);
    guest.put16(record(1) + 0x014u, 0x4000u);
    frame(mixer, guest);
    subframe(mixer, guest, 0xC000u);
    // The first three samples come from the reset resampler history.
    expect(guest.gets16(kLeft + 3u * 2u) == 0x2000, "voice at half from its first sample", guest.gets16(kLeft + 3u * 2u));
    expect(guest.gets16(kLeft + 79u * 2u) == 0x2000, "voice at half", guest.gets16(kLeft + 79u * 2u));
    int loud = 0;
    for (std::uint32_t k = 0; k < 80u; ++k) {
        loud += guest.gets16(kRight + k * 2u) != 0 ? 1 : 0;
    }
    expect(loud == 0, "muted music silent from its first sample", loud);
    expect(mixer.channel_category(0u).decision.bus == galaxy::audio::NativeAudioBus::Voice, "owner category");
    expect(mixer.channel_category(1u).decision.bus == galaxy::audio::NativeAudioBus::Music, "format category");
    expect(mixer.stats().category_owner_misses == 1u, "one sound without an owner",
           static_cast<long long>(mixer.stats().category_owner_misses));

    mixer.set_bus_gains({1.0f, 0.0f, 0.0f, 1.0f, 1.0f, 1.0f});
    subframe(mixer, guest, 0xC000u);
    const std::int16_t first = guest.gets16(kLeft + 80u * 2u);
    expect(first > 0x1F00 && first < 0x2000, "mute ramps from the previous gain", first);
    const std::int16_t quarter = guest.gets16(kLeft + 159u * 2u);
    expect(quarter > 0x1700 && quarter < 0x1900, "mute is a quarter done after one subframe", quarter);
    for (int k = 0; k < 3; ++k) {
        subframe(mixer, guest, 0xC000u);
    }
    expect(guest.gets16(kLeft + 399u * 2u) == 0, "mute reached within the ramp", guest.gets16(kLeft + 399u * 2u));
    int rising = 0;
    for (std::uint32_t k = 81u; k < 400u; ++k) {
        rising += guest.gets16(kLeft + k * 2u) > guest.gets16(kLeft + (k - 1u) * 2u) ? 1 : 0;
    }
    expect(rising == 0, "mute ramp is monotonic", rising);

    // A new sound on channel 0: a music stream (JASAramStream lists the
    // channel; its JAIStream holds a section-2 ID). Music is at unity.
    guest.put32(kJasChannel + 0x10u, kStream);
    guest.put32(kStream + 0x90u, kJasChannel);
    guest.put32(kStream - 0xA8u + 0x18u, 0x02000001u);
    guest.put16(record(0) + 0x008u, 1u);  // playStart
    mixer.set_bus_gains({1.0f, 1.0f, 0.0f, 1.0f, 1.0f, 1.0f});
    subframe(mixer, guest, 0xC000u);
    expect(guest.gets16(kLeft + 400u * 2u) == 0x4000, "new sound at its own gain at once", guest.gets16(kLeft + 400u * 2u));
    expect(mixer.channel_category(0u).owner.route == galaxy::audio::JAudioOwnerRoute::Stream, "stream owner");
    const std::int16_t unmuting = guest.gets16(kRight + 479u * 2u);
    expect(unmuting > 0x0E00 && unmuting < 0x1200, "unmuted music ramps up over the ramp", unmuting);
}

void test_stray_release() {
    Guest guest;
    JAudioHostMixer mixer;
    start(mixer, guest);
    expect(!subframe(mixer, guest, 0u), "release outside a frame does nothing");
    frame(mixer, guest);
    for (int i = 0; i < 7; ++i) {
        expect(subframe(mixer, guest, 0u), "seven subframes per frame");
    }
    expect(!subframe(mixer, guest, 0u), "eighth release ignored");
    expect(mixer.stats().stray_releases == 2u, "stray count", static_cast<long long>(mixer.stats().stray_releases));
}

}  // namespace

int main() {
    test_gain_chain();
    test_ramp();
    test_adpcm_bookkeeping();
    test_adpcm_loop();
    test_fx_delay();
    test_auto_mixer_pan();
    test_fir_delay();
    test_iir_average();
    test_square_oscillator();
    test_bus_categories();
    test_stray_release();
    if (g_failures != 0) {
        std::fprintf(stderr, "%d failure(s)\n", g_failures);
        return EXIT_FAILURE;
    }
    std::puts("jaudio host mixer tests passed");
    return EXIT_SUCCESS;
}
