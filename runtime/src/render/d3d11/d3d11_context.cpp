// SPDX-License-Identifier: GPL-3.0-only
// d3d11::Context (d3d11/d3d11_context.h): the device, the frame fence, textures, upload memory and its address space; and
// the gpu:: functions gpu_types.h declares for every back end.
#include "galaxy/render/d3d11/d3d11_context.h"

#include "galaxy/render/render_config.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <iostream>

namespace galaxy::render::d3d11 {

namespace {

std::string narrow(const wchar_t* text) {
    if (text == nullptr || *text == L'\0') {
        return {};
    }
    const int bytes = WideCharToMultiByte(CP_UTF8, 0, text, -1, nullptr, 0, nullptr, nullptr);
    std::string out(bytes > 0 ? static_cast<std::size_t>(bytes - 1) : 0u, '\0');
    if (bytes > 1) {
        WideCharToMultiByte(CP_UTF8, 0, text, -1, out.data(), bytes, nullptr, nullptr);
    }
    return out;
}

const char* level_name(D3D_FEATURE_LEVEL level) {
    switch (level) {
    case D3D_FEATURE_LEVEL_12_1: return "12_1";
    case D3D_FEATURE_LEVEL_12_0: return "12_0";
    case D3D_FEATURE_LEVEL_11_1: return "11_1";
    case D3D_FEATURE_LEVEL_11_0: return "11_0";
    case D3D_FEATURE_LEVEL_10_1: return "10_1";
    case D3D_FEATURE_LEVEL_10_0: return "10_0";
    default: return "9_x";
    }
}

void set_name(ID3D11DeviceChild* object, const char* name) {
    if (object != nullptr && name != nullptr) {
        object->SetPrivateData(WKPDID_D3DDebugObjectName, static_cast<UINT>(std::strlen(name)), name);
    }
}

std::uint64_t round_up(std::uint64_t value, std::uint64_t step) {
    return (value + step - 1u) / step * step;
}

}  // namespace

Context& Context::get() noexcept {
    static Context context;
    return context;
}

bool Context::initialize(const std::string& chosen_gpu, bool debug_layer) {
    if (device_ != nullptr) {
        return true;
    }
    failure_.clear();
    if (FAILED(CreateDXGIFactory2(debug_layer ? DXGI_CREATE_FACTORY_DEBUG : 0u, IID_PPV_ARGS(&factory_))) &&
        FAILED(CreateDXGIFactory2(0u, IID_PPV_ARGS(&factory_)))) {
        failure_ = "No DXGI factory";
        return false;
    }
    // Adapters in the system's order for performance where DXGI 1.6 gives it, else its plain order; software ones never.
    std::vector<ComPtr<IDXGIAdapter1>> adapters;
    ComPtr<IDXGIFactory6> factory6;
    if (SUCCEEDED(factory_.As(&factory6))) {
        ComPtr<IDXGIAdapter1> adapter;
        for (UINT i = 0; factory6->EnumAdapterByGpuPreference(i, DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE,
                                                             IID_PPV_ARGS(&adapter)) != DXGI_ERROR_NOT_FOUND;
             ++i) {
            adapters.push_back(adapter);
            adapter.Reset();
        }
    } else {
        ComPtr<IDXGIAdapter1> adapter;
        for (UINT i = 0; factory_->EnumAdapters1(i, &adapter) != DXGI_ERROR_NOT_FOUND; ++i) {
            adapters.push_back(adapter);
            adapter.Reset();
        }
    }
    // The chosen adapter first, when the settings name one (config.ini gpu=, as gpu_identity writes it: Direct3D 12's
    // and Vulkan's rule).
    if (!chosen_gpu.empty()) {
        const auto chosen = [&](const ComPtr<IDXGIAdapter1>& a) {
            DXGI_ADAPTER_DESC1 d{};
            return SUCCEEDED(a->GetDesc1(&d)) && gpu_identity(d.VendorId, d.DeviceId, d.SubSysId) == chosen_gpu;
        };
        const bool present = std::any_of(adapters.begin(), adapters.end(), chosen);
        std::stable_partition(adapters.begin(), adapters.end(), chosen);
        std::cout << "[gpu] chosen " << chosen_gpu << (present ? " first" : " not present; choosing automatically") << '\n';
    }
    std::string refused;
    for (const ComPtr<IDXGIAdapter1>& adapter : adapters) {
        DXGI_ADAPTER_DESC1 desc{};
        if (FAILED(adapter->GetDesc1(&desc)) || (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) != 0u) {
            continue;
        }
        const std::string name = narrow(desc.Description);
        static constexpr D3D_FEATURE_LEVEL kLevels[] = {D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_1,
                                                        D3D_FEATURE_LEVEL_10_0};
        UINT flags = debug_layer ? D3D11_CREATE_DEVICE_DEBUG : 0u;
        ComPtr<ID3D11Device> device;
        ComPtr<ID3D11DeviceContext> context;
        D3D_FEATURE_LEVEL level{};
        HRESULT hr = D3D11CreateDevice(adapter.Get(), D3D_DRIVER_TYPE_UNKNOWN, nullptr, flags, kLevels,
                                       static_cast<UINT>(std::size(kLevels)), D3D11_SDK_VERSION, &device, &level, &context);
        if (FAILED(hr) && debug_layer) {
            // The debug layer is not installed (Graphics Tools): the device without it.
            flags &= ~static_cast<UINT>(D3D11_CREATE_DEVICE_DEBUG);
            hr = D3D11CreateDevice(adapter.Get(), D3D_DRIVER_TYPE_UNKNOWN, nullptr, flags, kLevels,
                                   static_cast<UINT>(std::size(kLevels)), D3D11_SDK_VERSION, &device, &level, &context);
        }
        if (FAILED(hr)) {
            continue;
        }
        if (level < kMinimumFeatureLevel) {
            refused = name + ": feature level " + level_name(level) + ", 11_0 needed";
            std::cout << "[gpu] passed over " << refused << '\n';
            continue;
        }
        ComPtr<ID3D11Device1> device1;
        ComPtr<ID3D11DeviceContext1> context1;
        if (FAILED(device.As(&device1)) || FAILED(context.As(&context1))) {
            refused = name + ": no Direct3D 11.1 runtime";
            continue;
        }
        // Constant buffers bound at an offset and written in part: the draws' rows live in shared buffers.
        D3D11_FEATURE_DATA_D3D11_OPTIONS options{};
        if (FAILED(device1->CheckFeatureSupport(D3D11_FEATURE_D3D11_OPTIONS, &options, sizeof(options))) ||
            !options.ConstantBufferOffsetting || !options.ConstantBufferPartialUpdate) {
            refused = name + ": no constant buffer offsets";
            std::cout << "[gpu] passed over " << refused << '\n';
            continue;
        }
        adapter_ = adapter;
        device_ = device1;
        immediate_ = context1;
        device_name_ = name;
        caps_ = DeviceCaps{};
        caps_.level = level;
        caps_.vendor = desc.VendorId;
        caps_.dedicated_bytes = desc.DedicatedVideoMemory;
        caps_.unified_memory = desc.DedicatedVideoMemory < (std::uint64_t{512} << 20u);
        // The debug layer's messages, read back each frame (report_messages): Direct3D 11 has no callback.
        if ((flags & D3D11_CREATE_DEVICE_DEBUG) != 0u && SUCCEEDED(device1.As(&info_queue_))) {
            info_queue_->SetMessageCountLimit(4096u);
        }
        std::cout << "[gpu] d3d11 debug-layer=" << (info_queue_ != nullptr ? 1 : 0) << '\n';
        break;
    }
    if (device_ == nullptr) {
        failure_ = refused.empty() ? "No hardware Direct3D 11 adapter supports feature level 11_0"
                                   : "No hardware Direct3D 11 adapter can run the renderer (" + refused + ")";
        factory_.Reset();
        return false;
    }
    ComPtr<IDXGIFactory4> factory4;
    caps_.flip_model = SUCCEEDED(factory_.As(&factory4));
    ComPtr<IDXGIFactory5> factory5;
    BOOL tearing = FALSE;
    caps_.tearing = SUCCEEDED(factory_.As(&factory5)) &&
                    SUCCEEDED(factory5->CheckFeatureSupport(DXGI_FEATURE_PRESENT_ALLOW_TEARING, &tearing, sizeof(tearing))) &&
                    tearing != FALSE;
    // The most samples every scene format takes.
    caps_.max_samples = 1u;
    for (const unsigned samples : {2u, 4u, 8u}) {
        bool all = true;
        for (const DXGI_FORMAT format : {DXGI_FORMAT_R10G10B10A2_UNORM, DXGI_FORMAT_R16G16B16A16_FLOAT,
                                         DXGI_FORMAT_R8G8B8A8_UNORM, DXGI_FORMAT_D32_FLOAT_S8X24_UINT}) {
            UINT quality = 0u;
            all = all && SUCCEEDED(device_->CheckMultisampleQualityLevels(format, samples, &quality)) && quality > 0u;
        }
        if (all) {
            caps_.max_samples = samples;
        }
    }
    // The frame fence: ID3D11Fence where the runtime and driver have it (Direct3D 11.4), else event queries.
    ComPtr<ID3D11Device5> device5;
    if (SUCCEEDED(device_.As(&device5)) && SUCCEEDED(device5->CreateFence(0u, D3D11_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence_)))) {
        fence_event_ = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    } else {
        fence_.Reset();
        fence_queries_.resize(8u);
        D3D11_QUERY_DESC query{D3D11_QUERY_EVENT, 0u};
        for (ComPtr<ID3D11Query>& q : fence_queries_) {
            if (FAILED(device_->CreateQuery(&query, &q))) {
                failure_ = "No event queries";
                shutdown();
                return false;
            }
        }
    }
    submitted_ = completed_ = 0u;
    std::cout << "[gpu] Direct3D 11 on " << device_name_ << ", feature level " << level_name(caps_.level)
              << (fence_ != nullptr ? ", fence" : ", event queries") << (caps_.tearing ? ", tearing" : "") << '\n';
    return true;
}

