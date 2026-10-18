// SPDX-License-Identifier: GPL-3.0-only
#pragma once

// The Vulkan device every Vulkan GX object shares (docs/VULKAN.md): instance and device through volk, one graphics
// queue, VMA for memory. Every submission gets the next fence value (as D3D12's fence), kept with a VkFence of its
// own, which works on any Vulkan 1.0 device (timeline semaphores need 1.2 or an extension many Android drivers lack).
// Objects released while the GPU may still use them are destroyed once it passes the last submitted value.

#include <volk.h>

#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

struct VmaAllocator_T;
struct VmaAllocation_T;

namespace galaxy::gx::vk {

class Context;

// An image with its views. The render thread tracks `layout`; the last reference hands the Vulkan objects to the
// context, which destroys them after the GPU is done.
struct Texture : std::enable_shared_from_this<Texture> {
    VkImage image = VK_NULL_HANDLE;
    VmaAllocation_T* allocation = nullptr;
    VkImageView view = VK_NULL_HANDLE;         // every level, sampled
    VkImageView target_view = VK_NULL_HANDLE;  // level 0, render target (render textures only)
    VkFramebuffer framebuffer = VK_NULL_HANDLE;  // made by the renderer for its render pass, on first use
    VkFormat format = VK_FORMAT_UNDEFINED;
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::uint32_t levels = 1;
    VkImageLayout layout = VK_IMAGE_LAYOUT_UNDEFINED;
    std::uint64_t allocation_bytes = 0;

    Texture() = default;
    Texture(const Texture&) = delete;
    Texture& operator=(const Texture&) = delete;
    ~Texture();
};

// An owned host-visible, persistently mapped buffer; the last reference defers its destruction as Texture's.
struct Buffer {
    VkBuffer buffer = VK_NULL_HANDLE;
    VmaAllocation_T* allocation = nullptr;
    std::uint8_t* mapped = nullptr;
    std::uint64_t size = 0;

    Buffer() = default;
    Buffer(const Buffer&) = delete;
    Buffer& operator=(const Buffer&) = delete;
    ~Buffer();
};

// A host-visible, persistently mapped buffer the owner destroys (Context::destroy_buffer).
struct MappedBuffer {
    VkBuffer buffer = VK_NULL_HANDLE;
    VmaAllocation_T* allocation = nullptr;
    std::uint8_t* mapped = nullptr;
    std::uint64_t size = 0;
};

class Context {
public:
    static Context& get() noexcept;

    // `prefer_integrated`: GALAXY_GPU_PREFERENCE=low picks the integrated GPU first. `validation` loads the Khronos
    // validation layer when present.
    bool initialize(bool prefer_integrated, bool validation, std::vector<const char*> instance_extensions);
    void shutdown() noexcept;
    [[nodiscard]] bool ready() const noexcept { return device_ != VK_NULL_HANDLE; }

    [[nodiscard]] VkInstance instance() const noexcept { return instance_; }
    [[nodiscard]] VkPhysicalDevice physical_device() const noexcept { return physical_; }
    [[nodiscard]] VkDevice device() const noexcept { return device_; }
    [[nodiscard]] VkQueue queue() const noexcept { return queue_; }
    [[nodiscard]] std::uint32_t queue_family() const noexcept { return queue_family_; }
    [[nodiscard]] VmaAllocator_T* allocator() const noexcept { return allocator_; }
    [[nodiscard]] const VkPhysicalDeviceProperties& properties() const noexcept { return properties_; }
    [[nodiscard]] const std::string& device_name() const noexcept { return device_name_; }
    [[nodiscard]] bool dual_source_blend() const noexcept { return dual_source_blend_; }
    [[nodiscard]] bool logic_op() const noexcept { return logic_op_; }
    [[nodiscard]] bool geometry_shader() const noexcept { return geometry_shader_; }
    [[nodiscard]] bool sampler_anisotropy() const noexcept { return sampler_anisotropy_; }
    [[nodiscard]] bool bc_textures() const noexcept { return bc_textures_; }

    // Submits one command buffer (waiting on `wait_semaphore` at `wait_stage` and signalling `signal_semaphore` when
    // given) and returns its fence value.
    std::uint64_t submit(VkCommandBuffer commands, VkSemaphore wait_semaphore = VK_NULL_HANDLE,
                         VkPipelineStageFlags wait_stage = 0, VkSemaphore signal_semaphore = VK_NULL_HANDLE);
    // The highest fence value the GPU has passed.
    [[nodiscard]] std::uint64_t completed_value();
    // Waits until the GPU has passed `value`; false on a timeout or a lost device.
    bool wait(std::uint64_t value, std::uint64_t timeout_ns = UINT64_MAX);
    [[nodiscard]] std::uint64_t submitted_value() const noexcept {
        return submitted_.load(std::memory_order_acquire);
    }
    // Runs `destroy` once the GPU has passed the next submission, which carries any commands being recorded now
    // (one submitting thread: the render thread).
    void defer(std::function<void()> destroy);
    // Runs the deferred destructions the GPU has passed.
    void collect();

    // RGBA8 (or `format`) image with a sampled view of `levels` levels; `render_target` adds a level-0 target view.
    [[nodiscard]] std::shared_ptr<Texture> create_texture(std::uint32_t width, std::uint32_t height, std::uint32_t levels,
                                                          VkFormat format, VkImageUsageFlags usage,
                                                          bool render_target, const char* name);
    [[nodiscard]] MappedBuffer create_mapped_buffer(std::uint64_t size, VkBufferUsageFlags usage, const char* name);
    [[nodiscard]] std::shared_ptr<Buffer> create_buffer(std::uint64_t size, VkBufferUsageFlags usage, const char* name);
    void destroy_buffer(MappedBuffer& buffer) noexcept;
    void set_name(VkObjectType type, std::uint64_t handle, const char* name) const noexcept;

private:
    struct Deferred {
        std::uint64_t value;
        std::function<void()> destroy;
    };
    VkInstance instance_ = VK_NULL_HANDLE;
    VkDebugUtilsMessengerEXT messenger_ = VK_NULL_HANDLE;
    VkPhysicalDevice physical_ = VK_NULL_HANDLE;
    VkDevice device_ = VK_NULL_HANDLE;
    VkQueue queue_ = VK_NULL_HANDLE;
    std::uint32_t queue_family_ = 0;
    VmaAllocator_T* allocator_ = nullptr;
    VkPhysicalDeviceProperties properties_{};
    std::string device_name_;
    bool dual_source_blend_ = false;
    bool logic_op_ = false;
    bool geometry_shader_ = false;
    bool sampler_anisotropy_ = false;
    bool bc_textures_ = false;
    bool debug_names_ = false;
    struct Pending {
        std::uint64_t value;
        VkFence fence;
    };
    std::mutex submit_mutex_;
    std::vector<Pending> pending_;   // in submission order
    std::vector<VkFence> free_fences_;
    std::atomic<std::uint64_t> submitted_{0};
    std::uint64_t completed_ = 0;
    std::uint32_t waiters_ = 0;  // wait() calls between reading a fence and waiting on it
    std::mutex deferred_mutex_;
    std::vector<Deferred> deferred_;
};

// Throws std::runtime_error naming `what` when `result` is an error.
void check(VkResult result, const char* what);

}  // namespace galaxy::gx::vk
