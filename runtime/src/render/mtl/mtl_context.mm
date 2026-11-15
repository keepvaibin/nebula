// SPDX-License-Identifier: GPL-3.0-only
// The Metal device (mtl_context.h).
#include "galaxy/render/mtl/mtl_context.h"

#import <Foundation/Foundation.h>
#import <Metal/Metal.h>
#include <TargetConditionals.h>
#include <objc/message.h>
#include <objc/runtime.h>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <utility>

namespace galaxy::render::mtl {

// Format's numbers are Metal's.
static_assert(static_cast<std::uint32_t>(Format::R8) == MTLPixelFormatR8Unorm);
static_assert(static_cast<std::uint32_t>(Format::Rg8) == MTLPixelFormatRG8Unorm);
static_assert(static_cast<std::uint32_t>(Format::Rgba8) == MTLPixelFormatRGBA8Unorm);
static_assert(static_cast<std::uint32_t>(Format::Bgra8) == MTLPixelFormatBGRA8Unorm);
static_assert(static_cast<std::uint32_t>(Format::Rgb10a2) == MTLPixelFormatRGB10A2Unorm);
static_assert(static_cast<std::uint32_t>(Format::Bgr10a2) == MTLPixelFormatBGR10A2Unorm);
static_assert(static_cast<std::uint32_t>(Format::R16f) == MTLPixelFormatR16Float);
static_assert(static_cast<std::uint32_t>(Format::Rg16f) == MTLPixelFormatRG16Float);
static_assert(static_cast<std::uint32_t>(Format::Rgba16f) == MTLPixelFormatRGBA16Float);
static_assert(static_cast<std::uint32_t>(Format::R32f) == MTLPixelFormatR32Float);
static_assert(static_cast<std::uint32_t>(Format::R32u) == MTLPixelFormatR32Uint);
static_assert(static_cast<std::uint32_t>(Format::Depth32fStencil8) == MTLPixelFormatDepth32Float_Stencil8);
static_assert(static_cast<std::uint32_t>(Format::Depth32f) == MTLPixelFormatDepth32Float);
static_assert(static_cast<std::uint32_t>(Format::Stencil8) == MTLPixelFormatStencil8);
static_assert(static_cast<std::uint32_t>(Format::Astc4x4) == MTLPixelFormatASTC_4x4_LDR);
#if TARGET_OS_OSX
static_assert(static_cast<std::uint32_t>(Format::Bc1) == MTLPixelFormatBC1_RGBA);
#endif
static_assert(std::uint32_t{kUseSample} == std::uint32_t{MTLTextureUsageShaderRead} &&
              std::uint32_t{kUseWrite} == std::uint32_t{MTLTextureUsageShaderWrite} &&
              std::uint32_t{kUseTarget} == std::uint32_t{MTLTextureUsageRenderTarget} &&
              std::uint32_t{kUseViews} == std::uint32_t{MTLTextureUsagePixelFormatView});

Object retain(Object object) noexcept {
    return object != nullptr ? const_cast<void*>(CFRetain(object)) : nullptr;
}

void release(Object object) noexcept {
    if (object != nullptr) {
        CFRelease(object);
    }
}

namespace {

id<MTLDevice> device_of(Object object) noexcept {
    return (__bridge id<MTLDevice>)object;
}

bool depth_or_stencil(Format format) noexcept {
    return format == Format::Depth32fStencil8 || format == Format::Depth32f || format == Format::Stencil8;
}

// MetalFX is asked for by name: the framework is not on macOS 12 or iOS 15, where the program must still start.
bool metalfx_supports(const char* descriptor_class, id<MTLDevice> device) noexcept {
    static const bool loaded = [] {
        return [[NSBundle bundleWithPath:@"/System/Library/Frameworks/MetalFX.framework"] load];
    }();
    if (!loaded) {
        return false;
    }
    const Class descriptor = NSClassFromString([NSString stringWithUTF8String:descriptor_class]);
    const SEL supports = NSSelectorFromString(@"supportsDevice:");
    if (descriptor == Nil || ![descriptor respondsToSelector:supports]) {
        return false;
    }
    return reinterpret_cast<BOOL (*)(Class, SEL, id<MTLDevice>)>(objc_msgSend)(descriptor, supports, device) != NO;
}

MTLResourceOptions options_of(Storage storage, bool unified) noexcept {
    switch (storage) {
    case Storage::Private: return MTLResourceStorageModePrivate;
#if TARGET_OS_OSX
    case Storage::Managed: return unified ? MTLResourceStorageModeShared : MTLResourceStorageModeManaged;
#else
    case Storage::Managed: return MTLResourceStorageModeShared;
#endif
    default: return MTLResourceStorageModeShared;
    }
}

DeviceCaps caps_of(id<MTLDevice> device) {
    DeviceCaps caps;
    caps.unified_memory = device.hasUnifiedMemory;
    // MTLGPUFamilyApple<n> is 1000 + n; a later family holds the earlier ones.
    for (unsigned family = 16u; family >= 1u; --family) {
        if ([device supportsFamily:static_cast<MTLGPUFamily>(1000 + family)]) {
            caps.apple_family = family;
            break;
        }
    }
    caps.apple_gpu = caps.apple_family != 0u;
    caps.metal3 = [device supportsFamily:static_cast<MTLGPUFamily>(5001)];
    const bool tier2 = device.argumentBuffersSupport == MTLArgumentBuffersTier2;
    if (@available(macOS 13.0, iOS 16.0, *)) {
        caps.texture_arrays = tier2;  // resource ids written to a buffer (gpuResourceID)
        caps.language_major = 3u;
        caps.language_minor = 0u;
        caps.recommended_memory = device.recommendedMaxWorkingSetSize;
    }
    caps.sampler_array_max = static_cast<std::uint32_t>(std::min<NSUInteger>(device.maxArgumentBufferSamplerCount, 1u << 20u));
#if TARGET_OS_OSX
    caps.bc_textures = device.supportsBCTextureCompression;
    caps.depth24 = device.depth24Stencil8PixelFormatSupported;
    if (caps.recommended_memory == 0u) {
        caps.recommended_memory = device.recommendedMaxWorkingSetSize;
    }
#else
    if (@available(iOS 16.4, *)) {
        caps.bc_textures = device.supportsBCTextureCompression;
    }
#endif
    for (const unsigned samples : {8u, 4u, 2u}) {
        if ([device supportsTextureSampleCount:samples]) {
            caps.max_samples = samples;
            break;
        }
    }
    caps.memoryless = caps.apple_gpu;
    caps.metalfx_spatial = metalfx_supports("MTLFXSpatialScalerDescriptor", device);
    caps.metalfx_temporal = metalfx_supports("MTLFXTemporalScalerDescriptor", device);
    caps.metalfx_interpolation = metalfx_supports("MTLFXFrameInterpolatorDescriptor", device);
    caps.binary_archives = true;  // macOS 11, iOS 14: every system this is built for
    caps.max_buffer_bytes = device.maxBufferLength;
    // Metal's feature tables: 8192 on an A8, 32768 from the A19 and the M5, 16384 between and on an Intel Mac.
    caps.max_texture_size = caps.apple_gpu && caps.apple_family <= 2u ? 8192u : caps.apple_family >= 10u ? 32768u : 16384u;
    return caps;
}

std::string identity_of(id<MTLDevice> device) {
    char text[32]{};
    std::snprintf(text, sizeof(text), "%016llx", static_cast<unsigned long long>(device.registryID));
    return std::string(device.name.UTF8String ?: "") + "|" + text;
}

}  // namespace

Texture::~Texture() {
    const Object held = texture, held_stencil = stencil, held_sampled = sampled;
    if (held == nullptr && held_stencil == nullptr && held_sampled == nullptr) {
        return;
    }
    Context& context = Context::get();
    if (context.ready()) {
        context.defer([held, held_stencil, held_sampled] {
            release(held_sampled);
            release(held_stencil);
            release(held);
        });
    } else {
        release(held_sampled);
        release(held_stencil);
        release(held);
    }
}

Buffer::~Buffer() {
    const Object held = buffer;
    const std::uint64_t held_address = address;
    if (held == nullptr) {
        return;
    }
    Context& context = Context::get();
    if (context.ready()) {
        // The address goes with the buffer: nothing encoded can name it once the frames that may have are done.
        context.defer([held, held_address] {
            Context::get().release_address(held_address);
            release(held);
        });
    } else {
        context.release_address(held_address);
        release(held);
    }
}

Context& Context::get() noexcept {
    static Context context;
    return context;
}

bool Context::initialize(const std::string& chosen_gpu) {
    if (device_ != nullptr) {
        return true;
    }
    failure_.clear();
    @autoreleasepool {
        id<MTLDevice> device = nil;
#if TARGET_OS_OSX
        // The chosen one; else a discrete GPU before the integrated one (never one in an external enclosure unless
        // chosen: it goes when the cable does).
        NSArray<id<MTLDevice>>* const devices = MTLCopyAllDevices();
        for (id<MTLDevice> candidate in devices) {
            std::fprintf(stderr, "[metal] GPU: %s (%s%s%s)\n", candidate.name.UTF8String,
                         candidate.isLowPower ? "integrated" : "discrete", candidate.isRemovable ? ", external" : "",
                         candidate.isHeadless ? ", no display" : "");
        }
        if (!chosen_gpu.empty()) {
            for (id<MTLDevice> candidate in devices) {
                if (identity_of(candidate) == chosen_gpu || chosen_gpu == (candidate.name.UTF8String ?: "")) {
                    device = candidate;
                    break;
                }
            }
        }
        if (device == nil) {
            for (id<MTLDevice> candidate in devices) {
                if (!candidate.isLowPower && !candidate.isRemovable && !candidate.isHeadless) {
                    device = candidate;
                    break;
                }
            }
        }
#else
        (void)chosen_gpu;
#endif
        if (device == nil) {
            device = MTLCreateSystemDefaultDevice();
        }
        if (device == nil) {
            failure_ = "no Metal device";
            std::fprintf(stderr, "[metal] no Metal device\n");
            return false;
        }
        id<MTLCommandQueue> queue = [device newCommandQueue];
        if (queue == nil) {
            failure_ = "no Metal command queue";
            return false;
        }
        queue.label = @"Nebula";
        caps_ = caps_of(device);
        device_name_ = device.name.UTF8String ?: "";
        gpu_id_ = identity_of(device);
        device_ = (__bridge_retained void*)device;
        queue_ = (__bridge_retained void*)queue;
    }
    submitted_.store(0u, std::memory_order_release);
    completed_.store(0u, std::memory_order_release);
    {
        const std::lock_guard<std::mutex> lock(wait_mutex_);
        failed_ = false;
        failed_what_.clear();
    }
    std::fprintf(stderr,
                 "[metal] device=\"%s\" family=%s%u%s memory=%s language=%u.%u\n", device_name_.c_str(),
                 caps_.apple_gpu ? "Apple" : "Mac", caps_.apple_gpu ? caps_.apple_family : 2u, caps_.metal3 ? " Metal3" : "",
                 caps_.unified_memory ? "unified" : "discrete", caps_.language_major, caps_.language_minor);
    std::fprintf(stderr,
                 "[metal] features: texture-arrays=%d sampler-array=%u bc=%d msaa=%u memoryless=%d metalfx-spatial=%d "
                 "metalfx-temporal=%d metalfx-interpolation=%d max-texture=%u budget=%lluMB\n",
                 caps_.texture_arrays ? 1 : 0, caps_.sampler_array_max, caps_.bc_textures ? 1 : 0, caps_.max_samples,
                 caps_.memoryless ? 1 : 0, caps_.metalfx_spatial ? 1 : 0, caps_.metalfx_temporal ? 1 : 0,
                 caps_.metalfx_interpolation ? 1 : 0, caps_.max_texture_size,
                 static_cast<unsigned long long>(caps_.recommended_memory >> 20u));
    return true;
}

void Context::shutdown() noexcept {
    if (device_ == nullptr) {
        return;
    }
    // Everything committed finishes, then everything released goes.
    (void)wait(submitted_value(), 2000000000ull);
    std::vector<Deferred> rest;
    {
        const std::lock_guard<std::mutex> lock(deferred_mutex_);
        rest.swap(deferred_);
    }
    const Object device = std::exchange(device_, nullptr);
    const Object queue = std::exchange(queue_, nullptr);
    for (Deferred& entry : rest) {
        entry.destroy();
    }
    release(queue);
    release(device);
}

std::uint64_t Context::commit_with(Object command_buffer, bool frame) {
    id<MTLCommandBuffer> const commands = (__bridge id<MTLCommandBuffer>)command_buffer;
    // One committing thread (the render thread): the values rise in commit order.
    const std::uint64_t value = submitted_.load(std::memory_order_relaxed) + 1u;
    [commands addCompletedHandler:^(id<MTLCommandBuffer> done) {
        const bool failed = done.status == MTLCommandBufferStatusError;
        Context::get().completed(value, failed, failed ? std::string(done.error.localizedDescription.UTF8String ?: "") : std::string());
    }];
    if (frame) {
        const std::lock_guard<std::mutex> lock(deferred_mutex_);
        for (Deferred& entry : deferred_) {
            if (entry.value == 0u) {
                entry.value = value;
            }
        }
    }
    submitted_.store(value, std::memory_order_release);
    [commands commit];
    return value;
}

std::uint64_t Context::commit(Object command_buffer) {
    return commit_with(command_buffer, false);
}

std::uint64_t Context::commit_frame(Object command_buffer) {
    return commit_with(command_buffer, true);
}

void Context::completed(std::uint64_t value, bool failed, std::string what) {
    // Handlers come on Metal's own threads, not always in commit order; the GPU runs a queue's buffers in order, so a
    // later one done means every earlier one is.
    std::uint64_t seen = completed_.load(std::memory_order_relaxed);
    while (seen < value && !completed_.compare_exchange_weak(seen, value, std::memory_order_release, std::memory_order_relaxed)) {
    }
    {
        const std::lock_guard<std::mutex> lock(wait_mutex_);
        if (failed && !failed_) {
            failed_ = true;
            failed_what_ = std::move(what);
        }
    }
    wait_wake_.notify_all();
}

bool Context::wait(std::uint64_t value, std::uint64_t timeout_ns) {
    std::unique_lock<std::mutex> lock(wait_mutex_);
    const auto passed = [&] { return completed_.load(std::memory_order_acquire) >= value || failed_; };
    if (timeout_ns == UINT64_MAX) {
        wait_wake_.wait(lock, passed);
    } else if (!wait_wake_.wait_for(lock, std::chrono::nanoseconds(timeout_ns), passed)) {
        return false;
    }
    return !failed_;
}

bool Context::take_failure(std::string& what) {
    const std::lock_guard<std::mutex> lock(wait_mutex_);
    if (!failed_) {
        return false;
    }
    what = std::move(failed_what_);
    failed_what_.clear();
    failed_ = false;
    return true;
}

void Context::defer(std::function<void()> destroy) {
    const std::lock_guard<std::mutex> lock(deferred_mutex_);
    deferred_.push_back({0u, std::move(destroy)});
}

void Context::collect() {
    const std::uint64_t done = completed_value();
    std::vector<Deferred> ready;
    {
        const std::lock_guard<std::mutex> lock(deferred_mutex_);
        const auto first_kept = std::stable_partition(deferred_.begin(), deferred_.end(), [done](const Deferred& entry) {
            return entry.value != 0u && entry.value <= done;
        });
        ready.assign(std::make_move_iterator(deferred_.begin()), std::make_move_iterator(first_kept));
        deferred_.erase(deferred_.begin(), first_kept);
    }
    for (Deferred& entry : ready) {
        entry.destroy();
    }
}

std::shared_ptr<Texture> Context::create_texture(std::uint32_t width, std::uint32_t height, std::uint32_t levels,
                                                 Format format, std::uint32_t use, Storage storage, const char* name,
                                                 std::uint32_t samples) {
    if (device_ == nullptr || width == 0u || height == 0u) {
        return nullptr;
    }
    @autoreleasepool {
        MTLTextureDescriptor* const descriptor = [MTLTextureDescriptor new];
        descriptor.textureType = samples > 1u ? MTLTextureType2DMultisample : MTLTextureType2D;
        descriptor.pixelFormat = static_cast<MTLPixelFormat>(format);
        descriptor.width = width;
        descriptor.height = height;
        descriptor.mipmapLevelCount = samples > 1u ? 1u : std::max(levels, 1u);
        descriptor.sampleCount = std::max(samples, 1u);
        descriptor.usage = static_cast<MTLTextureUsage>(use);
        // Depth, stencil and multisampled textures are the GPU's alone on every system.
        const Storage placed = depth_or_stencil(format) || samples > 1u ? Storage::Private : storage;
        // A texture the CPU writes: shared memory on an Apple GPU; on an Intel Mac a managed pair, whichever GPU it is
        // (its integrated GPUs share the system's memory and still take no shared texture).
        MTLStorageMode mode = MTLStorageModePrivate;
        if (placed != Storage::Private) {
#if TARGET_OS_OSX
            mode = caps_.apple_gpu ? MTLStorageModeShared : MTLStorageModeManaged;
#else
            mode = MTLStorageModeShared;
#endif
        }
        descriptor.storageMode = mode;
        id<MTLTexture> const made = [device_of(device_) newTextureWithDescriptor:descriptor];
        if (made == nil) {
            std::fprintf(stderr, "[metal] no texture %ux%u (%s)\n", width, height, name != nullptr ? name : "");
            return nullptr;
        }
        if (name != nullptr) {
            made.label = [NSString stringWithUTF8String:name];
        }
        auto texture = std::make_shared<Texture>();
        texture->format = format;
        texture->use = use;
        texture->width = width;
        texture->height = height;
        texture->levels = static_cast<std::uint32_t>(made.mipmapLevelCount);
        texture->samples = static_cast<std::uint32_t>(made.sampleCount);
        texture->storage = placed;
        texture->allocation_bytes = made.allocatedSize;
        texture->texture = (__bridge_retained void*)made;
        return texture;
    }
}

std::shared_ptr<Buffer> Context::create_buffer(std::uint64_t size, Storage storage, const char* name) {
    if (device_ == nullptr || size == 0u || size > caps_.max_buffer_bytes) {
        return nullptr;
    }
    @autoreleasepool {
        // A buffer the CPU writes every frame is shared memory, never a managed pair that needs telling of each write.
        const Storage placed = storage == Storage::Managed ? Storage::Shared : storage;
        id<MTLBuffer> const made = [device_of(device_) newBufferWithLength:size options:options_of(placed, caps_.unified_memory)];
        if (made == nil) {
            std::fprintf(stderr, "[metal] no buffer of %llu bytes (%s)\n", static_cast<unsigned long long>(size),
                         name != nullptr ? name : "");
            return nullptr;
        }
        if (name != nullptr) {
            made.label = [NSString stringWithUTF8String:name];
        }
        auto buffer = std::make_shared<Buffer>();
        buffer->size = size;
        buffer->storage = placed;
        buffer->mapped = placed == Storage::Private ? nullptr : static_cast<std::byte*>(made.contents);
        buffer->buffer = (__bridge_retained void*)made;
        buffer->address = register_address(buffer->buffer);
        if (buffer->address == 0u) {
            return nullptr;  // the buffer's destructor releases it
        }
        return buffer;
    }
}

bool Context::device_memory_budget(std::uint64_t& usage, std::uint64_t& budget) const noexcept {
    if (device_ == nullptr || caps_.recommended_memory == 0u) {
        return false;
    }
    usage = device_of(device_).currentAllocatedSize;
    budget = caps_.recommended_memory;
    return true;
}

std::uint64_t Context::register_address(Object buffer) {
    const std::lock_guard<std::mutex> lock(address_mutex_);
    std::uint32_t id = 0u;
    if (!free_address_ids_.empty()) {
        id = free_address_ids_.back();
        free_address_ids_.pop_back();
    } else if (next_address_id_ < kAddressIds) {
        id = next_address_id_++;
    } else {
        std::fprintf(stderr, "[metal] no buffer address left\n");
        return 0u;
    }
    AddressPage* page = address_pages_[id >> kAddressPageBits].load(std::memory_order_acquire);
    if (page == nullptr) {
        page = new AddressPage();
        address_pages_[id >> kAddressPageBits].store(page, std::memory_order_release);
    }
    page->slots[id & (kAddressPageSize - 1u)].store(buffer, std::memory_order_release);
    return std::uint64_t{id} << kAddressShift;
}

void Context::release_address(std::uint64_t address) noexcept {
    const auto id = static_cast<std::uint32_t>(address >> kAddressShift);
    if (id == 0u) {
        return;
    }
    const std::lock_guard<std::mutex> lock(address_mutex_);
    if (AddressPage* page = address_pages_[id >> kAddressPageBits].load(std::memory_order_acquire)) {
        page->slots[id & (kAddressPageSize - 1u)].store(nullptr, std::memory_order_release);
    }
    free_address_ids_.push_back(id);
}

}  // namespace galaxy::render::mtl