void Context::shutdown() noexcept {
    if (immediate_ != nullptr) {
        immediate_->ClearState();
        immediate_->Flush();
    }
    report_messages();
    info_queue_.Reset();
    fence_queries_.clear();
    fence_.Reset();
    if (fence_event_ != nullptr) {
        CloseHandle(fence_event_);
        fence_event_ = nullptr;
    }
    immediate_.Reset();
    device_.Reset();
    adapter_.Reset();
    factory_.Reset();
    submitted_ = completed_ = 0u;
}

unsigned Context::report_messages() noexcept {
    if (info_queue_ == nullptr) {
        return 0u;
    }
    // The tags the smoke scripts count (as [D3D12-LIVE-ERR] / [D3D12-LIVE-WARN] on Direct3D 12).
    const UINT64 count = info_queue_->GetNumStoredMessages();
    std::vector<std::byte> storage;
    for (UINT64 i = 0u; i < count; ++i) {
        SIZE_T bytes = 0u;
        if (FAILED(info_queue_->GetMessage(i, nullptr, &bytes)) || bytes == 0u) {
            continue;
        }
        storage.resize(bytes);
        auto* message = reinterpret_cast<D3D11_MESSAGE*>(storage.data());
        if (FAILED(info_queue_->GetMessage(i, message, &bytes))) {
            continue;
        }
        const bool error = message->Severity == D3D11_MESSAGE_SEVERITY_CORRUPTION ||
                           message->Severity == D3D11_MESSAGE_SEVERITY_ERROR;
        if (error || message->Severity == D3D11_MESSAGE_SEVERITY_WARNING) {
            std::fprintf(stderr, "[%s] id=%d %.*s\n", error ? "D3D11-LIVE-ERR" : "D3D11-LIVE-WARN",
                         static_cast<int>(message->ID), static_cast<int>(message->DescriptionByteLength),
                         message->pDescription);
        }
    }
    info_queue_->ClearStoredMessages();
    return static_cast<unsigned>(count);
}

