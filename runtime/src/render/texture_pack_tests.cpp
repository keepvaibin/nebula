#include "galaxy/texture_categories.h"
#include "galaxy/test_temp.h"
// SPDX-License-Identifier: GPL-3.0-only
// Texture packs: hashes, names, DDS layouts and decodes, PNG round trips, the index, loads, the budget and dumps, and
// the texture cache swapping a replacement in on a WARP device.

#include "galaxy/windows_api.h"

#include "galaxy/render/block4_astc.h"
#include "galaxy/render/render_core.h"
#include "galaxy/render/texture_store.h"
#include "galaxy/render/texture_pack.h"
#include "nebula_cooked_texture.h"

#include <d3d12.h>
#include <dxgi1_4.h>
#include <wrl/client.h>

#include <chrono>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <string>
#include <thread>
#include <vector>

using Microsoft::WRL::ComPtr;
using namespace galaxy::render;

namespace {

int failures = 0;

void expect(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAILED: " << message << '\n';
        ++failures;
    }
}

using galaxy::textures::Category;
using galaxy::textures::name_hash;

// The game's names these tests use (the store's names table).
void set_names() {
    const auto row = [](const char* name, Category category) {
        galaxy::textures::TextureName out;
        out.name = name;
        out.hash = name_hash(name);
        out.category = category;
        return out;
    };
    galaxy::textures::set_index_for_tests({row("Model/Body", Category::Planets), row("IconCmpr", Category::Icons),
                                           row("ModTex", Category::Objects), row("Coin", Category::Items),
                                           row("Shared", Category::Hud), row("OnlyA", Category::Hud),
                                           row("CacheTex", Category::Objects), row("Mario/Body", Category::MarioLuigi),
                                           row("Misc/Thing", Category::Other)});
}

void put32(std::vector<std::uint8_t>& v, std::size_t at, std::uint32_t value) { std::memcpy(v.data() + at, &value, 4u); }

std::vector<std::uint8_t> dds_header(std::uint32_t width, std::uint32_t height, std::uint32_t levels,
                                     std::uint32_t fourcc, std::uint32_t dxgi, std::uint32_t r_mask = 0u) {
    std::vector<std::uint8_t> file(dxgi != 0u ? 148u : 128u, 0u);
    std::memcpy(file.data(), "DDS ", 4u);
    put32(file, 4u, 124u);
    put32(file, 12u, height);
    put32(file, 16u, width);
    put32(file, 28u, levels);
    put32(file, 76u, 32u);
    if (fourcc != 0u) {
        put32(file, 80u, 0x4u);
        put32(file, 84u, fourcc);
    } else {
        put32(file, 80u, 0x41u);
        put32(file, 88u, 32u);
        put32(file, 92u, r_mask);
        put32(file, 104u, 0xFF000000u);
    }
    if (dxgi != 0u) {
        put32(file, 128u, dxgi);
        put32(file, 132u, 3u);
        put32(file, 140u, 1u);
    }
    return file;
}

constexpr std::uint32_t fourcc(const char* code) {
    return static_cast<std::uint32_t>(static_cast<std::uint8_t>(code[0])) |
           (static_cast<std::uint32_t>(static_cast<std::uint8_t>(code[1])) << 8u) |
           (static_cast<std::uint32_t>(static_cast<std::uint8_t>(code[2])) << 16u) |
           (static_cast<std::uint32_t>(static_cast<std::uint8_t>(code[3])) << 24u);
}

void write_file(const std::filesystem::path& path, const std::vector<std::uint8_t>& bytes) {
    std::ofstream out(path, std::ios::binary);
    out.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
}

