// SPDX-License-Identifier: GPL-3.0-only
// The game's bloom on Vulkan: the operator of game_bloom.h (game_bloom.hlsl, compiled on the device on a worker
// thread) over the RGBA8 EFB. Until it is ready the game draws its own chains (NEBULA_OPTION_BLOOM 0).
#pragma once

#include "galaxy/gx/vk/vk_context.h"

#include <array>
#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <thread>
#include <vector>

#include <vulkan/vulkan_core.h>

namespace galaxy::gx::vk {

class GameBloomVk {
public:
    // The renderer's descriptor sets for one pass (its frame pool, or a replay list's while one is recorded).
    using SetAllocator = std::function<VkDescriptorSet(VkDescriptorSetLayout)>;

    GameBloomVk() = default;
    GameBloomVk(const GameBloomVk&) = delete;
    GameBloomVk& operator=(const GameBloomVk&) = delete;
    ~GameBloomVk() { shutdown(); }

    // Starts compiling the passes.
    void initialize();
    void shutdown() noexcept;
    // Once the worker has finished: builds the compute pipelines (the composite's are made per EFB pass at first use).
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
        Texture* color = nullptr;   // the EFB's single-sample colour (COLOR_ATTACHMENT_OPTIMAL, no render pass open)
        unsigned frame_width = 0;   // the game's frame in EFB pixels (its 640 x 456 times the scale)
        unsigned frame_height = 0;
        unsigned logical_width = 640;
        unsigned logical_height = 456;
        unsigned scale = 1;
    };
    // BloomEffect::preDraw, BloomEffectSimple::draw with the game's own chain: the scene before the bloom works over it.
    bool keep(VkCommandBuffer commands, const Inputs& in);
    // The chain before its composite, recorded with no render pass open; false when there is nothing to draw.
    bool prepare(VkCommandBuffer commands, const Inputs& in, const Params& params, const SetAllocator& allocate);
    // The composite prepare() set up, in the open EFB pass `pass` of 2^samples_log2 samples (RGB; the game's alpha
    // stays).
    void finish(VkCommandBuffer commands, VkRenderPass pass, unsigned samples_log2, const SetAllocator& allocate);
    // The images recorded passes read, for a command buffer submitted again later (the interpolation replay).
    void hold(std::vector<std::shared_ptr<Texture>>& held) const;

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
    // The EFB colour's frame into `target` (its format), left shader-readable.
    bool copy_frame(VkCommandBuffer commands, const Inputs& in, std::shared_ptr<Texture>& target, const char* name);
    // Set 0 for a pass: t0-t2 and u0-u2 (null: the unused slot's image); `first_layout` the first's when given.
    [[nodiscard]] VkDescriptorSet set_of(const SetAllocator& allocate, std::array<const Texture*, 3> sampled,
                                         std::array<const Texture*, 3> storage,
                                         VkImageLayout first_layout = VK_IMAGE_LAYOUT_UNDEFINED) const;
    void dispatch(VkCommandBuffer commands, Program program, const Constants& c, VkDescriptorSet set, unsigned width,
                  unsigned height) const;

    bool ready_ = false;
    bool kept_ = false;
    std::thread worker_;
    std::atomic<bool> compiled_{false};
    std::array<std::vector<std::uint32_t>, kProgramCount> spirv_{};

    VkDescriptorSetLayout image_layout_ = VK_NULL_HANDLE;    // set 0: t0-t2 sampled, u0-u2 storage
    VkDescriptorSetLayout sampler_layout_ = VK_NULL_HANDLE;  // set 1: s0 linear clamp (immutable)
    VkPipelineLayout layout_ = VK_NULL_HANDLE;
    VkSampler sampler_ = VK_NULL_HANDLE;
    VkDescriptorPool sampler_pool_ = VK_NULL_HANDLE;
    VkDescriptorSet sampler_set_ = VK_NULL_HANDLE;
    std::array<VkPipeline, 3> compute_{};  // down, box, blur
    // The composite per EFB pass (by log2 of its samples) and the pass it was made for.
    std::array<VkPipeline, 4> composite_{};
    std::array<VkRenderPass, 4> composite_pass_{};

    // The pending composite (prepare to finish).
    bool pending_ = false;
    Constants composite_constants_{};
    const Texture* composite_inputs_[3]{};
    unsigned composite_width_ = 0;
    unsigned composite_height_ = 0;

    std::shared_ptr<Texture> dummy_;  // unused slots (a 1 x 1 work image)
    std::shared_ptr<Texture> keep_;   // the scene (the EFB's format)
    std::shared_ptr<Texture> source_; // the simple bloom's scene
    std::shared_ptr<Texture> work_, box_tmp_, mask_, level1_, level2_in_, level2_;
    std::array<std::shared_ptr<Texture>, 3> blur_h_;
    std::array<std::shared_ptr<Texture>, 3> blur2_h_;
};

}  // namespace galaxy::gx::vk
