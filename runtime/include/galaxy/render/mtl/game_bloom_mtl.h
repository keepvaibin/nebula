// SPDX-License-Identifier: GPL-3.0-only
// The game's bloom on Metal: the operator of game_bloom.h (game_bloom.hlsl, turned to MSL at build and compiled on
// the device) over the scene target. Until it is ready the game draws its own chains (NEBULA_OPTION_BLOOM 0). As
// vk/game_bloom_vk.h for Vulkan.
//
// Plain C++ (mtl_context.h): Metal's objects are mtl::Object.
#pragma once

#include "galaxy/render/mtl/mtl_context.h"

#include <array>
#include <cstdint>
#include <future>
#include <memory>

namespace galaxy::render::mtl {

class GameBloom {
public:
    GameBloom() = default;
    GameBloom(const GameBloom&) = delete;
    GameBloom& operator=(const GameBloom&) = delete;
    ~GameBloom() { shutdown(); }

    // Starts compiling the passes. `depth_format`: the scene pass's depth and stencil target.
    void initialize(Format depth_format);
    void shutdown() noexcept;
    // Once the worker has finished: builds the compute pipelines (the composite's are made per scene pass at first
    // use). True when ready.
    bool poll_ready();
    [[nodiscard]] bool ready() const noexcept { return ready_; }

    // As game_bloom.h's: Restore draws the kept scene alone (the game's own chain, the NATIVE setting).
    enum class Kind : std::uint8_t { Restore = 0, Normal = 1, Simple = 2 };
    struct Params {
        Kind kind = Kind::Normal;
        unsigned threshold = 128;
        unsigned channel = 0;
        unsigned tap1 = 42;
        unsigned tap2 = 21;
        unsigned intensity = 255;
        float ring_x = 1.0f;
        float ring_y = 1.0f;
        float strength = 1.0f;
    };
    struct Inputs {
        Texture* color = nullptr;  // the scene target's single-sample colour (no pass open)
        unsigned frame_width = 0;  // the frame in scene target pixels
        unsigned frame_height = 0;
        float scale = 1.0f;
    };
    // BloomEffect::preDraw, BloomEffectSimple::draw with the game's own chain: the scene before the bloom works over
    // it. `commands`: an id<MTLCommandBuffer> with no encoder open.
    bool keep(Object commands, const Inputs& in);
    // Keeping without a copy: the scene target continues on the texture this returns (the hand-over makes one when it
    // does not fit), and its picture is the kept scene. The composite writes the kept scene's alpha with its colour.
    [[nodiscard]] std::shared_ptr<Texture> take_kept_image();
    void keep_picture(std::shared_ptr<Texture> picture);
    // The simple bloom's source without a copy: the scene target continues on the image this returns (the hand-over makes
    // one when it does not fit), and give_source takes the scene's picture as the source; prepare() copies nothing then.
    // Only where the composite covers the whole target, which it then writes from the source.
    [[nodiscard]] std::shared_ptr<Texture> take_source_image();
    void give_source(std::shared_ptr<Texture> picture);
    // The chain before its composite, recorded with no encoder open; false when there is nothing to draw.
    bool prepare(Object commands, const Inputs& in, const Params& params);
    // The composite prepare() set up, in the open scene pass `encoder` (an id<MTLRenderCommandEncoder>) whose colour is
    // `color_format` over `samples` samples a pixel.
    void finish(Object encoder, Format color_format, unsigned samples);
    // A picture of the scene target's own format drawn back over the frame, each pixel's channels as they were (the
    // composite's kind 0, which loads the pixel): a capture that took the scene target's texture continues the scene
    // from it. prepare_draw_back with no encoder open (it may make the composite); false: the capture copies instead.
    // draw_back in the open scene pass `encoder`.
    [[nodiscard]] bool prepare_draw_back(Format color_format, unsigned samples);
    void draw_back(Object encoder, Format color_format, unsigned samples, const Texture& picture, unsigned width,
                   unsigned height);
    // Once a frame: hands back the textures no pass has asked for in 600 frames; prepare() makes them again.
    void trim_idle();

private:
    // game_bloom.hlsl's BloomConstants (pushed).
    struct Constants {
        std::uint32_t size[4];
        float sigma[4];
        float params[4];
        float uv[4];
    };
    enum Program : unsigned { kDown, kBox, kBlur, kCompositeVs, kCompositePs, kProgramCount };
    struct Functions {
        std::array<Object, kProgramCount> at{};  // id<MTLFunction>
        bool ok = false;
    };
    bool build();
    void destroy() noexcept;
    [[nodiscard]] Object composite_pipeline(Format color_format, unsigned samples);
    bool ensure(std::shared_ptr<Texture>& target, unsigned width, unsigned height, Format format, bool work, const char* name);
    // The scene target colour's frame into `target` (its format).
    bool copy_frame(Object commands, const Inputs& in, std::shared_ptr<Texture>& target, const char* name);
    // One compute pass: t0-t2 read, u0-u2 written (null: the unused slot's texture).
    void dispatch(Object compute_encoder, Program program, const Constants& c, std::array<const Texture*, 3> sampled,
                  std::array<const Texture*, 3> storage, unsigned width, unsigned height) const;

    bool ready_ = false;
    bool kept_ = false;
    bool source_given_ = false;  // give_source ran since the last prepare
    bool to_build_ = false;  // initialized: poll_ready makes the passes
    Format depth_format_ = Format::Depth32fStencil8;
    std::future<Functions> compiling_;
    Functions functions_{};
    Object sampler_ = nullptr;             // id<MTLSamplerState>: linear clamp
    Object no_depth_ = nullptr;            // id<MTLDepthStencilState>: the composite's
    std::array<Object, 3> compute_{};      // id<MTLComputePipelineState>: down, box, blur
    // The composite per scene pass: by the colour's format (the scene's, the wide scene's) and log2 of its samples.
    struct Composite {
        Format color = Format::None;
        unsigned samples = 0;
        Object pipeline = nullptr;  // id<MTLRenderPipelineState>
    };
    std::array<Composite, 8> composites_{};
    // The pending composite (prepare to finish).
    bool pending_ = false;
    Constants composite_constants_{};
    const Texture* composite_inputs_[3]{};
    unsigned composite_width_ = 0;
    unsigned composite_height_ = 0;
    std::shared_ptr<Texture> dummy_;      // unused slots that are read (a 1 x 1 work texture)
    std::shared_ptr<Texture> dummy_out_;  // unused slots that are written
    std::shared_ptr<Texture> keep_;    // the scene (the scene target's format)
    std::shared_ptr<Texture> source_;  // the simple bloom's scene
    std::shared_ptr<Texture> work_, box_tmp_, mask_, level1_, level2_in_, level2_;
    std::array<std::shared_ptr<Texture>, 3> blur_h_;
    std::array<std::shared_ptr<Texture>, 3> blur2_h_;
    // trim_idle() calls, and the clock at the last use of each group.
    std::uint64_t clock_ = 0;
    std::uint64_t keep_used_ = 0;
    std::uint64_t work_used_ = 0;
    std::uint64_t normal_used_ = 0;
    std::uint64_t simple_used_ = 0;
};

}  // namespace galaxy::render::mtl
