// SPDX-License-Identifier: GPL-3.0-only
// The compiled programs' file, linked into the program. Written by tools/dev/compile_programs.py:
//   "NBPROG03", vertex rows u32, pixel rows u32, the interface (40 hexadecimal digits), padding to 64 bytes;
//   rows sorted by id within each stage: id u64, flags u32, coords u32 (a written vertex program's texture coordinate
//   inputs, 0 for others), then per stored variant (kProgramStored: the variants, a pixel program's bound-table
//   forms, then its ray tracing forms) offset u32 and size u32.
#include "galaxy/render/program_table.h"

#include "galaxy/render/mod_shaders.h"

#include <algorithm>
#include <atomic>
#include <cstring>

#if defined(_WIN32)
#include "galaxy/windows_api.h"
#elif defined(__APPLE__)
__asm__(".section __TEXT,__const\n.balign 16\n.globl _nebula_programs_begin\n.private_extern _nebula_programs_begin\n"
        "_nebula_programs_begin:\n.incbin \"" GALAXY_PROGRAM_BLOB "\"\n.text\n");
extern "C" const unsigned char nebula_programs_begin[];
#else
__asm__(".section .rodata\n.balign 16\n.global nebula_programs_begin\nnebula_programs_begin:\n.incbin \"" GALAXY_PROGRAM_BLOB
        "\"\n.previous\n");
extern "C" const unsigned char nebula_programs_begin[];
#endif

namespace galaxy::render {
namespace {

constexpr std::uint32_t kReadsWorld = 1u;
constexpr std::uint32_t kLit = 2u;
constexpr std::uint32_t kSumsKey = 4u;

struct Row {
    std::uint64_t id;
    std::uint32_t flags;
    std::uint32_t coords;
    struct {
        std::uint32_t offset, size;
    } variants[kProgramStored];
};
static_assert(sizeof(Row) == 16u + 8u * kProgramStored);

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
    if (blob.bytes == nullptr || std::memcmp(blob.bytes, "NBPROG03", 8u) != 0) {
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

std::atomic<bool> g_bound_tables{false};
std::atomic<bool> g_triangle_ids{true};

bool exists(std::uint64_t id, unsigned variant, bool pixel) noexcept {
    return program_bytes(id, variant, pixel).size != 0u;
}

}  // namespace

void program_use_bound_tables(bool bound) noexcept {
    g_bound_tables.store(bound, std::memory_order_relaxed);
}

bool program_bound_tables() noexcept {
    return g_bound_tables.load(std::memory_order_relaxed);
}

void program_use_triangle_ids(bool ids) noexcept {
    g_triangle_ids.store(ids, std::memory_order_relaxed);
}

ProgramBytes program_bytes(std::uint64_t id, unsigned variant, bool pixel) noexcept {
    const bool traced = variant >= kProgramPrepass && variant < kProgramStored;
    if (variant >= kProgramVariants && !traced) {
        return {};
    }
    // A traced form's vertex stage: a written lit material's own (its light varyings) where it has one, for its
    // prepass too: the prepass writes the depth the scene pass then tests, so both run one vertex stage.
    bool own_form = false;
    if (!pixel && traced) {
        const unsigned own = variant == kProgramTracedMotion ? unsigned{kProgramTracedMotion} : unsigned{kProgramTraced};
        const Row* const row = find(id, false);
        if (row != nullptr && row->variants[own].size != 0u) {
            variant = own;
            own_form = true;
        }
    }
    if (!pixel && !own_form) {
        switch (variant) {
        case kProgramReactive:
        case kProgramLayer:
        case kProgramSample:
        case kProgramPrepass:
        case kProgramPrepassItem:
        case kProgramTraced:
        case kProgramTracedReactive:
        case kProgramTracedLayer:
            variant = kProgramBase;
            break;
        case kProgramTracedMotion:
            variant = kProgramMotion;
            break;
        default:
            break;
        }
    }
    // A vertex stage reads no texture: one form serves both. The tracer's forms are bindless only.
    if (pixel && g_bound_tables.load(std::memory_order_relaxed)) {
        if (traced) {
            return {};
        }
        variant += kProgramBoundOffset;
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
    if (aux == kAuxPrepass || aux == kAuxPrepassMotion) {
        return g_triangle_ids.load(std::memory_order_relaxed) ? kProgramPrepass : kProgramPrepassItem;
    }
    if (aux >= kAuxTraced) {
        const unsigned targets = aux & 7u;
        return targets == 1u   ? kProgramTracedMotion
               : targets == 2u ? kProgramTracedReactive
               : targets == 3u ? kProgramTracedLayer
                               : kProgramTraced;
    }
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

bool program_sums_key(std::uint64_t pixel) noexcept {
    const Row* row = find(pixel, true);
    return row != nullptr && (row->flags & kSumsKey) != 0u;
}

bool program_traced(std::uint64_t pixel) noexcept {
    return exists(pixel, kProgramTraced, true) &&
           (g_triangle_ids.load(std::memory_order_relaxed) || exists(pixel, kProgramPrepassItem, true));
}

const char* program_file_name() noexcept {
#if defined(NEBULA_GPU_VULKAN)
    return "spirv.shaders";
#elif defined(NEBULA_GPU_METAL)
    return "msl.shaders";
#elif defined(NEBULA_GPU_D3D11)
    return "dxbc.shaders";
#else
    return "dxil.shaders";
#endif
}

const char* program_interface() noexcept {
    return blob().interface_text;
}

}  // namespace galaxy::render
