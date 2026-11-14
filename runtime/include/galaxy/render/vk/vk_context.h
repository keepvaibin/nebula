// SPDX-License-Identifier: GPL-3.0-only
// The Vulkan device every Vulkan object shares (docs/VULKAN.md): instance and device through volk, one graphics
// queue, VMA for memory. Every submission gets the next fence value (as D3D12's fence): one timeline semaphore the
// submissions signal in turn where the device has timeline semaphores (core 1.2 or VK_KHR_timeline_semaphore), else a
// VkFence of its own each, which works on any Vulkan 1.0 device (many Android drivers lack timeline semaphores).
// Objects released while the GPU may still use them are destroyed once it passes the last submitted value.
#pragma once

#include <algorithm>
#include <volk.h>

#include "galaxy/render/rt_caps.h"
#include "galaxy/render/vk/device_quirks_vk.h"
#include "galaxy/render/vk/shader_compile_vk.h"

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

namespace galaxy::render::vk {

// The scene: the art's own blending, in gamma, at 10 bits a channel. No shader reads its alpha.
inline constexpr VkFormat kSceneColorFormat = VK_FORMAT_A2B10G10R10_UNORM_PACK32;
// A texture target's colour: the game keeps masks in its alpha.
inline constexpr VkFormat kTargetColorFormat = VK_FORMAT_R8G8B8A8_UNORM;
// The effects layer's colour and transmittance (RendererD3D12's kLayerFormat): in the scene target's own encoding.
inline constexpr VkFormat kLayerFormat = VK_FORMAT_R16G16B16A16_SFLOAT;
// A material's constants as a draw's binding sees them: 4096 rows, what Direct3D 12's constant buffer view holds, cut to
// the device's maxUniformBufferRange where that is less. A dynamic binding's whole range must lie in its buffer at any
// offset, so every upload buffer has this much room past its end (create_upload_memory).
inline constexpr std::uint64_t kMaterialConstantRange = 65536u;

class Context;

// An image with its views. The render thread tracks `layout`; the last reference hands the Vulkan objects to the
// context, which destroys them after the GPU is done.
struct Texture : std::enable_shared_from_this<Texture> {
    VkImage image = VK_NULL_HANDLE;
    VmaAllocation_T* allocation = nullptr;
    VkImageView view = VK_NULL_HANDLE;         // every level, sampled
    VkImageView target_view = VK_NULL_HANDLE;  // level 0, render target (render textures only)
    VkFramebuffer framebuffer = VK_NULL_HANDLE;  // made by the renderer for its render pass, on first use
    // The scene target pass's framebuffer with this image as the scene target's colour (a display copy's hand-over, E-10) and the scene target
    // depth target whose serial is scene_depth_serial.
    VkFramebuffer scene_framebuffer = VK_NULL_HANDLE;
    std::uint64_t scene_depth_serial = 0;
    VkFormat format = VK_FORMAT_UNDEFINED;
    VkImageUsageFlags usage = 0;
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

// An owned host-visible, persistently mapped buffer with a GPU address (gpu::Address, Context::resolve); the last
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

// A persistently mapped buffer (unmapped in BufferPlace::Device) without a GPU address (create_buffer wraps one in a
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

    // `chosen_gpu`: the GPU chosen in Nebula Settings (RenderConfig::gpu, gpu_identity), taken first when present.
    // A discrete GPU comes first otherwise. `validation` (--gpu-validation) loads the
    // Khronos validation layer when present. `create_surface`, when given, makes the window's surface once the
    // instance exists (returned in `surface`); the device chosen then presents to it. `ray_tracing`: the device is made
    // with what the tracer needs, where it has it (rt_caps()).
    bool initialize(const std::string& chosen_gpu, bool validation,
                    std::vector<const char*> instance_extensions,
                    const std::function<VkSurfaceKHR(VkInstance)>& create_surface = {},
                    VkSurfaceKHR* surface = nullptr, bool ray_tracing = false);
    void shutdown() noexcept;
    [[nodiscard]] bool bc_textures() const noexcept { return bc_textures_; }
    [[nodiscard]] bool astc_textures() const noexcept { return astc_textures_; }
    [[nodiscard]] bool ready() const noexcept { return device_ != VK_NULL_HANDLE; }
    // Why the last initialize failed (for the player's error message).
    [[nodiscard]] const std::string& failure() const noexcept { return failure_; }