void test_dds(const std::filesystem::path& root) {
    // BC7 64x64, 7 levels: 64*64 + 32*32 + 16*16 + 8*8 + 4*4 + 4*4 + 4*4 bytes (16 per 4x4 block).
    std::vector<std::uint8_t> bc7 = dds_header(64u, 64u, 7u, fourcc("DX10"), DXGI_FORMAT_BC7_UNORM_SRGB);
    bc7.resize(bc7.size() + 4096u + 1024u + 256u + 64u + 16u + 16u + 16u);
    DdsLayout layout{};
    expect(parse_dds(bc7, layout), "BC7 DX10 header parses");
    expect(layout.format == DdsFormat::BC7 && layout.levels == 7u && layout.width == 64u,
           "BC7 sRGB loads as BC7 with 7 levels");
    expect(layout.level_offset[0] == 148u && layout.level_offset[1] == 148u + 4096u && layout.rows[0] == 16u &&
               layout.row_bytes[0] == 256u && layout.row_bytes[6] == 16u,
           "BC7 level layout");
    // DXT1 truncated after its first level: one level kept.
    std::vector<std::uint8_t> dxt1 = dds_header(32u, 32u, 6u, fourcc("DXT1"), 0u);
    dxt1.resize(dxt1.size() + 512u + 10u);
    expect(parse_dds(dxt1, layout) && layout.format == DdsFormat::BC1 && layout.levels == 1u,
           "DXT1 keeps the levels the file holds");
    // DX9 A8R8G8B8 is BGRA in memory.
    std::vector<std::uint8_t> argb = dds_header(4u, 4u, 1u, 0u, 0u, 0x00FF0000u);
    argb.resize(argb.size() + 64u);
    expect(parse_dds(argb, layout) && layout.format == DdsFormat::BGRA8 && layout.row_bytes[0] == 16u,
           "A8R8G8B8 loads as BGRA8");
    std::vector<std::uint8_t> r5g6b5 = dds_header(4u, 4u, 1u, 0u, 0u, 0xF800u);
    r5g6b5.resize(r5g6b5.size() + 64u);
    expect(!parse_dds(r5g6b5, layout), "16-bit DDS is not supported");
    expect(!parse_dds(std::vector<std::uint8_t>(100u, 0u), layout), "short file rejected");

    // A BC1 block: colour0 white, colour1 black, every texel index 1 (black) -> opaque black.
    std::vector<std::uint8_t> bc1 = dds_header(8u, 8u, 1u, fourcc("DXT1"), 0u);
    for (int block = 0; block < 4; ++block) {
        const std::uint8_t bytes[8] = {0xFF, 0xFF, 0x00, 0x00, 0x55, 0x55, 0x55, 0x55};
        bc1.insert(bc1.end(), bytes, bytes + 8);
    }
    write_file(root / "bc1.dds", bc1);
    TexturePackPixels pixels;
    expect(load_texture_pack_image(root / "bc1.dds", true, pixels, nullptr) && pixels.width == 8u &&
               pixels.levels.size() == 4u && pixels.levels[0].size() == 256u && pixels.levels[0][0] == 0u &&
               pixels.levels[0][3] == 255u && pixels.levels[3].size() == 4u,
           "BC1 decodes to RGBA8 and its missing levels are generated");
    // BGRA8: blue and red swap to RGBA.
    std::vector<std::uint8_t> bgra = dds_header(2u, 1u, 1u, 0u, 0u, 0x00FF0000u);
    const std::uint8_t texels[8] = {10, 20, 30, 40, 50, 60, 70, 80};
    bgra.insert(bgra.end(), texels, texels + 8);
    write_file(root / "bgra.dds", bgra);
    expect(load_texture_pack_image(root / "bgra.dds", true, pixels, nullptr) && pixels.levels.size() == 2u &&
               pixels.levels[0][0] == 30u && pixels.levels[0][2] == 10u && pixels.levels[0][3] == 40u,
           "BGRA8 DDS loads as RGBA8");
}

