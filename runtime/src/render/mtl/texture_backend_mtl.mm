// SPDX-License-Identifier: GPL-3.0-only
// TextureStore's Metal side (texture_backend_mtl.h).
#include "galaxy/render/gpu_texture_backend.h"

#import <Foundation/Foundation.h>
#import <Metal/Metal.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <stdexcept>

namespace galaxy::render {

namespace {

// The largest shared heap; a texture that takes more than half of one has a heap of its own. The first shared heap is
// kFirstHeapBytes, each further one twice the largest held.
constexpr std::uint64_t kHeapBytes = 64ull << 20u;
constexpr std::uint64_t kFirstHeapBytes = 8ull << 20u;
// The array's first length in slots (it doubles as the store's indices reach its end).
constexpr std::uint32_t kFirstArraySlots = 4096u;

id<MTLDevice> device() noexcept {
    return (__bridge id<MTLDevice>)mtl::Context::get().device();
}

mtl::Format format_of(TexelFormat format) noexcept {
    return format == TexelFormat::R8    ? mtl::Format::R8
           : format == TexelFormat::Rg8 ? mtl::Format::Rg8
           : format == TexelFormat::Bc1 ? mtl::Format::Bc1
           : format == TexelFormat::Astc4x4 ? mtl::Format::Astc4x4
                                        : mtl::Format::Rgba8;
}

// A texture's place in the array: its resource id's eight bytes.
std::uint64_t resource_id(mtl::Object texture) noexcept {
    if (texture == nullptr) {
        return 0u;
    }
    if (@available(macOS 13.0, iOS 16.0, *)) {
        const MTLResourceID resource = ((__bridge id<MTLTexture>)texture).gpuResourceID;
        std::uint64_t bits = 0u;
        static_assert(sizeof(resource) == sizeof(bits));
        std::memcpy(&bits, &resource, sizeof(bits));
        return bits;
    }
    return 0u;
}

}  // namespace

bool TextureBackend::initialize(gpu::Device*, std::uint32_t view_capacity) {
    mtl::Context& context = mtl::Context::get();
    // The null texture: one transparent black texel, sampled where a draw binds no map.
    null_texture_ = context.create_texture(1u, 1u, 1u, mtl::Format::Rgba8, mtl::kUseSample, mtl::Storage::Managed, "null texture");
    if (!null_texture_) {
        return false;
    }
    @autoreleasepool {
        const std::uint8_t black[4]{};
        [(__bridge id<MTLTexture>)null_texture_->texture replaceRegion:MTLRegionMake2D(0, 0, 1, 1)
                                                           mipmapLevel:0
                                                             withBytes:black
                                                           bytesPerRow:4];
    }
    null_view_ = null_texture_->texture;
    null_id_ = resource_id(null_view_);
    views_.assign(std::size_t{view_capacity} + 1u, nullptr);
    loose_.clear();
    array_.reset();
    array_slots_ = 0u;
    if (context.caps().texture_arrays) {
        const std::uint32_t slots = std::max(view_capacity + 2u, kFirstArraySlots);
        array_ = context.create_buffer(std::uint64_t{slots} * 8u, mtl::Storage::Shared, "texture array");
        if (!array_) {
            return false;
        }
        array_slots_ = slots;
        auto* ids = reinterpret_cast<std::uint64_t*>(array_->mapped);
        std::fill(ids, ids + slots, null_id_);
    }
    std::fprintf(stderr, "[textures] %s; uploads: staged copies\n",
                 array_ ? "one array of every texture (resource ids)" : "a table of sixteen a draw");
    return true;
}

void TextureBackend::shutdown() {
    views_.clear();
    loose_.clear();
    array_.reset();
    array_slots_ = 0u;
    null_texture_.reset();
    null_view_ = nullptr;
    null_id_ = 0u;
    upload_list_ = nullptr;
    uploaded_ = false;
    // The heaps go once the textures made from them have (their releases are deferred behind the GPU).
    std::vector<Heap> heaps;
    {
        const std::lock_guard<std::mutex> lock(heaps_mutex_);
        heaps.swap(heaps_);
        heap_list_.clear();
        placed_ = 0u;
    }
    mtl::Context& context = mtl::Context::get();
    for (const Heap& heap : heaps) {
        const mtl::Object held = heap.heap;
        if (context.ready()) {
            context.defer([held] { mtl::release(held); });
        } else {
            mtl::release(held);
        }
    }
}

void TextureBackend::ensure_view(std::uint32_t index) {
    if (index >= views_.size()) {
        views_.resize(std::max<std::size_t>(std::size_t{index} + 1u, views_.size() * 2u), nullptr);
    }
}

void TextureBackend::write_view(std::uint32_t index, mtl::Texture* texture, unsigned) {
    ensure_view(index);
    views_[index] = texture->view();
    if (texture->heap != nullptr) {
        loose_.erase(index);
    } else {
        loose_[index] = views_[index];
        if (open_pass_ != nullptr && array_) {
            [(__bridge id<MTLRenderCommandEncoder>)open_pass_ useResource:(__bridge id<MTLTexture>)views_[index]
                                                                    usage:MTLResourceUsageRead
                                                                   stages:MTLRenderStageFragment];
        }
    }
    publish(index);
}

void TextureBackend::write_view(std::uint32_t index, mtl::Object texture) {
    ensure_view(index);
    views_[index] = texture;
    if (texture != nullptr) {
        loose_[index] = texture;
        if (open_pass_ != nullptr && array_) {
            [(__bridge id<MTLRenderCommandEncoder>)open_pass_ useResource:(__bridge id<MTLTexture>)texture
                                                                    usage:MTLResourceUsageRead
                                                                   stages:MTLRenderStageFragment];
        }
    } else {
        loose_.erase(index);
    }
    publish(index);
}

void TextureBackend::release_view(std::uint32_t index) {
    if (index >= views_.size()) {
        return;
    }
    views_[index] = nullptr;
    loose_.erase(index);
    publish(index);
}

std::uint32_t TextureBackend::live_views() const noexcept {
    for (std::size_t i = views_.size(); i > 0; --i) {
        if (views_[i - 1u] != nullptr) {
            return static_cast<std::uint32_t>(i);
        }
    }
    return 0u;
}

void TextureBackend::publish(std::uint32_t index) {
    if (!array_) {
        return;
    }
    // The last slot stays the null texture's: the store's indices end before it.
    if (index + 1u >= array_slots_) {
        grow_array(index);
        if (index + 1u >= array_slots_) {
            throw std::runtime_error("[textures] the texture array could not be made longer");
        }
    }
    const std::uint64_t id = views_[index] != nullptr ? resource_id(views_[index]) : null_id_;
    reinterpret_cast<std::uint64_t*>(array_->mapped)[index] = id;
}

void TextureBackend::grow_array(std::uint32_t index) {
    const std::uint64_t wanted = std::max<std::uint64_t>(std::uint64_t{array_slots_} * 2u, std::uint64_t{index} + 2u);
    const std::uint64_t most = mtl::Context::get().caps().max_buffer_bytes / 8u;
    const auto slots = static_cast<std::uint32_t>(std::min<std::uint64_t>({wanted, most, 0xFFFFFFFFull}));
    if (slots <= array_slots_) {
        return;
    }
    std::shared_ptr<mtl::Buffer> longer =
        mtl::Context::get().create_buffer(std::uint64_t{slots} * 8u, mtl::Storage::Shared, "texture array");
    if (!longer) {
        return;
    }
    auto* ids = reinterpret_cast<std::uint64_t*>(longer->mapped);
    for (std::uint32_t slot = 0; slot < slots; ++slot) {
        ids[slot] = slot < views_.size() && views_[slot] != nullptr ? resource_id(views_[slot]) : null_id_;
    }
    std::fprintf(stderr, "[textures] the texture array: %u slots\n", slots);
    // The one before stays until the frames that read it are done (the buffer's own release waits for the GPU).
    array_ = std::move(longer);
    array_slots_ = slots;
    ++array_generation_;
}

void TextureBackend::use_textures(mtl::Object render_encoder, const mtl::Object* targets, std::size_t target_count) const {
    if (render_encoder == nullptr) {
        return;
    }
    id<MTLRenderCommandEncoder> const encoder = (__bridge id<MTLRenderCommandEncoder>)render_encoder;
    {
        const std::lock_guard<std::mutex> lock(heaps_mutex_);
        if (!heap_list_.empty()) {
            [encoder useHeaps:(__unsafe_unretained id<MTLHeap> const*)(void*)heap_list_.data()
                        count:heap_list_.size()
                       stages:MTLRenderStageFragment];
        }
    }
    if (null_view_ != nullptr) {
        [encoder useResource:(__bridge id<MTLTexture>)null_view_ usage:MTLResourceUsageRead stages:MTLRenderStageFragment];
    }
    for (const auto& [slot, texture] : loose_) {
        id<MTLTexture> const picture = (__bridge id<MTLTexture>)texture;
        // A view of a target (its channels reordered) is the target.
        id<MTLTexture> const whole = picture.parentTexture != nil ? picture.parentTexture : picture;
        bool drawn_into = false;
        for (std::size_t i = 0; i < target_count; ++i) {
            drawn_into = drawn_into || (__bridge void*)whole == targets[i] || texture == targets[i];
        }
        if (!drawn_into) {
            [encoder useResource:picture usage:MTLResourceUsageRead stages:MTLRenderStageFragment];
        }
    }
}

TextureBackend::UploadChunk TextureBackend::create_upload_chunk(std::uint64_t size) {
    UploadChunk chunk{};
    chunk.resource = mtl::Context::get().create_buffer(size, mtl::Storage::Shared, "texture upload arena");
    if (!chunk.resource) {
        throw std::runtime_error("[TextureStore] create texture upload arena failed");
    }
    chunk.cpu_base = reinterpret_cast<std::uint8_t*>(chunk.resource->mapped);
    chunk.size = size;
    return chunk;
}

void TextureBackend::release_upload_chunk(UploadChunk& chunk) noexcept {
    chunk.cpu_base = nullptr;
    chunk.resource.reset();
}

bool TextureBackend::supports_bc1() {
    return mtl::Context::get().caps().bc_textures;
}

bool TextureBackend::supports_astc4x4() {
    return mtl::Context::get().caps().apple_gpu;
}

// A texture of `descriptor` (an MTLTextureDescriptor*) from a heap with room, else from a new heap.
mtl::Object TextureBackend::allocate(void* descriptor, mtl::Object& heap_out) {
    MTLTextureDescriptor* const wanted = (__bridge MTLTextureDescriptor*)descriptor;
    const MTLSizeAndAlign need = [device() heapTextureSizeAndAlignWithDescriptor:wanted];
    const std::lock_guard<std::mutex> lock(heaps_mutex_);
    for (const Heap& heap : heaps_) {
        if (heap.single) {
            continue;
        }
        id<MTLHeap> const from = (__bridge id<MTLHeap>)heap.heap;
        if ([from maxAvailableSizeWithAlignment:need.align] >= need.size) {
            if (id<MTLTexture> const made = [from newTextureWithDescriptor:wanted]) {
                ++placed_;
                heap_out = heap.heap;
                return (__bridge_retained void*)made;
            }
        }
    }
    const bool single = need.size > kHeapBytes / 2u;
    MTLHeapDescriptor* const heap_descriptor = [MTLHeapDescriptor new];
    heap_descriptor.type = MTLHeapTypeAutomatic;
    heap_descriptor.storageMode = MTLStorageModePrivate;
    // Tracked: a heap's textures are untracked by default, and then nothing orders a frame's uploads before the draws
    // that read them on a GPU that runs independent passes side by side (Apple's). Tracked by the heap, the uploads'
    // blit and each pass that names the heap run in the order they were recorded.
    heap_descriptor.hazardTrackingMode = MTLHazardTrackingModeTracked;
    // A shared heap: twice the largest shared heap held (kFirstHeapBytes when none), at least twice the texture, at most
    // kHeapBytes.
    std::uint64_t shared_size = kFirstHeapBytes;
    for (const Heap& held : heaps_) {
        if (!held.single) shared_size = std::max<std::uint64_t>(shared_size, std::uint64_t{held.size} * 2u);
    }
    while (shared_size < std::uint64_t{need.size} * 2u) shared_size *= 2u;
    shared_size = std::min(shared_size, kHeapBytes);
    heap_descriptor.size = single ? ((need.size + need.align - 1u) / need.align) * need.align : shared_size;
    id<MTLHeap> const fresh = [device() newHeapWithDescriptor:heap_descriptor];
    if (fresh == nil) {
        return nullptr;
    }
    fresh.label = single ? @"Nebula texture" : @"Nebula textures";
    id<MTLTexture> const made = [fresh newTextureWithDescriptor:wanted];
    if (made == nil) {
        return nullptr;
    }
    Heap heap;
    heap.heap = (__bridge_retained void*)fresh;
    heap.size = fresh.size;
    heap.single = single;
    heaps_.push_back(heap);
    heap_list_.push_back(heap.heap);
    if (open_pass_ != nullptr && array_) {
        // The pass that is open named the heaps there were when it began.
        [(__bridge id<MTLRenderCommandEncoder>)open_pass_ useHeap:fresh stages:MTLRenderStageFragment];
    }
    ++placed_;
    heap_out = heap.heap;
    return (__bridge_retained void*)made;
}

std::shared_ptr<mtl::Texture> TextureBackend::create_texture(
    std::uint32_t width, std::uint32_t height, unsigned levels, std::uint64_t& allocation_bytes, TexelFormat format, bool) {
    @autoreleasepool {
        const mtl::Format pixels = format_of(format);
        MTLTextureDescriptor* const descriptor = [MTLTextureDescriptor new];
        descriptor.textureType = MTLTextureType2D;
        descriptor.pixelFormat = static_cast<MTLPixelFormat>(pixels);
        descriptor.width = width;
        descriptor.height = height;
        descriptor.mipmapLevelCount = std::max(levels, 1u);
        descriptor.usage = MTLTextureUsageShaderRead;
        descriptor.storageMode = MTLStorageModePrivate;
        auto texture = std::make_shared<mtl::Texture>();
        texture->texture = allocate((__bridge void*)descriptor, texture->heap);
        if (texture->texture == nullptr) {
            throw std::runtime_error("[TextureStore] create default texture failed");
        }
        id<MTLTexture> const made = (__bridge id<MTLTexture>)texture->texture;
        texture->format = pixels;
        texture->use = mtl::kUseSample;
        texture->width = width;
        texture->height = height;
        texture->levels = static_cast<std::uint32_t>(made.mipmapLevelCount);
        texture->storage = mtl::Storage::Private;
        texture->allocation_bytes = made.allocatedSize;
        if (format == TexelFormat::R8 || format == TexelFormat::Rg8) {
            // The value in every colour channel, alpha the value or the second byte.
            const MTLTextureSwizzleChannels channels = MTLTextureSwizzleChannelsMake(
                MTLTextureSwizzleRed, MTLTextureSwizzleRed, MTLTextureSwizzleRed,
                format == TexelFormat::R8 ? MTLTextureSwizzleRed : MTLTextureSwizzleGreen);
            id<MTLTexture> const view = [made newTextureViewWithPixelFormat:made.pixelFormat
                                                                textureType:MTLTextureType2D
                                                                     levels:NSMakeRange(0, made.mipmapLevelCount)
                                                                     slices:NSMakeRange(0, 1)
                                                                    swizzle:channels];
            if (view == nil) {
                throw std::runtime_error("[TextureStore] create texture view failed");
            }
            texture->sampled = (__bridge_retained void*)view;
        }
        allocation_bytes = texture->allocation_bytes;
        return texture;
    }
}

bool TextureBackend::poolable(mtl::Texture* texture, std::uint32_t& width, std::uint32_t& height, std::uint32_t& levels) {
    if (texture->format != mtl::Format::Rgba8 || (texture->use & mtl::kUseTarget) != 0u) {
        return false;
    }
    width = texture->width;
    height = texture->height;
    levels = texture->levels;
    return true;
}

void TextureBackend::record_upload(mtl::Texture* texture, bool, const UploadChunk& staging, std::uint64_t offset,
                                   std::uint32_t width, std::uint32_t height, unsigned levels, const std::uint64_t* offsets,
                                   const std::uint32_t* pitches) {
    if (upload_list_ == nullptr || texture == nullptr || texture->texture == nullptr || !staging.resource) {
        return;
    }
    id<MTLBlitCommandEncoder> const blit = (__bridge id<MTLBlitCommandEncoder>)upload_list_;
    id<MTLBuffer> const source = (__bridge id<MTLBuffer>)staging.resource->buffer;
    id<MTLTexture> const target = (__bridge id<MTLTexture>)texture->texture;
    const bool blocks = texture->format == mtl::Format::Bc1 || texture->format == mtl::Format::Astc4x4;
    for (unsigned level = 0; level < levels; ++level) {
        const std::uint32_t level_width = std::max<std::uint32_t>(1u, width >> level);
        const std::uint32_t level_height = std::max<std::uint32_t>(1u, height >> level);
        // Rows of pitches[level] bytes: texel rows, or rows of four-by-four blocks.
        const std::uint32_t rows = blocks ? (level_height + 3u) / 4u : level_height;
        [blit copyFromBuffer:source
                sourceOffset:offset + offsets[level]
           sourceBytesPerRow:pitches[level]
         sourceBytesPerImage:std::uint64_t{pitches[level]} * rows
                  sourceSize:MTLSizeMake(level_width, level_height, 1)
                   toTexture:target
            destinationSlice:0
            destinationLevel:level
           destinationOrigin:MTLOriginMake(0, 0, 0)];
    }
    uploaded_ = true;
}

void TextureBackend::begin_frame(unsigned) {
    constexpr std::uint32_t kTrimFrames = 600u;
    if (++frames_since_trim_ < kTrimFrames) {
        return;
    }
    frames_since_trim_ = 0u;
    std::vector<mtl::Object> emptied;
    {
        const std::lock_guard<std::mutex> lock(heaps_mutex_);
        for (std::size_t at = heaps_.size(); at > 1u; --at) {
            if (((__bridge id<MTLHeap>)heaps_[at - 1u].heap).usedSize == 0u) {
                emptied.push_back(heaps_[at - 1u].heap);
                heaps_.erase(heaps_.begin() + static_cast<std::ptrdiff_t>(at - 1u));
            }
        }
        if (!emptied.empty()) {
            heap_list_.clear();
            for (const Heap& heap : heaps_) {
                heap_list_.push_back(heap.heap);
            }
        }
    }
    // A frame in flight may have named the heap: it goes behind the GPU like any other object.
    for (const mtl::Object heap : emptied) {
        mtl::Context::get().defer([heap] { mtl::release(heap); });
    }
}

TextureBackend::HeapStats TextureBackend::heap_stats() const noexcept {
    HeapStats stats;
    const std::lock_guard<std::mutex> lock(heaps_mutex_);
    for (const Heap& heap : heaps_) {
        id<MTLHeap> const held = (__bridge id<MTLHeap>)heap.heap;
        ++stats.heaps;
        stats.heap_bytes += heap.size;
        stats.used_bytes += held.usedSize;
        stats.committed_fallbacks += heap.single ? 1u : 0u;
    }
    stats.placed = placed_;
    return stats;
}

}  // namespace galaxy::render