    [[nodiscard]] VkInstance instance() const noexcept { return instance_; }
    [[nodiscard]] VkPhysicalDevice physical_device() const noexcept { return physical_; }
    [[nodiscard]] VkDevice device() const noexcept { return device_; }
    [[nodiscard]] VkQueue queue() const noexcept { return queue_; }
    [[nodiscard]] VkQueue generation_present_queue() const noexcept { return generation_present_queue_; }
    [[nodiscard]] VkQueue generation_acquire_queue() const noexcept { return generation_acquire_queue_; }
    [[nodiscard]] std::uint32_t queue_family() const noexcept { return queue_family_; }
    // Frame sync on one timeline semaphore (completed_value reads it, wait waits on it) rather than a fence per submission.
    [[nodiscard]] bool timeline_semaphores() const noexcept { return timeline_ != VK_NULL_HANDLE; }
    // A queue of a family that only transfers (no graphics or compute), when the device has one and timeline semaphores:
    // textures are copied there beside the frame (texture_backend_vk.cpp). Null otherwise.
    [[nodiscard]] VkQueue transfer_queue() const noexcept { return transfer_queue_; }
    [[nodiscard]] std::uint32_t transfer_family() const noexcept { return transfer_family_; }
    // Submits one command buffer to the transfer queue; it signals the transfer timeline semaphore, whose value is returned.
    std::uint64_t submit_transfer(VkCommandBuffer commands);
    // The highest transfer value submitted, and the highest it has passed.
    [[nodiscard]] std::uint64_t transfer_submitted() const noexcept { return transfer_submitted_.load(std::memory_order_acquire); }
    [[nodiscard]] std::uint64_t transfer_completed();
    // Waits until the transfer queue has passed `value`; false on a timeout or a lost device.
    bool wait_transfer(std::uint64_t value, std::uint64_t timeout_ns = UINT64_MAX);
    [[nodiscard]] VmaAllocator_T* allocator() const noexcept { return allocator_; }
    [[nodiscard]] const VkPhysicalDeviceProperties& properties() const noexcept { return properties_; }
    [[nodiscard]] const std::string& device_name() const noexcept { return device_name_; }
    // The device as config.ini's gpu key names it (gpu_identity); its PCI subsystem id (Windows: the DXGI adapter's
    // with the same LUID; 0 elsewhere) and LUID (0 when the driver gives none).
    [[nodiscard]] const std::string& gpu_id() const noexcept { return gpu_id_; }
    [[nodiscard]] std::uint32_t subsys_id() const noexcept { return subsys_id_; }
    [[nodiscard]] std::uint64_t luid() const noexcept { return luid_; }
    [[nodiscard]] const DeviceIdentity& identity() const noexcept { return identity_; }
    [[nodiscard]] const DeviceQuirks& quirks() const noexcept { return quirks_; }
    [[nodiscard]] bool sample_rate_shading() const noexcept { return sample_rate_shading_; }
    // Blend states that differ between a pipeline's colour attachments (the motion and reactive passes, the effects layer).
    [[nodiscard]] bool independent_blend() const noexcept { return independent_blend_; }
    // VK_EXT_debug_utils is on (the validation layer or RenderDoc's): objects are named and passes labelled.
    [[nodiscard]] bool debug_labels() const noexcept { return debug_names_; }
    [[nodiscard]] bool sampler_anisotropy() const noexcept { return sampler_anisotropy_; }
    // shaderImageGatherExtended: a gather of one chosen channel (FSR 1 EASU's GatherRed/Green/Blue).
    [[nodiscard]] bool gather_extended() const noexcept { return gather_extended_; }
    // shaderStorageImageExtendedFormats, enabled where offered: storage images of R16G16 float, R8 and the like.
    [[nodiscard]] bool storage_extended_formats() const noexcept { return storage_extended_formats_; }
    // pipelineStatisticsQuery: --diagnostics gpu-passes' shader invocation counts.
    [[nodiscard]] bool pipeline_statistics() const noexcept { return pipeline_statistics_; }
    [[nodiscard]] bool depth_clamp() const noexcept { return depth_clamp_; }
    // VK_EXT_depth_clip_enable: clamping without giving up clipping (D3D's rasterizer).
    [[nodiscard]] bool depth_clip_enable() const noexcept { return depth_clip_enable_; }
    // What FSR 3's compute passes need, enabled (temporal_upscaler_vk.cpp).
    [[nodiscard]] bool temporal_upscaling() const noexcept { return temporal_upscaling_; }
    // Descriptor indexing, decided in initialize: true when every texture sits in one update-after-bind array and every
    // sampler in another (runtime-sized, partially bound, update after bind and while pending, variable count, dynamic
    // indexing of sampled images, and update-after-bind limits that hold kBindlessTextureSlotsMin images and
    // kBindlessSamplerSlots samplers), enabled here. False: draws bind
    // their own tables of kBoundTableSlots maps (program.hlsli NEBULA_BOUND_TABLES).
    [[nodiscard]] bool bindless() const noexcept { return bindless_; }
    // With bindless(): the most image slots one array can hold on this device (its update-after-bind limits, room for the old
    // array beside a larger one while the frames that read it finish); the renderer allocates what the views need.
    [[nodiscard]] std::uint32_t texture_array_max() const noexcept { return texture_array_max_; }
    // The most maps one bound table can hold on this device: its per-stage image, sampler and resource limits.
    [[nodiscard]] std::uint32_t table_slots() const noexcept { return table_slots_; }
    [[nodiscard]] bool device_extension_enabled(const char* name) const noexcept {
        return std::find(enabled_device_extensions_.begin(), enabled_device_extensions_.end(), name) != enabled_device_extensions_.end();
    }
    // What XeSS asked for, enabled (Context::create).
    [[nodiscard]] bool xess_upscaling() const noexcept { return xess_upscaling_; }
    [[nodiscard]] bool display_timing() const noexcept { return display_timing_; }
    // VK_KHR_present_wait with VK_KHR_present_id, enabled: vkWaitForPresentKHR waits until a presented frame is on the screen.
    [[nodiscard]] bool present_wait() const noexcept { return present_wait_; }
    // VK_EXT_swapchain_colorspace on the instance (the surface can list HDR10 formats) and VK_EXT_hdr_metadata on the device
    // (vkSetHdrMetadataEXT), each where it is here.
    [[nodiscard]] bool swapchain_colorspace() const noexcept { return swapchain_colorspace_; }
    [[nodiscard]] bool hdr_metadata() const noexcept { return hdr_metadata_; }
    // Swapchain maintenance 1, enabled with its surface extension on the instance: a swap chain made with a list of present
    // modes takes any of them per present (VkSwapchainPresentModeInfoEXT).
    [[nodiscard]] bool present_mode_switching() const noexcept { return present_mode_switching_; }
    // The surface extension of swapchain maintenance 1 was enabled on the instance (the capability query of a mode's compatible
    // modes needs it).
    [[nodiscard]] bool surface_maintenance() const noexcept { return surface_maintenance_; }
    // VK_EXT_graphics_pipeline_library enabled on a driver that links fast: pipelines are linked from parts (pipelines_vk.cpp).
    [[nodiscard]] bool pipeline_libraries() const noexcept { return pipeline_libraries_; }
    // VK_EXT_host_image_copy was enabled (the device has it and its feature, and copies into SHADER_READ_ONLY_OPTIMAL): a
    // texture made with VK_IMAGE_USAGE_HOST_TRANSFER_BIT_EXT is written from host memory with no command (texture_backend_vk.cpp).
    [[nodiscard]] bool host_image_copy() const noexcept { return host_image_copy_; }
    // The device maps video memory for the CPU in bulk (resizable BAR): a device-local host-visible coherent type in a heap
    // larger than the 256 MB window, on a device that also has video memory the CPU cannot map. BufferPlace::DeviceMapped
    // buffers lie there; elsewhere they are Host.
    [[nodiscard]] bool device_mapped_memory() const noexcept { return device_mapped_memory_; }
    // Test only (GALAXY_TEST_DEVICE_LOST): the next submission reports VK_ERROR_DEVICE_LOST without reaching the driver, as a
    // lost device would; no API removes a Vulkan device. The fault report is skipped (the device is not lost).
    void simulate_device_loss() noexcept { simulated_loss_.store(true, std::memory_order_release); }
    // What the device reports for ray tracing (acceleration structures, ray queries, device addresses on Vulkan 1.2),
    // and whether it was made with them: `enabled` only when initialize was asked to and the device has it all.
    [[nodiscard]] const RtCaps& rt_caps() const noexcept { return rt_caps_; }
    // VK_EXT_device_fault was enabled (the device has the extension and its feature).
    [[nodiscard]] bool device_fault() const noexcept { return device_fault_; }
    // After VK_ERROR_DEVICE_LOST (`where` names the call): what vkGetDeviceFaultInfoEXT knows, to the log, once per device.
    // Test builds also write the vendor binary beside the run log (logs/device_fault_<vendor>_<device>.bin in the settings folder). Nothing without the
    // extension; never throws.
    void report_device_fault(const char* where) noexcept;
    // vkCmdPipelineBarrier2: the core function on a Vulkan 1.3 device with the feature, the KHR one on a 1.2 device with
    // VK_KHR_synchronization2, else null (barriers_vk.h falls back to version-1 barriers).
    [[nodiscard]] PFN_vkCmdPipelineBarrier2 pipeline_barrier2() const noexcept { return pipeline_barrier2_; }
    // The largest device-local heap's use and budget (the driver's with VK_EXT_memory_budget, else VMA's estimate).
    bool device_memory_budget(std::uint64_t& usage, std::uint64_t& budget) const noexcept;
    // The allocator's own count of memory it holds for one group of heaps: blocks taken from the driver and the bytes the
    // images and buffers occupy in them (the `[vram] breakdown` line).
    struct HeapStats {
        std::uint64_t block_bytes = 0u, allocation_bytes = 0u;
        std::uint32_t blocks = 0u, allocations = 0u;
    };
    // `device_local`: the largest device-local heap (device_memory_budget's); `other`: every other heap, summed. False: no allocator.
    bool heap_stats(HeapStats& device_local, HeapStats& other) const noexcept;

