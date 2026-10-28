// SPDX-License-Identifier: GPL-3.0-only
// The Vulkan device every Vulkan GX object shares (docs/VULKAN.md): instance and device through volk, one graphics
// queue, VMA for memory. Every submission gets the next fence value (as D3D12's fence), kept with a VkFence of its
// own, which works on any Vulkan 1.0 device (timeline semaphores need 1.2 or an extension many Android drivers lack).
// Objects released while the GPU may still use them are destroyed once it passes the last submitted value.
#pragma once

#include <volk.h>

#include "galaxy/gx/vk/device_quirks_vk.h"

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <span>
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

// An owned host-visible, persistently mapped buffer with a GX address (gpu::Address, Context::resolve); the last
// reference defers its destruction as Texture's.
struct Buffer {
    VkBuffer buffer = VK_NULL_HANDLE;
    VmaAllocation_T* allocation = nullptr;
    std::byte* mapped = nullptr;
    std::uint64_t size = 0;
    std::uint64_t address = 0;

    Buffer() = default;
    Buffer(const Buffer&) = delete;
    Buffer& operator=(const Buffer&) = delete;
    ~Buffer();
};

// Where a buffer's memory lies. Host: host memory the GPU reads across the bus (D3D12's upload heap), for data
// written each frame. DeviceMapped: device memory the CPU writes where the device maps it (unified memory, resizable
// BAR), else host memory, for data the GPU reads every frame. Device: device memory only, for what the GPU writes.
enum class BufferPlace : std::uint8_t { Host, DeviceMapped, Device };

// A persistently mapped buffer (unmapped in BufferPlace::Device) without a GX address (create_buffer wraps one in a
// Buffer).
struct MappedBuffer {
    VkBuffer buffer = VK_NULL_HANDLE;
    VmaAllocation_T* allocation = nullptr;
    std::byte* mapped = nullptr;
    std::uint64_t size = 0;
};

class Context {
public:
    static Context& get() noexcept;

    // `prefer_integrated`: GALAXY_GPU_PREFERENCE=low picks the integrated GPU first. `validation` loads the Khronos
    // validation layer when present. `create_surface`, when given, makes the window's surface once the instance
    // exists (returned in `surface`); the device chosen then presents to it.
    bool initialize(bool prefer_integrated, bool validation, std::vector<const char*> instance_extensions,
                    const std::function<VkSurfaceKHR(VkInstance)>& create_surface = {},
                    VkSurfaceKHR* surface = nullptr);
    void shutdown() noexcept;
    [[nodiscard]] bool ready() const noexcept { return device_ != VK_NULL_HANDLE; }
    // Why the last initialize failed (for the player's error message).
    [[nodiscard]] const std::string& failure() const noexcept { return failure_; }

    [[nodiscard]] VkInstance instance() const noexcept { return instance_; }
    [[nodiscard]] VkPhysicalDevice physical_device() const noexcept { return physical_; }
    [[nodiscard]] VkDevice device() const noexcept { return device_; }
    [[nodiscard]] VkQueue queue() const noexcept { return queue_; }
    [[nodiscard]] std::uint32_t queue_family() const noexcept { return queue_family_; }
    [[nodiscard]] VmaAllocator_T* allocator() const noexcept { return allocator_; }
    [[nodiscard]] const VkPhysicalDeviceProperties& properties() const noexcept { return properties_; }
    [[nodiscard]] const std::string& device_name() const noexcept { return device_name_; }
    [[nodiscard]] const DeviceIdentity& identity() const noexcept { return identity_; }
    [[nodiscard]] const DeviceQuirks& quirks() const noexcept { return quirks_; }
    [[nodiscard]] bool dual_source_blend() const noexcept { return dual_source_blend_; }
    [[nodiscard]] bool logic_op() const noexcept { return logic_op_; }
    [[nodiscard]] bool geometry_shader() const noexcept { return geometry_shader_; }
    [[nodiscard]] bool tessellation_shader() const noexcept { return tessellation_shader_; }
    [[nodiscard]] bool sampler_anisotropy() const noexcept { return sampler_anisotropy_; }
    // shaderImageGatherExtended: a gather of one chosen channel (FSR 1 EASU's GatherRed/Green/Blue).
    [[nodiscard]] bool gather_extended() const noexcept { return gather_extended_; }
    // pipelineStatisticsQuery: GALAXY_GPU_PASSES' shader invocation counts.
    [[nodiscard]] bool pipeline_statistics() const noexcept { return pipeline_statistics_; }
    [[nodiscard]] bool depth_clamp() const noexcept { return depth_clamp_; }
    // VK_EXT_depth_clip_enable: clamping without giving up clipping (D3D's rasterizer).
    [[nodiscard]] bool depth_clip_enable() const noexcept { return depth_clip_enable_; }
    [[nodiscard]] bool display_timing() const noexcept { return display_timing_; }
    // The largest device-local heap's use and budget (the driver's with VK_EXT_memory_budget, else VMA's estimate).
    bool device_memory_budget(std::uint64_t& usage, std::uint64_t& budget) const noexcept;

