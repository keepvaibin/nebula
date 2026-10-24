// SPDX-License-Identifier: GPL-3.0-only
// Texture packs: hashes, names, DDS layouts and decodes, PNG round trips, the index, loads, the budget and dumps, and
// the texture cache swapping a replacement in on a WARP device.

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif

#include "galaxy/gx/gx_backend.h"
#include "galaxy/gx/rt_materials.h"
#include "galaxy/gx/texture_cache.h"
#include "galaxy/gx/texture_pack.h"

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
using namespace galaxy::gx;

namespace {

int failures = 0;

void expect(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAILED: " << message << '\n';
        ++failures;
    }
}

std::uint64_t hash_of(const char* text) {
    return texture_pack_hash(reinterpret_cast<const std::uint8_t*>(text), std::strlen(text));
}

std::uint64_t hash_of(const std::uint8_t* data, std::size_t bytes) { return texture_pack_hash(data, bytes); }

void test_hash() {
    // Reference values: python-xxhash 3.x, xxh64(data, seed=0).
    expect(hash_of("") == 0xef46db3751d8e999ull, "xxh64 of the empty input");
    expect(hash_of("abc") == 0x44bc2cf5ad770999ull, "xxh64 of abc");
    expect(hash_of("Nobody inspects the spammish repetition") == 0xfbcea83c8a378bf1ull, "xxh64 of a 39-byte input");
    std::vector<std::uint8_t> bytes(768);
    for (std::size_t i = 0; i < bytes.size(); ++i) bytes[i] = static_cast<std::uint8_t>(i);
    expect(hash_of(bytes.data(), bytes.size()) == 0x8e03c838c596036full, "xxh64 of 768 bytes (stripes and tail)");
}

void test_names() {
    const char* valid[] = {
        "tex1_256x256_m_123c307fab917b99_1",
        "tex1_200x40_63f3408997969cd6_9a81ceb434acb873_8",
        "tex1_48x51_53403146169829a2_$_9",
        "tex1_36x36_4653193aaa14d048_14_mip2",
        "tex1_64x64_0123456789abcdef_14_arb",
        "tex1_128x128_m_02b5073505a48fb4_0_arb_mip1",
        "tex1_64x128_d4a37bf6901674f2_14v",
        "tex1_1024x8_ffffffffffffffff_0123456789abcdef_10",
    };
    for (const char* stem : valid) {
        const auto name = parse_texture_pack_name(stem);
        expect(name.has_value(), stem);
        if (name && std::string_view(stem).back() != 'v') expect(texture_pack_name(*name) == stem, "name round trip");
    }
    const auto slip = parse_texture_pack_name("tex1_64x128_d4a37bf6901674f2_14v");
    expect(slip && slip->format == 14u && texture_pack_name(*slip) == "tex1_64x128_d4a37bf6901674f2_14",
           "a letter after the format number is a slip");
    expect(!parse_texture_pack_name("tex1_128x128_cf175626cca47369_142").has_value(), "format 142 does not exist");
    const auto m = parse_texture_pack_name("tex1_256x256_m_123c307fab917b99_1");
    expect(m && m->width == 256u && m->height == 256u && m->mipmapped && m->format == 1u &&
               m->hash == 0x123c307fab917b99ull && m->tlut == 0u && !m->any_tlut,
           "fields of a mipmapped I8 name");
    const auto c4 = parse_texture_pack_name("tex1_200x40_63f3408997969cd6_9a81ceb434acb873_8");
    expect(c4 && c4->format == 8u && c4->tlut == 0x9a81ceb434acb873ull, "palette hash of a C4 name");
    const auto any = parse_texture_pack_name("tex1_48x51_53403146169829a2_$_9");
    expect(any && any->any_tlut && any->width == 48u && any->height == 51u, "palette wildcard");
    const auto mip = parse_texture_pack_name("tex1_36x36_4653193aaa14d048_14_mip2");
    expect(mip && mip->level == 2u && mip->format == 14u, "custom level suffix");
    const auto arb = parse_texture_pack_name("tex1_128x128_m_02b5073505a48fb4_0_arb_mip7");
    expect(arb && arb->arbitrary && arb->level == 7u && arb->mipmapped, "arbitrary level suffix");
    const char* invalid[] = {
        "tex1_64x64_0123456789abcde_14",                    // 15 hex digits
        "tex1_64x64_0123456789abcdef_7",                    // no GX format 7
        "tex1_64x64_0123456789abcdef_1234567890abcdef_14",  // palette hash on a direct format
        "efb1_n000007_160x120_6",                           // EFB copies are never replaced
        "tex1_0x64_0123456789abcdef_14",
        "tex1_64x64_0123456789abcdef_14_mip0",
        "tex1_64x64_0123456789abcdef_14_mip2_arb",
        "tex1_64x64_0123456789abcdef_14_extra",
        "tex1_64x64_m",
    };
    for (const char* stem : invalid) {
        expect(!parse_texture_pack_name(stem).has_value(), stem);
    }
}

