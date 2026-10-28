// SPDX-License-Identifier: GPL-3.0-only
// Ray tracing's shader preparation: the pipeline library and the meter (rt_prepare.h).

#include "galaxy/gx/rt_prepare.h"

#include "galaxy/atomic_file.h"
#include "galaxy/gx/render_config.h"
#include "rt_shader_manifest.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <cwchar>
#include <fstream>
#include <initializer_list>
#include <iterator>
#include <mutex>
#include <new>
#include <string_view>
#include <unordered_map>

namespace galaxy::gx::rt_prepare {

namespace {

constexpr std::uint32_t kMagic = 0x4C54524Eu;  // "NRTL"
constexpr std::uint32_t kVersion = 1u;
constexpr std::size_t kNameBytes = 40u;  // the adapter's and the driver's, each
// A library larger than this is not one of ours (the whole set compiles to tens of megabytes).
constexpr std::uint64_t kMaxLibraryBytes = 1ull << 30;

// ---- The meter ----

constexpr std::size_t kShaders = std::size(kRtShaderManifest);

// A compile's time grows with its bytecode's size to this power: cold compiles (RTX 5090, driver 610.88) took 2.1 s
// for 104 000 bytes, 4.1 s for 150 452, 8.8 s for 240 624 and 61.6 s for the 791 308-byte clear pass, each pair 1.63
// to 1.71.
constexpr double kSizePower = 1.66;
// A compile's cost in milliseconds per weight before this start has finished one to judge by: the clear pass's.
const double kDefaultCost = 61568.0 / std::pow(791308.0, kSizePower);

enum class State : std::uint8_t { Waiting, Running, Done };

struct Meter {
    std::mutex lock;
    std::array<State, kShaders> state{};
    std::array<std::chrono::steady_clock::time_point, kShaders> since{};
    // The cost of the largest finished compile of a second or more (ms per weight), and its weight. The compiles run
    // side by side: a small one's time is mostly the driver's fixed cost and the others' contention.
    double cost = 0.0;
    double cost_weight = 0.0;
    float shown = 0.0f;         // what progress() last returned
};

Meter& meter() {
    static Meter m;
    return m;
}

// A shader's share of the preparation: its compile's expected time, in proportion.
double weight(std::size_t index) {
    return std::pow(static_cast<double>(kRtShaderManifest[index].bytes), kSizePower);
}

// The share of its expected time `x` (elapsed over expected) a compile in flight counts: its time, until 0.8 of it;
// then on towards all of it without reaching it, so a compile longer than expected slows the bar and never stops it.
double in_flight(double x) {
    return x < 0.8 ? x : 0.8 + 0.2 * (1.0 - std::exp(-(x - 0.8) / 0.2));
}

// A DXIL container's digest (the 16 bytes after "DXBC"); null when the bytecode is not a container.
const unsigned char* container_digest(const void* code, std::size_t bytes) {
    return code != nullptr && bytes >= 20u && std::memcmp(code, "DXBC", 4u) == 0
               ? static_cast<const unsigned char*>(code) + 4
               : nullptr;
}

// The manifest entry of a shader's bytecode; kShaders when it is not one.
std::size_t entry_of(const void* code, std::size_t bytes) {
    static const std::unordered_map<std::string, std::size_t> index = [] {
        std::unordered_map<std::string, std::size_t> map;
        for (std::size_t i = 0; i < kShaders; ++i) map.emplace(kRtShaderManifest[i].digest, i);
        return map;
    }();
    const unsigned char* digest = container_digest(code, bytes);
    if (digest == nullptr) {
        return kShaders;
    }
    char hex[33];
    for (unsigned i = 0; i < 16u; ++i) std::snprintf(hex + 2u * i, 3u, "%02x", digest[i]);
    const auto it = index.find(std::string(hex, 32u));
    return it != index.end() && kRtShaderManifest[it->second].bytes == bytes ? it->second : kShaders;
}

void meter_started(const D3D12_SHADER_BYTECODE& shader) noexcept {
    const std::size_t i = entry_of(shader.pShaderBytecode, shader.BytecodeLength);
    if (i == kShaders) return;
    Meter& m = meter();
    const std::lock_guard<std::mutex> guard(m.lock);
    if (m.state[i] == State::Waiting) {
        m.state[i] = State::Running;
        m.since[i] = std::chrono::steady_clock::now();
    }
}

// `compiled`: its compile's time teaches the meter (a load from the library says nothing of a compile's cost).
void meter_finished(const D3D12_SHADER_BYTECODE& shader, bool compiled) noexcept {
    const std::size_t i = entry_of(shader.pShaderBytecode, shader.BytecodeLength);
    if (i == kShaders) return;
    Meter& m = meter();
    const std::lock_guard<std::mutex> guard(m.lock);
    if (compiled && m.state[i] == State::Running) {
        const double ms =
            std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - m.since[i]).count();
        if (ms >= 1000.0) {
            if (weight(i) > m.cost_weight) {
                m.cost = ms / weight(i);
                m.cost_weight = weight(i);
            }
        }
    }
    // A shader several pipelines share counts once, at the first of them.
    m.state[i] = State::Done;
}

// ---- The library ----

// FNV-1a over a pipeline's description, field by field (never a struct's padding).
struct NameHash {
    std::uint64_t value = 0xCBF29CE484222325ull;
    void bytes(const void* data, std::size_t size) {
        const auto* p = static_cast<const unsigned char*>(data);
        for (std::size_t i = 0; i < size; ++i) value = (value ^ p[i]) * 0x100000001B3ull;
    }
    template <typename T>
    void field(T v) {
        bytes(&v, sizeof(v));
    }
    void text(const char* s) {
        if (s != nullptr) bytes(s, std::strlen(s));
        field<char>('\0');
    }
    // A shader by its container's digest (the whole bytecode for one without), its size beside it.
    void shader(const D3D12_SHADER_BYTECODE& code) {
        field<std::uint64_t>(code.BytecodeLength);
        if (const unsigned char* digest = container_digest(code.pShaderBytecode, code.BytecodeLength)) {
            bytes(digest, 16u);
        } else if (code.pShaderBytecode != nullptr) {
            bytes(code.pShaderBytecode, code.BytecodeLength);
        }
    }
};

struct Library {
    std::mutex lock;
    Identity identity;
    bool opened = false;
    bool prepared = false;
    Header header;                   // what the file holds, or will once saved
    std::vector<std::uint8_t> blob;  // the bytes the library was made over, which must outlive it
    Microsoft::WRL::ComPtr<ID3D12PipelineLibrary> library;
    bool dirty = false;              // pipelines stored since the file was read or written
    unsigned compiled = 0u;
    std::unordered_map<const ID3D12RootSignature*, std::uint64_t> roots;
};

Library& library() {
    static Library l;
    return l;
}

std::uint64_t root_hash(const ID3D12RootSignature* root) {
    Library& l = library();
    const std::lock_guard<std::mutex> guard(l.lock);
    const auto it = l.roots.find(root);
    return it != l.roots.end() ? it->second : 0u;
}

std::wstring pipeline_name(const NameHash& hash) {
    wchar_t name[24];
    std::swprintf(name, std::size(name), L"rt-%016llx", static_cast<unsigned long long>(hash.value));
    return name;
}

// The pipeline `name` from the library (`load` calls the library's Load*Pipeline).
template <typename Load>
bool load_from_library(Load&& load) {
    Library& l = library();
    const std::lock_guard<std::mutex> guard(l.lock);
    return l.library && SUCCEEDED(load(l.library.Get()));
}

void store_in_library(const std::wstring& name, ID3D12PipelineState* pso) {
    Library& l = library();
    const std::lock_guard<std::mutex> guard(l.lock);
    ++l.compiled;
    if (!l.library) return;
    const HRESULT hr = l.library->StorePipeline(name.c_str(), pso);
    if (SUCCEEDED(hr)) {
        l.dirty = true;
    } else {
        std::fprintf(stderr, "[rt-prepare] could not keep pipeline %ls (0x%08lx)\n", name.c_str(),
                     static_cast<unsigned long>(hr));
    }
}

// A compile, timed: a compile of seconds is the driver making it from the bytecode.
template <typename Create>
bool compile(std::initializer_list<D3D12_SHADER_BYTECODE> shaders, Create&& create) {
    for (const D3D12_SHADER_BYTECODE& s : shaders) meter_started(s);
    const auto start = std::chrono::steady_clock::now();
    const bool made = create();
    const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
    for (const D3D12_SHADER_BYTECODE& s : shaders) meter_finished(s, true);
    if (ms >= 2000.0) {
        std::size_t bytes = 0u;
        for (const D3D12_SHADER_BYTECODE& s : shaders) bytes += s.BytecodeLength;
        std::fprintf(stderr, "[rt-prepare] pipeline of %zu shader bytes compiled in %.0f ms\n", bytes, ms);
    }
    return made;
}

std::vector<std::uint8_t> read_file(const std::filesystem::path& path, std::uint64_t limit) {
    std::ifstream in(path, std::ios::binary | std::ios::ate);
    if (!in) return {};
    const std::streamoff size = in.tellg();
    if (size <= 0 || static_cast<std::uint64_t>(size) > limit) return {};
    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(size));
    in.seekg(0);
    if (!in.read(reinterpret_cast<char*>(bytes.data()), size)) return {};
    return bytes;
}