std::uint64_t Context::signal() {
    const std::uint64_t value = ++submitted_;
    if (fence_ != nullptr) {
        ComPtr<ID3D11DeviceContext4> context4;
        if (SUCCEEDED(immediate_.As(&context4))) {
            context4->Signal(fence_.Get(), value);
        }
    } else if (!fence_queries_.empty()) {
        // A query's slot is used again only after its value was passed (wait keeps fewer in flight than slots).
        if (value > fence_queries_.size()) {
            wait(value - fence_queries_.size());
        }
        immediate_->End(fence_queries_[value % fence_queries_.size()].Get());
    }
    immediate_->Flush();
    return value;
}

std::uint64_t Context::completed_value() noexcept {
    if (fence_ != nullptr) {
        completed_ = std::max(completed_, fence_->GetCompletedValue());
        return completed_;
    }
    while (completed_ < submitted_) {
        BOOL done = FALSE;
        if (immediate_->GetData(fence_queries_[(completed_ + 1u) % fence_queries_.size()].Get(), &done, sizeof(done),
                                D3D11_ASYNC_GETDATA_DONOTFLUSH) != S_OK ||
            done == FALSE) {
            break;
        }
        ++completed_;
    }
    return completed_;
}

bool Context::wait(std::uint64_t value) {
    if (value > submitted_) {
        value = submitted_;
    }
    if (completed_value() >= value) {
        return true;
    }
    if (fence_ != nullptr) {
        if (FAILED(fence_->SetEventOnCompletion(value, fence_event_))) {
            return false;
        }
        WaitForSingleObject(fence_event_, INFINITE);
        (void)completed_value();
        return true;
    }
    while (completed_ < value) {
        BOOL done = FALSE;
        const HRESULT hr =
            immediate_->GetData(fence_queries_[(completed_ + 1u) % fence_queries_.size()].Get(), &done, sizeof(done), 0u);
        if (FAILED(hr)) {
            return false;
        }
        if (hr == S_OK && done != FALSE) {
            ++completed_;
        } else {
            SwitchToThread();
        }
    }
    return true;
}

