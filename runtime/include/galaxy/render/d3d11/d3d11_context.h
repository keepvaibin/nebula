// SPDX-License-Identifier: GPL-3.0-only
// Direct3D 11's objects as the renderer and its parts hold them: the device and its one immediate context, textures with
// their views, buffers with their place in the renderer's address space, pipelines, a draw's own tables, and the
// frame's fence.
//
// Every call on the immediate context is the render thread's (Direct3D 11 contexts are single-threaded). The other
// threads (texture decoding, pipeline workers) create objects on the device, which is free-threaded.
//
// Upload memory (gpu::create_upload_memory) is system memory the CPU writes through the mapped pointer as on the other back
// ends (VirtualAlloc with MEM_WRITE_WATCH), and the buffers in video memory draws read. Direct3D 11 fixes a buffer's binds
// when it is made, and a constant buffer can be nothing else, so a Buffer has one video-memory buffer for each way draws
// bind it (BufferUse), made at the first such bind from the whole copy. Before the draws that read it run, the pages written
// since the last upload reach each of them (Context::upload_written: GetWriteWatch, then UpdateSubresource1 of each run of
// pages with D3D11_COPY_NO_OVERWRITE: a range written in a frame is never one a frame in flight still reads). The shared code
// needs no call of its own.
#pragma once

#include "galaxy/render/gpu_types.h"

#include <d3d11_4.h>
#include <dxgi1_6.h>

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

namespace galaxy::render::d3d11 {

using Microsoft::WRL::ComPtr;

// Feature level 11_0 is the least the renderer runs on; 10_x adapters are refused by name (Context::initialize).
inline constexpr D3D_FEATURE_LEVEL kMinimumFeatureLevel = D3D_FEATURE_LEVEL_11_0;

// A texture and the views it was made with (each null when the texture has no such use).
struct Texture : std::enable_shared_from_this<Texture> {
    ComPtr<ID3D11Texture2D> texture;
    ComPtr<ID3D11ShaderResourceView> view;      // sampled (a depth texture: its depth)
    ComPtr<ID3D11ShaderResourceView> stencil;   // a depth texture's stencil, where one was asked for
    ComPtr<ID3D11RenderTargetView> target;      // a colour target
    ComPtr<ID3D11DepthStencilView> depth;       // a depth and stencil target
    ComPtr<ID3D11DepthStencilView> depth_read;  // the same, read-only (sampled while tested)
    ComPtr<ID3D11UnorderedAccessView> storage;  // written by compute
    DXGI_FORMAT format = DXGI_FORMAT_UNKNOWN;
    std::uint32_t width = 0u;
    std::uint32_t height = 0u;
    std::uint32_t levels = 1u;
    std::uint32_t samples = 1u;
    std::uint64_t allocation_bytes = 0u;
    // A store texture staged as one or two channels (1: R8, 2: Rg8) and kept as RGBA8, widened when written: Direct3D 11
    // views cannot repeat a channel as the other renderers' do (R8 read as rrrr, Rg8 as rrrg).
    std::uint8_t widened = 0u;

    Texture() = default;
    Texture(const Texture&) = delete;
    Texture& operator=(const Texture&) = delete;
};

// How draws bind a buffer.
enum class BufferUse : std::uint8_t {
    Geometry,   // vertices and indices
    Constants,  // constant buffers bound at an offset (VSSetConstantBuffers1: 256-byte steps)
    Rows,       // structured buffers of 16-byte elements (t16 the composed rows, t17 the instance table)
    Count
};

// Upload memory: the copy the CPU writes, its address range, and its video-memory buffers by use.
struct Buffer {
    std::byte* shadow = nullptr;  // VirtualAlloc with MEM_WRITE_WATCH
    std::uint64_t size = 0u;      // a multiple of 256
    std::uint64_t address = 0u;   // its first byte in the address space (gpu::Address)
    std::array<ComPtr<ID3D11Buffer>, static_cast<std::size_t>(BufferUse::Count)> gpu{};
    // The video-memory buffer for `use`, made now from the whole copy if there is none (render thread).
    [[nodiscard]] ID3D11Buffer* bound(BufferUse use);
    // A structured view of 16-byte elements from `offset` to the end (render thread; kept until the buffer goes, the
    // oldest dropped past kRowViews).
    [[nodiscard]] ID3D11ShaderResourceView* rows_view(std::uint64_t offset);
    static constexpr std::size_t kRowViews = 64u;
    std::vector<std::pair<std::uint64_t, ComPtr<ID3D11ShaderResourceView>>> row_views;

