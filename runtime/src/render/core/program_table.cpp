// SPDX-License-Identifier: GPL-3.0-only
// The compiled programs' file, linked into the program. Written by tools/dev/compile_programs.py:
//   "NBPROG01", vertex rows u32, pixel rows u32, the interface (40 hexadecimal digits), padding to 64 bytes;
//   rows sorted by id within each stage: id u64, flags u32, coords u32 (a written vertex program's texture coordinate
//   inputs, 0 for others), then per variant offset u32 and size u32.
#include "galaxy/render/program_table.h"

#include "galaxy/render/mod_shaders.h"

#include <algorithm>
#include <cstring>

#if defined(_WIN32)
#include "galaxy/windows_api.h"
#else
__asm__(".section .rodata\n.balign 16\n.global nebula_programs_begin\nnebula_programs_begin:\n.incbin \"" GALAXY_PROGRAM_BLOB
        "\"\n.previous\n");
extern "C" const unsigned char nebula_programs_begin[];
#endif

namespace galaxy::render {
namespace {

constexpr std::uint32_t kReadsWorld = 1u;
constexpr std::uint32_t kLit = 2u;

struct Row {
    std::uint64_t id;
    std::uint32_t flags;
    std::uint32_t coords;
    struct {
        std::uint32_t offset, size;
    } variants[kProgramVariants];
};
static_assert(sizeof(Row) == 56u);

struct Blob {
    const unsigned char* bytes = nullptr;
    const Row* vertex = nullptr;
    const Row* pixel = nullptr;
    std::uint32_t vertex_count = 0u, pixel_count = 0u;
    char interface_text[41]{};
};

Blob open_blob() noexcept {
    Blob blob;
#if defined(_WIN32)
    const HMODULE module = GetModuleHandleW(nullptr);
    HRSRC resource = FindResourceW(module, MAKEINTRESOURCEW(101), MAKEINTRESOURCEW(10));
    if (resource == nullptr) {
        return blob;
    }
    blob.bytes = static_cast<const unsigned char*>(LockResource(LoadResource(module, resource)));
#else
    blob.bytes = nebula_programs_begin;
#endif
    if (blob.bytes == nullptr || std::memcmp(blob.bytes, "NBPROG01", 8u) != 0) {
        blob.bytes = nullptr;
        return blob;
    }
    std::memcpy(&blob.vertex_count, blob.bytes + 8u, 4u);
    std::memcpy(&blob.pixel_count, blob.bytes + 12u, 4u);
    std::memcpy(blob.interface_text, blob.bytes + 16u, 40u);
    blob.vertex = reinterpret_cast<const Row*>(blob.bytes + 64u);
    blob.pixel = blob.vertex + blob.vertex_count;
    return blob;
}

const Blob& blob() noexcept {
    static const Blob opened = open_blob();
    return opened;
}

const Row* find(std::uint64_t id, bool pixel) noexcept {
    const Blob& b = blob();
    const Row* first = pixel ? b.pixel : b.vertex;
    const Row* last = first + (pixel ? b.pixel_count : b.vertex_count);
    const Row* at = std::lower_bound(first, last, id, [](const Row& row, std::uint64_t value) { return row.id < value; });
    return at != last && at->id == id ? at : nullptr;
}

bool exists(std::uint64_t id, unsigned variant, bool pixel) noexcept {
    return program_bytes(id, variant, pixel).size != 0u;
}

}  // namespace

ProgramBytes program_bytes(std::uint64_t id, unsigned variant, bool pixel) noexcept {
    if (variant >= kProgramVariants) {
        return {};
    }
    if (!pixel) {
        switch (variant) {
        case kProgramReactive:
        case kProgramLayer:
        case kProgramSample:
            variant = kProgramBase;
            break;
        default:
            break;
        }
    }
    const Row* row = find(id, pixel);
    if (row == nullptr) {
        return mod_program_bytes(id, variant, pixel);
    }
    if (row->variants[variant].size == 0u) {
        return {};
    }
    return {blob().bytes + row->variants[variant].offset, row->variants[variant].size};
}

unsigned program_variant(std::uint64_t pixel, unsigned aux, unsigned samples_log2) noexcept {
    if (aux != 0u && aux != 4u) {
        return aux == 1u ? kProgramMotion : aux == 2u ? kProgramReactive : kProgramLayer;
    }
    return samples_log2 != 0u && exists(pixel, kProgramSample, true) ? kProgramSample : kProgramBase;
}

bool program_reads_world(std::uint64_t vertex) noexcept {
    const Row* row = find(vertex, false);
    return row == nullptr || (row->flags & kReadsWorld) != 0u;
}

std::uint32_t program_coords(std::uint64_t vertex) noexcept {
    const Row* row = find(vertex, false);
    return row != nullptr ? row->coords : 0u;
}

bool program_lit(std::uint64_t pixel) noexcept {
    const Row* row = find(pixel, true);
    return row != nullptr && (row->flags & kLit) != 0u;
}

std::uint32_t program_count(bool pixel) noexcept {
    return pixel ? blob().pixel_count : blob().vertex_count;
}

const char* program_file_name() noexcept {
#if defined(NEBULA_GPU_VULKAN)
    return "spirv.shaders";
#else
    return "dxil.shaders";
#endif
}

const char* program_interface() noexcept {
    return blob().interface_text;
}

}  // namespace galaxy::render