    // Submits command buffers in order (waiting on `wait_semaphore` at `wait_stage` and signalling `signal_semaphore`
    // when given; `transfer_wait`, when not 0, also waits for the transfer queue to pass that value before the fragment
    // and transfer stages) and returns their fence value.
    std::uint64_t submit(std::span<const VkCommandBuffer> commands, VkSemaphore wait_semaphore = VK_NULL_HANDLE,
                         VkPipelineStageFlags wait_stage = 0, VkSemaphore signal_semaphore = VK_NULL_HANDLE,
                         std::uint64_t transfer_wait = 0);
    std::uint64_t submit(VkCommandBuffer commands) { return submit(std::span<const VkCommandBuffer>(&commands, 1u)); }
    // A frame's submission (the renderer's end_frame): objects released since the previous one (defer) are destroyed
    // once it completes.
    std::uint64_t submit_frame(std::span<const VkCommandBuffer> commands, VkSemaphore wait_semaphore = VK_NULL_HANDLE,
                               VkPipelineStageFlags wait_stage = 0, VkSemaphore signal_semaphore = VK_NULL_HANDLE,
                               std::uint64_t transfer_wait = 0);
    // The highest fence value the GPU has passed.
    [[nodiscard]] std::uint64_t completed_value();
    // Waits until the GPU has passed `value`; false on a timeout or a lost device.
    bool wait(std::uint64_t value, std::uint64_t timeout_ns = UINT64_MAX);
    // Runs `destroy` once the GPU has passed the next frame submission (submit_frame), which carries any commands
    // being recorded now; other submissions in between carry none of them (one submitting thread: the
    // render thread).
    void defer(std::function<void()> destroy);
    // Runs the deferred destructions the GPU has passed.
    void collect();

