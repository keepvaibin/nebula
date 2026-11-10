// SPDX-License-Identifier: GPL-3.0-only
// The Metal device every Metal object shares (docs/APPLE.md): the device, one command queue, and what the device says
// it has. As vk::Context for Vulkan: every committed command buffer gets the next fence value, objects released while
// the GPU may still use them are destroyed once it passes the next frame's, and a GPU address (gpu::Address) names a
// buffer and a byte in it.
//
// The header is plain C++ (the renderer's shared code includes it): Metal's objects are held as untyped pointers that
// own a reference each, and the Objective-C++ sources (.mm) turn them back.
#pragma once

#include <array>
#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace galaxy::render::mtl {

// A Metal object (id<MTLDevice>, id<MTLTexture>, ...) with one reference held for whoever keeps the pointer.
using Object = void*;
// Another reference to `object`, and giving one back (null: nothing).
[[nodiscard]] Object retain(Object object) noexcept;
void release(Object object) noexcept;

// MTLPixelFormat's numbers for the formats the renderer names (mtl_context.mm checks them against Metal's).
enum class Format : std::uint32_t {
    None = 0u,
    R8 = 10u,
    Rg8 = 30u,
    Rgba8 = 70u,
    Bgra8 = 80u,
    Rgb10a2 = 90u,   // the scene: 10 bits a channel (Vulkan's A2B10G10R10)
    Bgr10a2 = 94u,
    R16f = 25u,
    Rg16f = 65u,
    Rgba16f = 115u,  // the effects layer; an extended-range (HDR) drawable
    R32f = 55u,
    R32u = 53u,
    Bc1 = 130u,
    Depth32fStencil8 = 260u,
    Depth32f = 252u,
    Stencil8 = 253u,
};

// What a texture is made for (MTLTextureUsage's bits).
enum TextureUse : std::uint32_t {
    kUseSample = 1u,
    kUseWrite = 2u,   // a compute shader or MetalFX writes it
    kUseTarget = 4u,  // drawn into
    kUseViews = 16u,  // views of another format or of its channels are made of it
};

// Where a texture's or a buffer's memory lies. Shared: one memory the CPU and the GPU both read (every Apple GPU's;
// an Intel Mac's integrated one). Managed: a CPU copy and a GPU copy kept alike (a discrete GPU on an Intel Mac).
// Private: the GPU's alone.
enum class Storage : std::uint8_t { Shared, Managed, Private };

// A texture. The last reference hands the Metal object to the context, which releases it after the GPU is done.
struct Texture : std::enable_shared_from_this<Texture> {
    Object texture = nullptr;   // id<MTLTexture>
    Object stencil = nullptr;   // id<MTLTexture>: a depth texture's stencil, viewed for sampling, where one was asked for
    Object sampled = nullptr;   // id<MTLTexture>: what draws sample where that is a view of `texture` (channels reordered)
    Object heap = nullptr;      // id<MTLHeap> it was made from (not retained: the texture backend keeps its heaps), else null
    Format format = Format::None;
    std::uint32_t use = 0u;
    std::uint32_t width = 0u;
    std::uint32_t height = 0u;
    std::uint32_t levels = 1u;
    std::uint32_t samples = 1u;
    Storage storage = Storage::Private;
    std::uint64_t allocation_bytes = 0u;

    // What a draw samples: the view when there is one.
    [[nodiscard]] Object view() const noexcept { return sampled != nullptr ? sampled : texture; }

    Texture() = default;
    Texture(const Texture&) = delete;
    Texture& operator=(const Texture&) = delete;
    ~Texture();
};

// A buffer the CPU writes and the GPU reads (or, Storage::Private, the GPU's own), with a GPU address; released as a
// texture is.
struct Buffer {
    Object buffer = nullptr;  // id<MTLBuffer>
    std::byte* mapped = nullptr;
    std::uint64_t size = 0u;
    std::uint64_t address = 0u;
    Storage storage = Storage::Shared;

    Buffer() = default;
    Buffer(const Buffer&) = delete;
    Buffer& operator=(const Buffer&) = delete;
    ~Buffer();
};

// What the device has, asked of it at start (never told from its name).
struct DeviceCaps {
    bool unified_memory = false;       // one memory for the CPU and the GPU
    bool apple_gpu = false;            // an Apple-designed GPU (a tile-based deferred renderer): any MTLGPUFamilyApple
    unsigned apple_family = 0u;        // the highest MTLGPUFamilyApple<n> it is of; 0 on an Intel Mac's GPUs
    bool metal3 = false;               // MTLGPUFamilyMetal3
    // Arrays of every texture and sampler, read by index (argument buffers tier 2, with resource ids written to a
    // buffer: macOS 13, iOS 16); else each draw binds its own table of sixteen.
    bool texture_arrays = false;
    std::uint32_t sampler_array_max = 16u;  // samplers one function reaches through an argument buffer
    bool bc_textures = false;          // BC1 (else the store keeps those textures as RGBA8)
    bool depth24 = false;              // Depth24Unorm_Stencil8 (an Intel Mac's discrete GPUs; never needed)
    unsigned max_samples = 1u;         // the most samples a pixel the scene's formats take (MSAA)
    bool memoryless = false;           // render targets that live in tile memory only (Apple GPUs)
    bool metalfx_spatial = false;      // MTLFXSpatialScaler
    bool metalfx_temporal = false;     // MTLFXTemporalScaler
    bool metalfx_interpolation = false;  // MTLFXFrameInterpolator (OS 26)
    bool binary_archives = false;      // MTLBinaryArchive: compiled pipelines kept on disk
    bool edr = false;                  // the layer can show values past white (extended dynamic range)
    std::uint64_t max_buffer_bytes = 0u;
    std::uint64_t recommended_memory = 0u;  // recommendedMaxWorkingSetSize: the video memory budget (0: not known)
    std::uint32_t max_texture_size = 0u;
    std::uint32_t language_major = 2u, language_minor = 4u;  // the newest Metal Shading Language the system compiles
};

