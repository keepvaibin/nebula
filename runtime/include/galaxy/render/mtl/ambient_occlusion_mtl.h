// SPDX-License-Identifier: GPL-3.0-only
// Ambient occlusion on Metal: ambient_occlusion.cpp's passes (ambient_occlusion.hlsl, turned to MSL at build), as
// vk/ambient_occlusion_vk.h for Vulkan.
//
// Plain C++ (mtl_context.h): Metal's objects are mtl::Object.
#pragma once

#include "galaxy/render/ambient_occlusion_objects.h"
#include "galaxy/render/gpu_upload_ring.h"
#include "galaxy/render/mtl/mtl_context.h"

#include <array>
#include <functional>
#include <memory>
#include <span>
#include <vector>

namespace galaxy::render::mtl {

class AmbientOcclusion {
public:
    enum class Stage { Distance, Occlusion, Denoise, Upsample };
    using StageMarker = std::function<void(Stage)>;

    AmbientOcclusion() = default;
    AmbientOcclusion(const AmbientOcclusion&) = delete;
    AmbientOcclusion& operator=(const AmbientOcclusion&) = delete;
    ~AmbientOcclusion() { shutdown(); }

    // The compute pipelines; the multiply's are made per scene pass at first use. False leaves the pass off.
    // `depth_format`: the scene pass's depth and stencil target.
    bool initialize(Format depth_format);
    void shutdown() noexcept;
    [[nodiscard]] bool ready() const noexcept { return ready_; }
    // With no encoder open on `commands` (an id<MTLCommandBuffer>), from the scene's single-sample depth: the colour
    // multiplier of each scene target pixel (at one occlusion pixel per scene target pixel the multiply makes it from the
    // denoised occlusion itself). False: nothing to multiply.
    bool prepare(Object commands, const Texture& depth, const RtView& view, bool ssao, std::span<const ao::ObjectOccluder> objects,
                 UploadRing& ring, const StageMarker& mark = {});
    // Inside a scene pass `encoder` (an id<MTLRenderCommandEncoder>) whose colour is `color_format` over `samples`
    // samples a pixel: the multiply.
    void finish(Object encoder, Format color_format, unsigned samples);
    // Once a frame: hands back the occlusion targets after 600 frames in which prepare() did not run.
    void trim_idle();

private:
    enum Pass : unsigned { kDistance, kOcclusion, kDenoise, kUpsample, kSsao, kPassCount };
    [[nodiscard]] Object function(unsigned index);  // id<MTLFunction>, by kShaders' order
    [[nodiscard]] Object apply_pipeline(Format color_format, unsigned samples);
    [[nodiscard]] bool ensure(std::shared_ptr<Texture>& target, unsigned width, unsigned height, Format format, const char* name);
    void dispatch(Object compute_encoder, Pass pass, const Texture& depth, const Texture& sampled1, const Texture& sampled2,
                  const Texture& storage, unsigned width, unsigned height) const;

    bool ready_ = false;
    Format depth_format_ = Format::Depth32fStencil8;
    std::array<Object, 8> functions_{};            // id<MTLFunction>
    std::array<Object, kPassCount> compute_{};     // id<MTLComputePipelineState>
    Object no_depth_ = nullptr;                    // id<MTLDepthStencilState>: the multiply's
    struct Apply {
        Format color = Format::None;
        unsigned samples = 0;
        bool fused = false;         // makes the multiplier from the denoised occlusion (AO_PASS_apply_ps)
        Object pipeline = nullptr;  // id<MTLRenderPipelineState>
    };
    std::array<Apply, 16> apply_{};
    // This frame's occlusion is one pixel per scene target pixel: no multiplier image or pass; the multiply reads the
    // denoised occlusion.
    bool fused_ = false;
    unsigned target_width_ = 0u;  // the scene target's size at the last prepare()
    unsigned target_height_ = 0u;
    std::shared_ptr<Texture> distance_;
    std::shared_ptr<Texture> raw_;
    std::shared_ptr<Texture> denoised_;
    std::shared_ptr<Texture> scale_;
    std::uint64_t clock_ = 0;  // trim_idle() calls
    std::uint64_t used_ = 0;   // the clock at the last prepare()
    ao::Constants constants_{};
    std::vector<float> object_values_;
    std::vector<std::uint32_t> object_tiles_;
    ao::ObjectBinScratch object_scratch_;
    // The scene's models and their tiles for this frame's passes: the buffers (not retained: the ring's) and offsets.
    Object objects_buffer_ = nullptr;
    std::uint64_t objects_offset_ = 0;
    Object tiles_buffer_ = nullptr;
    std::uint64_t tiles_offset_ = 0;
};

}  // namespace galaxy::render::mtl