void test_tlut_hash() {
    // TMEM palette: 256 big-endian entries.
    std::vector<std::uint8_t> palette(512);
    for (std::size_t i = 0; i < palette.size(); ++i) palette[i] = static_cast<std::uint8_t>(i * 7u + 3u);
    // C8 8x4 (one tile): indices 3..7.
    std::vector<std::uint8_t> c8(32, 5u);
    c8[0] = 3u;
    c8[31] = 7u;
    expect(texture_pack_tlut_hash(TexFormat::C8, c8.data(), 8u, 4u, palette.data()) == hash_of(palette.data() + 6u, 10u),
           "C8 palette hash covers indices 3..7");
    // C4 8x8 (one tile): nibbles 1..14.
    std::vector<std::uint8_t> c4(32, 0x55u);
    c4[3] = 0x1Eu;
    expect(texture_pack_tlut_hash(TexFormat::C4, c4.data(), 8u, 8u, palette.data()) == hash_of(palette.data() + 2u, 28u),
           "C4 palette hash covers nibbles 1..14");
    // C14X2 4x4 (one tile): big-endian indices, top two bits ignored.
    std::vector<std::uint8_t> c14(32, 0u);
    for (std::size_t i = 0; i < c14.size(); i += 2u) {
        c14[i] = 0xC0u;
        c14[i + 1u] = 0x10u;
    }
    c14[1] = 0x12u;
    std::vector<std::uint8_t> big(32768);
    for (std::size_t i = 0; i < big.size(); ++i) big[i] = static_cast<std::uint8_t>(i ^ (i >> 8u));
    expect(texture_pack_tlut_hash(TexFormat::C14X2, c14.data(), 4u, 4u, big.data()) ==
               hash_of(big.data() + 0x10u * 2u, 3u * 2u),
           "C14X2 palette hash covers indices 0x10..0x12");
    expect(texture_pack_tlut_hash(TexFormat::RGBA8, c8.data(), 4u, 4u, palette.data()) == 0u,
           "direct formats have no palette hash");
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
    TexMode mode{};
    mode.wrap_s = TexWrap::Repeat;
    mode.wrap_t = TexWrap::Repeat;
    mode.mag_filter = TexMagFilter::Linear;
    mode.min_filter = TexMinFilter::Linear;
    mode.min_lod_x16 = 0;
    mode.max_lod_x16 = 0;
    // A 4x replacement of a mip-less texture: levels 0..2 of the replacement (the guest's base size).
    const std::uint32_t key = pack_sampler_key(mode, false, 9u, 32u);
    expect(((key >> 5u) & 0x7u) == static_cast<std::uint32_t>(TexMinFilter::LinearMipLinear),
           "mip-less guest sampler gains a mip filter for a replacement");
    expect(((key >> 16u) & 0xFFu) == 0u && ((key >> 24u) & 0xFFu) == 32u, "LOD range 0..2 for a 4x replacement");
    // Authored mips: the guest range moves by the shift.
    mode.min_filter = TexMinFilter::LinearMipLinear;
    mode.min_lod_x16 = 8;
    mode.max_lod_x16 = 48;
    const std::uint32_t authored = pack_sampler_key(mode, false, 9u, 32u);
    expect(((authored >> 16u) & 0xFFu) == 40u && ((authored >> 24u) & 0xFFu) == 80u,
           "authored LOD range shifts by log2(scale)");
    expect(pack_sampler_key(mode, false, 4u, 0u) == pack_sampler_key(mode, false, 4u),
           "no shift leaves the key unchanged");
    // A guest range from its base level: close up the replacement's own base level shows.
    mode.min_lod_x16 = 0;
    const std::uint32_t from_base = pack_sampler_key(mode, false, 9u, 32u);
    expect(((from_base >> 16u) & 0xFFu) == 0u && ((from_base >> 24u) & 0xFFu) == 80u,
           "a guest range from level 0 keeps the replacement's level 0");
    // Arbitrary levels at 4x internal resolution with an 8x replacement: bias +2 - 3 = -1 LOD, whole chain, mip mode 3.
    mode.lod_bias_x32 = 0;
    const std::uint32_t arbitrary = pack_sampler_key(mode, false, 11u, 48u, true, 64u);
    expect(static_cast<std::int8_t>((arbitrary >> 8u) & 0xFFu) == -32 && ((arbitrary >> 16u) & 0xFFu) == 0u &&
               ((arbitrary >> 24u) & 0xFFu) == 0xFFu && ((arbitrary >> 5u) & 0x7u) == 7u,
           "arbitrary levels pair with the guest's by index");
    // Near filtering stays near.
    mode.min_filter = TexMinFilter::Near;
    mode.mag_filter = TexMagFilter::Near;
    const std::uint32_t nearest = pack_sampler_key(mode, false, 9u, 16u);
    expect(((nearest >> 5u) & 0x7u) == static_cast<std::uint32_t>(TexMinFilter::NearMipNear),
           "nearest guest sampler keeps nearest filtering");
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
    const std::filesystem::path game = root / "My Pack" / TexturePacks::kGameId / "sub";
    std::filesystem::create_directories(game);
    // A 32x32 RGBA8 guest texture replaced by a 128x128 PNG.
    std::vector<std::uint8_t> guest(32u * 32u * 4u, 0x5Au);
    TexturePackName png{};
    png.width = 32u;
    png.height = 32u;
    png.format = static_cast<std::uint8_t>(TexFormat::RGBA8);
    png.hash = hash_of(guest.data(), guest.size());
    std::vector<std::uint8_t> big(128u * 128u * 4u);
    for (std::size_t i = 0; i < big.size(); ++i) big[i] = static_cast<std::uint8_t>(i * 13u);
    expect(write_png(game / (texture_pack_name(png) + ".png"), 128u, 128u, big.data()), "pack PNG written");
    // Its custom level 1 (64x64, all 7s) beside it.
    TexturePackName level1 = png;
    level1.level = 1u;
    std::vector<std::uint8_t> sevens(64u * 64u * 4u, 7u);
    expect(write_png(game / (texture_pack_name(level1) + ".png"), 64u, 64u, sevens.data()), "custom level written");
    // The same texture as the game samples it with its own mipmaps (_m): another file, kept beside the first.
    TexturePackName png_m = png;
    png_m.mipmapped = true;
    expect(write_png(game / (texture_pack_name(png_m) + ".png"), 64u, 64u, sevens.data()), "_m variant written");
    // A 16x16 CMPR guest texture replaced by a 64x64 BC1 DDS with its 7 levels.
    TexturePackName dds{};
    dds.width = 16u;
    dds.height = 16u;
    dds.format = static_cast<std::uint8_t>(TexFormat::CMPR);
    dds.hash = 0x0123456789abcdefull;
    std::vector<std::uint8_t> bc1 = dds_header(64u, 64u, 7u, fourcc("DXT1"), 0u);
    bc1.resize(bc1.size() + 2048u + 512u + 128u + 32u + 8u + 8u + 8u, 0x11u);
    write_file(game / (texture_pack_name(dds) + ".dds"), bc1);
    // A PNG of the DDS's name in the same folder loses to the DDS.
    write_png(game / (texture_pack_name(dds) + ".png"), 64u, 64u, sevens.data());
    // Not a texture name: counted as a file, not indexed.
    { std::ofstream out(game / "readme.png"); out << "x"; }
    // A mod's textures/ folder without a game folder: indexed itself, after (winning over) the pack.
    const std::filesystem::path mod = root / "mod" / "textures";
    std::filesystem::create_directories(mod);
    TexturePackName mod_name{};
    mod_name.width = 8u;
    mod_name.height = 8u;
    mod_name.format = static_cast<std::uint8_t>(TexFormat::I8);
    mod_name.hash = 0x1111222233334444ull;
    std::vector<std::uint8_t> mod_pixels(16u * 16u * 4u, 0x22u);
    expect(write_png(mod / (texture_pack_name(mod_name) + ".png"), 16u, 16u, mod_pixels.data()), "mod PNG written");

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
    expect(packs.stats().names == 4u && packs.stats().files == 7u && packs.stats().folders == 2u &&
               packs.stats().levels == 1u,
           "four textures (one custom level) among seven files in two folders");
    expect(packs.may_match(32u, 32u, TexFormat::RGBA8) && !packs.may_match(32u, 32u, TexFormat::RGB565),
           "size and format prefilter");
    const std::uint32_t png_slot = packs.find(32u, 32u, TexFormat::RGBA8, png.hash, 0u);
    const std::uint32_t dds_slot = packs.find(16u, 16u, TexFormat::CMPR, dds.hash, 0u);
    const std::uint32_t mod_slot = packs.find(8u, 8u, TexFormat::I8, mod_name.hash, 0u);
    expect(png_slot != TexturePacks::kNone && dds_slot != TexturePacks::kNone && mod_slot != TexturePacks::kNone,
           "every name found");
    expect(packs.file(dds_slot).extension() == ".dds", "a DDS wins over a PNG of the same name");
    const std::uint32_t png_m_slot = packs.find(32u, 32u, TexFormat::RGBA8, png.hash, 0u, true);
    expect(png_m_slot != TexturePacks::kNone && png_m_slot != png_slot, "the _m variant is its own texture");
    expect(packs.find(8u, 8u, TexFormat::I8, mod_name.hash, 0u, true) == mod_slot,
           "a texture used with mipmaps finds the pack's file named without");
    expect(packs.find(32u, 32u, TexFormat::RGBA8, png.hash ^ 1u, 0u) == TexturePacks::kNone, "other hash misses");
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
    expect(a != nullptr && a->lod_shift_x16 == 32u && b != nullptr && b->lod_shift_x16 == 32u,
           "4x replacements shift the LOD by 2");
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

    // Dump: a 4x4 texture written once as a PNG under its name.
    settings.dump = true;
    packs.configure(settings);
    expect(packs.dumping(), "dump mode on");
    TexturePackName dumped{};
    dumped.width = 4u;
    dumped.height = 4u;
    dumped.format = static_cast<std::uint8_t>(TexFormat::IA8);
    dumped.hash = 0xfedcba9876543210ull;
    std::vector<std::uint8_t> pixels(64u);
    for (std::size_t i = 0; i < pixels.size(); ++i) pixels[i] = static_cast<std::uint8_t>(i * 4u);
    packs.dump(dumped, 4u, 4u, pixels.data());
    packs.dump(dumped, 4u, 4u, pixels.data());
    const std::filesystem::path file = settings.dumps / TexturePacks::kGameId / (texture_pack_name(dumped) + ".png");
    for (int i = 0; i < 2000 && packs.stats().dumped == 0u; ++i) std::this_thread::sleep_for(std::chrono::milliseconds(5));
    TexturePackPixels back;
    expect(packs.stats().dumped == 1u && load_texture_pack_image(file, false, back, nullptr) && back.width == 4u &&
               back.levels[0] == pixels,
           "dumped PNG holds the texture once");
    packs.begin_frame();
    expect(packs.pump(uploads) && !packs.pump(uploads), "toggling the dump asks for one look-up");

    // The game folder itself as the pack folder.
    settings.packs = {root / "My Pack" / TexturePacks::kGameId};
    packs.configure(settings);
    changed = false;
    expect(pump_until(packs, uploads, [&] { return changed && packs.stats().names == 3u; }, &changed),
           "a game folder given directly is indexed");

    // Off: the index goes and lookups stop.
    settings.enabled = false;
    packs.configure(settings);
    packs.begin_frame();
    expect(packs.pump(uploads) && !packs.enabled() &&
               packs.find(32u, 32u, TexFormat::RGBA8, png.hash, 0u) == TexturePacks::kNone,
           "turning packs off drops the index");
    packs.shutdown();
}