void put_u32(std::uint8_t* at, std::uint32_t v) { std::memcpy(at, &v, 4u); }
void put_u64(std::uint8_t* at, std::uint64_t v) { std::memcpy(at, &v, 8u); }
std::uint32_t get_u32(const std::uint8_t* at) { std::uint32_t v; std::memcpy(&v, at, 4u); return v; }
std::uint64_t get_u64(const std::uint8_t* at) { std::uint64_t v; std::memcpy(&v, at, 8u); return v; }

// Header layout: magic, version, shader set, adapter, driver, complete, (reserved), blob bytes.
constexpr std::size_t kAtShaders = 8u;
constexpr std::size_t kAtAdapter = 16u;
constexpr std::size_t kAtDriver = kAtAdapter + kNameBytes;
constexpr std::size_t kAtComplete = kAtDriver + kNameBytes;
constexpr std::size_t kAtBlobBytes = kAtComplete + 8u;
static_assert(kAtBlobBytes + 8u == kHeaderBytes);

}  // namespace

// ---- The header ----

std::vector<std::uint8_t> format_header(const Header& header, std::uint64_t blob_bytes) {
    std::vector<std::uint8_t> out(kHeaderBytes, 0u);
    put_u32(out.data(), kMagic);
    put_u32(out.data() + 4u, kVersion);
    put_u64(out.data() + kAtShaders, header.shaders);
    std::memcpy(out.data() + kAtAdapter, header.adapter.data(), std::min(header.adapter.size(), kNameBytes - 1u));
    std::memcpy(out.data() + kAtDriver, header.driver.data(), std::min(header.driver.size(), kNameBytes - 1u));
    put_u32(out.data() + kAtComplete, header.complete ? 1u : 0u);
    put_u64(out.data() + kAtBlobBytes, blob_bytes);
    return out;
}