std::shared_ptr<Texture> Context::create_texture(std::uint32_t width, std::uint32_t height, std::uint32_t levels,
                                                 DXGI_FORMAT format, std::uint32_t bind, const char* name,
                                                 std::uint32_t samples) {
    if (device_ == nullptr || width == 0u || height == 0u) {
        return {};
    }
    const bool depth = format == DXGI_FORMAT_D32_FLOAT_S8X24_UINT || format == DXGI_FORMAT_D24_UNORM_S8_UINT ||
                       format == DXGI_FORMAT_D32_FLOAT;
    D3D11_TEXTURE2D_DESC desc{};
    desc.Width = width;
    desc.Height = height;
    desc.MipLevels = std::max(1u, levels);
    desc.ArraySize = 1u;
    // A sampled depth texture is made typeless so that it can have both views.
    desc.Format = depth && (bind & D3D11_BIND_SHADER_RESOURCE) != 0u
                      ? (format == DXGI_FORMAT_D32_FLOAT_S8X24_UINT ? DXGI_FORMAT_R32G8X24_TYPELESS
                         : format == DXGI_FORMAT_D24_UNORM_S8_UINT  ? DXGI_FORMAT_R24G8_TYPELESS
                                                                    : DXGI_FORMAT_R32_TYPELESS)
                      : format;
    desc.SampleDesc.Count = std::max(1u, samples);
    desc.Usage = D3D11_USAGE_DEFAULT;
    desc.BindFlags = bind;
    auto texture = std::make_shared<Texture>();
    if (FAILED(device_->CreateTexture2D(&desc, nullptr, &texture->texture))) {
        return {};
    }
    set_name(texture->texture.Get(), name);
    const bool ms = desc.SampleDesc.Count > 1u;
    if ((bind & D3D11_BIND_SHADER_RESOURCE) != 0u) {
        D3D11_SHADER_RESOURCE_VIEW_DESC view{};
        view.Format = !depth ? format
                      : format == DXGI_FORMAT_D32_FLOAT_S8X24_UINT ? DXGI_FORMAT_R32_FLOAT_X8X24_TYPELESS
                      : format == DXGI_FORMAT_D24_UNORM_S8_UINT    ? DXGI_FORMAT_R24_UNORM_X8_TYPELESS
                                                                   : DXGI_FORMAT_R32_FLOAT;
        view.ViewDimension = ms ? D3D11_SRV_DIMENSION_TEXTURE2DMS : D3D11_SRV_DIMENSION_TEXTURE2D;
        view.Texture2D.MipLevels = desc.MipLevels;
        if (FAILED(device_->CreateShaderResourceView(texture->texture.Get(), &view, &texture->view))) {
            return {};
        }
        if (depth && format != DXGI_FORMAT_D32_FLOAT) {
            view.Format = format == DXGI_FORMAT_D32_FLOAT_S8X24_UINT ? DXGI_FORMAT_X32_TYPELESS_G8X24_UINT
                                                                     : DXGI_FORMAT_X24_TYPELESS_G8_UINT;
            device_->CreateShaderResourceView(texture->texture.Get(), &view, &texture->stencil);
        }
    }
    if ((bind & D3D11_BIND_RENDER_TARGET) != 0u) {
        D3D11_RENDER_TARGET_VIEW_DESC view{};
        view.Format = format;
        view.ViewDimension = ms ? D3D11_RTV_DIMENSION_TEXTURE2DMS : D3D11_RTV_DIMENSION_TEXTURE2D;
        if (FAILED(device_->CreateRenderTargetView(texture->texture.Get(), &view, &texture->target))) {
            return {};
        }
    }
    if ((bind & D3D11_BIND_DEPTH_STENCIL) != 0u) {
        D3D11_DEPTH_STENCIL_VIEW_DESC view{};
        view.Format = format;
        view.ViewDimension = ms ? D3D11_DSV_DIMENSION_TEXTURE2DMS : D3D11_DSV_DIMENSION_TEXTURE2D;
        if (FAILED(device_->CreateDepthStencilView(texture->texture.Get(), &view, &texture->depth))) {
            return {};
        }
        if ((bind & D3D11_BIND_SHADER_RESOURCE) != 0u) {
            view.Flags = D3D11_DSV_READ_ONLY_DEPTH | D3D11_DSV_READ_ONLY_STENCIL;
            device_->CreateDepthStencilView(texture->texture.Get(), &view, &texture->depth_read);
        }
    }
    if ((bind & D3D11_BIND_UNORDERED_ACCESS) != 0u) {
        D3D11_UNORDERED_ACCESS_VIEW_DESC view{};
        view.Format = format;
        view.ViewDimension = D3D11_UAV_DIMENSION_TEXTURE2D;
        if (FAILED(device_->CreateUnorderedAccessView(texture->texture.Get(), &view, &texture->storage))) {
            return {};
        }
    }
    texture->format = format;
    texture->width = width;
    texture->height = height;
    texture->levels = desc.MipLevels;
    texture->samples = desc.SampleDesc.Count;
    // The allocation's size as the levels' bytes (Direct3D 11 does not report it).
    std::uint64_t bytes = 0u;
    const bool blocks = format == DXGI_FORMAT_BC1_UNORM;
    const std::uint32_t texel = format == DXGI_FORMAT_R16G16B16A16_FLOAT || format == DXGI_FORMAT_D32_FLOAT_S8X24_UINT ? 8u
                                : format == DXGI_FORMAT_R8_UNORM                                                       ? 1u
                                : format == DXGI_FORMAT_R8G8_UNORM                                                     ? 2u
                                                                                                                       : 4u;
    for (std::uint32_t level = 0; level < desc.MipLevels; ++level) {
        const std::uint64_t w = std::max(1u, width >> level), h = std::max(1u, height >> level);
        bytes += blocks ? (w + 3u) / 4u * ((h + 3u) / 4u) * 8u : w * h * texel;
    }
    texture->allocation_bytes = bytes * desc.SampleDesc.Count;
    return texture;
}