// Priority: packs side by side in the TexturePacks folder by name (a later one wins a texture), then each mod's
// textures/ folder in load order (later wins); textures only one pack names come from it whatever its place.
void test_priority(const std::filesystem::path& root) {
    TexturePackName shared{};
    shared.width = 16u;
    shared.height = 16u;
    shared.format = static_cast<std::uint8_t>(TexFormat::RGBA8);
    shared.hash = 0x5555aaaa5555aaaaull;
    TexturePackName only_a = shared;
    only_a.hash = 0x1234ull;
    std::vector<std::uint8_t> pixels(32u * 32u * 4u, 9u);
    const std::filesystem::path packs = root / "priority" / "TexturePacks";
    for (const char* pack : {"A HD pack", "B buttons"}) {
        std::filesystem::create_directories(packs / pack / "RMGE01");
        write_png(packs / pack / "RMGE01" / (texture_pack_name(shared) + ".png"), 32u, 32u, pixels.data());
    }
    write_png(packs / "A HD pack" / "RMGE01" / (texture_pack_name(only_a) + ".png"), 32u, 32u, pixels.data());
    const std::vector<std::filesystem::path> folders{packs};
    const auto index = TexturePacks::build_index(folders, 1u);
    const auto file_of = [&](const std::shared_ptr<TexturePacks::Index>& in, std::uint64_t hash) {
        for (const auto& record : in->records) {
            if (record.name.hash == hash) return record.file;
        }
        return std::filesystem::path{};
    };
    expect(file_of(index, shared.hash).parent_path().parent_path().filename() == "B buttons",
           "packs side by side: the later by name wins a texture");
    expect(file_of(index, only_a.hash).parent_path().parent_path().filename() == "A HD pack",
           "a texture one pack names comes from it");
    const std::filesystem::path mod = root / "priority" / "mods" / "Switch prompts" / "textures";
    std::filesystem::create_directories(mod);
    write_png(mod / (texture_pack_name(shared) + ".png"), 32u, 32u, pixels.data());
    const std::vector<std::filesystem::path> with_mod{packs, mod};
    expect(file_of(TexturePacks::build_index(with_mod, 2u), shared.hash).parent_path() == mod,
           "a mod's textures win over the TexturePacks folder");
}