bool parse_header(std::span<const std::uint8_t> bytes, Header& out, std::uint64_t& blob_bytes) {
    if (bytes.size() < kHeaderBytes || get_u32(bytes.data()) != kMagic || get_u32(bytes.data() + 4u) != kVersion) {
        return false;
    }
    const auto name = [&](std::size_t at) -> std::string {
        const char* text = reinterpret_cast<const char*>(bytes.data() + at);
        const std::size_t length = static_cast<std::size_t>(std::find(text, text + kNameBytes, '\0') - text);
        return length < kNameBytes ? std::string(text, length) : std::string{};
    };
    const std::uint32_t complete = get_u32(bytes.data() + kAtComplete);
    if (complete > 1u) return false;
    Header header;
    header.shaders = get_u64(bytes.data() + kAtShaders);
    header.adapter = name(kAtAdapter);
    header.driver = name(kAtDriver);
    header.complete = complete == 1u;
    out = std::move(header);
    blob_bytes = get_u64(bytes.data() + kAtBlobBytes);
    return true;
}

bool header_covers(const Header& header, std::uint64_t shaders, const Identity& identity) {
    return header.complete && header.shaders == shaders && !identity.adapter.empty() &&
           header.adapter == identity.adapter && header.driver == identity.driver;
}

// ---- The library ----