    // Submits command buffers in order (waiting on `wait_semaphore` at `wait_stage` and signalling `signal_semaphore`
    // when given) and returns their fence value.
    std::uint64_t submit(std::span<const VkCommandBuffer> commands, VkSemaphore wait_semaphore = VK_NULL_HANDLE,
                         VkPipelineStageFlags wait_stage = 0, VkSemaphore signal_semaphore = VK_NULL_HANDLE);
    std::uint64_t submit(VkCommandBuffer commands) { return submit(std::span<const VkCommandBuffer>(&commands, 1u)); }
    // A frame's submission (the renderer's end_frame): objects released since the previous one (defer) are destroyed
    // once it completes.
    std::uint64_t submit_frame(std::span<const VkCommandBuffer> commands, VkSemaphore wait_semaphore = VK_NULL_HANDLE,
                               VkPipelineStageFlags wait_stage = 0, VkSemaphore signal_semaphore = VK_NULL_HANDLE);
    // The highest fence value the GPU has passed.
    [[nodiscard]] std::uint64_t completed_value();
    // Waits until the GPU has passed `value`; false on a timeout or a lost device.
    bool wait(std::uint64_t value, std::uint64_t timeout_ns = UINT64_MAX);
    // Runs `destroy` once the GPU has passed the next frame submission (submit_frame), which carries any commands
    // being recorded now; peeks and other submissions in between carry none of them (one submitting thread: the
    // render thread).
    void defer(std::function<void()> destroy);
    // Runs the deferred destructions the GPU has passed.
    void collect();

    // RGBA8 (or `format`) image with a sampled view of `levels` levels; `render_target` adds a level-0 target view.
    // `samples` per pixel above one: a multisampled target (one level).
    // Replaces an R8 texture's sampled view with one reading R in every channel (GX's I8). False if it fails.
    bool grey_view(Texture& texture) const;
    [[nodiscard]] std::shared_ptr<Texture> create_texture(std::uint32_t width, std::uint32_t height, std::uint32_t levels,
                                                          VkFormat format, VkImageUsageFlags usage,
                                                          bool render_target, const char* name,
                                                          VkSampleCountFlagBits samples = VK_SAMPLE_COUNT_1_BIT);
    // A linear-tiled transfer image (DeviceQuirks::linear_readback); empty when the device cannot copy `format` to
    // and from linear images.
    [[nodiscard]] std::shared_ptr<Texture> create_linear_image(std::uint32_t width, std::uint32_t height,
                                                               VkFormat format, const char* name);
    [[nodiscard]] MappedBuffer create_mapped_buffer(std::uint64_t size, VkBufferUsageFlags usage, const char* name,
                                                    BufferPlace place = BufferPlace::Host);
    // A buffer with a GX address.
    [[nodiscard]] std::shared_ptr<Buffer> create_buffer(std::uint64_t size, VkBufferUsageFlags usage, const char* name,
                                                        BufferPlace place = BufferPlace::Host);
    // A mapped transfer-destination buffer the CPU reads (readbacks), in cached host memory (coherent where the driver
    // reads that back faster: DeviceQuirks::coherent_readback); no GX address.
    [[nodiscard]] std::shared_ptr<Buffer> create_readback_buffer(std::uint64_t size, const char* name);
    // Makes the GPU's writes to a readback buffer visible to the CPU (after its fence).
    void invalidate(const Buffer& buffer) const noexcept;
    void set_name(VkObjectType type, std::uint64_t handle, const char* name) const noexcept;

    // The GX address space (gpu::Address): an address's top bits name a buffer and its low kAddressShift bits a byte
    // in it, so the shared code's address arithmetic works; draws turn addresses back into (buffer, offset).
    static constexpr unsigned kAddressShift = 40;
    struct BufferOffset {
        VkBuffer buffer = VK_NULL_HANDLE;
        VkDeviceSize offset = 0;
    };
    [[nodiscard]] BufferOffset resolve(std::uint64_t address) const noexcept {
        const std::uint64_t id = address >> kAddressShift;
        if (id == 0u || id >= kAddressSlots) {
            return {};
        }
        return {address_slots_[id].load(std::memory_order_acquire),
                address & ((std::uint64_t{1} << kAddressShift) - 1u)};
    }
    // Frees a buffer's address once nothing recorded can name it (the buffer's deferred destruction).
    void release_address(std::uint64_t address) noexcept;

private:
    static constexpr std::size_t kAddressSlots = 4096;
    [[nodiscard]] std::uint64_t register_address(VkBuffer buffer);
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
    std::string failure_;
    DeviceIdentity identity_;
    DeviceQuirks quirks_;
    bool dual_source_blend_ = false;
    bool logic_op_ = false;
    bool geometry_shader_ = false;
    bool tessellation_shader_ = false;
    bool sampler_anisotropy_ = false;
    bool gather_extended_ = false;
    bool pipeline_statistics_ = false;
    bool bc_textures_ = false;
    bool memory_budget_ = false;
    bool display_timing_ = false;  // VK_GOOGLE_display_timing
    bool depth_clamp_ = false;
    bool depth_clip_enable_ = false;
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
    std::array<std::atomic<VkBuffer>, kAddressSlots> address_slots_{};
    std::mutex address_mutex_;
    std::vector<std::uint32_t> free_address_ids_;
    std::uint32_t next_address_id_ = 1;  // 0 is the null address
};

// Throws std::runtime_error naming `what` when `result` is an error.
void check(VkResult result, const char* what);

// The calling thread's CPU time (nanoseconds; 0 where the platform has no per-thread clock).
[[nodiscard]] std::int64_t thread_cpu_ns() noexcept;
// CPU time spent inside driver calls since the last take (a driver that spins where it should sleep shows here):
// RendererVk logs it with the render thread's own every 10 seconds.
enum class DriverCall : unsigned { Acquire, Present, Submit, Wait, Poll, Count };
void note_driver_cpu(DriverCall call, std::int64_t ns) noexcept;
[[nodiscard]] std::array<std::int64_t, static_cast<unsigned>(DriverCall::Count)> take_driver_cpu() noexcept;

}  // namespace galaxy::gx::vk