void test_png(const std::filesystem::path& root) {
    std::vector<std::uint8_t> rgba(6u * 5u * 4u);
    for (std::size_t i = 0; i < rgba.size(); ++i) rgba[i] = static_cast<std::uint8_t>(i * 13u + 1u);
    expect(write_png(root / "round.png", 6u, 5u, rgba.data()), "PNG written");
    TexturePackPixels pixels;
    std::string error;
    expect(load_texture_pack_image(root / "round.png", false, pixels, &error) && pixels.width == 6u &&
               pixels.height == 5u && pixels.levels.size() == 3u && pixels.levels[0] == rgba &&
               pixels.levels[1].size() == 3u * 2u * 4u && pixels.levels[2].size() == 4u,
           "PNG round trip, levels down to 1x1");
    { std::ofstream out(root / "bad.png"); out << "not a png"; }
    expect(!load_texture_pack_image(root / "bad.png", false, pixels, &error) && !error.empty(),
           "a broken PNG is refused with a reason");
}

void test_sampler_shift() {
    TextureSampler mode{};
    mode.mag_linear = mode.min_linear = true;
    auto key = make_sampler_key(mode, false, 9u, 2.0f);
    expect(key.max_lod == 2.0f && key.min_lod == 0.0f && ((key.filter >> 5u) & 7u) == 6u,
           "mip-less texture replacement samples down to its source size");
    mode.mip = 2; mode.min_lod = 0.5f; mode.max_lod = 3.0f;
    key = make_sampler_key(mode, false, 9u, 2.0f);
    expect(key.min_lod == 2.5f && key.max_lod == 5.0f, "authored LOD range follows the replacement scale");
    mode.min_lod = 0.0f;
    key = make_sampler_key(mode, false, 9u, 2.0f);
    expect(key.min_lod == 0.0f, "close-up replacement base level remains visible");
    key = make_sampler_key(mode, false, 11u, 3.0f, true, 2.0f);
    expect(key.lod_bias == -1.0f && key.max_lod == 10.0f && ((key.filter >> 5u) & 7u) == 7u,
           "arbitrary replacement levels pair by authored index across the full chain");
}

struct Gpu {
    ComPtr<ID3D12Device> device;
    ComPtr<ID3D12CommandQueue> queue;
    ComPtr<ID3D12CommandAllocator> allocator;
    ComPtr<ID3D12GraphicsCommandList> list;
    ComPtr<ID3D12Fence> fence;
    std::uint64_t value = 0;

    bool create() {
        ComPtr<IDXGIFactory4> factory;
        ComPtr<IDXGIAdapter> adapter;
        D3D12_COMMAND_QUEUE_DESC qd{};
        return SUCCEEDED(CreateDXGIFactory1(IID_PPV_ARGS(&factory))) &&
               SUCCEEDED(factory->EnumWarpAdapter(IID_PPV_ARGS(&adapter))) &&
               SUCCEEDED(D3D12CreateDevice(adapter.Get(), D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&device))) &&
               SUCCEEDED(device->CreateCommandQueue(&qd, IID_PPV_ARGS(&queue))) &&
               SUCCEEDED(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&allocator))) &&
               SUCCEEDED(device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, allocator.Get(), nullptr,
                                                   IID_PPV_ARGS(&list))) &&
               SUCCEEDED(device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence)));
    }

    void run() {
        list->Close();
        ID3D12CommandList* lists[] = {list.Get()};
        queue->ExecuteCommandLists(1, lists);
        queue->Signal(fence.Get(), ++value);
        while (fence->GetCompletedValue() < value) std::this_thread::yield();
        allocator->Reset();
        list->Reset(allocator.Get(), nullptr);
    }

    gpu::TextureRef texture(std::uint32_t width, std::uint32_t height) {
        D3D12_HEAP_PROPERTIES heap{};
        heap.Type = D3D12_HEAP_TYPE_DEFAULT;
        D3D12_RESOURCE_DESC rd{};
        rd.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        rd.Width = width;
        rd.Height = height;
        rd.DepthOrArraySize = 1;
        rd.MipLevels = 1;
        rd.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        rd.SampleDesc.Count = 1;
        gpu::TextureRef out;
        device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &rd, D3D12_RESOURCE_STATE_COMMON, nullptr,
                                        IID_PPV_ARGS(&out));
        return out;
    }
};

