// Implements the galaxy::gx public API (gx_d3d12.h) over a single GxBackend.

#include "galaxy/gx_d3d12.h"

#include <atomic>

#include "galaxy/gx/gx_backend.h"

namespace galaxy::gx {

namespace {
GxBackend& backend() {
    static GxBackend instance;
    return instance;
}
}  // namespace

bool initialize(int width, int height) {
    return backend().initialize(width, height);
}

std::string startup_failure() {
    return backend().startup_failure();
}

FramePeCompletionToken render_frame(
    const std::byte* fifo_data,
    std::size_t fifo_size,
    GuestMemory* memory,
    const GxServices* services,
    bool present_swap_chain,
    std::uint32_t displayed_xfb_addr) {
    return backend().render_frame(
        fifo_data,
        fifo_size,
        memory,
        services,
        present_swap_chain,
        displayed_xfb_addr);
}

void wait_for_render_idle() {
    backend().wait_for_render_idle();
}

bool wait_for_frame_pe_completion_for(
    FramePeCompletionToken token,
    std::uint32_t timeout_ms) {
    return backend().wait_for_frame_pe_completion_for(token, timeout_ms);
}

void attach_movie_frame(const char* name, std::int32_t frame, float frame_rate, std::uint32_t y, std::uint32_t u,
                        std::uint32_t v, std::uint32_t width, std::uint32_t height) noexcept {
    backend().attach_movie_frame(name, frame, frame_rate, y, u, v, width, height);
}
bool movie_replacement_showing() noexcept { return MovieReplacements::showing(); }
void set_safety_scene_surround(float intensity) noexcept { backend().set_safety_scene_surround(intensity); }

void attach_fifo_marker(FifoMarker marker, std::size_t fifo_offset) noexcept {
    backend().attach_fifo_marker(marker, fifo_offset);
}

void attach_scene_view(const float view[12], const float* up, const char* stage) noexcept {
    backend().attach_scene_view(view, up, stage);
}

bool draw_identity_wanted() noexcept {
    return backend().draw_identity_wanted();
}

bool rt_shadow_casters_wanted() noexcept {
    return backend().rt_shadow_casters_wanted();
}

unsigned bloom_route() noexcept {
    return backend().bloom_route();
}

void attach_stripe_particle(std::size_t stream_offset, std::uint32_t particle, std::uint32_t age_lifetime,
                            std::uint8_t strip) {
    backend().attach_stripe_particle({stream_offset, particle, age_lifetime, strip});
}

bool peek_efb(
    std::uint16_t x,
    std::uint16_t y,
    EfbPeekKind kind,
    std::uint32_t& value) {
    return backend().peek_efb(x, y, kind, value);
}

bool peek_efb_published(
    std::uint16_t x,
    std::uint16_t y,
    EfbPeekKind kind,
    std::uint16_t token,
    std::uint32_t& value) {
    return backend().peek_efb_published(x, y, kind, token, value);
}

void set_indexed_load_host_order(bool host_order) noexcept {
    extern std::atomic<bool> g_indexed_load_host_order;
    g_indexed_load_host_order.store(host_order, std::memory_order_relaxed);
}

void notify_guest_memory_write(
    std::uint32_t address,
    std::uint32_t size) noexcept {
    backend().notify_guest_memory_write(address, size);
}

void pump_messages() {
    backend().pump_messages();
}

bool quit_requested() noexcept {
    return backend().quit_requested();
}

std::uint64_t xfb_copy_count() noexcept {
    return backend().xfb_copy_count();
}

XfbPresentStats xfb_present_stats() noexcept {
    return backend().xfb_present_stats();
}

}  // namespace galaxy::gx