std::shared_ptr<Buffer> Context::create_buffer(std::uint64_t size, const char*) {
    if (device_ == nullptr || size == 0u) {
        return {};
    }
    auto buffer = std::make_shared<Buffer>();
    buffer->size = round_up(size, 256u);
    buffer->shadow = static_cast<std::byte*>(
        VirtualAlloc(nullptr, buffer->size, MEM_RESERVE | MEM_COMMIT | MEM_WRITE_WATCH, PAGE_READWRITE));
    if (buffer->shadow == nullptr) {
        return {};
    }
    buffer->address = register_address(buffer.get());
    if (buffer->address == 0u) {
        return {};
    }
    std::lock_guard guard(buffers_mutex_);
    upload_buffers_.push_back(buffer.get());
    return buffer;
}

ID3D11Buffer* Buffer::bound(BufferUse use) {
    ComPtr<ID3D11Buffer>& made = gpu[static_cast<std::size_t>(use)];
    if (made == nullptr) {
        D3D11_BUFFER_DESC desc{};
        desc.ByteWidth = static_cast<UINT>(size);
        desc.Usage = D3D11_USAGE_DEFAULT;
        desc.BindFlags = use == BufferUse::Geometry    ? D3D11_BIND_VERTEX_BUFFER | D3D11_BIND_INDEX_BUFFER
                         : use == BufferUse::Constants ? D3D11_BIND_CONSTANT_BUFFER
                                                       : D3D11_BIND_SHADER_RESOURCE;
        if (use == BufferUse::Rows) {
            desc.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
            desc.StructureByteStride = 16u;
        }
        D3D11_SUBRESOURCE_DATA data{shadow, 0u, 0u};
        if (FAILED(Context::get().device()->CreateBuffer(&desc, &data, &made))) {
            made.Reset();
        }
    }
    return made.Get();
}