std::filesystem::path library_path() {
    const std::filesystem::path folder = shader_cache_directory();
    return folder.empty() ? folder : folder / "rt-pipelines.bin";
}

namespace {

// What a library file (its first bytes, its whole size) is to this build on `identity`.
struct Verdict {
    const char* why = nullptr;  // why it is not prepared; null when it is (header_covers)
    bool ours = false;          // its pipelines are this build's for this adapter and driver, complete or not
};

Verdict judge(std::span<const std::uint8_t> bytes, std::uint64_t file_bytes, const Identity& identity, Header& header) {
    std::uint64_t blob_bytes = 0u;
    if (file_bytes == 0u) return {"none kept", false};
    if (!parse_header(bytes, header, blob_bytes) || blob_bytes == 0u || blob_bytes != file_bytes - kHeaderBytes) {
        return {"not readable", false};
    }
    if (header.shaders != kRtShaderSetFingerprint) return {"made for other shaders", false};
    if (identity.adapter.empty() || header.adapter != identity.adapter || header.driver != identity.driver) {
        return {"made for another graphics card or driver", false};
    }
    return {header.complete ? nullptr : "incomplete", true};
}

}  // namespace

void identify(const Identity& identity) {
    Library& l = library();
    const std::lock_guard<std::mutex> guard(l.lock);
    l.identity = identity;
    const std::filesystem::path path = library_path();
    std::error_code ec;
    const std::uint64_t file_bytes = path.empty() ? 0u : std::filesystem::file_size(path, ec);
    std::vector<std::uint8_t> head;
    if (!ec && file_bytes >= kHeaderBytes && file_bytes <= kHeaderBytes + kMaxLibraryBytes) {
        std::ifstream in(path, std::ios::binary);
        head.resize(kHeaderBytes);
        if (!in.read(reinterpret_cast<char*>(head.data()), static_cast<std::streamsize>(kHeaderBytes))) head.clear();
    }
    Header header;
    const Verdict verdict = judge(head, head.empty() ? 0u : file_bytes, identity, header);
    l.prepared = verdict.why == nullptr;
    std::fprintf(stderr, "[rt-prepare] library %s (adapter %s, driver %s)\n",
                 verdict.why == nullptr ? "prepared" : verdict.why,
                 identity.adapter.c_str(), identity.driver.empty() ? "unknown" : identity.driver.c_str());
}

