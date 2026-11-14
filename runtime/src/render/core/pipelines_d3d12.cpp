// SPDX-License-Identifier: GPL-3.0-only
#include "render/core/pipelines.h"

#include "render/core/pipeline_cache_file.h"
#include "galaxy/render/program_table.h"
#include "galaxy/render/render_config.h"
#include "galaxy/render/renderer_d3d12.h"

#include <nebula_model.h>

#include <array>
#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <cwchar>
#include <filesystem>
#include <fstream>
#include <memory>
#include <mutex>
#include <new>
#include <shared_mutex>
#include <system_error>
#include <unordered_map>
#include <vector>

namespace galaxy::render::core {
namespace {

// The cache file: a header (pipeline_cache_file.h: the adapter's vendor, device, revision and driver version, and the size and
// hash of the library blob), then the blob. A file whose header or hash does not fit is never handed to the runtime. The runtime
// itself also refuses a blob from another driver version or adapter (D3D12_ERROR_DRIVER_VERSION_MISMATCH,
// D3D12_ERROR_ADAPTER_NOT_FOUND) and the cache starts empty; the file's name carries the adapter's identity, so two cards do not
// take turns replacing one file.
// What the cache is good for: the adapter's vendor, device and revision and its user-mode driver's version.
D3d12CacheIdentity adapter_identity(gpu::Device* device) {
    Microsoft::WRL::ComPtr<IDXGIFactory4> factory;
    Microsoft::WRL::ComPtr<IDXGIAdapter1> adapter;
    if (FAILED(CreateDXGIFactory1(IID_PPV_ARGS(&factory))) || FAILED(factory->EnumAdapterByLuid(device->GetAdapterLuid(), IID_PPV_ARGS(&adapter)))) {
        return {};
    }
    DXGI_ADAPTER_DESC1 desc{};
    LARGE_INTEGER driver{};
    if (FAILED(adapter->GetDesc1(&desc)) || FAILED(adapter->CheckInterfaceSupport(__uuidof(IDXGIDevice), &driver))) {
        return {};
    }
    D3d12CacheIdentity identity;
    identity.known = true;
    identity.vendor_id = desc.VendorId;
    identity.device_id = desc.DeviceId;
    identity.revision = desc.Revision;
    identity.driver_version = static_cast<std::uint64_t>(driver.QuadPart);
    return identity;
}

// The driver's compiled pipelines, kept between runs. A blob from another driver or device fails to open and starts over.
struct DriverCache {
    Microsoft::WRL::ComPtr<ID3D12PipelineLibrary> library;
    std::vector<char> blob;  // the library reads it for as long as it lives
    std::filesystem::path file;
    D3d12CacheIdentity identity;
    std::mutex lock;
    // The library itself: loads hold it shared, so they run side by side; StorePipeline, GetSerializedSize, Serialize and
    // the release hold it alone. Taken before `lock`.
    std::shared_mutex use;
    bool opened = false;
    bool changed = false;
};
DriverCache gCache;
// The library synchronizes itself except when two threads load the same pipeline, which can change that pipeline's state
// in it unsafely (CreatePipelineLibrary, Thread Safety). A name is loaded by one thread at a time here because a pipeline
// is made by one thread at a time (Made, below); other names load side by side.
// A store or a serialization running beside other names' loads crashed inside one driver's library (an access violation
// in StorePipeline while other workers loaded, on a cold start), so those take the library alone (DriverCache::use) and
// loads share it.

ID3D12PipelineLibrary* driver_cache(gpu::Device* device) {
    std::lock_guard guard(gCache.lock);
    if (gCache.opened) {
        return gCache.library.Get();
    }
    gCache.opened = true;
    Microsoft::WRL::ComPtr<ID3D12Device1> device1;
    if (FAILED(device->QueryInterface(IID_PPV_ARGS(&device1)))) {
        return nullptr;
    }
    gCache.identity = adapter_identity(device);
    gCache.file = shader_cache_directory() / d3d12_cache_file_name(gCache.identity);
    if (std::ifstream in(gCache.file, std::ios::binary | std::ios::ate); in) {
        std::vector<char> file(static_cast<std::size_t>(in.tellg()));
        in.seekg(0);
        in.read(file.data(), static_cast<std::streamsize>(file.size()));
        gCache.blob = unwrap_d3d12_cache(gCache.identity, file);
    }
    if (gCache.blob.empty() || FAILED(device1->CreatePipelineLibrary(gCache.blob.data(), gCache.blob.size(), IID_PPV_ARGS(&gCache.library)))) {
        gCache.blob.clear();
        if (FAILED(device1->CreatePipelineLibrary(nullptr, 0u, IID_PPV_ARGS(&gCache.library)))) {
            gCache.library.Reset();
        }
    }
    return gCache.library.Get();
}

// Serializing needs no name lock (only loads of one name do), but it takes the library alone: loads and stores wait for
// the copy, and the file is written after; one writer at a time.
std::size_t write_driver_cache() noexcept {
    static std::mutex writing;
    std::lock_guard one(writing);
    std::vector<char> bytes;
    std::filesystem::path file;
    {
        const std::unique_lock alone(gCache.use);
        std::lock_guard guard(gCache.lock);
        if (gCache.library == nullptr || !gCache.changed) {
            return 0u;
        }
        const std::size_t blob_size = gCache.library->GetSerializedSize();
        std::vector<char> blob(blob_size);
        if (blob_size == 0u || FAILED(gCache.library->Serialize(blob.data(), blob_size))) {
            return 0u;
        }
        bytes = wrap_d3d12_cache(gCache.identity, blob.data(), blob.size());
        gCache.changed = false;
        file = gCache.file;
    }
    if (!replace_file(file, bytes.data(), bytes.size())) {
        std::lock_guard guard(gCache.lock);
        gCache.changed = true;
        return 0u;
    }
    return bytes.size();
}

void release_driver_cache() noexcept {
    write_driver_cache();
    const std::unique_lock alone(gCache.use);
    std::lock_guard guard(gCache.lock);
    gCache.library.Reset();
    gCache.blob.clear();
    gCache.opened = false;
    gCache.changed = false;
}

// A pipeline's name: two independent hashes of everything Direct3D is given for it (pipeline_name, below). Two requests
// whose descriptions are the same to Direct3D have one name whatever ids, variants and passes they came from (the game's
// ids and state bits tell apart pipelines Direct3D does not), so such a pipeline is made once and kept once in the
// library; a program cooked again under the same id has other bytes and so another name (a stored name is never replaced).
struct PipelineName {
    std::uint64_t a = 14695981039346656037ull;
    std::uint64_t b = 0x9E3779B97F4A7C15ull;
    void bytes(const void* data, std::size_t size) noexcept {
        const auto* at = static_cast<const unsigned char*>(data);
        for (std::size_t i = 0; i < size; ++i) {
            a = (a ^ at[i]) * 1099511628211ull;
            b = (b ^ (b >> 29u)) * 0xFF51AFD7ED558CCDull + at[i] + 1u;
        }
    }
    // A value without padding (an integer, an enumeration).
    template <typename T>
    void value(const T& v) noexcept {
        bytes(&v, sizeof(v));
    }
    [[nodiscard]] bool operator==(const PipelineName& other) const noexcept { return a == other.a && b == other.b; }
};

struct PipelineNameHash {
    [[nodiscard]] std::size_t operator()(const PipelineName& name) const noexcept { return static_cast<std::size_t>(name.a); }
};

std::array<wchar_t, 40> cache_name(const PipelineName& name) noexcept {
    std::array<wchar_t, 40> text{};
    std::swprintf(text.data(), text.size(), L"%016llx%016llx", static_cast<unsigned long long>(name.a),
                  static_cast<unsigned long long>(name.b));
    return text;
}

D3D12_BLEND blend_factor(std::uint32_t f) noexcept {
    switch (f) {
    case NEBULA_FACTOR_ZERO: return D3D12_BLEND_ZERO;
    case NEBULA_FACTOR_SRC_COLOR: return D3D12_BLEND_SRC_COLOR;
    case NEBULA_FACTOR_INV_SRC_COLOR: return D3D12_BLEND_INV_SRC_COLOR;
    case NEBULA_FACTOR_SRC_ALPHA: return D3D12_BLEND_SRC_ALPHA;
    case NEBULA_FACTOR_INV_SRC_ALPHA: return D3D12_BLEND_INV_SRC_ALPHA;
    case NEBULA_FACTOR_DST_ALPHA: return D3D12_BLEND_DEST_ALPHA;
    case NEBULA_FACTOR_INV_DST_ALPHA: return D3D12_BLEND_INV_DEST_ALPHA;
    case NEBULA_FACTOR_DST_COLOR: return D3D12_BLEND_DEST_COLOR;
    case NEBULA_FACTOR_INV_DST_COLOR: return D3D12_BLEND_INV_DEST_COLOR;
    default: return D3D12_BLEND_ONE;
    }
}

// Colour factors are invalid on the alpha channel.
D3D12_BLEND alpha_factor(D3D12_BLEND b) noexcept {
    switch (b) {
    case D3D12_BLEND_SRC_COLOR: return D3D12_BLEND_SRC_ALPHA;
    case D3D12_BLEND_INV_SRC_COLOR: return D3D12_BLEND_INV_SRC_ALPHA;
    case D3D12_BLEND_DEST_COLOR: return D3D12_BLEND_DEST_ALPHA;
    case D3D12_BLEND_INV_DEST_COLOR: return D3D12_BLEND_INV_DEST_ALPHA;
    default: return b;
    }
}

// A pipeline's input elements, as many as its inputs and its program name; the device's own ceiling is checked where the
// layout is made.
struct InputList {
    std::vector<D3D12_INPUT_ELEMENT_DESC> elements;
    void add(const char* name, UINT index, DXGI_FORMAT format, UINT slot, UINT offset) {
        elements.push_back({name, index, format, slot, offset, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0u});
    }
    [[nodiscard]] bool has(const char* name, UINT index) const noexcept {
        for (const D3D12_INPUT_ELEMENT_DESC& element : elements) {
            if (element.SemanticIndex == index && std::strcmp(element.SemanticName, name) == 0) {
                return true;
            }
        }
        return false;
    }
};

DXGI_FORMAT input_format(std::uint32_t format) noexcept {
    switch (format) {
    case 0: return DXGI_FORMAT_R32G32B32_FLOAT;
    case 1: return DXGI_FORMAT_R32G32_FLOAT;
    case 2: return DXGI_FORMAT_R8G8B8A8_UNORM;
    default: return DXGI_FORMAT_R32_UINT;
    }
}

void fill_inputs(const PipelineEntry& entry, bool motion, InputList& list) {
    const NebulaPipelineDesc& desc = entry.desc;
    const bool site = (desc.vertex & NEBULA_PROGRAM_SITE) != 0u;
    const bool wide = site && (desc.vertex & 1u) != 0u;
    list.add("POSITION", 0u, DXGI_FORMAT_R32G32B32_FLOAT, gpu::kStreamPosition, 0u);
    if (desc.position_stride == 0u) {
        list.add("BLENDINDICES", 0u, DXGI_FORMAT_R32_UINT, gpu::kStreamMatrix, 0u);
    }
    if (site && motion) {
        list.add("POSITION", 1u, DXGI_FORMAT_R32G32B32_FLOAT, gpu::kStreamBefore, 0u);
    }
    for (const NebulaLayoutInput& input : entry.inputs) {
        const UINT slot = input.stream == NEBULA_STREAM_ZERO ? UINT{gpu::kStreamZero} : UINT{input.stream};
        if (slot >= gpu::kStreamCount) {
            continue;  // no such buffer slot: the input is left out
        }
        const DXGI_FORMAT format = input_format(input.format);
        switch (input.semantic) {
        case NEBULA_INPUT_NORMAL: list.add("NORMAL", 0u, format, slot, input.offset); break;
        case NEBULA_INPUT_COLOR0: list.add("COLOR", 0u, format, slot, input.offset); break;
        case NEBULA_INPUT_COLOR1: list.add("COLOR", 1u, format, slot, input.offset); break;
        case NEBULA_INPUT_TANGENT: list.add("TANGENT", 0u, format, slot, input.offset); break;
        case NEBULA_INPUT_BINORMAL: list.add("BINORMAL", 0u, format, slot, input.offset); break;
        case NEBULA_INPUT_WIDEN: list.add("BLENDINDICES", 1u, format, slot, input.offset); break;
        case NEBULA_INPUT_MATRIX: list.add("BLENDINDICES", 0u, format, slot, input.offset); break;
        case NEBULA_INPUT_GAIN: list.add("BLENDWEIGHT", 0u, format, slot, input.offset); break;
        default:
            if (input.semantic >= NEBULA_INPUT_COORD0) {
                list.add("TEXCOORD", input.semantic - NEBULA_INPUT_COORD0, format, slot, input.offset);
            }
            break;
        }
    }
    const auto zero = [&](const char* name, UINT index, DXGI_FORMAT format) {
        if (!list.has(name, index)) {
            list.add(name, index, format, gpu::kStreamZero, 0u);
        }
    };
    // A ring's vertices name no matrix: every one is the item's first.
    zero("BLENDINDICES", 0u, DXGI_FORMAT_R32_UINT);
    if (!site) {
        return;
    }
    // A written shader's vertex declares everything: what this site doesn't send reads zero.
    zero("NORMAL", 0u, DXGI_FORMAT_R32G32B32_FLOAT);
    zero("TANGENT", 0u, DXGI_FORMAT_R32G32B32_FLOAT);
    if (wide) {
        zero("BLENDINDICES", 1u, DXGI_FORMAT_R32_UINT);
    }
    zero("BINORMAL", 0u, DXGI_FORMAT_R32G32B32_FLOAT);
    zero("COLOR", 0u, DXGI_FORMAT_R8G8B8A8_UNORM);
    zero("COLOR", 1u, DXGI_FORMAT_R8G8B8A8_UNORM);
    // The coordinates the written shader's vertex stage declares (its NATIVE_COORDS) beyond those sent.
    const UINT declared = program_coords(desc.vertex);
    for (UINT i = 0; i < declared; ++i) {
        zero("TEXCOORD", i, DXGI_FORMAT_R32G32_FLOAT);
    }
}

PipelineName pipeline_name(const D3D12_GRAPHICS_PIPELINE_STATE_DESC& pd, const InputList& inputs, const ProgramBytes& vs,
                           const ProgramBytes& ps) noexcept {
    PipelineName name;
    // The states (blend, sample mask, rasterizer, depth and stencil), then the formats and counts after the input layout.
    // The description was cleared whole before it was filled (build), so the bytes between its fields are zero. The root
    // signature is the device's one; the other stages, the stream output and the cached blob are never set.
    const auto* raw = reinterpret_cast<const unsigned char*>(&pd);
    constexpr std::size_t states = offsetof(D3D12_GRAPHICS_PIPELINE_STATE_DESC, BlendState);
    constexpr std::size_t layout = offsetof(D3D12_GRAPHICS_PIPELINE_STATE_DESC, InputLayout);
    constexpr std::size_t formats = offsetof(D3D12_GRAPHICS_PIPELINE_STATE_DESC, IBStripCutValue);
    constexpr std::size_t cached = offsetof(D3D12_GRAPHICS_PIPELINE_STATE_DESC, CachedPSO);
    name.bytes(raw + states, layout - states);
    name.bytes(raw + formats, cached - formats);
    name.value(pd.Flags);
    name.value(static_cast<std::uint64_t>(inputs.elements.size()));
    for (const D3D12_INPUT_ELEMENT_DESC& element : inputs.elements) {
        name.bytes(element.SemanticName, std::strlen(element.SemanticName) + 1u);
        name.value(element.SemanticIndex);
        name.value(element.Format);
        name.value(element.InputSlot);
        name.value(element.AlignedByteOffset);
        name.value(element.InputSlotClass);
        name.value(element.InstanceDataStepRate);
    }
    for (const ProgramBytes* program : {&vs, &ps}) {
        name.value(static_cast<std::uint64_t>(program->size));
        name.bytes(program->data, program->size);
    }
    return name;
}

// One creation of a pipeline at a time, and one object for every request of it. The workers and the draws ask for
// pipelines by the game's ids; several of those come to the same description (the 24 creations in flight in one crash
// dump of 11-13 held 3 pairs of identical ones). Made twice at once, the same pipeline is compiled twice, and two
// threads are inside the driver building the same thing, which engines that key their requests by the description never
// do. That dump's fault was inside the driver (its end-of-frame cache trim, under Present, read a cache entry whose
// owner had been freed while a worker's CreateGraphicsPipelineState was using the same entry); whether identical
// creations at once are what brings it about is not proven, and they are waste either way. So a request first looks
// for the pipeline among those made, then for a creation of it in flight, whose result it waits for and takes, and only
// then makes it itself.
struct Creation {
    gpu::Pipeline* result = nullptr;  // owned by the Pipelines that made it (owned_), until destroy_all
    bool finished = false;
    DWORD maker = 0u;      // the thread making it
    bool hurried = false;  // an asker above the maker lent it its priority (hurry); the maker takes its own back
    int restore = 0;
};

struct Made {
    std::mutex lock;
    std::condition_variable done;
    std::unordered_map<PipelineName, gpu::Pipeline*, PipelineNameHash> made;
    std::unordered_map<PipelineName, std::shared_ptr<Creation>, PipelineNameHash> making;
    std::atomic<std::uint64_t> shared{0u};  // requests answered by a pipeline made, or being made, for another
};
Made gMade;

// A draw waits for a pipeline a worker is making. The workers run at the lowest priority, so the maker could stay
// behind every other thread while the frame waits for it: the asker lends it its own priority until it is made.
// With gMade.lock held.
void hurry(Creation& creation) noexcept {
    const int mine = GetThreadPriority(GetCurrentThread());
    if (mine == THREAD_PRIORITY_ERROR_RETURN) {
        return;
    }
    const HANDLE maker = OpenThread(THREAD_SET_INFORMATION | THREAD_QUERY_INFORMATION, FALSE, creation.maker);
    if (maker == nullptr) {
        return;
    }
    const int theirs = GetThreadPriority(maker);
    if (theirs != THREAD_PRIORITY_ERROR_RETURN && theirs < mine && SetThreadPriority(maker, mine) != FALSE) {
        if (!creation.hurried) {
            creation.restore = theirs;
        }
        creation.hurried = true;
    }
    CloseHandle(maker);
}

// Ends the maker's creation on every way out of build: the askers get its result, and a pipeline made is remembered.
struct Finish {
    PipelineName name;
    std::shared_ptr<Creation> creation;
    gpu::Pipeline* result = nullptr;
    ~Finish() {
        {
            const std::lock_guard held(gMade.lock);
            creation->result = result;
            creation->finished = true;
            if (result != nullptr) {
                try {
                    gMade.made.emplace(name, result);
                } catch (...) {
                    // Not remembered: the next request loads it from the library by its name.
                }
            }
            gMade.making.erase(name);
            if (creation->hurried) {
                (void)SetThreadPriority(GetCurrentThread(), creation->restore);
            }
        }
        gMade.done.notify_all();
    }
};

}  // namespace

gpu::Pipeline* Pipelines::build(const PipelineEntry& entry, unsigned variant, unsigned aux) {
    const NebulaPipelineDesc& desc = entry.desc;
    const unsigned samples_log2 = variant & kSceneVariantSamplesMask;
    const unsigned program = program_variant(desc.pixel, aux,
        (desc.state & NEBULA_STATE_PIXEL_SHADING) != 0u ? 0u : samples_log2);
    // The tracer's prepass of a motion draw goes through the vertex stage's motion form, as the draw itself will.
    const ProgramBytes vs = program_bytes(desc.vertex, aux == kAuxPrepassMotion ? unsigned{kProgramTracedMotion} : program, false);
    const ProgramBytes ps = program_bytes(desc.pixel, program, true);
    if (vs.size == 0u || ps.size == 0u) {
        std::fprintf(stderr, "[pipelines] no program %llx %llx variant %u\n", static_cast<unsigned long long>(desc.vertex),
                     static_cast<unsigned long long>(desc.pixel), program);
        return nullptr;
    }
    const unsigned targets = aux_targets(aux);
    const bool prepass = targets == kAuxPrepass;

    D3D12_GRAPHICS_PIPELINE_STATE_DESC pd;
    std::memset(&pd, 0, sizeof(pd));  // every byte, the padding too: the name hashes the description's bytes (pipeline_name)
    pd.pRootSignature = root_;
    pd.VS = {vs.data, vs.size};
    pd.PS = {ps.data, ps.size};
    InputList inputs;
    fill_inputs(entry, aux_motion_form(aux), inputs);
    if (inputs.elements.size() > D3D12_IA_VERTEX_INPUT_STRUCTURE_ELEMENT_COUNT) {
        std::fprintf(stderr, "[pipelines] %zu vertex inputs: the device takes %u\n", inputs.elements.size(),
                     static_cast<unsigned>(D3D12_IA_VERTEX_INPUT_STRUCTURE_ELEMENT_COUNT));
        return nullptr;
    }
    pd.InputLayout = {inputs.elements.data(), static_cast<UINT>(inputs.elements.size())};

    const std::uint32_t state = desc.state;
    const bool target = (variant & kSceneVariantTarget) != 0u;
    D3D12_RENDER_TARGET_BLEND_DESC& rt = pd.BlendState.RenderTarget[0];
    rt.RenderTargetWriteMask = static_cast<UINT8>(
        ((state & NEBULA_STATE_COLOR_WRITE) != 0u
             ? D3D12_COLOR_WRITE_ENABLE_RED | D3D12_COLOR_WRITE_ENABLE_GREEN | D3D12_COLOR_WRITE_ENABLE_BLUE
             : 0) |
        ((state & (NEBULA_STATE_ALPHA_WRITE | NEBULA_STATE_ALPHA_PASS)) != 0u && target ? D3D12_COLOR_WRITE_ENABLE_ALPHA : 0));
    const std::uint32_t op = state & 3u;
    if (op != NEBULA_BLEND_REPLACE) {
        rt.BlendEnable = TRUE;
        if (op == NEBULA_BLEND_SUBTRACT) {
            rt.SrcBlend = D3D12_BLEND_ONE;
            rt.DestBlend = D3D12_BLEND_ONE;
            rt.BlendOp = D3D12_BLEND_OP_REV_SUBTRACT;
        } else {
            rt.SrcBlend = blend_factor((state >> 2u) & 15u);
            rt.DestBlend = blend_factor((state >> 6u) & 15u);
            rt.BlendOp = D3D12_BLEND_OP_ADD;
            if (!target) {
                // No destination alpha in the scene: it reads as one.
                if (rt.SrcBlend == D3D12_BLEND_DEST_ALPHA) rt.SrcBlend = D3D12_BLEND_ONE;
                else if (rt.SrcBlend == D3D12_BLEND_INV_DEST_ALPHA) rt.SrcBlend = D3D12_BLEND_ZERO;
                if (rt.DestBlend == D3D12_BLEND_DEST_ALPHA) rt.DestBlend = D3D12_BLEND_ONE;
                else if (rt.DestBlend == D3D12_BLEND_INV_DEST_ALPHA) rt.DestBlend = D3D12_BLEND_ZERO;
            }
        }
        rt.SrcBlendAlpha = alpha_factor(rt.SrcBlend);
        rt.DestBlendAlpha = alpha_factor(rt.DestBlend);
        rt.BlendOpAlpha = rt.BlendOp;
    }

    // An added clear shell's traced form (FB b94, EVERYTHING 6.7): an exchange, premultiplied: the program gives F R and
    // F, what is behind keeps 1 - F.
    if (aux >= kAuxTraced && (aux & kAuxExchange) != 0u) {
        rt.BlendEnable = TRUE;
        rt.SrcBlend = D3D12_BLEND_ONE;
        rt.DestBlend = D3D12_BLEND_INV_SRC_ALPHA;
        rt.BlendOp = D3D12_BLEND_OP_ADD;
        rt.SrcBlendAlpha = D3D12_BLEND_ONE;
        rt.DestBlendAlpha = D3D12_BLEND_INV_SRC_ALPHA;
        rt.BlendOpAlpha = D3D12_BLEND_OP_ADD;
    }

    pd.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
    // Culling all has no state: the walker draws nothing.
    switch ((state >> 15u) & 3u) {
    case NEBULA_CULL_BACK: pd.RasterizerState.CullMode = D3D12_CULL_MODE_BACK; break;
    case NEBULA_CULL_FRONT: pd.RasterizerState.CullMode = D3D12_CULL_MODE_FRONT; break;
    default: pd.RasterizerState.CullMode = D3D12_CULL_MODE_NONE; break;
    }
    pd.RasterizerState.FrontCounterClockwise = FALSE;
    pd.RasterizerState.DepthClipEnable = TRUE;

    // Depth is reversed: the game's less is greater.
    static constexpr D3D12_COMPARISON_FUNC kDepth[8] = {
        D3D12_COMPARISON_FUNC_NEVER,         D3D12_COMPARISON_FUNC_GREATER,   D3D12_COMPARISON_FUNC_EQUAL,
        D3D12_COMPARISON_FUNC_GREATER_EQUAL, D3D12_COMPARISON_FUNC_LESS,      D3D12_COMPARISON_FUNC_NOT_EQUAL,
        D3D12_COMPARISON_FUNC_LESS_EQUAL,    D3D12_COMPARISON_FUNC_ALWAYS,
    };
    if ((state & (1u << 10u)) != 0u) {
        pd.DepthStencilState.DepthEnable = TRUE;
        pd.DepthStencilState.DepthFunc = kDepth[(state >> 11u) & 7u];
        pd.DepthStencilState.DepthWriteMask = (state & (1u << 14u)) != 0u ? D3D12_DEPTH_WRITE_MASK_ALL : D3D12_DEPTH_WRITE_MASK_ZERO;
    }
    // The prepass leaves the stencil alone: a draw's marks are made once, by the scene pass.
    if (const std::uint32_t test = (state >> 20u) & 3u; test != 0u && !prepass) {
        static constexpr D3D12_COMPARISON_FUNC kStencilTest[4] = {D3D12_COMPARISON_FUNC_ALWAYS, D3D12_COMPARISON_FUNC_ALWAYS,
                                                                  D3D12_COMPARISON_FUNC_EQUAL, D3D12_COMPARISON_FUNC_NOT_EQUAL};
        static constexpr D3D12_STENCIL_OP kStencilOp[4] = {D3D12_STENCIL_OP_KEEP, D3D12_STENCIL_OP_INCR_SAT, D3D12_STENCIL_OP_DECR_SAT,
                                                           D3D12_STENCIL_OP_ZERO};
        pd.DepthStencilState.StencilEnable = TRUE;
        pd.DepthStencilState.StencilReadMask = 0xFFu;
        pd.DepthStencilState.StencilWriteMask = 0xFFu;
        D3D12_DEPTH_STENCILOP_DESC face{};
        face.StencilFailOp = D3D12_STENCIL_OP_KEEP;
        face.StencilDepthFailOp = D3D12_STENCIL_OP_KEEP;
        face.StencilPassOp = kStencilOp[(state >> 22u) & 3u];
        face.StencilFunc = kStencilTest[test];
        pd.DepthStencilState.FrontFace = face;
        pd.DepthStencilState.BackFace = face;
    }

    pd.SampleMask = UINT_MAX;
    pd.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    pd.NumRenderTargets = 1u;
    pd.RTVFormats[0] = target ? kTargetColorFormat : (variant & kSceneVariantHdr) != 0u ? kSceneHdrColorFormat : kSceneColorFormat;
    if (prepass) {
        // The item target alone: the draw's item where it is nearest.
        pd.RTVFormats[0] = kItemFormat;
        rt.RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_RED | D3D12_COLOR_WRITE_ENABLE_GREEN;
        rt.BlendEnable = FALSE;  // a blended entry's item (the second layer) replaces like any other
    } else if (targets == 1u) {
        pd.NumRenderTargets = 2u;
        pd.RTVFormats[1] = kMotionVectorFormat;
    } else if (targets == 4u) {
        // The motion pass's second target is bound and left as it is (the base program writes target 0 only).
        pd.NumRenderTargets = 2u;
        pd.RTVFormats[1] = kMotionVectorFormat;
        pd.BlendState.RenderTarget[1].RenderTargetWriteMask = 0u;
    } else if (targets == 2u || targets == 3u) {
        pd.NumRenderTargets = 2u;
        pd.RTVFormats[1] = kReactiveMaskFormat;
        pd.BlendState.IndependentBlendEnable = TRUE;
        D3D12_RENDER_TARGET_BLEND_DESC& mask = pd.BlendState.RenderTarget[1];
        mask.BlendEnable = TRUE;
        mask.SrcBlend = D3D12_BLEND_ONE;
        mask.DestBlend = D3D12_BLEND_ONE;
        mask.BlendOp = D3D12_BLEND_OP_MAX;
        mask.SrcBlendAlpha = D3D12_BLEND_ONE;
        mask.DestBlendAlpha = D3D12_BLEND_ONE;
        mask.BlendOpAlpha = D3D12_BLEND_OP_MAX;
        mask.RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_RED;
        if (targets == 3u) {
            pd.NumRenderTargets = 4u;
            pd.RTVFormats[2] = kLayerFormat;
            pd.RTVFormats[3] = kLayerFormat;
            D3D12_RENDER_TARGET_BLEND_DESC& color = pd.BlendState.RenderTarget[2];
            color = rt;
            color.LogicOpEnable = FALSE;
            color.RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_RED | D3D12_COLOR_WRITE_ENABLE_GREEN | D3D12_COLOR_WRITE_ENABLE_BLUE;
            D3D12_RENDER_TARGET_BLEND_DESC& through = pd.BlendState.RenderTarget[3];
            through = color;
            through.SrcBlend = D3D12_BLEND_ZERO;
            through.BlendOp = D3D12_BLEND_OP_ADD;
            through.DestBlend = rt.BlendOp == D3D12_BLEND_OP_ADD ? rt.DestBlend : D3D12_BLEND_ONE;
        }
    }
    pd.DSVFormat = kSceneDepthFormat;
    pd.SampleDesc.Count = 1u << samples_log2;

    const PipelineName name = pipeline_name(pd, inputs, vs, ps);
    std::shared_ptr<Creation> creation = std::make_shared<Creation>();
    {
        std::unique_lock held(gMade.lock);
        if (const auto found = gMade.made.find(name); found != gMade.made.end()) {
            gMade.shared.fetch_add(1u, std::memory_order_relaxed);
            return found->second;
        }
        const auto [at, first] = gMade.making.try_emplace(name, creation);
        if (!first) {
            // Another thread is making this very pipeline: its result is this request's too.
            creation = at->second;
            hurry(*creation);
            gMade.done.wait(held, [&creation] { return creation->finished; });
            if (creation->result != nullptr) {
                gMade.shared.fetch_add(1u, std::memory_order_relaxed);
            }
            return creation->result;
        }
        creation->maker = GetCurrentThreadId();
    }
    Finish finish{name, creation};
    gpu::PipelineRef pipeline;
    ID3D12PipelineLibrary* cache = driver_cache(device_);
    HRESULT hr = E_FAIL;
    if (cache != nullptr) {
        const std::array<wchar_t, 40> text = cache_name(name);
        {
            const std::shared_lock shared(gCache.use);
            hr = cache->LoadGraphicsPipeline(text.data(), &pd, IID_PPV_ARGS(&pipeline));
        }
        if (FAILED(hr)) {
            hr = device_->CreateGraphicsPipelineState(&pd, IID_PPV_ARGS(&pipeline));
            if (SUCCEEDED(hr)) {
                const std::unique_lock alone(gCache.use);
                const std::lock_guard guard(gCache.lock);
                gCache.changed = SUCCEEDED(cache->StorePipeline(text.data(), pipeline.Get())) || gCache.changed;
            }
        }
    } else {
        hr = device_->CreateGraphicsPipelineState(&pd, IID_PPV_ARGS(&pipeline));
    }
    if (FAILED(hr)) {
        std::fprintf(stderr, "[pipelines] create failed %08lx: %llx %llx state %08x variant %u aux %u\n", static_cast<unsigned long>(hr),
                     static_cast<unsigned long long>(desc.vertex), static_cast<unsigned long long>(desc.pixel), state, variant, aux);
        return nullptr;
    }
    gpu::Pipeline* const raw = pipeline.Get();
    {
        const std::lock_guard guard(lock_);
        owned_.push_back(std::move(pipeline));
    }
    finish.result = raw;
    return raw;
}

// Direct3D 12 has no library parts to link: its draws wait for the whole pipeline, and ID3D12PipelineLibrary (the driver cache
// loaded at start) makes that a load, not a compile, for every pipeline a run has made before.
gpu::Pipeline* Pipelines::build_fast(const PipelineEntry&, unsigned, unsigned) {
    return nullptr;
}

void Pipelines::destroy_all() noexcept {
    {
        // No creation is running: the workers have ended (shutdown) and no draw is made here.
        const std::lock_guard held(gMade.lock);
        gMade.made.clear();
    }
    owned_.clear();
    release_driver_cache();
}

std::size_t Pipelines::save_cache() noexcept {
    std::size_t distinct = 0u;
    {
        const std::lock_guard held(gMade.lock);
        distinct = gMade.made.size();
    }
    std::fprintf(stderr, "[pipelines] d3d12: %zu pipelines made or loaded, %llu more requests shared one of them\n", distinct,
                 static_cast<unsigned long long>(gMade.shared.load(std::memory_order_relaxed)));
    return write_driver_cache();
}

}  // namespace galaxy::render::core