    Buffer() = default;
    Buffer(const Buffer&) = delete;
    Buffer& operator=(const Buffer&) = delete;
    ~Buffer();
};

// One pipeline's state objects. core::Pipelines (core/pipelines_d3d11.cpp) shares identical state objects between
// pipelines: Direct3D 11 itself returns the same object for the same description.
struct Pipeline {
    ComPtr<ID3D11VertexShader> vertex;
    ComPtr<ID3D11PixelShader> pixel;  // null: depth only
    ComPtr<ID3D11InputLayout> input;
    ComPtr<ID3D11BlendState1> blend;
    ComPtr<ID3D11RasterizerState1> rasterizer;
    ComPtr<ID3D11DepthStencilState> depth_stencil;
    D3D11_PRIMITIVE_TOPOLOGY topology = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
    std::uint32_t stencil_reference = 0u;
    std::uint32_t sample_mask = 0xFFFFFFFFu;
};

// A draw's own table (the programs' NEBULA_BOUND_TABLES form): up to kBoundTableSlots views or samplers bound from slot 0
// of the pixel stage. Not owning: the texture store and the sampler cache keep what they name.
inline constexpr std::uint32_t kBoundTableSlots = 16u;
struct BoundTable {
    std::array<void*, kBoundTableSlots> items{};  // ID3D11ShaderResourceView* or ID3D11SamplerState*
    std::uint32_t count = 0u;                     // slots bound (the highest named plus one)
};

// The pipeline formats and what the programs were compiled for (Pipelines::RootLayout on Direct3D 11).
struct RenderLayout {
    DXGI_FORMAT scene_color = DXGI_FORMAT_R10G10B10A2_UNORM;
    DXGI_FORMAT wide_color = DXGI_FORMAT_R16G16B16A16_FLOAT;  // kSceneVariantHdr
    DXGI_FORMAT target_color = DXGI_FORMAT_R8G8B8A8_UNORM;    // kSceneVariantTarget
    DXGI_FORMAT depth = DXGI_FORMAT_D32_FLOAT_S8X24_UINT;
    DXGI_FORMAT motion = DXGI_FORMAT_R16G16_FLOAT;
    DXGI_FORMAT reactive = DXGI_FORMAT_R8_UNORM;
    DXGI_FORMAT layer = DXGI_FORMAT_R16G16B16A16_FLOAT;
    bool layer_targets = false;
    unsigned max_samples = 1u;
};

struct DeviceCaps {
    D3D_FEATURE_LEVEL level = D3D_FEATURE_LEVEL_11_0;
    bool tearing = false;               // DXGI_FEATURE_PRESENT_ALLOW_TEARING
    bool flip_model = false;            // DXGI 1.4+: FLIP_DISCARD swap chains
    bool unified_memory = false;        // the adapter has no video memory of its own (DXGI: DedicatedVideoMemory small)
    unsigned max_samples = 1u;          // the most samples a pixel the scene's formats take (MSAA)
    std::uint64_t dedicated_bytes = 0u;
    std::uint32_t vendor = 0u;
};

class Context {
public:
    static Context& get() noexcept;

    // The device on the chosen adapter (`chosen_gpu`: the settings' name, empty for the system's choice) at feature level
    // 11_0 or above. False: failure() says why (an adapter of 10_x is named).
    bool initialize(const std::string& chosen_gpu, bool debug_layer);
    void shutdown() noexcept;
    [[nodiscard]] bool ready() const noexcept { return device_ != nullptr; }
    [[nodiscard]] const std::string& failure() const noexcept { return failure_; }

    [[nodiscard]] ID3D11Device1* device() const noexcept { return device_.Get(); }
    [[nodiscard]] ID3D11DeviceContext1* immediate() const noexcept { return immediate_.Get(); }
    [[nodiscard]] IDXGIFactory2* factory() const noexcept { return factory_.Get(); }
    [[nodiscard]] IDXGIAdapter1* adapter() const noexcept { return adapter_.Get(); }
    [[nodiscard]] const DeviceCaps& caps() const noexcept { return caps_; }
    [[nodiscard]] const std::string& device_name() const noexcept { return device_name_; }

    // The frame fence (ID3D11Fence where the driver offers ID3D11Device5, else an event query a frame): signal after a
    // frame's last command; completed_value the highest the GPU has passed; wait blocks for one.
    std::uint64_t signal();
    [[nodiscard]] std::uint64_t submitted_value() const noexcept { return submitted_; }
    [[nodiscard]] std::uint64_t completed_value() noexcept;
    bool wait(std::uint64_t value);

    // A texture of `levels` levels with the views `bind` asks for (D3D11_BIND_*); `samples` above one: a multisampled
    // target. Null when the device refuses it.
    [[nodiscard]] std::shared_ptr<Texture> create_texture(std::uint32_t width, std::uint32_t height, std::uint32_t levels,
                                                          DXGI_FORMAT format, std::uint32_t bind, const char* name,
                                                          std::uint32_t samples = 1u);
    // Upload memory of `size` bytes (zeroed), with its address range. Null when the memory cannot be had.
    [[nodiscard]] std::shared_ptr<Buffer> create_buffer(std::uint64_t size, const char* name);
    // Every upload buffer's bytes written since the last call reach the GPU (render thread, before draws that read them).
    void upload_written();

    // The address space (gpu::Address): an address's top bits name a buffer and its low kAddressShift bits a byte in it.
    static constexpr unsigned kAddressShift = 40;
    struct BufferOffset {
        Buffer* buffer = nullptr;  // not owned: the buffer's owner keeps it
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
        std::array<std::atomic<Buffer*>, kAddressPageSize> slots{};
    };
    friend struct Buffer;  // its destructor leaves the upload list
    [[nodiscard]] std::uint64_t register_address(Buffer* buffer);

    ComPtr<IDXGIFactory2> factory_;
    ComPtr<IDXGIAdapter1> adapter_;
    ComPtr<ID3D11Device1> device_;
    ComPtr<ID3D11DeviceContext1> immediate_;
    ComPtr<ID3D11Fence> fence_;  // null: event queries
    HANDLE fence_event_ = nullptr;
    std::vector<ComPtr<ID3D11Query>> fence_queries_;  // by value modulo their count, without ID3D11Fence
    std::uint64_t submitted_ = 0u;
    std::uint64_t completed_ = 0u;
    DeviceCaps caps_{};
    std::string device_name_;
    std::string failure_;
    std::mutex buffers_mutex_;
    std::vector<Buffer*> upload_buffers_;  // every buffer with a system-memory copy
    std::array<std::atomic<AddressPage*>, kAddressIds / kAddressPageSize> address_pages_{};
    std::mutex address_mutex_;
    std::vector<std::uint32_t> free_address_ids_;
    std::uint32_t next_address_id_ = 1;  // 0 is the null address
};

}  // namespace galaxy::render::d3d11