ID3D11ShaderResourceView* Buffer::rows_view(std::uint64_t offset) {
    for (const auto& [at, view] : row_views) {
        if (at == offset) {
            return view.Get();
        }
    }
    ID3D11Buffer* rows = bound(BufferUse::Rows);
    if (rows == nullptr || offset >= size) {
        return nullptr;
    }
    D3D11_SHADER_RESOURCE_VIEW_DESC desc{};
    desc.Format = DXGI_FORMAT_UNKNOWN;
    desc.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
    desc.Buffer.FirstElement = static_cast<UINT>(offset / 16u);
    desc.Buffer.NumElements = static_cast<UINT>((size - offset) / 16u);
    ComPtr<ID3D11ShaderResourceView> view;
    if (FAILED(Context::get().device()->CreateShaderResourceView(rows, &desc, &view))) {
        return nullptr;
    }
    if (row_views.size() >= kRowViews) {
        row_views.erase(row_views.begin());  // Direct3D 11 keeps a released view while bound or in flight
    }
    row_views.emplace_back(offset, view);
    return view.Get();
}

Buffer::~Buffer() {
    Context& context = Context::get();
    context.release_address(address);
    {
        std::lock_guard guard(context.buffers_mutex_);
        std::erase(context.upload_buffers_, this);
    }
    if (shadow != nullptr) {
        VirtualFree(shadow, 0u, MEM_RELEASE);
    }
}

