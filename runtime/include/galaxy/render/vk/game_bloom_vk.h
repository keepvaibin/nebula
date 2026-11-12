// SPDX-License-Identifier: GPL-3.0-only
// The game's bloom on Vulkan: the operator of game_bloom.h (game_bloom.hlsl, compiled on the device on a worker
// thread) over the RGBA8 scene target. Until it is ready the game draws its own chains (NEBULA_OPTION_BLOOM 0).
#pragma once

#include "galaxy/render/vk/vk_context.h"

#include <array>
#include <cstdint>
#include <functional>
#include <memory>
#include <vector>

#include <vulkan/vulkan_core.h>

namespace galaxy::render::vk {

class GameBloomVk {
public:
    // The renderer's descriptor sets for one pass.
    using SetAllocator = std::function<VkDescriptorSet(VkDescriptorSetLayout)>;

    GameBloomVk() = default;
    GameBloomVk(const GameBloomVk&) = delete;
    GameBloomVk& operator=(const GameBloomVk&) = delete;
    ~GameBloomVk() { shutdown(); }

    // Starts compiling the passes.
    void initialize();
    void shutdown() noexcept;
    // Once the worker has finished: builds the compute pipelines (the composite's are made per scene target pass at first use).
    // True when ready.
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
        Texture* color = nullptr;   // the scene target's single-sample colour (COLOR_ATTACHMENT_OPTIMAL, no render pass open)
        unsigned frame_width = 0;   // the frame in scene target pixels
        unsigned frame_height = 0;
        float scale = 1.0f;
    };
    // BloomEffect::preDraw, BloomEffectSimple::draw with the game's own chain: the scene before the bloom works over it.
    bool keep(VkCommandBuffer commands, const Inputs& in);
    // Keeping without a copy: the scene target continues on the image this returns (the hand-over makes one when it
    // does not fit), and its picture is the kept scene. The composite writes the kept scene's alpha with its colour.
    [[nodiscard]] std::shared_ptr<Texture> take_kept_image();
    void keep_picture(std::shared_ptr<Texture> picture);
    // The chain before its composite, recorded with no render pass open; false when there is nothing to draw.
    bool prepare(VkCommandBuffer commands, const Inputs& in, const Params& params, const SetAllocator& allocate);
    // The composite prepare() set up, in the open scene target pass `pass` of 2^samples_log2 samples (RGB; the game's alpha
    // stays).
    void finish(VkCommandBuffer commands, VkRenderPass pass, unsigned samples_log2, const SetAllocator& allocate);
    // A picture of the scene target's own format drawn back over the frame, each pixel's channels as they were (the
    // composite's kind 0, which loads the pixel): a capture that took the scene target's image continues the scene from
    // it. prepare_draw_back with no render pass open (it may make the composite and the unused slot's image); false: the
    // capture copies instead. draw_back in the open scene target pass `pass`, `picture` shader-readable.
    [[nodiscard]] bool prepare_draw_back(VkCommandBuffer commands, VkRenderPass pass, unsigned samples_log2);
    void draw_back(VkCommandBuffer commands, VkRenderPass pass, unsigned samples_log2, const SetAllocator& allocate,
                   const Texture& picture, unsigned width, unsigned height);
    // Once a frame, outside a submission being recorded: hands back the images no pass has asked for in 600 frames (the
    // bloom switched off, a scene without one, the other bloom's kind); prepare() makes them again. The images go
    // through the context's deferred destruction, behind the fence of any submission naming them.
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

    bool build();
    void destroy() noexcept;
    [[nodiscard]] VkShaderModule module(Program program) const;
    [[nodiscard]] VkPipeline composite_pipeline(VkRenderPass pass, unsigned samples_log2);
    bool ensure(std::shared_ptr<Texture>& target, unsigned width, unsigned height, VkFormat format, bool work,
                const char* name);
    // The scene target colour's frame into `target` (its format), left shader-readable.
    bool copy_frame(VkCommandBuffer commands, const Inputs& in, std::shared_ptr<Texture>& target, const char* name);
    // Set 0 for a pass: t0-t2 and u0-u2 (null: the unused slot's image); `first_layout` the first's when given.
    [[nodiscard]] VkDescriptorSet set_of(const SetAllocator& allocate, std::array<const Texture*, 3> sampled,
                                         std::array<const Texture*, 3> storage,
                                         VkImageLayout first_layout = VK_IMAGE_LAYOUT_UNDEFINED) const;
    void dispatch(VkCommandBuffer commands, Program program, const Constants& c, VkDescriptorSet set, unsigned width,
                  unsigned height) const;

    bool ready_ = false;
    bool kept_ = false;
    bool to_build_ = false;  // initialized: poll_ready makes the passes

    VkDescriptorSetLayout image_layout_ = VK_NULL_HANDLE;    // set 0: t0-t2 sampled, u0-u2 storage
    VkDescriptorSetLayout sampler_layout_ = VK_NULL_HANDLE;  // set 1: s0 linear clamp (immutable)
    VkPipelineLayout layout_ = VK_NULL_HANDLE;
    VkSampler sampler_ = VK_NULL_HANDLE;
    VkDescriptorPool sampler_pool_ = VK_NULL_HANDLE;
    VkDescriptorSet sampler_set_ = VK_NULL_HANDLE;
    std::array<VkPipeline, 3> compute_{};  // down, box, blur
    // The composite per scene target pass (by log2 of its samples) and the pass it was made for.
    std::array<VkPipeline, 4> composite_{};
    std::array<VkRenderPass, 4> composite_pass_{};

    // The pending composite (prepare to finish).
    bool pending_ = false;
    Constants composite_constants_{};
    const Texture* composite_inputs_[3]{};
    unsigned composite_width_ = 0;
    unsigned composite_height_ = 0;

    std::shared_ptr<Texture> dummy_;  // unused slots (a 1 x 1 work image)
    std::shared_ptr<Texture> keep_;   // the scene (the scene target's format)
    std::shared_ptr<Texture> source_; // the simple bloom's scene
    std::shared_ptr<Texture> work_, box_tmp_, mask_, level1_, level2_in_, level2_;
    std::array<std::shared_ptr<Texture>, 3> blur_h_;
    std::array<std::shared_ptr<Texture>, 3> blur2_h_;

    // trim_idle() calls, and the clock at the last use of each group: the kept scene, the level 1 images, the normal
    // bloom's level 2 and second blur, the simple bloom's source.
    std::uint64_t clock_ = 0;
    std::uint64_t keep_used_ = 0;
    std::uint64_t work_used_ = 0;
    std::uint64_t normal_used_ = 0;
    std::uint64_t simple_used_ = 0;
};

}  // namespace galaxy::render::vk
