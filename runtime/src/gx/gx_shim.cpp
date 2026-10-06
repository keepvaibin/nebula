// Implements the galaxy::gx public API (gx_d3d12.h) over a single GxBackend.

#include "galaxy/gx_d3d12.h"

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

FramePeCompletionToken render_frame(
    const std::byte* fifo_data,
    std::size_t fifo_size,
    GuestMemoryV1* memory,
    const NativeServicesV1* services,
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

void wait_for_render_fifo_effects_idle() {
    backend().wait_for_render_fifo_effects_idle();
}

void wait_for_frame_pe_completion(FramePeCompletionToken token) {
    backend().wait_for_frame_pe_completion(token);
}

bool wait_for_frame_pe_completion_for(
    FramePeCompletionToken token,
    std::uint32_t timeout_ms) {
    return backend().wait_for_frame_pe_completion_for(token, timeout_ms);
}

void set_pointer_response(PointerResponseStamp stamp) noexcept { backend().set_pointer_response(stamp); }
void set_prerecorded_movie_content(bool movie) noexcept { backend().set_prerecorded_movie_content(movie); }
void set_safety_scene_surround(float intensity) noexcept { backend().set_safety_scene_surround(intensity); }

bool enqueue_pointer_depth_capture(const std::shared_ptr<PointerDepthCapture>& capture) {
    return backend().enqueue_pointer_depth_capture(capture);
}

bool capture_pointer_depth(std::span<std::uint32_t> pixels) {
    return backend().capture_pointer_depth(pixels);
}

bool peek_efb(
    std::uint16_t x,
    std::uint16_t y,
    EfbPeekKind kind,
    std::uint32_t& value) {
    return backend().peek_efb(x, y, kind, value);
}

void present_cached_xfb(
    std::uint32_t displayed_xfb_addr,
    bool present_swap_chain) {
    backend().present_cached_xfb(displayed_xfb_addr, present_swap_chain);
}

void scan_pe_events(
    const std::byte* fifo_data,
    std::size_t fifo_size,
    GuestMemoryV1* memory,
    const NativeServicesV1* services) {
    backend().scan_pe_events_on_sim_thread(
        fifo_data, fifo_size, memory, services);
}

void install_guest_dirty_tracker(GuestMemoryV1* memory) noexcept {
    backend().install_guest_dirty_tracker(memory);
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

std::uint64_t xfb_causal_trace_frame_index() noexcept {
    return backend().xfb_causal_trace_frame_index();
}

XfbPresentStats xfb_present_stats() noexcept {
    return backend().xfb_present_stats();
}

XfbMeasurementCadenceStats xfb_measurement_cadence_stats() noexcept {
    return backend().xfb_measurement_cadence_stats();
}

void reset_xfb_measurement_window_stats() noexcept {
    backend().reset_xfb_measurement_window_stats();
}

void shutdown() {
    backend().shutdown();
}

}  // namespace galaxy::gx