// The texture cache with a pack: the original shows first, the replacement takes its place at a frame begin, binds
// without a decode once resident, and turning packs off brings the original back. Dump mode writes the original under
// the name the pack uses.
void test_cache(Gpu& gpu, const std::filesystem::path& root) {
    std::filesystem::create_directories(root / "cache" / TexturePacks::kGameId);
    // A 32x32 RGBA8 guest texture: 16 tiles of 64 bytes.
    constexpr std::uint32_t kAddress = 0x80100000u;
    std::vector<std::byte> guest(4096u);
    for (std::size_t i = 0; i < guest.size(); ++i) guest[i] = static_cast<std::byte>(i * 31u + 7u);
    galaxy::GuestMemoryRegion region{kAddress, static_cast<std::uint32_t>(guest.size()), guest.data()};
    galaxy::GuestMemory memory{};
    memory.regions = &region;
    memory.region_count = 1u;
    TexImage image{};
    image.guest_addr = kAddress;
    image.width = 32u;
    image.height = 32u;
    image.format = TexFormat::RGBA8;
    TexMode mode{};
    mode.wrap_s = TexWrap::Repeat;
    mode.wrap_t = TexWrap::Repeat;
    mode.mag_filter = TexMagFilter::Linear;
    mode.min_filter = TexMinFilter::Linear;
    TexturePackName name{};
    name.width = 32u;
    name.height = 32u;
    name.format = static_cast<std::uint8_t>(TexFormat::RGBA8);
    name.hash = hash_of(reinterpret_cast<const std::uint8_t*>(guest.data()), guest.size());
    std::vector<std::uint8_t> big(96u * 96u * 4u, 0x80u);
    expect(write_png(root / "cache" / TexturePacks::kGameId / (texture_pack_name(name) + ".png"), 96u, 96u, big.data()),
           "cache test PNG written");

    // Large (TMEM model, descriptor tables): on the heap.
    const auto owner = std::make_unique<TextureCache>();
    TextureCache& cache = *owner;
    expect(cache.initialize(gpu.device.Get()), "cache initializes");
    unsigned frame = 0;
    const auto next_frame = [&] {
        gpu.run();
        cache.begin_frame(frame++ % 2u, 2u);
        cache.backend().set_upload_list(gpu.list.Get());
        cache.apply_texture_packs();
    };
    cache.begin_frame(frame++ % 2u, 2u);
    cache.backend().set_upload_list(gpu.list.Get());
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
    const TextureHandle original = cache.get(image, mode, TlutRef{}, &memory);
    expect(original.resource != nullptr && original.resource->GetDesc().Width == 32u && original.lod_shift_x16 == 0u,
           "the original shows while its replacement loads");
    TextureHandle replaced{};
    for (int i = 0; i < 400; ++i) {
        next_frame();
        replaced = cache.get(image, mode, TlutRef{}, &memory);
        if (replaced.resource != nullptr && replaced.resource->GetDesc().Width == 96u) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    expect(replaced.resource != nullptr && replaced.resource->GetDesc().Width == 96u && replaced.width == 32u &&
               replaced.height == 32u && replaced.mip_levels == 7u && replaced.lod_shift_x16 == 25u,
           "the replacement takes the original's place (guest size kept, 3x shift)");
    cache.invalidate_all();
    next_frame();
    const TextureHandle bound = cache.get(image, mode, TlutRef{}, &memory);
    expect(bound.resource == replaced.resource, "a resident replacement binds again without a decode");
    next_frame();
    expect(gpu.device->GetDeviceRemovedReason() == S_OK, "device alive with the replacement");

    // Packs off: the original decodes again; dump mode writes it under its pack name.
    settings.enabled = false;
    settings.dump = true;
    cache.configure_texture_packs(settings);
    next_frame();
    const TextureHandle back = cache.get(image, mode, TlutRef{}, &memory);
    expect(back.resource != nullptr && back.resource->GetDesc().Width == 32u, "packs off: the original again");
    const std::filesystem::path dumped =
        settings.dumps / TexturePacks::kGameId / (texture_pack_name(name) + ".png");
    for (int i = 0; i < 2000 && !std::filesystem::exists(dumped); ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    TexturePackPixels pixels;
    expect(load_texture_pack_image(dumped, false, pixels, nullptr) && pixels.width == 32u && pixels.height == 32u,
           "dump writes the texture under its pack name");
    next_frame();
    gpu.run();
    cache.backend().set_upload_list(nullptr);
    cache.shutdown();
}

}  // namespace

int main() {
    const std::filesystem::path root =
        std::filesystem::temp_directory_path() / ("nebula-texture-pack-test-" + std::to_string(GetCurrentProcessId()));
    std::filesystem::remove_all(root);
    std::filesystem::create_directories(root);
    test_hash();
    test_names();
    test_tlut_hash();
    test_dds(root);
    test_png(root);
    test_sampler_shift();
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
