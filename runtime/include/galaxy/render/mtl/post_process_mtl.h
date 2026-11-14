// SPDX-License-Identifier: GPL-3.0-only
// Presentation post-processing on Metal, the other renderers' chain (post_process.h, vk/post_process_vk.h) with the
// same shaders: SMAA 1x or FXAA of the 3D scene before the 2D layer draws, so menus and text stay sharp, and the
// scaling chain to the window (FSR 1 EASU when enlarging, else the footprint box filter, then RCAS). The programs are
// post_process.hlsl's, turned to MSL at build (mtl/utility_msl.h post_msl) and compiled on a worker thread: until
// they are ready the renderer presents with its plain blit.
//
// Plain C++ (mtl_context.h): Metal's objects are mtl::Object.
#pragma once

#include "galaxy/render/gpu_types.h"
#include "galaxy/render/mtl/metalfx_mtl.h"
#include "galaxy/render/mtl/mtl_context.h"
#include "galaxy/render/post_process_settings.h"

#include <array>
#include <cstdint>
#include <functional>
#include <future>
#include <memory>
#include <unordered_map>
#include <vector>

namespace galaxy::render::mtl {

class PostProcessor {
public:
    PostProcessor() = default;
    PostProcessor(const PostProcessor&) = delete;
    PostProcessor& operator=(const PostProcessor&) = delete;
    ~PostProcessor() { shutdown(); }

    // Starts compiling every pass. `scene_format`: the scene target's colour (pictures between passes have it too);
    // `depth_format`: the scene pass's depth and stencil target.
    void initialize(const PostProcessSettings& settings, Format scene_format, Format depth_format);
    void shutdown() noexcept;
    // Once the worker has finished: builds the pipelines and runs the SMAA and FXAA self-tests (a submission the
    // render thread waits for), so status() is final. True when the chain is ready.
    bool poll_ready();
    [[nodiscard]] bool ready() const noexcept { return ready_; }
    [[nodiscard]] const PostProcessStatus& status() const noexcept { return status_; }
    [[nodiscard]] const PostProcessSettings& settings() const noexcept { return settings_; }
    // New settings between frames; an SMAA quality change rebuilds its two pipelines.
    void apply_settings(const PostProcessSettings& settings);

    // Scene AA of the scene target colour `color` (no encoder open on `commands`, an id<MTLCommandBuffer>), in two
    // parts around the renderer beginning its scene pass again: prepare_scene_aa records the passes before (a copy of
    // the scene, SMAA's edges and weights) and returns the pass to finish with; finish_scene_aa draws the result into
    // the open scene pass `encoder` (an id<MTLRenderCommandEncoder> over `samples` samples a pixel), RGB only (the
    // alpha the game left there stays). With `trade` (a single-sample scene) a pass that runs reads the scene's own
    // image instead of a copy (scene_traded), and finish_scene_aa writes every pixel and channel of the image that took
    // its place (the alpha the scene's): the scene pass may open without loading it.
    using SceneTrade = std::function<std::shared_ptr<Texture>(std::shared_ptr<Texture> spare)>;
    AaRun prepare_scene_aa(Object commands, const Texture& color, unsigned width, unsigned height, unsigned picture_width,
                           unsigned picture_height, unsigned display_width, unsigned display_height,
                           const SceneTrade& trade = {});
    void finish_scene_aa(Object encoder, AaRun pass, unsigned width, unsigned height, unsigned samples);
    [[nodiscard]] bool scene_traded() const noexcept { return scene_traded_; }

    // The scaling chain from `source` to a `target_width` x `target_height` picture: prepare records the passes
    // before the target's own pass (into the window-sized intermediate); finish draws the last one in the target's
    // pass `encoder` at `viewport`, whose origin is the picture's in the target, dithered for an 8-bit window or
    // encoded as `output` says. `target_format`: the target's pixel format.
    struct PresentStep {
        Object pipeline = nullptr;  // id<MTLRenderPipelineState>; null: the renderer draws with its blit
        Object input = nullptr;     // id<MTLTexture>
        PostConstants constants{};
        bool ten_bit = false;       // the target keeps the scene's ten bits a channel: the last write is plain
    };
    // `hdr`: the target is written as HDR10 (finish_present's `output`), never as a plain copy.
    PresentStep prepare_present(Object commands, const Texture& source, unsigned source_width, unsigned source_height,
                                unsigned target_width, unsigned target_height, Format target_format, bool hdr = false);
    void finish_present(Object encoder, const PresentStep& step, const gpu::Rect& viewport,
                        const PostOutput& output = PostOutput{});