// Pumps until `done` or about two seconds.
template <typename Done>
bool pump_until(TexturePacks& packs, std::vector<std::pair<std::uint32_t, TexturePackPixels>>& uploads, Done done,
                bool* changed = nullptr) {
    for (int i = 0; i < 400; ++i) {
        packs.begin_frame();
        const bool now = packs.pump(uploads);
        if (changed != nullptr) *changed = *changed || now;
        if (done()) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    return false;
}

void test_packs(Gpu& gpu, const std::filesystem::path& root) {
    // Files by the game's names: <model>/<texture> under any folders of the pack's own, <texture> for a texture file's.
    const std::filesystem::path game = root / "My Pack" / TexturePacks::kGameId;
    std::filesystem::create_directories(game / "Planets" / "Model");
    // A 32x32 game texture replaced by a 128x128 PNG, with its custom level 1 (64x64, all 7s) beside it.
    std::vector<std::uint8_t> big(128u * 128u * 4u);
    for (std::size_t i = 0; i < big.size(); ++i) big[i] = static_cast<std::uint8_t>(i * 13u);
    expect(write_png(game / "Planets" / "Model" / "Body.png", 128u, 128u, big.data()), "pack PNG written");
    std::vector<std::uint8_t> sevens(64u * 64u * 4u, 7u);
    expect(write_png(game / "Planets" / "Model" / "Body_mip1.png", 64u, 64u, sevens.data()), "custom level written");
    // A texture file's replacement as a 64x64 BC1 DDS with its 7 levels; a PNG of its name loses to it.
    std::vector<std::uint8_t> bc1 = dds_header(64u, 64u, 7u, fourcc("DXT1"), 0u);
    bc1.resize(bc1.size() + 2048u + 512u + 128u + 32u + 8u + 8u + 8u, 0x11u);
    write_file(game / "IconCmpr.dds", bc1);
    write_png(game / "IconCmpr.png", 64u, 64u, sevens.data());
    // A name the game has no texture of, and a Dolphin name: counted and listed, not indexed.
    { std::ofstream out(game / "readme.png"); out << "x"; }
    { std::ofstream out(game / "tex1_8x8_0123456789abcdef_14.png"); out << "x"; }
    // A converted pack's originals are never read.
    std::filesystem::create_directories(game / ".dolphin");
    { std::ofstream out(game / ".dolphin" / "Coin.png"); out << "x"; }
    // A mod's textures/ folder without a game folder: indexed itself, after (winning over) the pack.
    const std::filesystem::path mod = root / "mod" / "textures";
    std::filesystem::create_directories(mod);
    std::vector<std::uint8_t> mod_pixels(16u * 16u * 4u, 0x22u);
    expect(write_png(mod / "ModTex.png", 16u, 16u, mod_pixels.data()), "mod PNG written");

    TexturePacks packs;
    TexturePackSettings settings{};
    settings.packs = {root / "My Pack", mod};
    settings.dumps = root / "dumps";
    settings.budget_bytes = 64ull << 20;
    packs.configure(settings);
    std::vector<std::pair<std::uint32_t, TexturePackPixels>> uploads;
    bool changed = false;
    expect(pump_until(packs, uploads, [&] { return packs.stats().names != 0u; }, &changed) && changed,
           "index published");
    const TexturePacks::Stats stats = packs.stats();
    expect(stats.names == 3u && stats.files == 7u && stats.folders == 2u && stats.levels == 1u && stats.dolphin == 1u &&
               stats.unknown == 1u,
           "three textures (one custom level) among seven files in two folders; one Dolphin name, one unknown");
    const TexturePacks::Summary summary = TexturePacks::summary();
    expect(summary.dolphin == std::vector<std::string>{"tex1_8x8_0123456789abcdef_14.png"} &&
               summary.unknown == std::vector<std::string>{"readme.png"},
           "the summary names the files left out");
    const std::uint32_t png_slot = packs.find(name_hash("Model/Body"));
    const std::uint32_t dds_slot = packs.find(name_hash("IconCmpr"));
    const std::uint32_t mod_slot = packs.find(name_hash("ModTex"));
    expect(png_slot != TexturePacks::kNone && dds_slot != TexturePacks::kNone && mod_slot != TexturePacks::kNone,
           "every name found");
    expect(packs.find(name_hash("model/BODY")) == png_slot, "names without case");
    expect(packs.file(dds_slot).extension() == ".dds", "a DDS wins over a PNG of the same name");
    expect(packs.find(name_hash("Coin")) == TexturePacks::kNone && packs.find(0u) == TexturePacks::kNone,
           "a texture no pack names misses");
    packs.add_user(png_slot);
    packs.add_user(dds_slot);
    std::vector<std::pair<std::uint32_t, TexturePackPixels>> loaded;
    expect(pump_until(packs, uploads,
                      [&] {
                          for (auto& upload : uploads) loaded.push_back(std::move(upload));
                          uploads.clear();
                          return loaded.size() >= 2u;
                      }),
           "both replacements handed out to upload");
    for (auto& [slot, pixels] : loaded) {
        if (slot == png_slot) {
            expect(pixels.width == 128u && pixels.levels.size() == 8u && pixels.levels[1].size() == 64u * 64u * 4u &&
                       pixels.levels[1][0] == 7u && pixels.levels[0] == big,
                   "PNG: 128x128, full chain, custom level 1");
        } else {
            expect(slot == dds_slot && pixels.width == 64u && pixels.levels.size() == 7u, "DDS: 64x64, its 7 levels");
        }
        TextureReplacement replacement{};
        replacement.texture = gpu.texture(pixels.width, pixels.height);
        replacement.width = pixels.width;
        replacement.height = pixels.height;
        replacement.levels = static_cast<std::uint32_t>(pixels.levels.size());
        replacement.bytes = 1u << 20;
        packs.make_resident(slot, std::move(replacement));
    }
    const TextureReplacement* a = packs.resident(png_slot);
    const TextureReplacement* b = packs.resident(dds_slot);
    expect(a != nullptr && a->lod_shift == 0.0f && b != nullptr && b->lod_shift == 0.0f,
           "resident (the texture store sets the LOD shift as it binds them)");
    expect(packs.stats().resident == 2u && packs.stats().resident_bytes == 2u << 20, "two resident");

    // Budget: both unbound, then a budget below their sum evicts the least recently used one.
    packs.remove_user(png_slot);
    packs.begin_frame();
    packs.begin_frame();
    packs.remove_user(dds_slot);
    settings.budget_bytes = (2u << 20) - 1u;
    packs.configure(settings);
    expect(packs.stats().evictions == 1u && packs.resident(png_slot) == nullptr && packs.resident(dds_slot) != nullptr,
           "budget evicts the least recently used unbound replacement");

    // Dump: a 4x4 texture written once as a PNG under its category and name; a hash the game does not name is counted.
    settings.dump = true;
    packs.configure(settings);
    expect(packs.dumping(), "dump mode on");
    std::vector<std::uint8_t> pixels(64u);
    for (std::size_t i = 0; i < pixels.size(); ++i) pixels[i] = static_cast<std::uint8_t>(i * 4u);
    packs.dump(name_hash("Coin"), 4u, 4u, pixels.data());
    packs.dump(name_hash("Coin"), 4u, 4u, pixels.data());
    packs.dump(name_hash("NotAName"), 4u, 4u, pixels.data());
    const std::filesystem::path file = settings.dumps / TexturePacks::kGameId / "Items" / "Coin.png";
    for (int i = 0; i < 2000 && (packs.stats().dumped == 0u || packs.stats().unnamed_dumps == 0u); ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    TexturePackPixels back;
    expect(packs.stats().dumped == 1u && packs.stats().unnamed_dumps == 1u &&
               load_texture_pack_image(file, false, back, nullptr) && back.width == 4u && back.levels[0] == pixels,
           "dumped PNG holds the texture once, under its category");
    packs.begin_frame();
    expect(packs.pump(uploads) && !packs.pump(uploads), "toggling the dump asks for one look-up");

    // The game folder itself as the pack folder.
    settings.packs = {root / "My Pack" / TexturePacks::kGameId};
    packs.configure(settings);
    changed = false;
    expect(pump_until(packs, uploads, [&] { return changed && packs.stats().names == 2u; }, &changed),
           "a game folder given directly is indexed");

    // Off: the index goes and lookups stop.
    settings.enabled = false;
    packs.configure(settings);
    packs.begin_frame();
    expect(packs.pump(uploads) && !packs.enabled() &&
               packs.find(name_hash("Model/Body")) == TexturePacks::kNone,
           "turning packs off drops the index");
    packs.shutdown();
}

// Priority: packs side by side in the TexturePacks folder by name (a later one wins a texture), then each mod's
// textures/ folder in load order (later wins); textures only one pack names come from it whatever its place.
void test_priority(const std::filesystem::path& root) {
    std::vector<std::uint8_t> pixels(32u * 32u * 4u, 9u);
    const std::filesystem::path packs = root / "priority" / "TexturePacks";
    for (const char* pack : {"A HD pack", "B buttons"}) {
        std::filesystem::create_directories(packs / pack / "RMGE01");
        write_png(packs / pack / "RMGE01" / "Shared.png", 32u, 32u, pixels.data());
    }
    write_png(packs / "A HD pack" / "RMGE01" / "OnlyA.png", 32u, 32u, pixels.data());
    const std::vector<std::filesystem::path> folders{packs};
    const auto index = TexturePacks::build_index(folders, {}, 1u);
    const auto file_of = [](const std::shared_ptr<TexturePacks::Index>& in, const char* name) {
        const auto it = in->names.find(name_hash(name));
        return it != in->names.end() ? in->records[it->second].file : std::filesystem::path{};
    };
    expect(file_of(index, "Shared").parent_path().parent_path().filename() == "B buttons",
           "packs side by side: the later by name wins a texture");
    expect(file_of(index, "OnlyA").parent_path().parent_path().filename() == "A HD pack",
           "a texture one pack names comes from it");
    const std::filesystem::path mod = root / "priority" / "mods" / "Switch prompts" / "textures";
    std::filesystem::create_directories(mod);
    write_png(mod / "Shared.png", 32u, 32u, pixels.data());
    const std::vector<std::filesystem::path> with_mod{packs, mod};
    expect(file_of(TexturePacks::build_index(with_mod, {}, 2u), "Shared").parent_path() == mod,
           "a mod's textures win over the TexturePacks folder");
}

// The texture store with a pack: the original shows first, the replacement takes its place at a frame begin, and
// turning packs off brings the original back. Dump mode writes the original under the name the pack uses.
void test_cache(Gpu& gpu, const std::filesystem::path& root) {
    std::filesystem::create_directories(root / "cache" / TexturePacks::kGameId);
    // A cooked texture record (nebula_cooked_texture.h): 32x32 RGBA8 texels, named for packs by the disc's level 0.
    std::vector<std::byte> texels(NEBULA_TEXTURE_HEADER + nebula_texture_level_bytes(32u, 32u, 1u, NEBULA_TEXTURE_RGBA8));
    nebula_texture_init(texels.data(), 32u, 32u, 1u, NEBULA_TEXTURE_RGBA8, 0u);
    for (std::size_t i = NEBULA_TEXTURE_HEADER; i < texels.size(); ++i) texels[i] = static_cast<std::byte>(i * 31u + 7u);
    {
        auto* const record = reinterpret_cast<NebulaTexture*>(texels.data());
        record->source_format = 6u;  // RGBA8 on the disc
        record->name_hash = name_hash("CacheTex");
    }
    std::vector<std::uint8_t> big(96u * 96u * 4u, 0x80u);
    expect(write_png(root / "cache" / TexturePacks::kGameId / "CacheTex.png", 96u, 96u, big.data()), "cache test PNG written");

    const auto owner = std::make_unique<TextureStore>();
    TextureStore& cache = *owner;
    DeviceGate gate;
    gate.set_device(gpu.device.Get());
    expect(cache.initialize(gpu.device.Get(), gate), "cache initializes");
    NebulaTextureImage made{};
    made.bytes = texels.data();
    made.size = static_cast<std::uint32_t>(texels.size());
    made.width = 32u;
    made.height = 32u;
    made.format = NEBULA_TEXTURE_IMAGE;
    made.levels = 1u;
    const std::uint32_t id = cache.register_image(made);
    unsigned frame = 0;
    std::vector<std::uint32_t> freed;
    const auto start_frame = [&] {
        cache.begin_frame(frame++ % 2u, 2u);
        cache.backend().set_upload_list(gpu.list.Get());
        if (const auto ops = cache.take_ops()) {
            cache.apply_ops(*ops, freed);
        }
    };
    const auto next_frame = [&] {
        gpu.run();
        start_frame();
        cache.apply_texture_packs();
    };
    start_frame();
    TexturePackSettings settings{};
    settings.packs = {root / "cache"};
    settings.dumps = root / "cache-dumps";
    settings.budget_bytes = 64ull << 20;
    cache.configure_texture_packs(settings);
    for (int i = 0; i < 400 && cache.texture_pack_stats().names == 0u; ++i) {
        next_frame();
        if (cache.texture_pack_stats().names == 0u) std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    expect(cache.texture_pack_stats().names == 1u, "cache publishes the pack index");
    const TextureHandle original = cache.handle(id);
    expect(original.resource != nullptr && original.resource->GetDesc().Width == 32u && original.lod_shift == 0u,
           "the original shows while its replacement loads");
    TextureHandle replaced{};
    for (int i = 0; i < 400; ++i) {
        next_frame();
        replaced = cache.handle(id);
        if (replaced.resource != nullptr && replaced.resource->GetDesc().Width == 96u) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    expect(replaced.resource != nullptr && replaced.resource->GetDesc().Width == 96u && replaced.width == 32u &&
               replaced.height == 32u && replaced.mip_levels == 7u && std::fabs(replaced.lod_shift - std::log2(3.0f)) < 1e-6f,
           "the replacement takes the original's place (source size kept, 3x shift)");
    next_frame();
    expect(cache.handle(id).resource == replaced.resource, "a resident replacement stays bound");
    next_frame();
    expect(gpu.device->GetDeviceRemovedReason() == S_OK, "device alive with the replacement");

    // Packs off: the original again; dump mode writes it under its pack name.
    settings.enabled = false;
    settings.dump = true;
    cache.configure_texture_packs(settings);
    next_frame();
    const TextureHandle back = cache.handle(id);
    expect(back.resource != nullptr && back.resource->GetDesc().Width == 32u, "packs off: the original again");
    // Images are dumped as they load; the store keeps no copy of those already on the GPU.
    (void)cache.register_image(made);
    next_frame();
    const std::filesystem::path dumped = settings.dumps / TexturePacks::kGameId / "Objects" / "CacheTex.png";
    for (int i = 0; i < 2000 && !std::filesystem::exists(dumped); ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    TexturePackPixels pixels;
    expect(load_texture_pack_image(dumped, false, pixels, nullptr) && pixels.width == 32u && pixels.height == 32u,
           "dump writes the texture under its name");
    next_frame();
    gpu.run();
    cache.backend().set_upload_list(nullptr);
    cache.shutdown();
}

}  // namespace

// BLOCK4 blocks as ASTC 4x4: two blocks against bytes an independent decoder of the ASTC specification decodes to the
// record's colours (within the header's bound), and every block back to itself.
void test_block4_astc() {
    namespace astc = galaxy::render::block4_astc;
    const std::uint8_t four[8] = {0x00, 0xF8, 0x1F, 0x00, 0xE4, 0xE4, 0xE4, 0xE4};
    const std::uint8_t four_astc[16] = {0x43, 0x00, 0xFF, 0x01, 0x00, 0x00, 0x00, 0xFE,
                                        0x01, 0x00, 0x40, 0x39, 0x8D, 0x70, 0xA4, 0x70};
    const std::uint8_t three[8] = {0x1F, 0x00, 0x00, 0xF8, 0x24, 0x49, 0x92, 0x24};
    const std::uint8_t three_astc[16] = {0x51, 0x00, 0x01, 0xFE, 0x01, 0x00, 0xFE, 0x01,
                                         0x00, 0x00, 0x00, 0x00, 0x00, 0x63, 0xE4, 0x1E};
    std::uint8_t out[16];
    astc::from_block4(four, out);
    expect(std::memcmp(out, four_astc, 16) == 0, "a four-colour block as ASTC");
    astc::from_block4(three, out);
    expect(std::memcmp(out, three_astc, 16) == 0, "a three-colour block as ASTC");
    std::uint32_t state = 12345u;
    bool round_trip = true;
    for (unsigned n = 0; n < 20000u; ++n) {
        std::uint8_t block[8];
        for (auto& byte : block) {
            state = state * 1664525u + 1013904223u;
            byte = static_cast<std::uint8_t>(state >> 24);
        }
        if (n % 4u == 0u) {
            block[2] = block[0];
            block[3] = block[1];
        }
        // The record's blocks are opaque (texture.rs blocks_opaque): a three-colour block has no index 3.
        const unsigned c0 = block[0] | block[1] << 8, c1 = block[2] | block[3] << 8;
        if (c0 <= c1) {
            for (unsigned k = 4; k < 8u; ++k) {
                const unsigned threes = block[k] & block[k] >> 1 & 0x55u;
                block[k] = static_cast<std::uint8_t>(block[k] & ~threes);
            }
        }
        std::uint8_t back[8];
        astc::from_block4(block, out);
        astc::to_block4(out, back);
        round_trip = round_trip && std::memcmp(back, block, 8) == 0;
    }
    expect(round_trip, "every block back to itself");
}

// A category off leaves its textures out of a pack's index; the others stay.
void test_categories_off() {
    const std::filesystem::path root = galaxy::test_temp_path("nebula_pack_categories");
    std::error_code error;
    std::filesystem::remove_all(root, error);
    for (const char* folder : {"Mario", "Misc", "Other"}) std::filesystem::create_directories(root / folder, error);
    for (const char* name : {"Mario/Body.png", "Coin.png", "Misc/Thing.png", "Other/Unknown.png"}) {
        std::ofstream(root / name, std::ios::binary) << "x";
    }
    const std::vector<std::filesystem::path> packs{root};
    const auto count = [&packs](std::uint32_t off) {
        const std::vector<std::uint32_t> categories_off{off};
        return TexturePacks::build_index(packs, categories_off, 1u)->records.size();
    };
    expect(count(0u) == 3u, "every texture the game names with nothing off");
    expect(count(galaxy::textures::category_bit(Category::MarioLuigi)) == 2u, "a category off leaves its texture out");
    expect(count(galaxy::textures::category_bit(Category::MarioLuigi) | galaxy::textures::category_bit(Category::Items)) == 1u,
           "two categories off");
    expect(count(galaxy::textures::category_bit(Category::Other)) == 2u, "the game's Other textures off");
    std::filesystem::remove_all(root, error);
}

int main() {
    set_names();
    test_categories_off();
    const std::filesystem::path root = galaxy::test_temp_path("nebula-texture-pack-test");
    std::filesystem::remove_all(root);
    std::filesystem::create_directories(root);
    test_dds(root);
    test_png(root);
    test_sampler_shift();
    test_block4_astc();
    test_priority(root);
    Gpu gpu;
    if (!gpu.create()) {
        std::cerr << "FAILED: WARP device\n";
        ++failures;
    } else {
        try {
            test_packs(gpu, root);
            test_cache(gpu, root);
        } catch (const std::exception& error) {
            std::cerr << "FAILED: threw: " << error.what() << std::endl;
            ++failures;
        }
    }
    std::error_code error;
    std::filesystem::remove_all(root, error);
    if (failures == 0) {
        std::cout << "texture pack tests passed\n";
    }
    return failures == 0 ? 0 : 1;
}