class Context {
public:
    static Context& get() noexcept;

    // `chosen_gpu`: the GPU chosen in the settings (RenderConfig::gpu), taken first when present; else a Mac's
    // discrete GPU before its integrated one, as the system's default is.
    bool initialize(const std::string& chosen_gpu);
    void shutdown() noexcept;
    [[nodiscard]] bool ready() const noexcept { return device_ != nullptr; }
    [[nodiscard]] const std::string& failure() const noexcept { return failure_; }

    [[nodiscard]] Object device() const noexcept { return device_; }  // id<MTLDevice>
    [[nodiscard]] Object queue() const noexcept { return queue_; }    // id<MTLCommandQueue>
    [[nodiscard]] const DeviceCaps& caps() const noexcept { return caps_; }
    [[nodiscard]] const std::string& device_name() const noexcept { return device_name_; }
    // The device as config.ini's gpu key names it: its name and registry id.
    [[nodiscard]] const std::string& gpu_id() const noexcept { return gpu_id_; }

    // Commits `command_buffer` (an id<MTLCommandBuffer> of this queue, encoding ended) and returns its fence value.
    std::uint64_t commit(Object command_buffer);
    // A frame's last commit: objects released since the frame before (defer) are destroyed once it completes.
    std::uint64_t commit_frame(Object command_buffer);
    [[nodiscard]] std::uint64_t submitted_value() const noexcept { return submitted_.load(std::memory_order_acquire); }
    // The highest fence value the GPU has passed.
    [[nodiscard]] std::uint64_t completed_value() const noexcept { return completed_.load(std::memory_order_acquire); }
    // Waits until the GPU has passed `value`; false on a timeout or when a command buffer of it failed.
    bool wait(std::uint64_t value, std::uint64_t timeout_ns = UINT64_MAX);
    // A command buffer ended with an error since the last take (the device was lost, the system took the GPU away).
    [[nodiscard]] bool take_failure(std::string& what);
    // Runs `destroy` once the GPU has passed the next frame's commit, which carries any commands being encoded now.
    void defer(std::function<void()> destroy);
    // Runs the deferred destructions the GPU has passed (the render thread, once a frame).
    void collect();

    // A texture of `levels` levels; `samples` above one: a multisampled target. Null when the device refuses it.
    [[nodiscard]] std::shared_ptr<Texture> create_texture(std::uint32_t width, std::uint32_t height, std::uint32_t levels,
                                                          Format format, std::uint32_t use, Storage storage,
                                                          const char* name, std::uint32_t samples = 1u);
    // A mapped buffer (unmapped when Storage::Private) with a GPU address.
    [[nodiscard]] std::shared_ptr<Buffer> create_buffer(std::uint64_t size, Storage storage, const char* name);
    // The video memory in use and its budget (currentAllocatedSize, recommendedMaxWorkingSetSize); false: not known.
    bool device_memory_budget(std::uint64_t& usage, std::uint64_t& budget) const noexcept;

    // The GPU address space (gpu::Address): an address's top bits name a buffer and its low kAddressShift bits a byte
    // in it, so the shared code's address arithmetic works; draws turn addresses back into (buffer, offset).
    static constexpr unsigned kAddressShift = 40;
    struct BufferOffset {
        Object buffer = nullptr;  // id<MTLBuffer>, not retained: the buffer's owner keeps it
        std::uint64_t offset = 0;
    };
    [[nodiscard]] BufferOffset resolve(std::uint64_t address) const noexcept {
        const std::uint64_t id = address >> kAddressShift;
        if (id == 0u) {
            return {};
        }
        const AddressPage* page = address_pages_[id >> kAddressPageBits].load(std::memory_order_acquire);
        return {page ? page->slots[id & (kAddressPageSize - 1u)].load(std::memory_order_acquire) : nullptr,
                address & ((std::uint64_t{1} << kAddressShift) - 1u)};
    }
    void release_address(std::uint64_t address) noexcept;

private:
    static constexpr unsigned kAddressPageBits = 12;
    static constexpr std::size_t kAddressPageSize = std::size_t{1} << kAddressPageBits;
    static constexpr std::uint32_t kAddressIds = std::uint32_t{1} << (64u - kAddressShift);
    struct AddressPage {
        std::array<std::atomic<Object>, kAddressPageSize> slots{};
    };
    [[nodiscard]] std::uint64_t register_address(Object buffer);
    std::uint64_t commit_with(Object command_buffer, bool frame);
    void completed(std::uint64_t value, bool failed, std::string what);
    struct Deferred {
        std::uint64_t value;  // 0: waiting for the next frame's commit
        std::function<void()> destroy;
    };

    Object device_ = nullptr;
    Object queue_ = nullptr;
    DeviceCaps caps_{};
    std::string device_name_;
    std::string gpu_id_;
    std::string failure_;
    std::atomic<std::uint64_t> submitted_{0};
    std::atomic<std::uint64_t> completed_{0};
    std::mutex wait_mutex_;
    std::condition_variable wait_wake_;
    bool failed_ = false;       // under wait_mutex_
    std::string failed_what_;
    std::mutex deferred_mutex_;
    std::vector<Deferred> deferred_;
    std::array<std::atomic<AddressPage*>, kAddressIds / kAddressPageSize> address_pages_{};
    std::mutex address_mutex_;
    std::vector<std::uint32_t> free_address_ids_;
    std::uint32_t next_address_id_ = 1;  // 0 is the null address
};

}  // namespace galaxy::render::mtl