    // RGBA8 (or `format`) image with a sampled view of `levels` levels; `render_target` adds a level-0 target view.
    // `samples` per pixel above one: a multisampled target (one level).
    // Replaces an R8 texture's sampled view with one reading R in every channel. False if it fails.
    bool grey_view(Texture& texture) const;
    [[nodiscard]] std::shared_ptr<Texture> create_texture(std::uint32_t width, std::uint32_t height, std::uint32_t levels,
                                                          VkFormat format, VkImageUsageFlags usage,
                                                          bool render_target, const char* name,
                                                          VkSampleCountFlagBits samples = VK_SAMPLE_COUNT_1_BIT);
    [[nodiscard]] MappedBuffer create_mapped_buffer(std::uint64_t size, VkBufferUsageFlags usage, const char* name,
                                                    BufferPlace place = BufferPlace::Host);
    // A buffer with a GPU address.
    [[nodiscard]] std::shared_ptr<Buffer> create_buffer(std::uint64_t size, VkBufferUsageFlags usage, const char* name,
                                                        BufferPlace place = BufferPlace::Host);
    // A mapped transfer-destination buffer the CPU reads (readbacks), in cached host memory (coherent where the driver
    // reads that back faster: DeviceQuirks::coherent_readback); no GPU address. `usage` adds to transfer destination
    // (a storage buffer a compute shader writes).
    [[nodiscard]] std::shared_ptr<Buffer> create_readback_buffer(std::uint64_t size, const char* name,
                                                                 VkBufferUsageFlags usage = 0u);
    // Makes the GPU's writes to a readback buffer visible to the CPU (after its fence).
    void invalidate(const Buffer& buffer) const noexcept;
    void set_name(VkObjectType type, std::uint64_t handle, const char* name) const noexcept;

