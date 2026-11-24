// Implements the galaxy::render public API (render_api.h) over a single RenderCore.

#include "galaxy/render_api.h"

#include <atomic>

#include "galaxy/render/render_core.h"

namespace galaxy::render {

namespace {
RenderCore& backend() {
    static RenderCore instance;
    return instance;
}
}  // namespace

bool initialize(int width, int height) {
    return backend().initialize(width, height);
}

std::string startup_failure() {
    return backend().startup_failure();
}

void render_frame(bool present_swap_chain, bool black, bool list_shows) {
    backend().render_frame(present_swap_chain, black, list_shows);
}

void set_step_moment(std::int64_t qpc) noexcept {
    backend().set_step_moment(qpc);
}

void wait_for_render_idle() {
    backend().wait_for_render_idle();
}

bool finish_pictures(std::uint32_t limit_ms) {
    return backend().finish_pictures(std::chrono::milliseconds(limit_ms));
}

void set_frame_end_presents(bool enabled) noexcept {
    backend().set_frame_end_presents(enabled);
}

void attach_movie_frame(const char* name, std::int32_t frame, float frame_rate, const void* data, std::uint64_t size,
                        std::uint32_t width, std::uint32_t height) {
    backend().attach_movie_frame(name, frame, frame_rate, data, size, width, height);
}

core::Registries& registries() noexcept {
    return backend().registries();
}

PipelineProgress pipeline_progress() noexcept {
    return backend().pipeline_progress();
}

void set_pipelines_hurried(bool hurried) noexcept {
    backend().set_pipelines_hurried(hurried);
}

void drain_pipelines() noexcept {
    backend().drain_pipelines();
}

TextureStore& textures() noexcept {
    return backend().textures();
}

void attach_frame_list(NebulaFrameList* list) noexcept {
    backend().attach_frame_list(list);
}

unsigned bloom_route() noexcept {
    return backend().bloom_route();
}

void pump_messages() {
    backend().pump_messages();
}

bool quit_requested() noexcept {
    return backend().quit_requested();
}

void set_window_hidden(bool hidden) noexcept {
    backend().set_window_hidden(hidden);
}

void set_window_background(bool background) noexcept {
    backend().set_window_background(background);
}

bool window_occluded() noexcept {
    return backend().window_occluded();
}

void set_gpu_timing(bool on) noexcept {
    backend().set_gpu_timing(on);
}

bool gpu_timing_on() noexcept {
    return backend().gpu_timing_on();
}

std::size_t gpu_pass_times(GpuPassTime* out, std::size_t capacity) noexcept {
    return backend().gpu_pass_times(out, capacity);
}

bool video_memory(std::uint64_t& usage, std::uint64_t& budget) noexcept {
    return backend().video_memory(usage, budget);
}

std::uint64_t pictures_made() noexcept {
    return backend().pictures_made();
}

DisplayPresentStats display_present_stats() noexcept {
    return backend().display_present_stats();
}

}  // namespace galaxy::render