    // The passes' programs (post_programs_vk.inc's order).
    enum Program : unsigned {
#define POST(name, pass, library, quality, stage) name,
#include "galaxy/render/vk/post_programs_vk.inc"
#undef POST
        kProgramCount
    };

private:
    struct Target {
        std::shared_ptr<Texture> texture;
        unsigned width = 0;
        unsigned height = 0;
        std::uint64_t used = 0;  // clock_ when a pass last asked for it
    };
    // A pipeline: the programs, what it draws into and how.
    struct PipelineKey {
        std::uint8_t vertex = 0, fragment = 0;
        Format color = Format::None;
        Format depth = Format::None;    // the pass's depth and stencil target (None: the pass has none)
        std::uint8_t write_mask = 0;    // MTLColorWriteMask
        std::uint8_t samples = 1;
        std::uint8_t quality = 0;       // the SMAA quality, for the programs that follow it
        friend bool operator==(const PipelineKey&, const PipelineKey&) = default;
    };
    struct PipelineKeyHasher {
        std::size_t operator()(const PipelineKey& key) const noexcept;
    };
    // The compiled functions of every program (those that follow the SMAA quality: one per quality).
    struct Functions {
        std::array<std::array<Object, 4>, kProgramCount> at{};  // id<MTLFunction> by program and quality
        bool ok = false;
    };
    [[nodiscard]] Object pipeline(Program vertex, Program fragment, Format color, Format depth, std::uint8_t write_mask,
                                  unsigned samples = 1u);
    [[nodiscard]] Object function(Program program) const;
    bool build_pipelines();
    void release_pipelines() noexcept;
    bool create_lookup_textures(Object commands, std::vector<std::shared_ptr<Buffer>>& staging);
    bool ensure_target(Target& target, unsigned width, unsigned height, Format format);
    bool ensure_smaa_targets(unsigned width, unsigned height);
    void trim_idle_targets();
    bool record_aa(Object commands, AaRun pass, Object source, unsigned width, unsigned height);
    // How a draw uses the pass's stencil: not at all, marks it (reference 1), or draws where it is marked.
    enum class Stencil : std::uint8_t { None, Mark, Test, Count };
    void draw(Object encoder, Object pipeline, Stencil stencil, const Object* inputs, unsigned input_count,
              const gpu::Rect& viewport, const PostConstants& constants) const;
    void self_test();

    PostProcessSettings settings_{};
    PostProcessStatus status_{};
    bool ready_ = false;
    bool to_build_ = false;  // initialized: poll_ready makes the pipelines once the functions are compiled
    Format scene_format_ = Format::Rgb10a2;
    Format depth_format_ = Format::Depth32fStencil8;
    std::future<Functions> compiling_;
    Functions functions_{};
    std::unordered_map<PipelineKey, Object, PipelineKeyHasher> pipelines_;
    std::array<Object, static_cast<std::size_t>(Stencil::Count)> stencil_states_{};  // id<MTLDepthStencilState>
    Object samplers_[2]{};  // id<MTLSamplerState>: linear, point
    std::shared_ptr<Texture> area_texture_;
    std::shared_ptr<Texture> search_texture_;
    Target edges_;
    Target weights_;
    Target stencil_;  // shared by edges and weights
    Target antialiased_;
    Target scaled_;
    // The scene's image traded out (prepare_scene_aa with a trade): the passes' source, the next trade's spare.
    std::shared_ptr<Texture> traded_;
    std::uint64_t traded_used_ = 0;  // clock_ at the last trade
    bool scene_traded_ = false;  // this frame's passes read traded_
    // MetalFX's spatial scaler (settings upscaler kMetalFx) and the picture it writes; the sizes it could not be made
    // for are not tried again each frame.
    std::unique_ptr<SpatialScaler> spatial_;
    Target spatial_output_;
    unsigned spatial_refused_[4]{};
    // `source` enlarged by MetalFX into spatial_output_; false when the device or the sizes do not allow it.
    bool record_spatial(Object commands, const Texture& source, unsigned source_width, unsigned source_height,
                        unsigned target_width, unsigned target_height);
    std::uint64_t clock_ = 0;      // scene AA calls so far
    std::uint64_t smaa_used_ = 0;  // clock_ when SMAA's three targets were last asked for
};

}  // namespace galaxy::render::mtl
