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

void render_frame(
    const RenderServices* services,
    bool present_swap_chain,
    bool black) {
    backend().render_frame(
        services,
        present_swap_chain,
        black);
}

void wait_for_render_idle() {
    backend().wait_for_render_idle();
}

void set_frame_end_presents(bool enabled) noexcept {
    backend().set_frame_end_presents(enabled);
}

void attach_movie_frame(const char* name, std::int32_t frame, float frame_rate, std::uint64_t y, std::uint64_t u,
                        std::uint64_t v, std::uint32_t width, std::uint32_t height) noexcept {
    backend().attach_movie_frame(name, frame, frame_rate, y, u, v, width, height);
}
bool movie_replacement_showing() noexcept { return MovieReplacements::showing(); }

void attach_scene_marker(SceneMarker marker) {
    backend().attach_scene_marker(marker);
}

void attach_scene_opaque(const float view[12]) noexcept {
    backend().attach_scene_opaque(view);
}

void attach_scene_view(const float view[12], const float* up, const char* stage) noexcept {
    backend().attach_scene_view(view, up, stage);
}

bool draw_identity_wanted() noexcept {
    return backend().draw_identity_wanted();
}


unsigned bloom_route() noexcept {
    return backend().bloom_route();
}

void attach_shape_record(shapes::Record&& record) {
    backend().attach_shape_record(std::move(record));
}

shapes::Matrix* reserve_record_matrices(shapes::Record& record, std::uint32_t count) {
    return backend().reserve_record_matrices(record, count);
}

float* reserve_palette_rows(shapes::Record& record, std::uint32_t count) {
    return backend().reserve_palette_rows(record, count);
}

state::Op* reserve_record_ops(shapes::Record& record, std::uint32_t count) {
    return backend().reserve_record_ops(record, count);
}

ScreenProbe* reserve_record_probes(shapes::Record& record, std::uint32_t count) {
    return backend().reserve_record_probes(record, count);
}

RenderVertex* reserve_record_vertices(shapes::Record& record, std::uint32_t count) {
    return backend().reserve_record_vertices(record, count);
}

RenderVertexCore* reserve_record_corners(shapes::Record& record, std::uint32_t count) {
    return backend().reserve_record_corners(record, count);
}

std::uint32_t reserve_record_native(const void* native, std::size_t bytes) {
    return backend().reserve_record_native(native, bytes);
}

const shapes::Matrix* producer_record_matrices(const shapes::Record& record) {
    return backend().producer_record_matrices(record);
}

std::uint32_t screen_probe_results(std::uint32_t channel, ScreenProbe* probes, std::uint32_t capacity) {
    return backend().screen_probe_results(channel, probes, capacity);
}

void pump_messages() {
    backend().pump_messages();
}

bool quit_requested() noexcept {
    return backend().quit_requested();
}

std::uint64_t display_copy_count() noexcept {
    return backend().display_copy_count();
}

DisplayPresentStats display_present_stats() noexcept {
    return backend().display_present_stats();
}

}  // namespace galaxy::render
