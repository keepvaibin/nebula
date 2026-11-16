// SPDX-License-Identifier: GPL-3.0-only
// The ray tracer's back end on Metal (docs/APPLE.md, "State"). The render core holds a tracer whatever the renderer
// (rt_backend.h) and starts it only on a device whose capabilities say it can trace (rt_caps.h), which the Metal
// renderer never reports yet. Without the CMake option NEBULA_METAL_RAY_TRACING (platform/apple/metal.cmake), or
// compiled as C++, every method is a refusal the core links against. With it, as Objective-C++: structures in
// placement heaps, bottom- and top-level builds, refits, compaction, and the passes: the tracer's shaders as MSL
// (platform/apple/tools/rt_shaders_msl.cpp, shader_msl.h kRt* indices), one compute pipeline each, images, counters.
#include "galaxy/render/rt/rt_backend.h"
#include "galaxy/render/rt/rt_shaders.h"

#include <cstdio>

#if defined(__OBJC__) && defined(NEBULA_METAL_RAY_TRACING)
#define GALAXY_RT_METAL 1
#endif

#if defined(GALAXY_RT_METAL) && defined(GALAXY_RT_MSL)
#include "rt_shaders_msl.h"  // kRtShaders: each shader's MSL and threadgroup size, in RtShader's order
#endif

#if defined(GALAXY_RT_METAL)
#import <Metal/Metal.h>
#include <TargetConditionals.h>

#include "galaxy/render/mtl/mtl_context.h"
#include "galaxy/render/mtl/shader_msl.h"
#include "render/rt/rt_scene.h"

#include <bit>
#include <cstring>
#include <functional>
#include <memory>
#include <vector>
#endif

namespace galaxy::render {

#if defined(GALAXY_RT_METAL) && defined(GALAXY_RT_MSL)
static_assert(sizeof(kRtShaders) / sizeof(kRtShaders[0]) == static_cast<unsigned>(RtShader::Count),
              "shaders.tsv and RtShader list the same shaders");

RtShaderBytes rt_shader(RtShader shader) noexcept {
    const auto index = static_cast<unsigned>(shader);
    if (index >= static_cast<unsigned>(RtShader::Count)) {
        return {};
    }
    return {kRtShaders[index].data, kRtShaders[index].size};
}

bool rt_shaders_present() noexcept { return true; }
#else
RtShaderBytes rt_shader(RtShader) noexcept { return {}; }

bool rt_shaders_present() noexcept { return false; }
#endif

namespace rt {

#if defined(GALAXY_RT_METAL)

namespace {

constexpr unsigned kShaderCount = static_cast<unsigned>(RtShader::Count);
constexpr std::uint64_t kCountersBytes = std::uint64_t{kCounterWords} * 4u;  // the occluder bins after the counters
// Bytes a buffer slot the pass leaves empty reads (zeros, as Direct3D 12's null descriptors and Vulkan's robust ones).
constexpr std::uint64_t kNullBufferBytes = 4096u;

// A shader's library and pipeline from its MSL (MSL 3.1: rt_shaders_msl.cpp), with the indices it declares (its
// pipeline's binding reflection) so a pass's empty slots get an object of the declared kind. Nil and a reason when the
// device refuses it.
struct Pipeline {
    id<MTLComputePipelineState> state = nil;
    std::uint32_t buffers = 0u;   // bit n: [[buffer(n)]] declared
    std::uint32_t textures = 0u;  // bit n: [[texture(n)]] declared
    MTLTextureType texture_type[32]{};
    bool texture_uint[32]{};
};

Pipeline make_pipeline(id<MTLDevice> device, unsigned index, const char*& why) API_AVAILABLE(macos(14.0), ios(17.0));
Pipeline make_pipeline(id<MTLDevice> device, unsigned index, const char*& why) {
    Pipeline made;
#if defined(GALAXY_RT_MSL)
    @autoreleasepool {
        NSString* source = [[NSString alloc] initWithBytes:kRtShaders[index].data
                                                    length:kRtShaders[index].size
                                                  encoding:NSUTF8StringEncoding];
        MTLCompileOptions* options = [MTLCompileOptions new];
        options.languageVersion = MTLLanguageVersion3_1;
        NSError* error = nil;
        id<MTLLibrary> library = source != nil ? [device newLibraryWithSource:source options:options error:&error] : nil;
        id<MTLFunction> function = library != nil ? [library newFunctionWithName:@"main0"] : nil;
        if (function == nil) {
            why = "the MSL does not compile";
            if (error != nil) {
                std::fprintf(stderr, "[rt] metal: shader %u: %s\n", index, error.localizedDescription.UTF8String);
            }
            return made;
        }
        MTLComputePipelineReflection* reflection = nil;
        made.state = [device newComputePipelineStateWithFunction:function
                                                         options:MTLPipelineOptionBindingInfo
                                                      reflection:&reflection
                                                           error:&error];
        if (made.state == nil) {
            why = "the pipeline is refused";
            return made;
        }
        const unsigned threads = kRtShaders[index].threads[0] * kRtShaders[index].threads[1] * kRtShaders[index].threads[2];
        if (threads == 0u || threads > made.state.maxTotalThreadsPerThreadgroup) {
            why = "its threadgroup is larger than the pipeline takes";
            made.state = nil;
            return made;
        }
        for (id<MTLBinding> binding in reflection.bindings) {
            if (binding.index >= 32u) {
                continue;
            }
            const std::uint32_t bit = 1u << binding.index;
            // The scene is a binding of its own type at its buffer index.
            if (binding.type == MTLBindingTypeBuffer || binding.type == MTLBindingTypeInstanceAccelerationStructure) {
                made.buffers |= bit;
            } else if (binding.type == MTLBindingTypeTexture) {
                made.textures |= bit;
                const id<MTLTextureBinding> texture = (id<MTLTextureBinding>)binding;
                made.texture_type[binding.index] = texture.textureType;
                made.texture_uint[binding.index] = texture.textureDataType == MTLDataTypeUInt;
            }
        }
    }
#else
    (void)device;
    (void)index;
    why = "this build holds no MSL";
#endif
    return made;
}

// A page: a placement heap structures are made in at the caller's offsets, or a scratch buffer.
struct Page {
    id<MTLHeap> heap = nil;
    id<MTLBuffer> scratch = nil;
    std::uint64_t bytes = 0u;
};

// Instance options by name (rt_scene.h's kInstance* are Direct3D 12's and Vulkan's bits).
MTLAccelerationStructureInstanceOptions options_of(std::uint32_t flags) {
    MTLAccelerationStructureInstanceOptions options = MTLAccelerationStructureInstanceOptionNone;
    if ((flags & kInstanceNoCull) != 0u) {
        options |= MTLAccelerationStructureInstanceOptionDisableTriangleCulling;
    }
    if ((flags & kInstanceFlip) != 0u) {
        options |= MTLAccelerationStructureInstanceOptionTriangleFrontFacingWindingCounterClockwise;
    }
    if ((flags & kInstanceNotOpaque) != 0u) {
        options |= MTLAccelerationStructureInstanceOptionNonOpaque;
    }
    return options;
}

// The tracer's image formats as Metal's (rt_backend_vk.cpp format_of: the same channels and encodings).
MTLPixelFormat format_of(ImageFormat format) {
    switch (format) {
    case ImageFormat::Rgba8: return MTLPixelFormatRGBA8Unorm;
    case ImageFormat::Rgba16f: return MTLPixelFormatRGBA16Float;
    case ImageFormat::Rg16f: return MTLPixelFormatRG16Float;
    case ImageFormat::R16f: return MTLPixelFormatR16Float;
    case ImageFormat::R8: return MTLPixelFormatR8Unorm;
    case ImageFormat::Rgb10a2: return MTLPixelFormatRGB10A2Unorm;
    case ImageFormat::Rgba32f: return MTLPixelFormatRGBA32Float;
    case ImageFormat::Rgba32ui: return MTLPixelFormatRGBA32Uint;
    }
    return MTLPixelFormatInvalid;
}

// Holds a Metal object until the GPU has passed the frame that may still name it (mtl::Context::defer), as every Metal
// object of the renderer is let go.
void release_later(id object) {
    if (object != nil) {
        mtl::Context::get().defer([held = object]() { (void)held; });
    }
}

}  // namespace

struct Backend::State {
    id<MTLDevice> device = nil;
    std::vector<Page> pages;
    std::vector<std::uint32_t> free_pages;
    std::vector<id<MTLAccelerationStructure>> structures;  // Structure::handle - 1
    std::vector<std::uint64_t> free_structures;
    std::vector<id<MTLBuffer>> buffers;  // make_buffer's
    std::vector<std::uint32_t> free_buffers;
    std::vector<std::shared_ptr<mtl::Texture>> images;  // make_image's (the renderer's own kind: image_to_copy/sample)
    Live live_now{};
    Pipeline pipelines[kShaderCount];
    id<MTLCommandBuffer> commands = nil;
    id<MTLAccelerationStructureCommandEncoder> builds = nil;
    id<MTLComputeCommandEncoder> passes = nil;
    bool passes_resident = false;  // this compute encoder has made the structures and the texture table resident
    unsigned frame_slot = 0u;
    id<MTLBuffer> sizes[kFramesInFlight]{};  // kSizeSlots 64-bit answers a frame slot, shared memory
    // Each frame slot's instances in Metal's layout, written here from the renderer's (build_scene); the slot's last
    // frame has finished when it comes round, and a buffer outgrown this frame is held until then.
    id<MTLBuffer> instances[kFramesInFlight]{};
    std::uint64_t instances_at[kFramesInFlight]{};
    std::vector<id<MTLBuffer>> outgrown[kFramesInFlight];
    // The counters a frame slot's passes add to (shared memory), and what its last finished frame added.
    id<MTLBuffer> counters[kFramesInFlight]{};
    std::uint32_t counted[kFramesInFlight][kCounterWords]{};
    unsigned counter_words = kCounters;  // kept and zeroed: kCounterWords while the occluder bins count
    // The renderer's texture table this frame (begin's TextureTable: the argument buffer the draws bind at
    // kBufferTextureArray, and what makes its textures resident on a compute encoder).
    id<MTLBuffer> table = nil;
    const std::function<void(void*)>* table_resident = nullptr;
    // What a pass's empty slots read: zeros (a buffer), and 1x1 textures by type and data type.
    id<MTLBuffer> null_buffer = nil;
    id<MTLTexture> null_2d = nil, null_2d_uint = nil, null_3d = nil, null_3d_uint = nil;