void open(ID3D12Device* device) {
    Library& l = library();
    const std::lock_guard<std::mutex> guard(l.lock);
    if (l.opened) return;
    l.opened = true;
    const Identity identity = l.identity;
    Microsoft::WRL::ComPtr<ID3D12Device1> device1;
    if (device == nullptr || FAILED(device->QueryInterface(IID_PPV_ARGS(&device1)))) {
        std::fprintf(stderr, "[rt-prepare] no pipeline library on this device\n");
        l.prepared = false;
        return;
    }
    const std::filesystem::path path = library_path();
    std::vector<std::uint8_t> file;
    try {
        if (!path.empty()) file = read_file(path, kHeaderBytes + kMaxLibraryBytes);
    } catch (const std::bad_alloc&) {
        file.clear();
    }
    Header header;
    const Verdict verdict = judge(file, file.size(), identity, header);
    const char* why = verdict.why;
    // An incomplete library of these shaders still serves the pipelines it has.
    if (verdict.ours) {
        l.blob.assign(file.begin() + static_cast<std::ptrdiff_t>(kHeaderBytes), file.end());
        const HRESULT hr = device1->CreatePipelineLibrary(l.blob.data(), l.blob.size(), IID_PPV_ARGS(&l.library));
        if (FAILED(hr)) {
            // D3D12_ERROR_ADAPTER_NOT_FOUND, D3D12_ERROR_DRIVER_VERSION_MISMATCH: the runtime's own check.
            std::fprintf(stderr, "[rt-prepare] the kept library is refused (0x%08lx)\n", static_cast<unsigned long>(hr));
            why = "refused by the D3D12 runtime";
            l.library.Reset();
            l.blob.clear();
        }
    }
    const bool kept = l.library != nullptr;
    if (!kept) {
        header = Header{};
        if (FAILED(device1->CreatePipelineLibrary(nullptr, 0u, IID_PPV_ARGS(&l.library)))) l.library.Reset();
    }
    l.header.shaders = kRtShaderSetFingerprint;
    l.header.adapter = identity.adapter;
    l.header.driver = identity.driver;
    l.header.complete = kept && header.complete;
    l.prepared = kept && why == nullptr;
    std::fprintf(stderr, "[rt-prepare] library opened: %s (%zu bytes)\n", why == nullptr ? "prepared" : why,
                 l.blob.size());
}

bool prepared() noexcept {
    Library& l = library();
    const std::lock_guard<std::mutex> guard(l.lock);
    return l.prepared;
}

namespace {
std::atomic<FirstStart> s_first_start{FirstStart::None};
}  // namespace

void set_first_start(FirstStart phase) noexcept {
    s_first_start.store(phase, std::memory_order_release);
}

FirstStart first_start() noexcept {
    return s_first_start.load(std::memory_order_acquire);
}

std::filesystem::path request_path() {
    const std::filesystem::path config = render_config_path();
    return config.empty() ? config : config.parent_path() / "rt-prepare.request";
}

bool preparation_requested() {
    static const bool requested = [] {
        std::error_code ec;
        const std::filesystem::path path = request_path();
        return !path.empty() && std::filesystem::is_regular_file(path, ec);
    }();
    return requested;
}

void withdraw_request() {
    std::error_code ec;
    const std::filesystem::path path = request_path();
    if (!path.empty() && std::filesystem::remove(path, ec)) {
        std::fprintf(stderr, "[rt-prepare] request done\n");
    }
}

bool root_signature(ID3D12Device* device, const D3D12_ROOT_SIGNATURE_DESC& desc,
                    Microsoft::WRL::ComPtr<ID3D12RootSignature>& out) {
    Microsoft::WRL::ComPtr<ID3DBlob> blob;
    Microsoft::WRL::ComPtr<ID3DBlob> error;
    if (FAILED(D3D12SerializeRootSignature(&desc, D3D_ROOT_SIGNATURE_VERSION_1, &blob, &error)) ||
        FAILED(device->CreateRootSignature(0u, blob->GetBufferPointer(), blob->GetBufferSize(), IID_PPV_ARGS(&out)))) {
        return false;
    }
    NameHash hash;
    hash.bytes(blob->GetBufferPointer(), blob->GetBufferSize());
    Library& l = library();
    const std::lock_guard<std::mutex> guard(l.lock);
    l.roots[out.Get()] = hash.value;
    return true;
}

