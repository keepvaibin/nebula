// SPDX-License-Identifier: GPL-3.0-only
// The ray tracer's back end on Metal: none yet (docs/APPLE.md, "State"). The render core holds a tracer whatever the
// renderer (rt_backend.h) and starts it only on a device whose capabilities say it can trace (rt_caps.h), which the
// Metal renderer never reports. These are what the core links against: nothing here starts, builds or draws.
#include "galaxy/render/rt/rt_backend.h"
#include "galaxy/render/rt/rt_shaders.h"

namespace galaxy::render {

RtShaderBytes rt_shader(RtShader) noexcept { return {}; }

bool rt_shaders_present() noexcept { return false; }

namespace rt {

bool shaders_load(gpu::Device*) { return false; }

Backend::Backend() = default;
Backend::~Backend() = default;

bool Backend::start(gpu::Device*) { return false; }
void Backend::stop() noexcept {}
bool Backend::started() const noexcept { return false; }
BuildSizes Backend::sizes(const TriangleInput&, BuildKind) const { return {}; }
std::uint32_t Backend::make_page(std::uint64_t, bool) { return ~0u; }
void Backend::free_page(std::uint32_t) noexcept {}
Structure Backend::make_structure(const Range&, bool) { return {}; }
void Backend::free_structure(Structure&) noexcept {}
void Backend::frame(unsigned) noexcept {}
void Backend::begin(void*, unsigned, void*) noexcept {}
void Backend::build(const TriangleInput&, BuildKind, const Structure&, const Range&, bool) {}
BuildSizes Backend::scene_sizes(std::uint32_t) const { return {}; }
void Backend::build_scene(gpu::Address, std::uint32_t, const Structure&, const Range&) {}
void Backend::barrier() {}
void Backend::ask_compacted_size(const Structure&, std::uint32_t) {}
void Backend::end_size_asks() {}
std::uint64_t Backend::compacted_size(std::uint32_t, unsigned) const { return 0u; }
void Backend::compact(const Structure&, const Structure&) {}
std::uint32_t Backend::make_image(ImageFormat, std::uint32_t, std::uint32_t, const char*, std::uint32_t) { return kNoImage; }
void Backend::free_image(std::uint32_t) noexcept {}
void Backend::dispatch(RtShader, const PassBinds&, std::uint32_t, std::uint32_t) {}
std::uint32_t Backend::make_buffer(std::uint64_t, const char*) { return kNoBuffer; }
void Backend::free_buffer(std::uint32_t) noexcept {}
void Backend::buffer_for(std::uint32_t, bool) {}
BufferView Backend::view_of(gpu::Address, std::uint64_t, std::uint32_t& offset) const {
    offset = 0u;
    return {};
}
void* Backend::image_to_copy(std::uint32_t) { return nullptr; }
void* Backend::image_to_sample(std::uint32_t) { return nullptr; }
void Backend::end_counters() {}
void Backend::read_counters(unsigned, std::uint32_t out[kCounters]) const {
    for (unsigned i = 0; i < kCounters; ++i) {
        out[i] = 0u;
    }
}
void Backend::time_mark(unsigned, bool) {}
std::uint64_t Backend::timed_ticks(unsigned, unsigned) const { return 0u; }

// Opacity micromaps (5.3 G-2): none on this back end yet (Metal's are Apple's lane's).
bool Backend::micromaps() const noexcept { return false; }
BuildSizes Backend::micromap_sizes(const MicromapInput&) const { return {}; }
Structure Backend::make_micromap(const Range&) { return {}; }
void Backend::free_micromap(Structure& micromap) noexcept { micromap = Structure{}; }
void Backend::build_micromap(const MicromapInput&, const Structure&, const Range&) {}

}  // namespace rt
}  // namespace galaxy::render