    // The GPU address space (gpu::Address): an address's top bits name a buffer and its low kAddressShift bits a byte
    // in it, so the shared code's address arithmetic works; draws turn addresses back into (buffer, offset).
    static constexpr unsigned kAddressShift = 40;
    struct BufferOffset {
        VkBuffer buffer = VK_NULL_HANDLE;
        VkDeviceSize offset = 0;
    };
    [[nodiscard]] BufferOffset resolve(std::uint64_t address) const noexcept {
        const std::uint64_t id = address >> kAddressShift;
        if (id == 0u) {
            return {};
        }
        const AddressPage* page = address_pages_[id >> kAddressPageBits].load(std::memory_order_acquire);
        return {page ? page->slots[id & (kAddressPageSize - 1u)].load(std::memory_order_acquire) : VK_NULL_HANDLE,
                address & ((std::uint64_t{1} << kAddressShift) - 1u)};
    }
    // Frees a buffer's address once nothing recorded can name it (the buffer's deferred destruction).
    void release_address(std::uint64_t address) noexcept;

private:
    // Sparse pages cover every buffer ID in the 24/40-bit address partition.
    static constexpr unsigned kAddressPageBits = 12;
    static constexpr std::size_t kAddressPageSize = std::size_t{1} << kAddressPageBits;
    static constexpr std::uint32_t kAddressIds = std::uint32_t{1} << (64u - kAddressShift);
    struct AddressPage { std::array<std::atomic<VkBuffer>, kAddressPageSize> slots{}; };
    [[nodiscard]] std::uint64_t register_address(VkBuffer buffer);
    struct Deferred {
        std::uint64_t value;
        std::function<void()> destroy;
        std::uint64_t transfer = 0;  // the transfer queue's value at the call: it may still copy into what is released
    };
    VkInstance instance_ = VK_NULL_HANDLE;
    VkDebugUtilsMessengerEXT messenger_ = VK_NULL_HANDLE;
    VkPhysicalDevice physical_ = VK_NULL_HANDLE;
    VkDevice device_ = VK_NULL_HANDLE;
    VkQueue queue_ = VK_NULL_HANDLE;
    VkQueue generation_present_queue_ = VK_NULL_HANDLE;
    VkQueue generation_acquire_queue_ = VK_NULL_HANDLE;
    VkQueue transfer_queue_ = VK_NULL_HANDLE;
    std::uint32_t transfer_family_ = 0;
    VkSemaphore timeline_ = VK_NULL_HANDLE;           // the graphics queue's submissions, in turn
    VkSemaphore transfer_timeline_ = VK_NULL_HANDLE;  // the transfer queue's
    PFN_vkGetSemaphoreCounterValue timeline_value_ = nullptr;
    PFN_vkWaitSemaphores timeline_wait_ = nullptr;
    std::atomic<std::uint64_t> transfer_submitted_{0};
    std::uint32_t queue_family_ = 0;
    VmaAllocator_T* allocator_ = nullptr;
    VkPhysicalDeviceProperties properties_{};
    std::string device_name_;
    std::string gpu_id_;
    std::uint32_t subsys_id_ = 0;
    std::uint64_t luid_ = 0;
    std::string failure_;
    DeviceIdentity identity_;
    DeviceQuirks quirks_;
    bool sample_rate_shading_ = false;
    bool independent_blend_ = false;
    bool sampler_anisotropy_ = false;
    bool gather_extended_ = false;
    bool storage_extended_formats_ = false;
    bool pipeline_statistics_ = false;
    bool bc_textures_ = false;
    bool astc_textures_ = false;
    bool memory_budget_ = false;
    // device_memory_budget's last read of the driver's figures (VK_EXT_memory_budget): at most every 100 ms; render thread.
    mutable std::int64_t memory_read_ms_ = 0;
    mutable std::uint64_t memory_usage_ = 0u;
    mutable std::uint64_t memory_budget_bytes_ = 0u;
    bool display_timing_ = false;  // VK_GOOGLE_display_timing
    bool present_wait_ = false;             // VK_KHR_present_wait and present_id, enabled
    bool pipeline_libraries_ = false;       // VK_EXT_graphics_pipeline_library, enabled, fast linking
    bool present_mode_switching_ = false;   // swapchain maintenance 1, enabled
    bool surface_maintenance_ = false;      // the instance has the surface extension of that kind (decided in initialize)
    bool surface_maintenance_khr_ = false;  // ... in its KHR form (else EXT)
    bool swapchain_colorspace_ = false;     // VK_EXT_swapchain_colorspace, enabled on the instance
    bool hdr_metadata_ = false;             // VK_EXT_hdr_metadata, enabled on the device
    bool host_image_copy_ = false;      // VK_EXT_host_image_copy, enabled, with the layout the textures rest in
    bool device_mapped_memory_ = false; // see device_mapped_memory()
    RtCaps rt_caps_{};
    bool device_fault_ = false;         // VK_EXT_device_fault, enabled
    bool device_fault_binary_ = false;  // and its vendor binary feature (--diagnostics device-faults)
    std::atomic<bool> fault_reported_{false};
    std::atomic<bool> simulated_loss_{false};  // simulate_device_loss: asked, not yet submitted
    bool loss_was_simulated_ = false;          // the last loss was a simulated one: no fault to ask the driver for
    PFN_vkCmdPipelineBarrier2 pipeline_barrier2_ = nullptr;
    bool depth_clamp_ = false;
    bool depth_clip_enable_ = false;
    bool temporal_upscaling_ = false;
    bool bindless_ = false;
    std::uint32_t texture_array_max_ = 0;
    std::uint32_t table_slots_ = 0;
    std::vector<std::string> enabled_device_extensions_;
    bool xess_upscaling_ = false;
    bool xess_instance_ready_ = false;
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
    std::array<std::atomic<AddressPage*>, kAddressIds / kAddressPageSize> address_pages_{};
    std::mutex address_mutex_;
    std::vector<std::uint32_t> free_address_ids_;
    std::uint32_t next_address_id_ = 1;  // 0 is the null address
};

// Throws std::runtime_error naming `what` when `result` is an error.
void check(VkResult result, const char* what);

// The driver's pipeline cache the program pipelines are made with (core/pipelines_vk.cpp): read from the cache file the first
// time and written back with it. A pipeline made outside the program table passes it too, so a later start loads that pipeline
// from the file instead of compiling it. Internally synchronised (no VK_PIPELINE_CACHE_CREATE_EXTERNALLY_SYNCHRONIZED_BIT).
[[nodiscard]] VkPipelineCache shared_pipeline_cache();
// Writes the shared cache to its file and destroys it (Context::shutdown, before the device goes): a device made later, as
// after a failed start or a lost device, gets a cache of its own.
void release_shared_pipeline_cache() noexcept;

// The calling thread's CPU time (nanoseconds; 0 where the platform has no per-thread clock).
[[nodiscard]] std::int64_t thread_cpu_ns() noexcept;
// CPU time spent inside driver calls since the last take (a driver that spins where it should sleep shows here):
// RendererVk logs it with the render thread's own every 10 seconds.
enum class DriverCall : unsigned { Acquire, Present, Submit, Wait, Poll, Count };
void note_driver_cpu(DriverCall call, std::int64_t ns) noexcept;
[[nodiscard]] std::array<std::int64_t, static_cast<unsigned>(DriverCall::Count)> take_driver_cpu() noexcept;

}  // namespace galaxy::render::vk