void Context::upload_written() {
    std::lock_guard guard(buffers_mutex_);
    std::vector<void*> pages;
    for (Buffer* buffer : upload_buffers_) {
        bool any = false;
        for (const ComPtr<ID3D11Buffer>& made : buffer->gpu) {
            any = any || made != nullptr;
        }
        pages.resize(std::max<std::size_t>(pages.size(), 4096u));
        ULONG_PTR count = pages.size();
        DWORD page_bytes = 4096u;
        // Every written page since the last call; read and reset in one call, so a write after it is seen next time.
        while (GetWriteWatch(WRITE_WATCH_FLAG_RESET, buffer->shadow, buffer->size, pages.data(), &count, &page_bytes) == 0u) {
            if (any && count != 0u) {
                // Runs of adjacent pages, each one UpdateSubresource1 into every video-memory buffer made.
                std::size_t i = 0;
                while (i < count) {
                    const auto* first = static_cast<const std::byte*>(pages[i]);
                    std::size_t j = i + 1u;
                    while (j < count && static_cast<const std::byte*>(pages[j]) ==
                                            static_cast<const std::byte*>(pages[j - 1u]) + page_bytes) {
                        ++j;
                    }
                    const std::uint64_t begin = static_cast<std::uint64_t>(first - buffer->shadow);
                    const std::uint64_t end = std::min<std::uint64_t>(begin + (j - i) * page_bytes, buffer->size);
                    for (std::size_t use = 0; use < buffer->gpu.size(); ++use) {
                        if (buffer->gpu[use] == nullptr) {
                            continue;
                        }
                        // Constant buffers are written in 16-byte steps (ConstantBufferPartialUpdate); pages are aligned.
                        const D3D11_BOX box{static_cast<UINT>(begin), 0u, 0u, static_cast<UINT>(end), 1u, 1u};
                        immediate_->UpdateSubresource1(buffer->gpu[use].Get(), 0u, &box, buffer->shadow + begin, 0u, 0u,
                                                       D3D11_COPY_NO_OVERWRITE);
                    }
                    i = j;
                }
            }
            if (count < pages.size()) {
                break;
            }
            // More pages than the array holds: the rest after a second call (the first reset only those it returned).
            count = pages.size();
        }
    }
}

std::uint64_t Context::register_address(Buffer* buffer) {
    std::lock_guard guard(address_mutex_);
    std::uint32_t id = 0u;
    if (!free_address_ids_.empty()) {
        id = free_address_ids_.back();
        free_address_ids_.pop_back();
    } else if (next_address_id_ < kAddressIds) {
        id = next_address_id_++;
    } else {
        return 0u;
    }
    std::atomic<AddressPage*>& slot = address_pages_[id >> kAddressPageBits];
    AddressPage* page = slot.load(std::memory_order_acquire);
    if (page == nullptr) {
        page = new AddressPage();
        slot.store(page, std::memory_order_release);
    }
    page->slots[id & (kAddressPageSize - 1u)].store(buffer, std::memory_order_release);
    return std::uint64_t{id} << kAddressShift;
}

void Context::release_address(std::uint64_t address) noexcept {
    const std::uint64_t id = address >> kAddressShift;
    if (id == 0u) {
        return;
    }
    std::lock_guard guard(address_mutex_);
    AddressPage* page = address_pages_[id >> kAddressPageBits].load(std::memory_order_acquire);
    if (page != nullptr) {
        page->slots[id & (kAddressPageSize - 1u)].store(nullptr, std::memory_order_release);
    }
    free_address_ids_.push_back(static_cast<std::uint32_t>(id));
}

}  // namespace galaxy::render::d3d11

namespace galaxy::render::gpu {

TextureRef texture_ref(TextureObject* texture) {
    return texture != nullptr ? texture->shared_from_this() : TextureRef{};
}

TextureRef create_render_texture(Device*, std::uint32_t width, std::uint32_t height) {
    return d3d11::Context::get().create_texture(width, height, 1u, DXGI_FORMAT_R8G8B8A8_UNORM,
                                                D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET, "render texture");
}

UploadMemory create_upload_memory(Device*, std::size_t bytes, const char* name, UploadPlace) {
    UploadMemory memory;
    std::shared_ptr<d3d11::Buffer> buffer = d3d11::Context::get().create_buffer(bytes, name);
    if (buffer == nullptr) {
        return memory;
    }
    memory.cpu = buffer->shadow;
    memory.gpu = buffer->address;
    memory.local = false;
    memory.buffer = std::move(buffer);
    return memory;
}

bool device_removed(Device* device) noexcept {
    return device != nullptr && device->GetDeviceRemovedReason() != S_OK;
}

}  // namespace galaxy::render::gpu