    // One open encoder at a time on the frame's command buffer: builds and passes take turns, each closing the other.
    // Metal orders encoders over the tracked resources they share (heaps and buffers made tracked here), so what a
    // later encoder reads waits for an earlier one's writes.
    id<MTLAccelerationStructureCommandEncoder> encoder() {
        if (builds == nil && commands != nil) {
            close_passes();
            @autoreleasepool {
                builds = [commands accelerationStructureCommandEncoder];
            }
        }
        return builds;
    }
    id<MTLComputeCommandEncoder> compute() {
        if (passes == nil && commands != nil) {
            close_builds();
            // Serial: each dispatch sees the writes of the one before (Direct3D 12's and Vulkan's per-pass barriers).
            @autoreleasepool {
                passes = [commands computeCommandEncoderWithDispatchType:MTLDispatchTypeSerial];
            }
            passes_resident = false;
        }
        return passes;
    }
    void close_builds() {
        if (builds != nil) {
            [builds endEncoding];
            builds = nil;
        }
    }
    void close_passes() {
        if (passes != nil) {
            [passes endEncoding];
            passes = nil;
        }
    }
    void close() {
        close_builds();
        close_passes();
    }
    // Encoders left open on a command buffer that may be committed (end() not called): ended only while it is not.
    void retire() {
        if (builds == nil && passes == nil) {
            return;
        }
        if (commands != nil && commands.status < MTLCommandBufferStatusCommitted) {
            close();
        } else {
            std::fprintf(stderr, "[rt] metal: an encoder was left open past its command buffer's commit\n");
            builds = nil;
            passes = nil;
        }
    }
    id<MTLAccelerationStructure> structure_of(const Structure& structure) const {
        return structure.handle != 0u && structure.handle <= structures.size() ? structures[structure.handle - 1u] : nil;
    }
    id<MTLBuffer> scratch_of(const Range& range) const {
        return range.page < pages.size() ? pages[range.page].scratch : nil;
    }
    id<MTLTexture> image_of(std::uint32_t image) const {
        return image < images.size() && images[image] != nullptr ? (__bridge id<MTLTexture>)images[image]->texture : nil;
    }
    // A mesh's buffer and byte: a buffer of the tracer's, or an address of the renderer's (mtl_context.h).
    id<MTLBuffer> buffer_at(std::uint32_t own, gpu::Address at, std::uint64_t& offset) const {
        if (own != kNoBuffer) {
            offset = at;
            return own < buffers.size() ? buffers[own] : nil;
        }
        const mtl::Context::BufferOffset place = mtl::Context::get().resolve(at);
        offset = place.offset;
        return (__bridge id<MTLBuffer>)place.buffer;
    }
    MTLPrimitiveAccelerationStructureDescriptor* primitive_of(const TriangleInput& input, BuildKind kind) const {
        MTLAccelerationStructureTriangleGeometryDescriptor* geometry = [MTLAccelerationStructureTriangleGeometryDescriptor descriptor];
        std::uint64_t offset = 0u;
        geometry.vertexBuffer = buffer_at(input.own_buffer, input.positions, offset);
        geometry.vertexBufferOffset = offset;
        geometry.vertexStride = input.position_stride;  // vertexFormat's default: three floats
        if (input.indices != 0u) {
            geometry.indexBuffer = buffer_at(kNoBuffer, input.indices, offset);
            geometry.indexBufferOffset = offset;
            geometry.indexType = input.index_bytes == 2u ? MTLIndexTypeUInt16 : MTLIndexTypeUInt32;
        }
        geometry.triangleCount = input.triangles();
        geometry.opaque = YES;  // as the other back ends: an instance's NonOpaque option makes its hits candidates
        MTLPrimitiveAccelerationStructureDescriptor* descriptor = [MTLPrimitiveAccelerationStructureDescriptor descriptor];
        descriptor.geometryDescriptors = @[geometry];
        descriptor.usage = kind == BuildKind::Moving
                               ? (MTLAccelerationStructureUsageRefit | MTLAccelerationStructureUsagePreferFastBuild)
                               : MTLAccelerationStructureUsageNone;
        return descriptor;
    }
    // What a structure of `bytes` takes in a placement heap (its size there and its alignment): every size this back end
    // reports (sizes, scene_sizes, compacted_size) is the heap's, so the core's ranges hold the placed structure.
    std::uint64_t placed(std::uint64_t bytes) const {
        if (bytes == 0u) {
            return 0u;
        }
        if (@available(macOS 14.0, iOS 17.0, *)) {
            const MTLSizeAndAlign need = [device heapAccelerationStructureSizeAndAlignWithSize:bytes];
            return need.align <= kRangeAlign ? need.size : 0u;  // 0: no range of the core's alignment can hold it
        }
        return 0u;
    }
    id<MTLTexture> null_texture(MTLTextureType type, bool uint_data) {
        id<MTLTexture> __strong& slot = type == MTLTextureType3D ? (uint_data ? null_3d_uint : null_3d)
                                                                 : (uint_data ? null_2d_uint : null_2d);
        if (slot == nil) {
            MTLTextureDescriptor* descriptor = [MTLTextureDescriptor new];
            descriptor.textureType = type == MTLTextureType3D ? MTLTextureType3D : MTLTextureType2D;
            descriptor.pixelFormat = uint_data ? MTLPixelFormatRGBA32Uint : MTLPixelFormatRGBA16Float;
            descriptor.width = descriptor.height = descriptor.depth = 1u;
            descriptor.usage = MTLTextureUsageShaderRead | MTLTextureUsageShaderWrite;
            descriptor.storageMode = MTLStorageModePrivate;
            slot = [device newTextureWithDescriptor:descriptor];
        }
        return slot;
    }
};

Backend::Backend() = default;
Backend::~Backend() { stop(); }

bool Backend::start(gpu::Device*, void*) {
    stop();
    id<MTLDevice> device = (__bridge id<MTLDevice>)mtl::Context::get().device();
    const auto refuse = [](const char* why) {
        std::fprintf(stderr, "[rt] metal: %s\n", why);
        return false;
    };
    if (device == nil || !device.supportsRaytracing) {
        return refuse("the device has no ray tracing");
    }
    // Instances that name their structures by resource id (MTLIndirectAccelerationStructureInstanceDescriptor) and the
    // shaders' language (MSL 3.1): macOS 14, iOS 17.
    if (@available(macOS 14.0, iOS 17.0, *)) {
        // The shaders' needs, by capability: SIMD-group reductions (rt_count_sum's simd_sum: the Metal Shading Language
        // has them on macOS from Metal 2.1, on iOS on Apple7 GPUs by the feature set tables), a read-write rgba16f
        // texture (rt_reflect_out: tier 2), and the texture table as an argument buffer of a runtime array (tier 2).
#if !TARGET_OS_OSX
        if (![device supportsFamily:MTLGPUFamilyApple7]) {
            return refuse("the device's GPU family has no SIMD-group reductions");
        }
#endif
        if (device.readWriteTextureSupport != MTLReadWriteTextureTier2) {
            return refuse("the device cannot read and write a texture in one pass (tier 2)");
        }
        if (device.argumentBuffersSupport != MTLArgumentBuffersTier2) {
            return refuse("the device has no tier 2 argument buffers");
        }
        if (!rt_shaders_present()) {
            return refuse("this build holds no tracer shaders as MSL");
        }
        auto state = std::make_unique<State>();
        state->device = device;
        // Every shader's pipeline now, as Direct3D 12 makes its own at the start: a pass that cannot run means no tracer.
        for (unsigned index = 0; index < kShaderCount; ++index) {
            const char* why = nullptr;
            state->pipelines[index] = make_pipeline(device, index, why);
            if (state->pipelines[index].state == nil) {
                std::fprintf(stderr, "[rt] metal: shader %u: %s\n", index, why != nullptr ? why : "refused");
                return false;
            }
        }
        for (unsigned slot = 0; slot < kFramesInFlight; ++slot) {
            state->sizes[slot] = [device newBufferWithLength:kSizeSlots * sizeof(std::uint64_t) options:MTLResourceStorageModeShared];
            state->counters[slot] = [device newBufferWithLength:kCountersBytes options:MTLResourceStorageModeShared];
            if (state->sizes[slot] == nil || state->counters[slot] == nil) {
                return refuse("no memory for the size answers or the counters");
            }
            std::memset(state->sizes[slot].contents, 0, kSizeSlots * sizeof(std::uint64_t));
            std::memset(state->counters[slot].contents, 0, kCountersBytes);
        }
        state->null_buffer = [device newBufferWithLength:kNullBufferBytes options:MTLResourceStorageModeShared];
        if (state->null_buffer == nil) {
            return refuse("no memory for the empty slots' buffer");
        }
        std::memset(state->null_buffer.contents, 0, kNullBufferBytes);
        state_ = state.release();
        std::fprintf(stderr, "[rt] metal: %u pipelines made\n", kShaderCount);
        return true;
    }
    return refuse("ray tracing here needs macOS 14 or iOS 17");
}

void Backend::stop() noexcept {
    if (state_ == nullptr) {
        return;
    }
    State& s = *state_;
    s.retire();
    // What the GPU may still read goes after it has finished with it.
    for (Page& page : s.pages) {
        release_later(page.heap);
        release_later(page.scratch);
    }
    for (id<MTLAccelerationStructure> structure : s.structures) {
        release_later(structure);
    }
    for (id<MTLBuffer> buffer : s.buffers) {
        release_later(buffer);
    }
    for (unsigned slot = 0; slot < kFramesInFlight; ++slot) {
        release_later(s.instances[slot]);
        release_later(s.counters[slot]);
        release_later(s.sizes[slot]);
        for (id<MTLBuffer> buffer : s.outgrown[slot]) {
            release_later(buffer);
        }
    }
    release_later(s.null_buffer);
    release_later(s.null_2d);
    release_later(s.null_2d_uint);
    release_later(s.null_3d);
    release_later(s.null_3d_uint);
    for (Pipeline& pipeline : s.pipelines) {
        release_later(pipeline.state);
    }
    delete state_;  // images: mtl::Texture lets its texture go the same way
    state_ = nullptr;
}

bool Backend::started() const noexcept { return state_ != nullptr; }
bool Backend::failed() const noexcept { return false; }

BuildSizes Backend::sizes(const TriangleInput& input, BuildKind kind) const {
    if (state_ == nullptr) {
        return {};
    }
    // The descriptors made here and in the builds are autoreleased: the render thread has no pool of its own.
    MTLAccelerationStructureSizes sizes{};
    @autoreleasepool {
        sizes = [state_->device accelerationStructureSizesWithDescriptor:state_->primitive_of(input, kind)];
    }
    return {state_->placed(sizes.accelerationStructureSize), sizes.buildScratchBufferSize,
            kind == BuildKind::Moving ? sizes.refitScratchBufferSize : 0u};
}

std::uint32_t Backend::make_page(std::uint64_t bytes, bool scratch) {
    if (state_ == nullptr || bytes == 0u) {
        return ~0u;
    }
    State& s = *state_;
    Page page;
    page.bytes = bytes;
    if (scratch) {
        page.scratch = [s.device newBufferWithLength:bytes options:MTLResourceStorageModePrivate];
        if (page.scratch == nil) {
            return ~0u;
        }
    } else {
        MTLHeapDescriptor* descriptor = [MTLHeapDescriptor new];
        descriptor.type = MTLHeapTypePlacement;
        descriptor.storageMode = MTLStorageModePrivate;
        descriptor.hazardTrackingMode = MTLHazardTrackingModeTracked;
        descriptor.size = bytes;
        page.heap = [s.device newHeapWithDescriptor:descriptor];
        if (page.heap == nil) {
            return ~0u;
        }
    }
    if (!s.free_pages.empty()) {
        const std::uint32_t index = s.free_pages.back();
        s.free_pages.pop_back();
        s.pages[index] = page;
        return index;
    }
    s.pages.push_back(page);
    return static_cast<std::uint32_t>(s.pages.size() - 1u);
}

void Backend::free_page(std::uint32_t page) noexcept {
    if (state_ == nullptr || page >= state_->pages.size() || state_->pages[page].bytes == 0u) {
        return;
    }
    release_later(state_->pages[page].heap);
    release_later(state_->pages[page].scratch);
    state_->pages[page] = Page{};
    state_->free_pages.push_back(page);
}

Structure Backend::make_structure(const Range& range, bool) {
    if (state_ == nullptr || range.page >= state_->pages.size() || state_->pages[range.page].heap == nil) {
        return {};
    }
    State& s = *state_;
    if (@available(macOS 14.0, iOS 17.0, *)) {
        // Placed at the caller's offset: the range is a heap size this back end reported (placed()), and must hold what
        // the heap asks for a structure of that size at that offset.
        const MTLSizeAndAlign need = [s.device heapAccelerationStructureSizeAndAlignWithSize:range.size];
        if (need.align == 0u || range.offset % need.align != 0u || need.size > range.size) {
            std::fprintf(stderr, "[rt] metal: a structure's range does not hold its placement (%llu bytes at %llu)\n",
                         static_cast<unsigned long long>(range.size), static_cast<unsigned long long>(range.offset));
            return {};
        }
        id<MTLAccelerationStructure> made = [s.pages[range.page].heap newAccelerationStructureWithSize:range.size
                                                                                                offset:range.offset];
        if (made == nil) {
            return {};
        }
        Structure structure;
        structure.range = range;
        const MTLResourceID resource = made.gpuResourceID;  // what an instance names it by (its fields are not public)
        static_assert(sizeof(resource) == sizeof(structure.name));
        std::memcpy(&structure.name, &resource, sizeof(resource));
        if (!s.free_structures.empty()) {
            structure.handle = s.free_structures.back();
            s.free_structures.pop_back();
            s.structures[structure.handle - 1u] = made;
        } else {
            s.structures.push_back(made);
            structure.handle = s.structures.size();
        }
        return structure;
    }
    return {};
}

void Backend::free_structure(Structure& structure) noexcept {
    if (state_ != nullptr && state_->structure_of(structure) != nil) {
        // A top-level structure names it by resource id, which holds no reference: kept until the GPU is past it.
        release_later(state_->structures[structure.handle - 1u]);
        state_->structures[structure.handle - 1u] = nil;
        state_->free_structures.push_back(structure.handle);
    }
    structure = Structure{};
}

void Backend::frame(unsigned frame_slot) noexcept {
    if (state_ == nullptr || frame_slot >= kFramesInFlight) {
        return;
    }
    State& s = *state_;
    s.instances_at[frame_slot] = 0u;
    s.outgrown[frame_slot].clear();  // their frame has finished
    // The slot's last frame has finished: what its passes counted is kept for read_counters, then zeroed for this one.
    std::memcpy(s.counted[frame_slot], s.counters[frame_slot].contents, std::size_t{s.counter_words} * 4u);
    std::memset(s.counters[frame_slot].contents, 0, std::size_t{s.counter_words} * 4u);
}

void Backend::begin(void* commands, unsigned frame_slot, void* descriptors) noexcept {
    if (state_ == nullptr || frame_slot >= kFramesInFlight) {
        return;
    }
    State& s = *state_;
    s.retire();
    s.commands = (__bridge id<MTLCommandBuffer>)commands;
    s.frame_slot = frame_slot;
    // The renderer's texture table (TextureTable on Metal: `set` its argument buffer, id<MTLBuffer>; `layout` a
    // std::function<void(void*)> that makes the table's textures resident on the id<MTLComputeCommandEncoder> given).
    // Null when only builds follow.
    if (const auto* textures = static_cast<const TextureTable*>(descriptors); textures != nullptr) {
        s.table = (__bridge id<MTLBuffer>)textures->set;
        s.table_resident = static_cast<const std::function<void(void*)>*>(textures->layout);
    } else {
        s.table = nil;
        s.table_resident = nullptr;
    }
}

// The renderer encodes into the command buffer again: the open encoder ends first (one open encoder at a time).
void Backend::end() noexcept {
    if (state_ != nullptr) {
        state_->close();
    }
}

void Backend::build(const TriangleInput& input, BuildKind kind, const Structure& to, const Range& scratch, bool refit) {
    if (state_ == nullptr) {
        return;
    }
    State& s = *state_;
    id<MTLAccelerationStructure> structure = s.structure_of(to);
    // A refit whose scratch size is 0 (sizes) takes none: Metal's refit is given nil then.
    const bool no_scratch = refit && !scratch;
    id<MTLBuffer> memory = no_scratch ? nil : s.scratch_of(scratch);
    if (structure == nil || (memory == nil && !no_scratch)) {
        return;
    }
    id<MTLAccelerationStructureCommandEncoder> encoder = s.encoder();
    if (encoder == nil) {
        return;
    }
    @autoreleasepool {
        MTLPrimitiveAccelerationStructureDescriptor* descriptor = s.primitive_of(input, kind);
        if (descriptor.geometryDescriptors.firstObject == nil) {
            return;
        }
        if (refit) {
            [encoder refitAccelerationStructure:structure descriptor:descriptor destination:structure scratchBuffer:memory
                            scratchBufferOffset:scratch.offset];
        } else {
            [encoder buildAccelerationStructure:structure descriptor:descriptor scratchBuffer:memory
                            scratchBufferOffset:scratch.offset];
        }
    }
}

BuildSizes Backend::scene_sizes(std::uint32_t instances) const {
    if (state_ == nullptr) {
        return {};
    }
    if (@available(macOS 14.0, iOS 17.0, *)) {
        MTLAccelerationStructureSizes sizes{};
        @autoreleasepool {
            MTLInstanceAccelerationStructureDescriptor* descriptor = [MTLInstanceAccelerationStructureDescriptor descriptor];
            descriptor.instanceDescriptorType = MTLAccelerationStructureInstanceDescriptorTypeIndirect;
            descriptor.instanceCount = instances;
            sizes = [state_->device accelerationStructureSizesWithDescriptor:descriptor];
        }
        return {state_->placed(sizes.accelerationStructureSize), sizes.buildScratchBufferSize, 0u};
    }
    return {};
}

void Backend::build_scene(gpu::Address instances, std::uint32_t count, const Structure& to, const Range& scratch) {
    if (state_ == nullptr || count == 0u) {
        return;
    }
    State& s = *state_;
    id<MTLAccelerationStructure> structure = s.structure_of(to);
    id<MTLBuffer> memory = s.scratch_of(scratch);
    // The renderer's instances are read here on the CPU: they lie in an upload ring (shared or managed memory, written
    // by the CPU this frame); a ring without CPU contents gives no scene this frame.
    const mtl::Context::BufferOffset place = mtl::Context::get().resolve(instances);
    id<MTLBuffer> from = (__bridge id<MTLBuffer>)place.buffer;
    if (structure == nil || memory == nil || from == nil || from.contents == nullptr ||
        place.offset + std::uint64_t{count} * sizeof(Instance) > from.length) {
        return;
    }
    if (@available(macOS 14.0, iOS 17.0, *)) {
        // The renderer's instances (rt_scene.h, the layout Direct3D 12 and Vulkan take) in Metal's: the matrix by columns,
        // the item in userID, the structure by its resource id.
        using Metal = MTLIndirectAccelerationStructureInstanceDescriptor;
        const unsigned slot = s.frame_slot;
        const std::uint64_t at = s.instances_at[slot], bytes = std::uint64_t{count} * sizeof(Metal);
        if (s.instances[slot] == nil || at + bytes > s.instances[slot].length) {
            if (s.instances[slot] != nil) {
                s.outgrown[slot].push_back(s.instances[slot]);
            }
            s.instances[slot] = [s.device newBufferWithLength:std::bit_ceil(at + bytes) options:MTLResourceStorageModeShared];
            if (s.instances[slot] == nil) {
                return;
            }
        }
        const auto* in = reinterpret_cast<const Instance*>(static_cast<const std::byte*>(from.contents) + place.offset);
        auto* out = reinterpret_cast<Metal*>(static_cast<std::byte*>(s.instances[slot].contents) + at);
        for (std::uint32_t i = 0; i < count; ++i) {
            float columns[4][3];
            for (unsigned column = 0; column < 4u; ++column) {
                for (unsigned row = 0; row < 3u; ++row) {
                    columns[column][row] = in[i].transform[row][column];
                }
            }
            static_assert(sizeof(out[i].transformationMatrix) == sizeof(columns));
            std::memcpy(&out[i].transformationMatrix, columns, sizeof(columns));
            out[i].options = options_of(in[i].flags >> 24u);
            out[i].mask = in[i].id_mask >> 24u;
            out[i].intersectionFunctionTableOffset = 0u;  // no intersection functions: inline queries only
            out[i].userID = in[i].id_mask & 0xFFFFFFu;  // the item and its kind (rt_scene.h kItemBits), as InstanceID
            static_assert(sizeof(out[i].accelerationStructureID) == sizeof(in[i].structure));
            std::memcpy(&out[i].accelerationStructureID, &in[i].structure, sizeof(in[i].structure));
        }
        s.instances_at[slot] = at + bytes;

        id<MTLAccelerationStructureCommandEncoder> encoder = s.encoder();
        if (encoder == nil) {
            return;
        }
        @autoreleasepool {
            MTLInstanceAccelerationStructureDescriptor* descriptor = [MTLInstanceAccelerationStructureDescriptor descriptor];
            descriptor.instanceDescriptorType = MTLAccelerationStructureInstanceDescriptorTypeIndirect;
            descriptor.instanceDescriptorBuffer = s.instances[slot];
            descriptor.instanceDescriptorBufferOffset = at;
            descriptor.instanceDescriptorStride = sizeof(Metal);
            descriptor.instanceCount = count;
            // The bottom-level structures the instances name live in the structure pages' heaps.
            for (const Page& page : s.pages) {
                if (page.heap != nil) {
                    [encoder useHeap:page.heap];
                }
            }
            [encoder buildAccelerationStructure:structure descriptor:descriptor scratchBuffer:memory
                            scratchBufferOffset:scratch.offset];
        }
    }
}

void Backend::barrier() {
    if (state_ != nullptr) {
        state_->close();
    }
}

void Backend::ask_compacted_size(const Structure& structure, std::uint32_t slot) {
    if (state_ == nullptr || slot >= kSizeSlots) {
        return;
    }
    State& s = *state_;
    id<MTLAccelerationStructure> built = s.structure_of(structure);
    id<MTLAccelerationStructureCommandEncoder> encoder = built != nil ? s.encoder() : nil;
    if (encoder == nil) {
        return;
    }
    if (@available(macOS 14.0, iOS 17.0, *)) {
        [encoder writeCompactedAccelerationStructureSize:built
                                                toBuffer:s.sizes[s.frame_slot]
                                                  offset:slot * sizeof(std::uint64_t)
                                            sizeDataType:MTLDataTypeULong];
    }
}

void Backend::end_size_asks() {
    if (state_ != nullptr) {
        state_->close();  // shared memory: the answers are readable once the frame has finished
    }
}

std::uint64_t Backend::compacted_size(std::uint32_t slot, unsigned frame_slot) const {
    if (state_ == nullptr || slot >= kSizeSlots || frame_slot >= kFramesInFlight) {
        return 0u;
    }
    // As the heap places a structure of that size (placed()): the core's range for the copy holds it.
    return state_->placed(static_cast<const std::uint64_t*>(state_->sizes[frame_slot].contents)[slot]);
}

void Backend::compact(const Structure& from, const Structure& to) {
    if (state_ == nullptr) {
        return;
    }
    State& s = *state_;
    id<MTLAccelerationStructure> source = s.structure_of(from);
    id<MTLAccelerationStructure> destination = s.structure_of(to);
    id<MTLAccelerationStructureCommandEncoder> encoder = source != nil && destination != nil ? s.encoder() : nil;
    if (encoder != nil) {
        [encoder copyAndCompactAccelerationStructure:source toAccelerationStructure:destination];
    }
}

std::uint32_t Backend::make_buffer(std::uint64_t bytes, const char* name) {
    if (state_ == nullptr || bytes == 0u) {
        return kNoBuffer;
    }
    State& s = *state_;
    id<MTLBuffer> buffer = [s.device newBufferWithLength:bytes options:MTLResourceStorageModePrivate];
    if (buffer == nil) {
        return kNoBuffer;
    }
    if (name != nullptr) {
        @autoreleasepool {
            buffer.label = [NSString stringWithUTF8String:name];
        }
    }
    ++s.live_now.buffers;
    s.live_now.buffer_bytes += buffer.length;
    if (!s.free_buffers.empty()) {
        const std::uint32_t index = s.free_buffers.back();
        s.free_buffers.pop_back();
        s.buffers[index] = buffer;
        return index;
    }
    s.buffers.push_back(buffer);
    return static_cast<std::uint32_t>(s.buffers.size() - 1u);
}

void Backend::free_buffer(std::uint32_t buffer) noexcept {
    if (state_ == nullptr || buffer >= state_->buffers.size() || state_->buffers[buffer] == nil) {
        return;
    }
    --state_->live_now.buffers;
    state_->live_now.buffer_bytes -= state_->buffers[buffer].length;
    release_later(state_->buffers[buffer]);
    state_->buffers[buffer] = nil;
    state_->free_buffers.push_back(buffer);
}

// Metal orders encoders over tracked resources: a buffer a pass wrote is whole for the next build encoder.
void Backend::buffer_for(std::uint32_t, bool) {}

Backend::Live Backend::live() const noexcept { return state_ != nullptr ? state_->live_now : Live{}; }

std::uint64_t Backend::memory_budget() const noexcept {
    std::uint64_t usage = 0u, budget = 0u;
    return state_ != nullptr && mtl::Context::get().device_memory_budget(usage, budget) ? budget : 0u;
}

std::uint32_t Backend::make_image(ImageFormat format, std::uint32_t width, std::uint32_t height, const char* name,
                                  std::uint32_t depth) {
    if (state_ == nullptr || width == 0u || height == 0u || depth == 0u) {
        return kNoImage;
    }
    State& s = *state_;
    const MTLPixelFormat pixels = format_of(format);
    if (pixels == MTLPixelFormatInvalid) {
        return kNoImage;
    }
    MTLTextureDescriptor* descriptor = [MTLTextureDescriptor new];
    descriptor.textureType = depth > 1u ? MTLTextureType3D : MTLTextureType2D;
    descriptor.pixelFormat = pixels;
    descriptor.width = width;
    descriptor.height = height;
    descriptor.depth = depth;
    descriptor.mipmapLevelCount = 1u;
    descriptor.usage = MTLTextureUsageShaderRead | MTLTextureUsageShaderWrite;
    descriptor.storageMode = MTLStorageModePrivate;
    id<MTLTexture> made = [s.device newTextureWithDescriptor:descriptor];  // nil past the device's sizes or memory
    if (made == nil) {
        return kNoImage;
    }
    if (name != nullptr) {
        @autoreleasepool {
            made.label = [NSString stringWithUTF8String:name];
        }
    }
    auto image = std::make_shared<mtl::Texture>();
    image->format = static_cast<mtl::Format>(pixels);
    image->use = mtl::kUseSample | mtl::kUseWrite;
    image->width = width;
    image->height = height;
    image->storage = mtl::Storage::Private;
    image->allocation_bytes = made.allocatedSize;
    image->texture = (__bridge_retained void*)made;
    ++s.live_now.images;
    s.live_now.image_bytes += image->allocation_bytes;
    for (std::uint32_t i = 0; i < s.images.size(); ++i) {
        if (s.images[i] == nullptr) {
            s.images[i] = std::move(image);
            return i;
        }
    }
    s.images.push_back(std::move(image));
    return static_cast<std::uint32_t>(s.images.size() - 1u);
}

void Backend::free_image(std::uint32_t image) noexcept {
    if (state_ == nullptr || image >= state_->images.size() || state_->images[image] == nullptr) {
        return;
    }
    --state_->live_now.images;
    state_->live_now.image_bytes -= state_->images[image]->allocation_bytes;
    state_->images[image].reset();  // mtl::Texture's end: the texture goes once the GPU is past it
}

bool Backend::dispatch(RtShader shader, const PassBinds& binds, std::uint32_t groups_x, std::uint32_t groups_y) {
    if (state_ == nullptr) {
        return false;
    }
    State& s = *state_;
    const auto index = static_cast<unsigned>(shader);
    if (index >= kShaderCount || groups_x == 0u || groups_y == 0u || s.pipelines[index].state == nil) {
        return false;
    }
#if defined(GALAXY_RT_MSL)
    const Pipeline& pipeline = s.pipelines[index];
    if ((pipeline.buffers & (1u << mtl::kRtBufferTextureArray)) != 0u && s.table == nil) {
        return false;  // a pass that reads the texture table, in a begin that carried none
    }
    id<MTLAccelerationStructure> scene = s.structure_of(binds.scene);
    if ((pipeline.buffers & (1u << mtl::kRtBufferScene)) != 0u && scene == nil) {
        return false;  // nothing to trace: what it would write stays unwritten, as the caller is told
    }
    id<MTLComputeCommandEncoder> encoder = s.compute();
    if (encoder == nil) {
        return false;
    }
    if (!s.passes_resident) {
        // Resources reached only through another (the bottom-level structures through the scene, the textures through
        // the table) are made resident for the encoder: it cannot see through either.
        for (const Page& page : s.pages) {
            if (page.heap != nil) {
                [encoder useHeap:page.heap];
            }
        }
        if (s.table_resident != nullptr && *s.table_resident) {
            (*s.table_resident)((__bridge void*)encoder);
        }
        s.passes_resident = true;
    }
    [encoder setComputePipelineState:pipeline.state];
    std::uint32_t buffers_bound = 0u, textures_bound = 0u;
    const auto bind_buffer = [&](id<MTLBuffer> buffer, std::uint64_t offset, std::uint32_t at) {
        if (buffer != nil) {
            [encoder setBuffer:buffer offset:offset atIndex:at];
            buffers_bound |= 1u << at;
        }
    };
    const auto bind_view = [&](const BufferView& view, std::uint32_t at) {
        if (view.at != 0u) {
            const mtl::Context::BufferOffset place = mtl::Context::get().resolve(view.at);
            bind_buffer((__bridge id<MTLBuffer>)place.buffer, place.offset, at);
        }
    };
    const auto bind_texture = [&](id<MTLTexture> texture, std::uint32_t at) {
        if (texture != nil) {
            [encoder setTexture:texture atIndex:at];
            textures_bound |= 1u << at;
        }
    };
    if (scene != nil) {
        [encoder setAccelerationStructure:scene atBufferIndex:mtl::kRtBufferScene];
        buffers_bound |= 1u << mtl::kRtBufferScene;
    }
    for (std::uint32_t i = 0; i < kPassBuffers; ++i) {
        bind_view(binds.buffers[i], mtl::kRtBufferRenderer + i);
    }
    for (std::uint32_t i = 0; i < kPassTextures; ++i) {
        bind_texture((__bridge id<MTLTexture>)binds.textures[i].texture, mtl::kRtTextureRenderer + i);
    }
    for (std::uint32_t i = 0; i < kPassReads; ++i) {
        bind_texture(s.image_of(binds.reads[i]), mtl::kRtTextureRead + i);
    }
    for (std::uint32_t i = 0; i < kPassMeshBuffers; ++i) {
        bind_view(binds.mesh[i], mtl::kRtBufferMesh + i);
    }
    // u7 is the renderer's image where the pass names one (as on Direct3D 12 and Vulkan, it takes writes[7]'s place).
    for (std::uint32_t i = 0; i < kPassWrites; ++i) {
        if (i + 1u < kPassWrites || binds.write_texture.texture == nullptr) {
            bind_texture(s.image_of(binds.writes[i]), mtl::kRtTextureWritten + i);
        }
    }
    if (binds.write_texture.texture != nullptr) {
        bind_texture((__bridge id<MTLTexture>)binds.write_texture.texture, mtl::kRtTextureWritten + kPassWrites - 1u);
    }
    if (binds.counters) {
        bind_buffer(s.counters[s.frame_slot], 0u, mtl::kRtBufferCounters);
    }
    if (binds.write_buffer < s.buffers.size()) {
        bind_buffer(s.buffers[binds.write_buffer], 0u, mtl::kRtBufferWritten);
    }
    bind_view(binds.constants, mtl::kRtBufferFrame);
    bind_view(binds.pass_constants, mtl::kRtBufferPass);
    if ((pipeline.buffers & (1u << mtl::kRtBufferTextureArray)) != 0u) {
        bind_buffer(s.table, 0u, mtl::kRtBufferTextureArray);
    }
    // Every slot the shader declares and the pass left empty reads an object of its kind: zeros (Direct3D 12's null
    // descriptors, Vulkan's partially bound sets). The scene is never empty here (above).
    for (std::uint32_t at = 0; at < 32u; ++at) {
        const std::uint32_t bit = 1u << at;
        if ((pipeline.buffers & bit) != 0u && (buffers_bound & bit) == 0u && at != mtl::kRtBufferScene) {
            [encoder setBuffer:s.null_buffer offset:0u atIndex:at];
        }
        if ((pipeline.textures & bit) != 0u && (textures_bound & bit) == 0u) {
            if (id<MTLTexture> empty = s.null_texture(pipeline.texture_type[at], pipeline.texture_uint[at])) {
                [encoder setTexture:empty atIndex:at];
            }
        }
    }
    [encoder dispatchThreadgroups:MTLSizeMake(groups_x, groups_y, 1u)
            threadsPerThreadgroup:MTLSizeMake(kRtShaders[index].threads[0], kRtShaders[index].threads[1],
                                              kRtShaders[index].threads[2])];
    return true;
#else
    (void)binds;
    return false;
#endif
}

// Metal's buffers need no view: the pass binds the buffer at `at`'s own byte; offset as Vulkan's (a 256 multiple to
// start from) so the passes' byte arithmetic is the same on every back end.
BufferView Backend::view_of(gpu::Address at, std::uint64_t bytes, std::uint32_t& offset) const {
    const mtl::Context::BufferOffset place = mtl::Context::get().resolve(at);
    offset = static_cast<std::uint32_t>(place.offset % kRangeAlign);
    return {at - offset, bytes + offset};
}

// The renderer copies from it or samples it in its own encoders: the tracer's encoder ends first; Metal's hazard
// tracking orders the copy or the draws after the pass that wrote it.
void* Backend::image_to_copy(std::uint32_t image) {
    if (state_ == nullptr || image >= state_->images.size() || state_->images[image] == nullptr) {
        return nullptr;
    }
    state_->close();
    return state_->images[image].get();
}

void* Backend::image_to_sample(std::uint32_t image) {
    if (state_ == nullptr || image >= state_->images.size() || state_->images[image] == nullptr) {
        return nullptr;
    }
    state_->close();
    return state_->images[image].get();
}

// The counters lie in shared memory a frame slot (frame() keeps the slot's last frame's values and zeroes them).
void Backend::end_counters() {}

void Backend::read_counters(unsigned frame_slot, std::uint32_t out[kCounters]) const {
    for (unsigned i = 0; i < kCounters; ++i) {
        out[i] = state_ != nullptr ? state_->counted[frame_slot % kFramesInFlight][i] : 0u;
    }
}

void Backend::count_occluders(bool on) noexcept {
    if (state_ != nullptr) {
        state_->counter_words = on ? kCounterWords : kCounters;
    }
}

void Backend::read_occluders(unsigned frame_slot, std::uint32_t out[2u * kOccluderBins]) const {
    for (unsigned i = 0; i < 2u * kOccluderBins; ++i) {
        out[i] = state_ != nullptr ? state_->counted[frame_slot % kFramesInFlight][kCounters + i] : 0u;
    }
}

// No GPU times on Metal yet: Apple GPUs sample counters at stage boundaries only, not between two dispatches of one
// encoder (MTLCounterSamplingPoint); timed_ticks reports "the device has no timestamps".
void Backend::time_mark(unsigned, bool) {}
std::uint64_t Backend::timed_ticks(unsigned, unsigned) const { return 0u; }

#else  // without NEBULA_METAL_RAY_TRACING, or as C++: nothing starts

Backend::Backend() = default;
Backend::~Backend() = default;

bool Backend::start(gpu::Device*, void*) {
    std::fprintf(stderr, "[rt] metal: this build has no Metal ray tracing\n");
    return false;
}
void Backend::stop() noexcept {}
bool Backend::started() const noexcept { return false; }
bool Backend::failed() const noexcept { return false; }
BuildSizes Backend::sizes(const TriangleInput&, BuildKind) const { return {}; }
std::uint32_t Backend::make_page(std::uint64_t, bool) { return ~0u; }
void Backend::free_page(std::uint32_t) noexcept {}
Structure Backend::make_structure(const Range&, bool) { return {}; }
void Backend::free_structure(Structure&) noexcept {}
void Backend::frame(unsigned) noexcept {}
void Backend::begin(void*, unsigned, void*) noexcept {}
void Backend::end() noexcept {}
void Backend::build(const TriangleInput&, BuildKind, const Structure&, const Range&, bool) {}
BuildSizes Backend::scene_sizes(std::uint32_t) const { return {}; }
void Backend::build_scene(gpu::Address, std::uint32_t, const Structure&, const Range&) {}
void Backend::barrier() {}
void Backend::ask_compacted_size(const Structure&, std::uint32_t) {}
void Backend::end_size_asks() {}
std::uint64_t Backend::compacted_size(std::uint32_t, unsigned) const { return 0u; }
void Backend::compact(const Structure&, const Structure&) {}
std::uint32_t Backend::make_buffer(std::uint64_t, const char*) { return kNoBuffer; }
void Backend::free_buffer(std::uint32_t) noexcept {}
void Backend::buffer_for(std::uint32_t, bool) {}
Backend::Live Backend::live() const noexcept { return {}; }
std::uint64_t Backend::memory_budget() const noexcept { return 0u; }
std::uint32_t Backend::make_image(ImageFormat, std::uint32_t, std::uint32_t, const char*, std::uint32_t) { return kNoImage; }
void Backend::free_image(std::uint32_t) noexcept {}
bool Backend::dispatch(RtShader, const PassBinds&, std::uint32_t, std::uint32_t) { return false; }
BufferView Backend::view_of(gpu::Address, std::uint64_t, std::uint32_t& offset) const {
    offset = 0u;
    return {};
}
void* Backend::image_to_copy(std::uint32_t) { return nullptr; }
void* Backend::image_to_sample(std::uint32_t) { return nullptr; }
void Backend::end_counters() {}
void Backend::read_counters(unsigned, std::uint32_t out[kCounters]) const {
    for (unsigned i = 0; i < kCounters; ++i) {
        out[i] = 0u;
    }
}
void Backend::count_occluders(bool) noexcept {}
void Backend::read_occluders(unsigned, std::uint32_t out[2u * kOccluderBins]) const {
    for (unsigned i = 0; i < 2u * kOccluderBins; ++i) {
        out[i] = 0u;
    }
}
void Backend::time_mark(unsigned, bool) {}
std::uint64_t Backend::timed_ticks(unsigned, unsigned) const { return 0u; }

#endif

// The driver takes the tracer's shaders: the probe's pipeline is made from this build's MSL and let go. Logged.
bool shaders_load(gpu::Device*) {
#if defined(GALAXY_RT_METAL) && defined(GALAXY_RT_MSL)
    id<MTLDevice> device = (__bridge id<MTLDevice>)mtl::Context::get().device();
    if (device == nil) {
        return false;
    }
    if (@available(macOS 14.0, iOS 17.0, *)) {
        const char* why = nullptr;
        const Pipeline probe = make_pipeline(device, static_cast<unsigned>(RtShader::Probe), why);
        std::fprintf(stderr, probe.state != nil ? "[rt] shaders: loaded (metal, %zu bytes)\n" : "[rt] shaders: refused (metal, %zu bytes)\n",
                     kRtShaders[static_cast<unsigned>(RtShader::Probe)].size);
        return probe.state != nil;
    }
    return false;
#else
    std::fprintf(stderr, "[rt] shaders: none in this build\n");
    return false;
#endif
}

// Opacity micromaps (5.3 G-2): none on this back end (no Metal call for them is known here), so cutouts are asked at
// each hit, as on a device without them elsewhere.
bool Backend::micromaps() const noexcept { return false; }
BuildSizes Backend::micromap_sizes(const MicromapInput&) const { return {}; }
Structure Backend::make_micromap(const Range&) { return {}; }
void Backend::free_micromap(Structure& micromap) noexcept { micromap = Structure{}; }
void Backend::build_micromap(const MicromapInput&, const Structure&, const Range&) {}

}  // namespace rt
}  // namespace galaxy::render
