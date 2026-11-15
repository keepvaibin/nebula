// SPDX-License-Identifier: GPL-3.0-only
// benchmark_stats.h: frame-time statistics, a slow frame's cause and the frame-list file.
#include "galaxy/benchmark_stats.h"

#include <algorithm>
#include <cmath>
#include <numeric>

namespace galaxy::bench {

double percentile(const std::vector<double>& sorted, double p) noexcept {
    if (sorted.empty()) return 0.0;
    const double clamped = std::clamp(p, 0.0, 1.0);
    const auto rank = static_cast<std::size_t>(std::ceil(clamped * static_cast<double>(sorted.size())));
    return sorted[std::min(sorted.size() - 1u, rank == 0u ? 0u : rank - 1u)];
}

namespace {

// The FPS over the slowest `share` of `sorted` (ascending): at least one frame.
double low_fps(const std::vector<double>& sorted, double share) {
    if (sorted.empty()) return 0.0;
    const std::size_t count =
        std::max<std::size_t>(1u, static_cast<std::size_t>(std::floor(static_cast<double>(sorted.size()) * share)));
    const double sum = std::accumulate(sorted.end() - static_cast<std::ptrdiff_t>(count), sorted.end(), 0.0);
    return sum > 0.0 ? 1000.0 * static_cast<double>(count) / sum : 0.0;
}

}  // namespace

FrameStats frame_stats(const std::vector<double>& frame_ms) {
    std::vector<double> sorted;
    sorted.reserve(frame_ms.size());
    for (const double ms : frame_ms) {
        if (ms > 0.0 && std::isfinite(ms)) sorted.push_back(ms);
    }
    FrameStats s;
    if (sorted.empty()) return s;
    std::sort(sorted.begin(), sorted.end());
    s.frames = sorted.size();
    const double sum = std::accumulate(sorted.begin(), sorted.end(), 0.0);
    s.seconds = sum / 1000.0;
    s.average_ms = sum / static_cast<double>(s.frames);
    s.average_fps = 1000.0 * static_cast<double>(s.frames) / sum;
    s.p50_ms = percentile(sorted, 0.50);
    s.p95_ms = percentile(sorted, 0.95);
    s.p99_ms = percentile(sorted, 0.99);
    s.p999_ms = percentile(sorted, 0.999);
    s.worst_ms = sorted.back();
    s.low_10_fps = low_fps(sorted, 0.10);
    s.low_1_fps = low_fps(sorted, 0.01);
    s.low_01_fps = low_fps(sorted, 0.001);
    s.stutters = static_cast<std::uint32_t>(sorted.end() - std::upper_bound(sorted.begin(), sorted.end(), kStutterMs));
    return s;
}

Spread spread(std::vector<double> values) {
    values.erase(std::remove_if(values.begin(), values.end(), [](double v) { return !(v >= 0.0) || !std::isfinite(v); }),
                 values.end());
    Spread s;
    if (values.empty()) return s;
    std::sort(values.begin(), values.end());
    s.count = values.size();
    s.average = std::accumulate(values.begin(), values.end(), 0.0) / static_cast<double>(values.size());
    s.p99 = percentile(values, 0.99);
    s.worst = values.back();
    return s;
}

const char* cause_name(Cause cause) noexcept {
    switch (cause) {
    case Cause::Pipeline: return "pipeline built";
    case Cause::DiscRead: return "disc read";
    case Cause::Texture: return "texture made";
    case Cause::Gpu: return "gpu";
    case Cause::RenderThread: return "render thread";
    case Cause::GameThread: return "game thread";
    case Cause::Waiting: return "waiting";
    }
    return "waiting";
}

Cause cause_of(const FrameFacts& f) noexcept {
    if (f.pipelines != 0u) return Cause::Pipeline;
    if (f.read_bytes != 0u) return Cause::DiscRead;
    if (f.textures != 0u) return Cause::Texture;
    Cause busiest = Cause::Waiting;
    double most = f.frame_ms * 0.5;
    const auto weigh = [&](double ms, Cause cause) {
        if (ms >= most && ms > 0.0) {
            most = ms;
            busiest = cause;
        }
    };
    weigh(f.gpu_ms, Cause::Gpu);
    weigh(f.render_ms, Cause::RenderThread);
    weigh(f.game_ms, Cause::GameThread);
    return busiest;
}

namespace {

void put32(std::string& out, std::uint32_t v) {
    for (unsigned i = 0; i < 4u; ++i) out.push_back(static_cast<char>((v >> (8u * i)) & 0xffu));
}

bool get32(std::string_view bytes, std::size_t& at, std::uint32_t& v) {
    if (at > bytes.size() || bytes.size() - at < 4u) return false;
    v = 0u;
    for (unsigned i = 0; i < 4u; ++i) v |= static_cast<std::uint32_t>(static_cast<unsigned char>(bytes[at + i])) << (8u * i);
    at += 4u;
    return true;
}

constexpr char kMagic[4] = {'N', 'B', 'F', 'R'};
constexpr std::uint32_t kVersion = 1u;

bool printable(std::string_view text) {
    return std::all_of(text.begin(), text.end(), [](char c) { return c >= 0x20 && c < 0x7f; });
}

}  // namespace

std::string encode_frame_lists(const std::vector<FrameList>& lists) {
    std::string out(kMagic, sizeof(kMagic));
    put32(out, kVersion);
    std::uint32_t kept = 0u;
    for (const FrameList& list : lists) {
        kept += list.stage.size() <= 255u && printable(list.stage) && list.part <= 1u ? 1u : 0u;
    }
    put32(out, kept);
    for (const FrameList& list : lists) {
        if (list.stage.size() > 255u || !printable(list.stage) || list.part > 1u) continue;
        out.push_back(static_cast<char>(list.part));
        out.push_back(static_cast<char>(list.stage.size()));
        out += list.stage;
        const std::size_t frames = std::min<std::size_t>(list.frame_ms.size(), kMostFramesPerList);
        put32(out, static_cast<std::uint32_t>(frames));
        for (std::size_t i = 0; i < frames; ++i) {
            const double units = std::round(list.frame_ms[i] / kFrameUnitMs);
            const auto value = static_cast<std::uint16_t>(std::isfinite(units) ? std::clamp(units, 0.0, 65535.0) : 65535.0);
            out.push_back(static_cast<char>(value & 0xffu));
            out.push_back(static_cast<char>(value >> 8u));
        }
    }
    return out;
}

bool decode_frame_lists(std::string_view bytes, std::vector<FrameList>& lists) {
    lists.clear();
    std::uint32_t version = 0u, count = 0u;
    if (bytes.size() < sizeof(kMagic) || bytes.substr(0, sizeof(kMagic)) != std::string_view(kMagic, sizeof(kMagic))) {
        return false;
    }
    std::size_t at = sizeof(kMagic);
    if (!get32(bytes, at, version) || version != kVersion || !get32(bytes, at, count) || count > 64u) return false;
    std::vector<FrameList> read;
    for (std::uint32_t l = 0; l < count; ++l) {
        if (bytes.size() - at < 2u) return false;
        FrameList list;
        list.part = static_cast<unsigned char>(bytes[at++]);
        const std::size_t name = static_cast<unsigned char>(bytes[at++]);
        if (list.part > 1u || bytes.size() - at < name) return false;
        list.stage.assign(bytes.substr(at, name));
        at += name;
        std::uint32_t frames = 0u;
        if (!printable(list.stage) || !get32(bytes, at, frames) || frames > kMostFramesPerList ||
            (bytes.size() - at) / 2u < frames) {
            return false;
        }
        list.frame_ms.reserve(frames);
        for (std::uint32_t i = 0; i < frames; ++i) {
            const unsigned value = static_cast<unsigned char>(bytes[at]) | (static_cast<unsigned char>(bytes[at + 1u]) << 8u);
            at += 2u;
            list.frame_ms.push_back(static_cast<double>(value) * kFrameUnitMs);
        }
        read.push_back(std::move(list));
    }
    if (at != bytes.size()) return false;
    lists = std::move(read);
    return true;
}

}  // namespace galaxy::bench