bool compute(ID3D12Device* device, const D3D12_COMPUTE_PIPELINE_STATE_DESC& desc,
             Microsoft::WRL::ComPtr<ID3D12PipelineState>& out) {
    NameHash hash;
    hash.field<char>('C');
    hash.shader(desc.CS);
    hash.field(root_hash(desc.pRootSignature));
    hash.field(desc.NodeMask);
    hash.field(desc.Flags);
    const std::wstring name = pipeline_name(hash);
    if (load_from_library([&](ID3D12PipelineLibrary* l) {
            return l->LoadComputePipeline(name.c_str(), &desc, IID_PPV_ARGS(&out));
        })) {
        meter_finished(desc.CS, false);
        return true;
    }
    if (!compile({desc.CS}, [&] { return SUCCEEDED(device->CreateComputePipelineState(&desc, IID_PPV_ARGS(&out))); })) {
        return false;
    }
    store_in_library(name, out.Get());
    return true;
}

bool graphics(ID3D12Device* device, const D3D12_GRAPHICS_PIPELINE_STATE_DESC& desc,
              Microsoft::WRL::ComPtr<ID3D12PipelineState>& out) {
    NameHash hash;
    hash.field<char>('G');
    for (const D3D12_SHADER_BYTECODE* stage : {&desc.VS, &desc.PS, &desc.DS, &desc.HS, &desc.GS}) hash.shader(*stage);
    hash.field(root_hash(desc.pRootSignature));
    hash.field(desc.StreamOutput.NumEntries);
    const D3D12_BLEND_DESC& blend = desc.BlendState;
    hash.field(blend.AlphaToCoverageEnable);
    hash.field(blend.IndependentBlendEnable);
    for (const D3D12_RENDER_TARGET_BLEND_DESC& t : blend.RenderTarget) {
        hash.field(t.BlendEnable);
        hash.field(t.LogicOpEnable);
        hash.field(t.SrcBlend);
        hash.field(t.DestBlend);
        hash.field(t.BlendOp);
        hash.field(t.SrcBlendAlpha);
        hash.field(t.DestBlendAlpha);
        hash.field(t.BlendOpAlpha);
        hash.field(t.LogicOp);
        hash.field(t.RenderTargetWriteMask);
    }
    hash.field(desc.SampleMask);
    const D3D12_RASTERIZER_DESC& raster = desc.RasterizerState;
    hash.field(raster.FillMode);
    hash.field(raster.CullMode);
    hash.field(raster.FrontCounterClockwise);
    hash.field(raster.DepthBias);
    hash.field(raster.DepthBiasClamp);
    hash.field(raster.SlopeScaledDepthBias);
    hash.field(raster.DepthClipEnable);
    hash.field(raster.MultisampleEnable);
    hash.field(raster.AntialiasedLineEnable);
    hash.field(raster.ForcedSampleCount);
    hash.field(raster.ConservativeRaster);
    const D3D12_DEPTH_STENCIL_DESC& depth = desc.DepthStencilState;
    hash.field(depth.DepthEnable);
    hash.field(depth.DepthWriteMask);
    hash.field(depth.DepthFunc);
    hash.field(depth.StencilEnable);
    hash.field(depth.StencilReadMask);
    hash.field(depth.StencilWriteMask);
    for (const D3D12_DEPTH_STENCILOP_DESC* face : {&depth.FrontFace, &depth.BackFace}) {
        hash.field(face->StencilFailOp);
        hash.field(face->StencilDepthFailOp);
        hash.field(face->StencilPassOp);
        hash.field(face->StencilFunc);
    }
    hash.field(desc.InputLayout.NumElements);
    for (UINT i = 0; i < desc.InputLayout.NumElements; ++i) {
        const D3D12_INPUT_ELEMENT_DESC& e = desc.InputLayout.pInputElementDescs[i];
        hash.text(e.SemanticName);
        hash.field(e.SemanticIndex);
        hash.field(e.Format);
        hash.field(e.InputSlot);
        hash.field(e.AlignedByteOffset);
        hash.field(e.InputSlotClass);
        hash.field(e.InstanceDataStepRate);
    }
    hash.field(desc.IBStripCutValue);
    hash.field(desc.PrimitiveTopologyType);
    hash.field(desc.NumRenderTargets);
    for (const DXGI_FORMAT format : desc.RTVFormats) hash.field(format);
    hash.field(desc.DSVFormat);
    hash.field(desc.SampleDesc.Count);
    hash.field(desc.SampleDesc.Quality);
    hash.field(desc.NodeMask);
    hash.field(desc.Flags);
    const std::wstring name = pipeline_name(hash);
    if (load_from_library([&](ID3D12PipelineLibrary* l) {
            return l->LoadGraphicsPipeline(name.c_str(), &desc, IID_PPV_ARGS(&out));
        })) {
        meter_finished(desc.VS, false);
        meter_finished(desc.PS, false);
        return true;
    }
    if (!compile({desc.VS, desc.PS},
                 [&] { return SUCCEEDED(device->CreateGraphicsPipelineState(&desc, IID_PPV_ARGS(&out))); })) {
        return false;
    }
    store_in_library(name, out.Get());
    return true;
}

