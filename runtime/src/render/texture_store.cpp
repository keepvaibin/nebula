// Textures by id: levels staged on the calling thread, created and uploaded on the render thread.

#include "galaxy/windows_api.h"

#include "galaxy/render/texture_store.h"

#include "galaxy/loss_report.h"
#include "galaxy/render/render_config.h"
#include "galaxy/render/rt_materials.h"
#include "nebula_cooked_texture.h"
#include "nebula_game_api.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <utility>

namespace galaxy::render {

namespace {

constexpr std::uint64_t kStagingChunkBytes = 16ull << 20u;
constexpr std::size_t kFreeChunksKept = 4u;

std::uint64_t align_up(std::uint64_t value, std::uint64_t alignment) {
    return (value + alignment - 1u) & ~(alignment - 1u);
}

}  // namespace

void detail::expand_record_level(std::uint8_t layout, const std::uint8_t* src, std::uint32_t width, std::uint32_t height,
                                 RGBA8* out) {
    const std::size_t count = static_cast<std::size_t>(width) * height;
    switch (layout) {
    case NEBULA_TEXTURE_L8:
        for (std::size_t i = 0; i < count; ++i) {
            out[i] = RGBA8{src[i], src[i], src[i], src[i]};
        }
        break;
    case NEBULA_TEXTURE_LA8:
        for (std::size_t i = 0; i < count; ++i) {
            out[i] = RGBA8{src[i * 2u], src[i * 2u], src[i * 2u], src[i * 2u + 1u]};
        }
        break;
    case NEBULA_TEXTURE_BLOCK4: {
        const auto widen = [](std::uint16_t v) {
            const auto r = static_cast<std::uint8_t>((v >> 11) & 0x1Fu);
            const auto g = static_cast<std::uint8_t>((v >> 5) & 0x3Fu);
            const auto b = static_cast<std::uint8_t>(v & 0x1Fu);
            return RGBA8{static_cast<std::uint8_t>((r << 3) | (r >> 2)), static_cast<std::uint8_t>((g << 2) | (g >> 4)),
                         static_cast<std::uint8_t>((b << 3) | (b >> 2)), 0xFFu};
        };
        const std::uint32_t blocks_x = (width + 3u) / 4u;
        for (std::uint32_t by = 0; by < (height + 3u) / 4u; ++by) {
            for (std::uint32_t bx = 0; bx < blocks_x; ++bx) {
                const std::uint8_t* block = src + (static_cast<std::size_t>(by) * blocks_x + bx) * 8u;
                const auto c0 = static_cast<std::uint16_t>(block[0] | block[1] << 8);
                const auto c1 = static_cast<std::uint16_t>(block[2] | block[3] << 8);
                const RGBA8 a = widen(c0);
                const RGBA8 b = widen(c1);
                RGBA8 entries[4] = {a, b, {}, {}};
                if (c0 > c1) {
                    entries[2] = {block4_interpolated_channel(a.r, b.r), block4_interpolated_channel(a.g, b.g),
                                  block4_interpolated_channel(a.b, b.b), 0xFFu};
                    entries[3] = {block4_interpolated_channel(b.r, a.r), block4_interpolated_channel(b.g, a.g),
                                  block4_interpolated_channel(b.b, a.b), 0xFFu};
                } else {
                    entries[2] = {static_cast<std::uint8_t>((a.r + b.r) / 2u), static_cast<std::uint8_t>((a.g + b.g) / 2u),
                                  static_cast<std::uint8_t>((a.b + b.b) / 2u), 0xFFu};
                    entries[3] = entries[2];
                    entries[3].a = 0u;
                }
                const std::uint32_t indices = static_cast<std::uint32_t>(block[4]) | block[5] << 8 | block[6] << 16 |
                                              static_cast<std::uint32_t>(block[7]) << 24;
                for (std::uint32_t y = 0; y < 4u && by * 4u + y < height; ++y) {
                    for (std::uint32_t x = 0; x < 4u && bx * 4u + x < width; ++x) {
                        out[static_cast<std::size_t>(by * 4u + y) * width + bx * 4u + x] =
                            entries[(indices >> (2u * (y * 4u + x))) & 3u];
                    }
                }
            }
        }
        break;
    }
    default:
        std::memcpy(out, src, count * sizeof(RGBA8));
        break;
    }
}

struct TextureStore::Ops {
    std::vector<Op> list;
};

bool TextureStore::samples_generated(const TextureSampler& mode, const TextureHandle& handle) {
    // Only repeating textures read a generated chain: a clamped texture is a decal placed by a texture matrix (eyes,
    // HUD digits) whose small detail blended lower levels would wash out.
    if (!handle.generated_mips || min_filter_code(mode) != 4u || !mode.mag_linear || mode.wrap_s == TexWrap::Clamp ||
        mode.wrap_t == TexWrap::Clamp || (handle.width < 64u && handle.height < 64u)) {
        return false;
    }
    return get_render_config().enhanced_mipmaps;
}

bool TextureStore::initialize(gpu::Device* device, DeviceGate& gate) {
    gate_ = &gate;
    next_srv_index_ = 0u;
    free_srv_indices_.clear();
    queued_ = std::make_shared<Ops>();
    return backend_.initialize(device, kViewPageSize);
}

void TextureStore::shutdown() {
    packs_.shutdown();
    pack_waiters_.clear();
    pack_uploads_.clear();
    for (auto& slot : pack_staging_) {
        for (TextureBackend::UploadChunk& chunk : slot) TextureBackend::release_upload_chunk(chunk);
    }
    pack_staging_.clear();
    movie_staging_.clear();
    movie_ring_ = {};
    movie_active_ = false;
    {
        const auto heap = backend_.heap_stats();
        std::cerr << "[texture-heap] heaps=" << heap.heaps << " heap-bytes=" << heap.heap_bytes
                  << " used-bytes=" << heap.used_bytes << " placed=" << heap.placed
                  << " committed-fallbacks=" << heap.committed_fallbacks << '\n';
    }
    entries_.clear();
    retired_.clear();
    kept_.clear();
    {
        const std::lock_guard guard(lock_);
        queued_ = std::make_shared<Ops>();
        arena_chunk_.reset();
        free_chunks_.clear();
    }
    backend_.shutdown();
    next_srv_index_ = 0u;
    free_srv_indices_.clear();
}

void TextureStore::restart(gpu::Device* device) {
    if (!initialize(device, *gate_)) {
        throw std::runtime_error("[TextureStore] no textures on the new device");
    }
    for (const auto& [id, source] : sources_) {
        Op op;
        op.kind = Op::Kind::Create;
        op.id = id;
        if (stage_image(op, source.bytes, source.size, source.width, source.height, source.format)) {
            const std::lock_guard guard(lock_);
            queued_->list.push_back(std::move(op));
        }
    }
    for (const auto& [id, record] : built_ins_) {
        queue_built_in(id, record);
    }
}

TextureStore::ChunkRef TextureStore::allocate_staging(std::uint64_t bytes, std::uint64_t& offset) {
    const auto make = [this](std::uint64_t size) {
        auto* chunk = new TextureBackend::UploadChunk(backend_.create_upload_chunk(size));
        return ChunkRef(chunk, [](TextureBackend::UploadChunk* gone) {
            TextureBackend::release_upload_chunk(*gone);
            delete gone;
        });
    };
    if (bytes > kStagingChunkBytes) {
        offset = 0u;
        return make(bytes);
    }
    const std::lock_guard guard(lock_);
    arena_used_ = align_up(arena_used_, TextureBackend::kPlacementAlignment);
    if (arena_chunk_ == nullptr || arena_used_ + bytes > kStagingChunkBytes) {
        if (!free_chunks_.empty()) {
            arena_chunk_ = std::move(free_chunks_.back());
            free_chunks_.pop_back();
        } else {
            arena_chunk_ = make(kStagingChunkBytes);
        }
        arena_used_ = 0u;
    }
    offset = arena_used_;
    arena_used_ += bytes;
    return arena_chunk_;
}

TextureStore::Staged TextureStore::stage(const std::uint8_t* texels, std::uint32_t width, std::uint32_t height,
                                         std::uint32_t levels, std::uint8_t layout) {
    Staged staged;
    staged.width = width;
    staged.height = height;
    staged.levels = levels;
    staged.layout = layout;
    switch (layout) {
    case NEBULA_TEXTURE_L8: staged.format = TexelFormat::R8; break;
    case NEBULA_TEXTURE_LA8: staged.format = TexelFormat::Rg8; break;
    case NEBULA_TEXTURE_BLOCK4: staged.format = TextureBackend::supports_bc1() ? TexelFormat::Bc1 : TexelFormat::Rgba8; break;
    default: staged.format = TexelFormat::Rgba8; break;
    }
    const bool expand = staged.format == TexelFormat::Rgba8 && layout != NEBULA_TEXTURE_RGBA8;
    std::uint64_t bytes = 0u;
    for (std::uint32_t l = 0; l < levels; ++l) {
        const std::uint32_t lw = std::max(1u, width >> l);
        const std::uint32_t lh = std::max(1u, height >> l);
        bytes = align_up(bytes, TextureBackend::kPlacementAlignment);
        staged.offsets[l] = bytes;
        staged.pitches[l] = static_cast<std::uint32_t>(align_up(texel_row_bytes(staged.format, lw), TextureBackend::kPitchAlignment));
        bytes += static_cast<std::uint64_t>(staged.pitches[l]) * texel_rows(staged.format, lh);
    }
    staged.chunk = allocate_staging(bytes, staged.offset);
    std::uint8_t* const base = staged.chunk->cpu_base + staged.offset;
    std::vector<RGBA8> expanded;
    const std::uint8_t* source = texels;
    for (std::uint32_t l = 0; l < levels; ++l) {
        const std::uint32_t lw = std::max(1u, width >> l);
        const std::uint32_t lh = std::max(1u, height >> l);
        std::uint8_t* const out = base + staged.offsets[l];
        if (expand) {
            expanded.resize(std::size_t{lw} * lh);
            detail::expand_record_level(layout, source, lw, lh, expanded.data());
            for (std::uint32_t y = 0; y < lh; ++y) {
                std::memcpy(out + std::size_t{y} * staged.pitches[l], expanded.data() + std::size_t{y} * lw, std::size_t{lw} * 4u);
            }
        } else {
            const std::uint32_t row = texel_row_bytes(staged.format, lw);
            const std::uint32_t rows = texel_rows(staged.format, lh);
            for (std::uint32_t y = 0; y < rows; ++y) {
                std::memcpy(out + std::size_t{y} * staged.pitches[l], source + std::size_t{y} * row, row);
            }
        }
        source += nebula_texture_level_bytes(lw, lh, 1u, layout);
    }
    return staged;
}

// An image as the game hands it over, staged and named.
bool TextureStore::stage_image(Op& op, const void* bytes, std::uint32_t size, std::uint32_t width, std::uint32_t height,
                               std::uint8_t format) {
    const auto* first = static_cast<const std::uint8_t*>(bytes);
    if (format >= NEBULA_TEXTURE_LINEAR_L8 && format <= NEBULA_TEXTURE_LINEAR_RGBA8) {
        const std::uint8_t layout = nebula_texture_linear_layout(format);
        if (first == nullptr || width == 0u || height == 0u || size < nebula_texture_level_bytes(width, height, 1u, layout)) {
            return false;
        }
        op.staged = stage(first, width, height, 1u, layout);
        op.name.format = format;
        op.name.hash = texture_pack_hash(first, size);
        op.name.named = true;
        return true;
    }
    const NebulaTexture* const record = nebula_texture_record(bytes);
    if (record == nullptr || record->levels == 0u || record->levels > NEBULA_TEXTURE_MAX_LEVELS ||
        NEBULA_TEXTURE_HEADER + nebula_texture_level_bytes(record->width, record->height, record->levels, record->layout) > size) {
        return false;
    }
    const std::uint8_t* const texels = first + NEBULA_TEXTURE_HEADER;
    op.staged = stage(texels, record->width, record->height, record->levels, record->layout);
    op.generated = (record->flags & NEBULA_TEXTURE_GENERATED_LEVELS) != 0u;
    if (record->disc_format != NEBULA_TEXTURE_MADE) {
        op.name.format = record->disc_format;
        op.name.hash = record->pack_hash;
        op.name.palette = record->pack_palette_hash;
        op.name.mipmapped = record->levels > 1u && !op.generated;
        op.name.named = true;
    } else if (record->layout != NEBULA_TEXTURE_BLOCK4) {
        // A record the game made is named by its own texels.
        op.name.format = nebula_texture_linear_code(record->layout);
        op.name.hash = texture_pack_hash(texels, nebula_texture_level_bytes(record->width, record->height, 1u, record->layout));
        op.name.named = true;
    }
    return true;
}

std::uint32_t TextureStore::register_image(const NebulaTextureImage& image) {
    return gate_->use([&] {
        Op op;
        op.kind = Op::Kind::Create;
        const bool staged = stage_image(op, image.bytes, image.size, image.width, image.height, image.format);
        const std::lock_guard guard(lock_);
        const std::uint32_t id = next_image_++;
        if (staged) {
            op.id = id;
            queued_->list.push_back(std::move(op));
            sources_[id] = Source{image.bytes, image.size, image.width, image.height, image.format};
            if (image.format != NEBULA_TEXTURE_IMAGE) {
                linear_images_[id] = LinearImage{image.width, image.height, image.format};
            }
        }
        return id;
    });
}

void TextureStore::update_image(std::uint32_t id, const void* bytes, std::uint32_t size) {
    if (id == 0u || is_target(id)) {
        return;
    }
    gate_->use([&] {
        Op op;
        op.kind = Op::Kind::Update;
        op.id = id;
        LinearImage linear;
        {
            const std::lock_guard guard(lock_);
            const auto it = linear_images_.find(id);
            if (it != linear_images_.end()) {
                linear = it->second;
            }
        }
        if (!stage_image(op, bytes, size, linear.width, linear.height, linear.format != 0u ? linear.format : NEBULA_TEXTURE_IMAGE)) {
            return;
        }
        const std::lock_guard guard(lock_);
        queued_->list.push_back(std::move(op));
        if (const auto source = sources_.find(id); source != sources_.end()) {
            source->second.bytes = bytes;
            source->second.size = size;
        }
    });
}

void TextureStore::free_image(std::uint32_t id) {
    if (id == 0u) {
        return;
    }
    Op op;
    op.kind = is_target(id) ? Op::Kind::FreeTarget : Op::Kind::Free;
    op.id = id;
    const auto hold = gate_->hold();
    const std::lock_guard guard(lock_);
    linear_images_.erase(id);
    sources_.erase(id);
    built_ins_.erase(id);
    queued_->list.push_back(std::move(op));
}

std::uint32_t TextureStore::new_target() {
    const std::lock_guard guard(lock_);
    return kTargetBit | next_target_++;
}

void TextureStore::set_built_in(std::uint32_t id, const void* record_bytes) {
    if (id == 0u || is_target(id)) {
        return;
    }
    gate_->use([&] { queue_built_in(id, record_bytes); });
}

void TextureStore::queue_built_in(std::uint32_t id, const void* record_bytes) {
    Op op;
    op.kind = Op::Kind::BuiltIn;
    op.id = id;
    if (const NebulaTexture* const record = nebula_texture_record(record_bytes)) {
        if (record->layout != NEBULA_TEXTURE_RGBA8 || record->levels == 0u || record->levels > NEBULA_TEXTURE_MAX_LEVELS ||
            NEBULA_TEXTURE_HEADER + nebula_texture_level_bytes(record->width, record->height, record->levels, record->layout) > record->size) {
            return;
        }
        op.staged = stage(reinterpret_cast<const std::uint8_t*>(record) + NEBULA_TEXTURE_HEADER, record->width, record->height,
                          record->levels, record->layout);
    }
    const std::lock_guard guard(lock_);
    if (record_bytes != nullptr) {
        built_ins_[id] = record_bytes;
    } else {
        built_ins_.erase(id);
    }
    queued_->list.push_back(std::move(op));
}

std::shared_ptr<TextureStore::Ops> TextureStore::take_ops() {
    const std::lock_guard guard(lock_);
    if (queued_->list.empty()) {
        return nullptr;
    }
    return std::exchange(queued_, std::make_shared<Ops>());
}

void TextureStore::begin_frame(unsigned frame_slot, unsigned frames_in_flight) {
    if (frames_in_flight == 0u || frame_slot >= frames_in_flight) {
        throw std::runtime_error("[TextureStore] invalid frame slot");
    }
    if (retired_.size() != frames_in_flight) {
        retired_.resize(frames_in_flight);
        kept_.resize(frames_in_flight);
        pack_staging_.resize(frames_in_flight);
        movie_staging_.resize(frames_in_flight);
    }
    // The renderer has waited for this slot's fence: what it retired and read is idle.
    for (Retired& retired : retired_[frame_slot]) {
        if (retired.view != ~0u) {
            backend_.release_view(retired.view);
            free_srv_indices_.push_back(retired.view);
        }
    }
    retired_[frame_slot].clear();
    frame_slot_ = frame_slot;
    for (ChunkRef& chunk : kept_[frame_slot]) {
        if (chunk.use_count() == 1 && chunk->size == kStagingChunkBytes) {
            const std::lock_guard guard(lock_);
            if (free_chunks_.size() < kFreeChunksKept) {
                free_chunks_.push_back(std::move(chunk));
            }
        }
    }
    kept_[frame_slot].clear();
    packs_.retire_uploads(std::move(pack_staging_[frame_slot]));
    pack_staging_[frame_slot].clear();
    movie_staging_[frame_slot].clear();
    packs_.begin_frame();
}

void TextureStore::keep(ChunkRef chunk) {
    std::vector<ChunkRef>& kept = kept_[frame_slot_];
    if (kept.empty() || kept.back() != chunk) {
        kept.push_back(std::move(chunk));
    }
}

// The last list has walked for good: what it freed goes now.
void TextureStore::release_held(std::vector<std::uint32_t>& freed) {
    for (const std::uint32_t id : held_frees_) {
        if (!is_target(id)) {
            free_entry(id);
        }
        freed.push_back(id);
    }
    held_frees_.clear();
}

void TextureStore::apply_ops(Ops& ops, std::vector<std::uint32_t>& freed) {
    if (!backend_.has_upload_list()) {
        throw std::runtime_error("[TextureStore] ops without an upload list");
    }
    release_held(freed);
    for (Op& op : ops.list) {
        switch (op.kind) {
        case Op::Kind::Create: create_entry(op); break;
        case Op::Kind::Update: update_entry(op); break;
        case Op::Kind::Free:
        case Op::Kind::FreeTarget: held_frees_.push_back(op.id); break;
        case Op::Kind::BuiltIn: apply_built_in(op); break;
        }
        op.staged.chunk.reset();
    }
    ops.list.clear();
}

void TextureStore::apply_built_in(Op& op) {
    Entry* const entry = op.id < entries_.size() ? entries_[op.id].get() : nullptr;
    if (entry == nullptr) {
        return;
    }
    if (entry->built_in.texture) {
        retire(std::move(entry->built_in.texture), ~0u);
    }
    entry->built_in = {};
    if (op.staged.chunk != nullptr) {
        try {
            std::uint64_t bytes = 0u;
            gpu::TextureRef texture = backend_.create_texture(op.staged.width, op.staged.height, op.staged.levels, bytes);
            if (texture) {
                backend_.record_upload(gpu::raw(texture), false, *op.staged.chunk, op.staged.offset, op.staged.width, op.staged.height,
                                       op.staged.levels, op.staged.offsets.data(), op.staged.pitches.data());
                keep(op.staged.chunk);
                entry->built_in.texture = std::move(texture);
                entry->built_in.width = op.staged.width;
                entry->built_in.height = op.staged.height;
                entry->built_in.levels = op.staged.levels;
                entry->built_in.bytes = bytes;
                entry->built_in.lod_shift = static_cast<float>(std::max(0.0, std::log2(static_cast<double>(op.staged.width) / static_cast<double>(entry->own.width))));
            }
        } catch (const std::exception& error) {
            // Out of video memory: the original stays.
            std::cerr << "[texture] built-in replacement upload failed: " << error.what() << '\n';
            entry->built_in = {};
        }
    }
    look_up(op.id, *entry);
}

void TextureStore::make_own(Entry& entry, const Op& op) {
    std::uint64_t allocation = 0u;
    entry.texture = backend_.create_texture(op.staged.width, op.staged.height, op.staged.levels, allocation, op.staged.format);
    backend_.record_upload(gpu::raw(entry.texture), false, *op.staged.chunk, op.staged.offset, op.staged.width, op.staged.height,
                           op.staged.levels, op.staged.offsets.data(), op.staged.pitches.data());
    keep(op.staged.chunk);
    TextureHandle& own = entry.own;
    own.resource = gpu::raw(entry.texture);
    own.srv_index = allocate_srv_index();
    own.width = static_cast<std::uint32_t>(op.staged.width);
    own.height = static_cast<std::uint32_t>(op.staged.height);
    own.mip_levels = static_cast<std::uint8_t>(op.staged.levels);
    own.generated_mips = op.generated;
    backend_.write_view(own.srv_index, gpu::raw(entry.texture), op.staged.levels);
}

void TextureStore::create_entry(Op& op) {
    if (entries_.size() <= op.id) {
        entries_.resize(std::size_t{op.id} + 1u);
    }
    auto entry = std::make_unique<Entry>();
    make_own(*entry, op);
    entry->current = entry->own;
    entry->name = op.name;
    if (op.name.named) {
        ++pack_hashed_;
        if (rt_materials_loaded()) {
            note_rt_texture(op.id, op.name.hash, entry->own.width, entry->own.height, op.name.format);
        }
        if (packs_.dumping()) {
            dump_base_level(op.staged, op.name);
        }
    }
    look_up(op.id, *entry);
    entries_[op.id] = std::move(entry);
}

void TextureStore::update_entry(Op& op) {
    if (op.id >= entries_.size() || entries_[op.id] == nullptr) {
        create_entry(op);
        return;
    }
    Entry& entry = *entries_[op.id];
    TextureHandle& own = entry.own;
    const bool same = own.width == op.staged.width && own.height == op.staged.height && own.mip_levels == op.staged.levels &&
                      entry.name.format == op.name.format;
    if (same) {
        backend_.record_upload(gpu::raw(entry.texture), true, *op.staged.chunk, op.staged.offset, op.staged.width, op.staged.height,
                               op.staged.levels, op.staged.offsets.data(), op.staged.pitches.data());
        keep(op.staged.chunk);
        if (entry.name.hash == op.name.hash && entry.name.palette == op.name.palette) {
            return;
        }
    } else {
        const bool showing_own = entry.current.srv_index == own.srv_index;
        retire(std::move(entry.texture), own.srv_index);
        make_own(entry, op);
        if (showing_own) {
            entry.current = own;
        }
        ++revision_;
    }
    // New texels have a new name: what stood in for the old ones goes.
    entry.name = op.name;
    if (op.name.named && packs_.dumping()) {
        dump_base_level(op.staged, op.name);
    }
    look_up(op.id, entry);
}

void TextureStore::free_entry(std::uint32_t id) {
    if (id >= entries_.size() || entries_[id] == nullptr) {
        return;
    }
    Entry& entry = *entries_[id];
    stop_showing(entry);
    if (entry.built_in.texture) {
        retire(std::move(entry.built_in.texture), ~0u);
    }
    retire(std::move(entry.texture), entry.own.srv_index);
    entries_[id].reset();
    blank_images_.insert(id);
    // Tables holding it read nothing from now on.
    ++revision_;
}

void TextureStore::dump_base_level(const Staged& staged, const PackName& name) {
    const std::uint8_t* const base = staged.chunk->cpu_base + staged.offset;
    const std::uint32_t width = staged.width;
    const std::uint32_t height = staged.height;
    std::vector<RGBA8> pixels(std::size_t{width} * height);
    if (staged.format == TexelFormat::Rgba8) {
        for (std::uint32_t y = 0; y < height; ++y) {
            std::memcpy(pixels.data() + std::size_t{y} * width, base + std::size_t{y} * staged.pitches[0], std::size_t{width} * 4u);
        }
    } else {
        const std::uint32_t row = texel_row_bytes(staged.format, width);
        const std::uint32_t rows = texel_rows(staged.format, height);
        std::vector<std::uint8_t> packed(std::size_t{row} * rows);
        for (std::uint32_t y = 0; y < rows; ++y) {
            std::memcpy(packed.data() + std::size_t{y} * row, base + std::size_t{y} * staged.pitches[0], row);
        }
        detail::expand_record_level(staged.layout, packed.data(), width, height, pixels.data());
    }
    TexturePackName pack_name{};
    pack_name.width = width;
    pack_name.height = height;
    pack_name.format = name.format;
    pack_name.mipmapped = name.mipmapped;
    pack_name.hash = name.hash;
    pack_name.palette = name.palette;
    packs_.dump(pack_name, width, height, reinterpret_cast<const std::uint8_t*>(pixels.data()));
}

void TextureStore::stop_showing(Entry& entry) {
    if (entry.pack_slot != TexturePacks::kNone) {
        packs_.remove_user(entry.pack_slot);
        entry.pack_slot = TexturePacks::kNone;
    }
    entry.pack_waiting = false;
    if (entry.current.srv_index != entry.own.srv_index) {
        retire(std::move(entry.shown), entry.current.srv_index);
        entry.shown = gpu::TextureRef{};
    }
    entry.current = entry.own;
}

void TextureStore::show(Entry& entry, const TextureReplacement& replacement, std::uint32_t slot) {
    const std::uint32_t view = allocate_srv_index();
    backend_.write_view(view, gpu::raw(replacement.texture), replacement.levels);
    entry.shown = replacement.texture;
    entry.current = entry.own;
    entry.current.resource = gpu::raw(replacement.texture);
    entry.current.srv_index = view;
    entry.current.mip_levels = static_cast<std::uint8_t>(std::min(replacement.levels, 255u));
    entry.current.lod_shift = replacement.lod_shift;
    entry.current.lod_arbitrary = replacement.arbitrary;
    if (slot != TexturePacks::kNone) {
        entry.pack_slot = slot;
        packs_.add_user(slot);
    }
}

// Decides what stands for an entry: a pack's replacement, else the game's larger painting, else the original.
void TextureStore::look_up(std::uint32_t id, Entry& entry) {
    const bool was_own = entry.current.srv_index == entry.own.srv_index;
    stop_showing(entry);
    if (entry.name.named) {
        const TexturePackFormat format = static_cast<TexturePackFormat>(entry.name.format);
        if (packs_.may_match(entry.own.width, entry.own.height, format)) {
            const std::uint32_t slot = packs_.find(entry.own.width, entry.own.height, format, entry.name.hash, entry.name.palette,
                                                   entry.name.mipmapped);
            if (const TextureReplacement* const replacement = packs_.resident(slot)) {
                show(entry, *replacement, slot);
                ++pack_bound_;
                ++revision_;
                return;
            }
            if (slot != TexturePacks::kNone) {
                entry.pack_slot = slot;
                entry.pack_waiting = true;
                packs_.add_user(slot);
                pack_waiters_[slot].push_back(id);
                if (!was_own) {
                    ++revision_;
                }
                return;
            }
        }
    }
    // A dump wants the image itself.
    if (entry.built_in.texture && !packs_.dumping()) {
        show(entry, entry.built_in, TexturePacks::kNone);
        ++revision_;
        return;
    }
    if (!was_own) {
        ++revision_;
    }
}

TextureHandle TextureStore::handle(std::uint32_t id) {
    if (movie_active_) [[unlikely]] {
        for (unsigned p = 0; p < 3u; ++p) {
            if (id == movie_ids_[p]) {
                return movie_handles_[p];
            }
        }
    }
    if (id < entries_.size() && entries_[id] != nullptr) {
        return entries_[id]->current;
    }
    // An image that never came: blank. One that went with its scene is blank too and has lost nothing.
    if (blank_images_.insert(id).second) {
        galaxy::loss::note(galaxy::loss::Kind::BlankTexture);
        std::fprintf(stderr, "[texture] no image for texture %u (drawn blank)\n", id);
    }
    return {};
}

void TextureStore::retire(gpu::TextureRef texture, std::uint32_t view) {
    if (retired_.empty()) {
        if (view != ~0u) {
            backend_.release_view(view);
            free_srv_indices_.push_back(view);
        }
        return;
    }
    retired_[frame_slot_].push_back(Retired{std::move(texture), view});
}

std::uint32_t TextureStore::allocate_srv_index() {
    if (!free_srv_indices_.empty()) {
        const std::uint32_t index = free_srv_indices_.back();
        free_srv_indices_.pop_back();
        return index;
    }
    if (next_srv_index_ == NEBULA_NONE32) {
        throw std::runtime_error("[TextureStore] native texture IDs exhausted");
    }
    backend_.ensure_view(next_srv_index_);
    return next_srv_index_++;
}

void TextureStore::configure_texture_packs(const TexturePackSettings& settings) {
    if (!stager_set_) {
        // Before any load exists: the loader threads read it.
        packs_.set_stager([this](std::uint64_t size) { return backend_.create_upload_chunk(size); });
        stager_set_ = true;
    }
    packs_.configure(settings);
}

void TextureStore::apply_texture_packs() {
    if (!backend_.has_upload_list() || retired_.empty()) {
        return;
    }
    const auto apply_start = std::chrono::steady_clock::now();
    if (packs_.pump(pack_uploads_)) {
        // New index, packs turned off, or dumping toggled: every texture is looked up again.
        pack_waiters_.clear();
        for (std::uint32_t id = 0; id < entries_.size(); ++id) {
            if (entries_[id] != nullptr) {
                look_up(id, *entries_[id]);
            }
        }
    }
    // At most ~3 ms of uploads a frame (at least one); the rest wait, so a pack's first textures never stall a
    // frame (a 2048x2048 level chain is 22 MB to stage).
    constexpr auto kApplyBudget = std::chrono::microseconds(3000);
    std::size_t done = 0;
    for (; done < pack_uploads_.size(); ++done) {
        if (done != 0u && std::chrono::steady_clock::now() - apply_start > kApplyBudget) {
            break;
        }
        auto& [slot, pixels] = pack_uploads_[done];
        if (!packs_.uploading(slot)) {
            continue;  // handed out under an index replaced since
        }
        TextureReplacement replacement{};
        try {
            if (pixels.staging && pixels.level_count != 0u) {
                // Staged by the loader: create and record the copy; the buffer lives until this slot comes round.
                std::uint64_t allocation_bytes = 0;
                gpu::TextureRef texture = backend_.create_texture(pixels.width, pixels.height, pixels.level_count, allocation_bytes);
                if (texture) {
                    backend_.record_upload(gpu::raw(texture), false, *pixels.staging, 0u, pixels.width, pixels.height,
                                           pixels.level_count, pixels.offsets.data(), pixels.pitches.data());
                    pack_staging_[frame_slot_].push_back(std::move(*pixels.staging));
                    pixels.staging.reset();
                    replacement.texture = std::move(texture);
                    replacement.width = pixels.width;
                    replacement.height = pixels.height;
                    replacement.levels = pixels.level_count;
                    replacement.bytes = allocation_bytes;
                }
            } else if (!pixels.levels.empty()) {
                const auto levels = static_cast<unsigned>(pixels.levels.size());
                std::vector<std::uint64_t> offsets(levels);
                std::vector<std::uint32_t> pitches(levels);
                std::uint64_t staging_size = 0;
                for (unsigned l = 0; l < levels; ++l) {
                    const std::uint32_t lw = std::max<std::uint32_t>(1u, pixels.width >> l);
                    const std::uint32_t lh = std::max<std::uint32_t>(1u, pixels.height >> l);
                    staging_size = align_up(staging_size, TextureBackend::kPlacementAlignment);
                    offsets[l] = staging_size;
                    pitches[l] = static_cast<std::uint32_t>(align_up(lw * 4u, TextureBackend::kPitchAlignment));
                    staging_size += static_cast<std::uint64_t>(pitches[l]) * lh;
                }
                std::uint64_t offset = 0u;
                ChunkRef chunk = allocate_staging(staging_size, offset);
                std::uint8_t* const base = chunk->cpu_base + offset;
                for (unsigned l = 0; l < levels; ++l) {
                    const std::uint32_t lw = std::max<std::uint32_t>(1u, pixels.width >> l);
                    const std::uint32_t lh = std::max<std::uint32_t>(1u, pixels.height >> l);
                    for (std::uint32_t row = 0; row < lh; ++row) {
                        std::memcpy(base + offsets[l] + static_cast<std::uint64_t>(row) * pitches[l],
                                    pixels.levels[l].data() + static_cast<std::size_t>(row) * lw * 4u, lw * 4u);
                    }
                }
                std::uint64_t allocation_bytes = 0;
                gpu::TextureRef texture = backend_.create_texture(pixels.width, pixels.height, levels, allocation_bytes);
                if (texture) {
                    backend_.record_upload(gpu::raw(texture), false, *chunk, offset, pixels.width, pixels.height, levels,
                                           offsets.data(), pitches.data());
                    keep(std::move(chunk));
                    replacement.texture = std::move(texture);
                    replacement.width = pixels.width;
                    replacement.height = pixels.height;
                    replacement.levels = levels;
                    replacement.bytes = allocation_bytes;
                }
            }
        } catch (const std::exception& error) {
            // Out of video memory or staging: the original stays (the slot is marked failed).
            std::cerr << "[texture-pack] upload failed: " << error.what() << '\n';
            replacement = {};
        }
        pixels = {};
        packs_.make_resident(slot, std::move(replacement));
        const auto waiting = pack_waiters_.find(slot);
        const TextureReplacement* const resident = packs_.resident(slot);
        if (waiting == pack_waiters_.end() || resident == nullptr) {
            continue;
        }
        for (const std::uint32_t id : waiting->second) {
            Entry* const entry = id < entries_.size() ? entries_[id].get() : nullptr;
            if (entry == nullptr || entry->pack_slot != slot || !entry->pack_waiting) {
                continue;
            }
            stop_showing(*entry);
            show(*entry, *resident, slot);
            ++pack_swapped_;
            ++revision_;
        }
        pack_waiters_.erase(waiting);
    }
    pack_uploads_.erase(pack_uploads_.begin(), pack_uploads_.begin() + static_cast<std::ptrdiff_t>(done));
    const auto apply_ns = static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now() - apply_start).count());
    pack_apply_ns_ += apply_ns;
    pack_apply_max_ns_ = std::max(pack_apply_max_ns_, apply_ns);
    if (++pack_stats_frame_ % 600u == 0u && (packs_.enabled() || packs_.dumping())) {
        const TexturePacks::Stats stats = packs_.stats();
        std::cerr << "[texture-pack] names=" << stats.names << " hashed=" << pack_hashed_
                  << " apply-ms=" << static_cast<double>(pack_apply_ns_) / 1.0e6
                  << " apply-max-ms=" << static_cast<double>(pack_apply_max_ns_) / 1.0e6
                  << " bound=" << pack_bound_ << " swapped=" << pack_swapped_ << " loads=" << stats.loads
                  << " failures=" << stats.load_failures << " uploads=" << stats.uploads
                  << " upload-mb=" << (stats.upload_bytes >> 20u) << " resident=" << stats.resident
                  << " resident-mb=" << (stats.resident_bytes >> 20u) << " evictions=" << stats.evictions
                  << " deferred=" << stats.deferred << " load-ms=" << stats.load_ms << " dumped=" << stats.dumped
                  << '\n';
    }
}

void TextureStore::set_movie_override(const MovieFrameMark& mark, const MovieFrame* frame) {
    if (frame == nullptr || mark.frame < 0 || !frame->staging || frame->width == 0u || frame->height == 0u) {
        if (movie_active_) {
            ++revision_;
        }
        movie_active_ = false;
        movie_frame_ = -1;
        movie_staging_seen_ = nullptr;
        return;
    }
    // The movie quad's planes (nebula_game_api.h), never an image's id.
    constexpr std::array<std::uint32_t, 3> ids{NEBULA_TEXTURE_MOVIE_Y, NEBULA_TEXTURE_MOVIE_U, NEBULA_TEXTURE_MOVIE_V};
    const bool new_picture = frame->frame != movie_frame_ || frame->staging.get() != movie_staging_seen_;
    const bool same_planes = movie_active_ && ids == movie_ids_ && mark.width == movie_handles_[0].width &&
                             mark.height == movie_handles_[0].height;
    if ((same_planes && !new_picture) || !backend_.has_upload_list() || retired_.empty()) {
        return;
    }
    if (new_picture) {
        movie_ring_at_ = (movie_ring_at_ + 1u) % kMovieRing;
        for (unsigned p = 0; p < 3u; ++p) {
            const std::uint32_t w = p == 0u ? frame->width : (frame->width + 1u) / 2u;
            const std::uint32_t h = p == 0u ? frame->height : (frame->height + 1u) / 2u;
            MoviePlane& plane = movie_ring_[movie_ring_at_][p];
            if (!plane.texture || plane.width != w || plane.height != h) {
                if (plane.texture) {
                    // Another size: the old texture and its slot retire after the frames that may still read them.
                    retire(std::move(plane.texture), plane.srv_index);
                }
                std::uint64_t bytes = 0;
                plane.texture = backend_.create_texture(w, h, 1u, bytes, TexelFormat::R8);
                plane.width = w;
                plane.height = h;
                plane.uploaded = false;
                plane.srv_index = allocate_srv_index();
                backend_.write_view(plane.srv_index, gpu::raw(plane.texture), 1u);
            }
            const std::uint64_t level_offset = 0u;
            const std::uint32_t pitch = frame->pitches[p];
            backend_.record_upload(gpu::raw(plane.texture), plane.uploaded, *frame->staging, frame->offsets[p], w, h, 1u,
                                   &level_offset, &pitch);
            plane.uploaded = true;
        }
        movie_staging_[frame_slot_].push_back(frame->staging);
        movie_frame_ = frame->frame;
        movie_staging_seen_ = frame->staging.get();
    }
    movie_ids_ = ids;
    for (unsigned p = 0; p < 3u; ++p) {
        // The movie's chroma planes are half the luma plane's size each way.
        const MoviePlane& plane = movie_ring_[movie_ring_at_][p];
        TextureHandle handle{};
        handle.resource = gpu::raw(plane.texture);
        handle.srv_index = plane.srv_index;
        handle.width = static_cast<std::uint32_t>(p == 0u ? mark.width : mark.width >> 1u);
        handle.height = static_cast<std::uint32_t>(p == 0u ? mark.height : mark.height >> 1u);
        handle.mip_levels = 1u;
        movie_handles_[p] = handle;
    }
    movie_active_ = true;
    ++revision_;
}

}  // namespace galaxy::render