void finish_start(bool complete) {
    Library& l = library();
    std::vector<std::uint8_t> bytes;
    {
        const std::lock_guard<std::mutex> guard(l.lock);
        if (!l.library || (!l.dirty && (l.header.complete || !complete))) return;
        const SIZE_T size = l.library->GetSerializedSize();
        if (size == 0u || size > kMaxLibraryBytes) {
            std::fprintf(stderr, "[rt-prepare] library of %zu bytes not kept\n", static_cast<std::size_t>(size));
            return;
        }
        Header header = l.header;
        header.complete = header.complete || complete;
        bytes = format_header(header, size);
        bytes.resize(kHeaderBytes + size);
        const HRESULT hr = l.library->Serialize(bytes.data() + kHeaderBytes, size);
        if (FAILED(hr)) {
            std::fprintf(stderr, "[rt-prepare] library not serialized (0x%08lx)\n", static_cast<unsigned long>(hr));
            return;
        }
        // In this process every pipeline it holds loads from it, whether or not the file is written.
        l.header = header;
        l.dirty = false;
        l.prepared = l.header.complete;
    }
    const std::filesystem::path path = library_path();
    std::error_code ec;
    if (!path.empty()) std::filesystem::create_directories(path.parent_path(), ec);
    // The atomic writer takes any bytes.
    if (path.empty() || !galaxy::io::write_text_file_atomically(
                            path, std::string_view(reinterpret_cast<const char*>(bytes.data()), bytes.size()))) {
        std::fprintf(stderr, "[rt-prepare] library not written\n");
        return;
    }
    std::fprintf(stderr, "[rt-prepare] library written (%zu bytes%s)\n", bytes.size(),
                 complete ? ", complete" : "");
}

unsigned compiled() noexcept {
    Library& l = library();
    const std::lock_guard<std::mutex> guard(l.lock);
    return l.compiled;
}

// ---- The meter ----

void reset() noexcept {
    Meter& m = meter();
    const std::lock_guard<std::mutex> guard(m.lock);
    m.state.fill(State::Waiting);
    m.cost = 0.0;
    m.cost_weight = 0.0;
    m.shown = 0.0f;
}

float progress() noexcept {
    Meter& m = meter();
    const std::lock_guard<std::mutex> guard(m.lock);
    const auto now = std::chrono::steady_clock::now();
    const double cost = m.cost_weight > 0.0 ? m.cost : kDefaultCost;
    double total = 0.0;
    double done = 0.0;
    bool every_done = true;
    for (std::size_t i = 0; i < kShaders; ++i) {
        const double w = weight(i);
        total += w;
        if (m.state[i] == State::Done) {
            done += w;
            continue;
        }
        every_done = false;
        if (m.state[i] == State::Running) {
            const double elapsed = std::chrono::duration<double, std::milli>(now - m.since[i]).count();
            done += w * in_flight(elapsed / std::max(cost * w, 1.0));
        }
    }
    const float value = every_done ? 1.0f : total > 0.0 ? static_cast<float>(std::min(done / total, 0.99)) : 0.0f;
    m.shown = std::max(m.shown, value);
    return m.shown;
}

}  // namespace galaxy::gx::rt_prepare
