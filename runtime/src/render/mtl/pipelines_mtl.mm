// SPDX-License-Identifier: GPL-3.0-only
// core::Pipelines on Metal (core/pipelines.h; Vulkan's is core/pipelines_vk.cpp, which this follows state for state).
// A pipeline is a render pipeline state (the two programs, the vertex layout, the blends and the targets' formats),
// a depth and stencil state shared by every pipeline with the same one, and the raster state the encoder sets.
#include "render/core/pipelines.h"

#include "galaxy/render/mtl/draw_types.h"
#include "galaxy/render/mtl/mtl_context.h"
#include "galaxy/render/mtl/shader_msl.h"
#include "galaxy/render/program_table.h"
#include "galaxy/render/renderer_common.h"

#import <Foundation/Foundation.h>
#import <Metal/Metal.h>

#include <nebula_model.h>

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <unordered_map>
#include <vector>

namespace galaxy::render::core {
namespace {

// The vertex inputs' attribute numbers (shader_compile_vk.cpp semantic_location, kept by SPIRV-Cross): the fixed
// attributes, then the coordinates, as many as a program names.
enum Location : std::uint32_t {
    kPosition = 0u,
    kNormal = 1u,
    kTangent = 2u,
    kBinormal = 3u,
    kColor0 = 4u,
    kColor1 = 5u,
    kMatrix = 6u,
    kBefore = 7u,
    kWiden = 8u,
    kCoord0 = 9u,
};
constexpr std::uint32_t kNoLocation = ~0u;
constexpr std::uint32_t kMaxLocations = 31u;  // Metal's vertex attributes

MTLVertexFormat location_format(std::uint32_t location) noexcept {
    switch (location) {
    case kColor0:
    case kColor1: return MTLVertexFormatUChar4Normalized;
    case kMatrix:
    case kWiden: return MTLVertexFormatUInt;
    case kPosition:
    case kNormal:
    case kTangent:
    case kBinormal:
    case kBefore: return MTLVertexFormatFloat3;
    default: return MTLVertexFormatFloat2;
    }
}

MTLVertexFormat input_format(std::uint32_t format) noexcept {
    switch (format) {
    case 0: return MTLVertexFormatFloat3;
    case 1: return MTLVertexFormatFloat2;
    case 2: return MTLVertexFormatUChar4Normalized;
    default: return MTLVertexFormatUInt;
    }
}

std::uint32_t input_location(std::uint32_t semantic) noexcept {
    switch (semantic) {
    case NEBULA_INPUT_NORMAL: return kNormal;
    case NEBULA_INPUT_COLOR0: return kColor0;
    case NEBULA_INPUT_COLOR1: return kColor1;
    case NEBULA_INPUT_TANGENT: return kTangent;
    case NEBULA_INPUT_BINORMAL: return kBinormal;
    case NEBULA_INPUT_WIDEN: return kWiden;
    case NEBULA_INPUT_MATRIX: return kMatrix;
    default: return semantic >= NEBULA_INPUT_COORD0 ? kCoord0 + (semantic - NEBULA_INPUT_COORD0) : kNoLocation;
    }
}

// A program of a build made with Apple's compiler is not text: the programs' file holds, for each, the record naming its
// function in one of the app's two Metal libraries (platform/apple/tools/compile_metallib.py writes both).
struct ProgramRecord {
    char magic[8];             // "NBMTLFN1"
    std::uint32_t library;     // 0: programs_2_4.metallib, 1: programs_3_0.metallib
    std::uint32_t attributes;  // a vertex program's inputs, a bit each
    char name[24];             // the function, ended by a zero
};
static_assert(sizeof(ProgramRecord) == 40u);

bool record_of(const ProgramBytes& program, ProgramRecord& record) noexcept {
    if (program.size != sizeof(ProgramRecord) || std::memcmp(program.data, "NBMTLFN1", 8u) != 0) {
        return false;
    }
    std::memcpy(&record, program.data, sizeof(record));
    record.name[sizeof(record.name) - 1u] = '\0';
    return record.library < 2u;
}

// The attributes a vertex program reads (`[[attribute(n)]]` in its text), one flag for each up to the highest.
std::vector<bool> input_locations(const ProgramBytes& program) {
    if (ProgramRecord record; record_of(program, record)) {
        std::vector<bool> reads;
        for (std::uint32_t location = 0; location < kMaxLocations; ++location) {
            if ((record.attributes >> location & 1u) != 0u) {
                reads.resize(location + 1u, false);
                reads[location] = true;
            }
        }
        return reads;
    }
    static constexpr char kMark[] = "[[attribute(";
    std::vector<bool> reads;
    const char* const text = static_cast<const char*>(program.data);
    const char* const end = text + program.size;
    for (const char* at = text; (at = std::strstr(at, kMark)) != nullptr && at < end;) {
        at += sizeof(kMark) - 1u;
        const unsigned long location = std::strtoul(at, nullptr, 10);
        if (location < kMaxLocations) {
            if (location >= reads.size()) {
                reads.resize(location + 1u, false);
            }
            reads[location] = true;
        }
    }
    return reads;
}

MTLBlendFactor blend_factor(std::uint32_t f) noexcept {
    switch (f) {
    case NEBULA_FACTOR_ZERO: return MTLBlendFactorZero;
    case NEBULA_FACTOR_SRC_COLOR: return MTLBlendFactorSourceColor;
    case NEBULA_FACTOR_INV_SRC_COLOR: return MTLBlendFactorOneMinusSourceColor;
    case NEBULA_FACTOR_SRC_ALPHA: return MTLBlendFactorSourceAlpha;
    case NEBULA_FACTOR_INV_SRC_ALPHA: return MTLBlendFactorOneMinusSourceAlpha;
    case NEBULA_FACTOR_DST_ALPHA: return MTLBlendFactorDestinationAlpha;
    case NEBULA_FACTOR_INV_DST_ALPHA: return MTLBlendFactorOneMinusDestinationAlpha;
    case NEBULA_FACTOR_DST_COLOR: return MTLBlendFactorDestinationColor;
    case NEBULA_FACTOR_INV_DST_COLOR: return MTLBlendFactorOneMinusDestinationColor;
    default: return MTLBlendFactorOne;
    }
}

// The alpha channel's factor for a colour factor: the alpha of what the colour factor names.
MTLBlendFactor alpha_factor(MTLBlendFactor factor) noexcept {
    switch (factor) {
    case MTLBlendFactorSourceColor: return MTLBlendFactorSourceAlpha;
    case MTLBlendFactorOneMinusSourceColor: return MTLBlendFactorOneMinusSourceAlpha;
    case MTLBlendFactorDestinationColor: return MTLBlendFactorDestinationAlpha;
    case MTLBlendFactorOneMinusDestinationColor: return MTLBlendFactorOneMinusDestinationAlpha;
    default: return factor;
    }
}

MTLBlendFactor no_destination_alpha(MTLBlendFactor factor) noexcept {
    return factor == MTLBlendFactorDestinationAlpha             ? MTLBlendFactorOne
           : factor == MTLBlendFactorOneMinusDestinationAlpha   ? MTLBlendFactorZero
                                                                : factor;
}

// One target's blend, as pipelines_vk.cpp fills a VkPipelineColorBlendAttachmentState.
struct Blend {
    bool enabled = false;
    MTLBlendFactor source = MTLBlendFactorOne, destination = MTLBlendFactorZero;
    MTLBlendOperation operation = MTLBlendOperationAdd;
    MTLBlendFactor source_alpha = MTLBlendFactorOne, destination_alpha = MTLBlendFactorZero;
    MTLBlendOperation alpha_operation = MTLBlendOperationAdd;
    MTLColorWriteMask writes = MTLColorWriteMaskNone;
};

void apply(MTLRenderPipelineColorAttachmentDescriptor* attachment, const Blend& blend, MTLPixelFormat format) {
    attachment.pixelFormat = format;
    attachment.writeMask = blend.writes;
    attachment.blendingEnabled = blend.enabled;
    if (blend.enabled) {
        attachment.sourceRGBBlendFactor = blend.source;
        attachment.destinationRGBBlendFactor = blend.destination;
        attachment.rgbBlendOperation = blend.operation;
        attachment.sourceAlphaBlendFactor = blend.source_alpha;
        attachment.destinationAlphaBlendFactor = blend.destination_alpha;
        attachment.alphaBlendOperation = blend.alpha_operation;
    }
}

// The one device's compiled programs and depth states, shared by the building threads.
struct Programs {
    std::mutex lock;
    std::unordered_map<const void*, mtl::Object> functions;     // by a program's text: id<MTLFunction>
    std::unordered_map<std::uint32_t, mtl::Object> depth_states;  // by the state word's depth and stencil bits
    std::atomic<unsigned> failures_told{0u};
};
Programs gPrograms;

// What the compiling has cost so far (microseconds, whichever thread paid), for the [metal-gpu] trace.
std::atomic<std::uint64_t> gFunctionCount{0u}, gFunctionUs{0u}, gPipelineCount{0u}, gPipelineUs{0u}, gFunctionBytes{0u};

std::uint64_t now_us() noexcept {
    return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count());
}

id<MTLDevice> metal_device() noexcept {
    return (__bridge id<MTLDevice>)mtl::Context::get().device();
}

// The app's Metal libraries, opened at the first program asked of each: in the app's Resources, or beside a program
// that is not in an app (a development build). Called with gPrograms.lock held.
id<MTLLibrary> program_library(std::uint32_t index) {
    static id<MTLLibrary> libraries[2];
    static bool tried[2];
    if (tried[index]) {
        return libraries[index];
    }
    tried[index] = true;
    @autoreleasepool {
        NSString* const name = index == 0u ? @"programs_2_4" : @"programs_3_0";
        NSURL* url = [NSBundle.mainBundle URLForResource:name withExtension:@"metallib"];
        if (url == nil) {
            url = [[NSBundle.mainBundle.executableURL URLByDeletingLastPathComponent]
                URLByAppendingPathComponent:[name stringByAppendingPathExtension:@"metallib"]];
        }
        NSError* error = nil;
        libraries[index] = url != nil ? [metal_device() newLibraryWithURL:url error:&error] : nil;
        if (libraries[index] == nil) {
            std::fprintf(stderr, "[pipelines] %s.metallib is not read: %s\n", name.UTF8String,
                         error != nil ? error.localizedDescription.UTF8String : "it is not with the app");
        }
    }
    return libraries[index];
}

// The function of a program's text. Compiled outside the lock: the builds of other programs go on meanwhile; of two
// threads that compile the same text one result is kept.
mtl::Object function_of(const ProgramBytes& program, bool pixel) {
    {
        const std::lock_guard<std::mutex> guard(gPrograms.lock);
        if (const auto it = gPrograms.functions.find(program.data); it != gPrograms.functions.end()) {
            return it->second;
        }
    }
    if (ProgramRecord record; record_of(program, record)) {
        // Compiled when the app was built: the function is there to take. Taken outside the lock (a library makes
        // functions on any thread), as a text is compiled; of two threads that take the same one, one result is kept.
        id<MTLLibrary> library = nil;
        {
            const std::lock_guard<std::mutex> guard(gPrograms.lock);
            library = program_library(record.library);
        }
        @autoreleasepool {
            const std::uint64_t began = now_us();
            id<MTLFunction> const function = [library newFunctionWithName:@(record.name)];
            gFunctionUs.fetch_add(now_us() - began, std::memory_order_relaxed);
            gFunctionCount.fetch_add(1u, std::memory_order_relaxed);
            if (function == nil && gPrograms.failures_told.fetch_add(1u) < 8u) {
                std::fprintf(stderr, "[pipelines] the app's Metal library %u has no function %s\n", record.library, record.name);
            }
            const std::lock_guard<std::mutex> guard(gPrograms.lock);
            const auto [at, inserted] = gPrograms.functions.emplace(program.data, nullptr);
            if (inserted && function != nil) {
                at->second = (__bridge_retained void*)function;
            }
            return at->second;
        }
    }
    @autoreleasepool {
        MTLCompileOptions* const options = [MTLCompileOptions new];
        // A pixel program's array form reads arrays without a length (MSL 3.0); every other text is 2.4.
        options.languageVersion = MTLLanguageVersion2_4;
        if (pixel && !program_bound_tables()) {
            if (@available(macOS 13.0, iOS 16.0, *)) {
                options.languageVersion = MTLLanguageVersion3_0;
            }
        }
        // The arithmetic the programs state, as the other renderers' compilers keep it.
        options.fastMathEnabled = NO;
        NSString* source = [[NSString alloc] initWithBytes:program.data length:program.size encoding:NSUTF8StringEncoding];
        NSError* error = nil;
        const std::uint64_t began = now_us();
        id<MTLLibrary> const library = source != nil ? [metal_device() newLibraryWithSource:source options:options error:&error] : nil;
        gFunctionUs.fetch_add(now_us() - began, std::memory_order_relaxed);
        gFunctionCount.fetch_add(1u, std::memory_order_relaxed);
        gFunctionBytes.fetch_add(program.size, std::memory_order_relaxed);
        id<MTLFunction> const function = library != nil ? [library newFunctionWithName:@"main0"] : nil;
        if (function == nil) {
            if (gPrograms.failures_told.fetch_add(1u) < 8u) {
                std::fprintf(stderr, "[pipelines] a %s program does not compile: %s\n", pixel ? "pixel" : "vertex",
                             error != nil ? error.localizedDescription.UTF8String : "no function main0");
            }
            return nullptr;
        }
        const std::lock_guard<std::mutex> guard(gPrograms.lock);
        const auto [at, inserted] = gPrograms.functions.emplace(program.data, nullptr);
        if (inserted) {
            at->second = (__bridge_retained void*)function;
        }
        return at->second;
    }
}

// Depth is reversed: the game's less is greater.
constexpr MTLCompareFunction kDepth[8] = {
    MTLCompareFunctionNever, MTLCompareFunctionGreater,  MTLCompareFunctionEqual,     MTLCompareFunctionGreaterEqual,
    MTLCompareFunctionLess,  MTLCompareFunctionNotEqual, MTLCompareFunctionLessEqual, MTLCompareFunctionAlways,
};

// The depth and stencil state of a state word: bits 10 to 14 (test, function, write) and 20 to 23 (stencil).
mtl::Object depth_state_of(std::uint32_t state) {
    constexpr std::uint32_t kDepthBits = 0x1Fu << 10u, kStencilBits = 0xFu << 20u;
    const bool test = (state & (1u << 10u)) != 0u;
    // Without the test nothing of the function or the write counts.
    const std::uint32_t key = (test ? state & kDepthBits : 0u) | (state & kStencilBits);
    const std::lock_guard<std::mutex> guard(gPrograms.lock);
    if (const auto it = gPrograms.depth_states.find(key); it != gPrograms.depth_states.end()) {
        return it->second;
    }
    @autoreleasepool {
        MTLDepthStencilDescriptor* const descriptor = [MTLDepthStencilDescriptor new];
        descriptor.depthCompareFunction = test ? kDepth[(state >> 11u) & 7u] : MTLCompareFunctionAlways;
        descriptor.depthWriteEnabled = test && (state & (1u << 14u)) != 0u;
        if (const std::uint32_t stencil = (state >> 20u) & 3u; stencil != 0u) {
            static constexpr MTLCompareFunction kStencilTest[4] = {MTLCompareFunctionAlways, MTLCompareFunctionAlways,
                                                                  MTLCompareFunctionEqual, MTLCompareFunctionNotEqual};
            static constexpr MTLStencilOperation kStencilOp[4] = {MTLStencilOperationKeep, MTLStencilOperationIncrementClamp,
                                                                  MTLStencilOperationDecrementClamp, MTLStencilOperationZero};
            MTLStencilDescriptor* const face = [MTLStencilDescriptor new];
            face.stencilFailureOperation = MTLStencilOperationKeep;
            face.depthFailureOperation = MTLStencilOperationKeep;
            face.depthStencilPassOperation = kStencilOp[(state >> 22u) & 3u];
            face.stencilCompareFunction = kStencilTest[stencil];
            face.readMask = 0xFFu;
            face.writeMask = 0xFFu;
            descriptor.frontFaceStencil = face;
            descriptor.backFaceStencil = face;
        }
        id<MTLDepthStencilState> const made = [metal_device() newDepthStencilStateWithDescriptor:descriptor];
        const mtl::Object held = made != nil ? (__bridge_retained void*)made : nullptr;
        gPrograms.depth_states.emplace(key, held);
        return held;
    }
}

}  // namespace

}  // namespace galaxy::render::core

namespace galaxy::render::mtl {

void pipeline_compile_stats(std::uint64_t out[5]) noexcept {
    out[0] = core::gFunctionCount.load(std::memory_order_relaxed);
    out[1] = core::gFunctionUs.load(std::memory_order_relaxed);
    out[2] = core::gPipelineCount.load(std::memory_order_relaxed);
    out[3] = core::gPipelineUs.load(std::memory_order_relaxed);
    out[4] = core::gFunctionBytes.load(std::memory_order_relaxed);
}

}  // namespace galaxy::render::mtl

namespace galaxy::render::core {

gpu::Pipeline* Pipelines::build_fast(const PipelineEntry&, unsigned, unsigned) {
    return nullptr;  // Metal links nothing from parts: a draw that waits builds the whole pipeline
}

gpu::Pipeline* Pipelines::build(const PipelineEntry& entry, unsigned variant, unsigned aux) {
    const NebulaPipelineDesc& desc = entry.desc;
    const auto* layout = static_cast<const mtl::RenderLayout*>(root_);
    const unsigned samples_log2 = variant & kSceneVariantSamplesMask;
    const unsigned program = program_variant(desc.pixel, aux, (desc.state & NEBULA_STATE_PIXEL_SHADING) != 0u ? 0u : samples_log2);
    const ProgramBytes vs = program_bytes(desc.vertex, program, false);
    const ProgramBytes ps = program_bytes(desc.pixel, program, true);
    if (vs.size == 0u || ps.size == 0u) {
        std::fprintf(stderr, "[pipelines] no program %llx %llx variant %u\n", static_cast<unsigned long long>(desc.vertex),
                     static_cast<unsigned long long>(desc.pixel), program);
        return nullptr;
    }
    if (aux == 3u && !layout->layer_targets) {
        return nullptr;  // the device cannot draw the effects layer: the walker misses this draw
    }
    const mtl::Object vertex_function = function_of(vs, false);
    const mtl::Object pixel_function = function_of(ps, true);
    if (vertex_function == nullptr || pixel_function == nullptr) {
        return nullptr;
    }
    @autoreleasepool {
        MTLRenderPipelineDescriptor* const descriptor = [MTLRenderPipelineDescriptor new];
        descriptor.vertexFunction = (__bridge id<MTLFunction>)vertex_function;
        descriptor.fragmentFunction = (__bridge id<MTLFunction>)pixel_function;

        // What the vertex program reads; each from the list if named there, else the stream of zeros. A stride belongs to
        // a pipeline here, so the description's input strides decide the buffers' layouts.
        const std::vector<bool> reads = input_locations(vs);
        std::vector<bool> placed(reads.size(), false);
        MTLVertexDescriptor* const vertices = [MTLVertexDescriptor vertexDescriptor];
        const auto add = [&](std::uint32_t location, MTLVertexFormat format, std::uint32_t slot, std::uint32_t stride,
                             std::uint32_t offset) {
            if (location >= reads.size() || !reads[location] || placed[location]) {
                return;
            }
            placed[location] = true;
            const NSUInteger buffer = mtl::kBufferStream0 + slot;
            vertices.attributes[location].format = format;
            vertices.attributes[location].offset = offset;
            vertices.attributes[location].bufferIndex = buffer;
            if (vertices.layouts[buffer].stride == 0u) {
                if (stride != 0u) {
                    vertices.layouts[buffer].stride = stride;
                    vertices.layouts[buffer].stepFunction = MTLVertexStepFunctionPerVertex;
                    vertices.layouts[buffer].stepRate = 1u;
                } else {
                    // The stream of zeros: one value for every vertex.
                    vertices.layouts[buffer].stride = 16u;
                    vertices.layouts[buffer].stepFunction = MTLVertexStepFunctionConstant;
                    vertices.layouts[buffer].stepRate = 0u;
                }
            }
        };
        const std::uint32_t position_stride = desc.position_stride != 0u ? desc.position_stride : 12u;
        add(kPosition, MTLVertexFormatFloat3, gpu::kStreamPosition, position_stride, 0u);
        if (desc.position_stride == 0u) {
            add(kMatrix, MTLVertexFormatUInt, gpu::kStreamMatrix, 4u, 0u);
        }
        if (aux == 1u) {
            add(kBefore, MTLVertexFormatFloat3, gpu::kStreamBefore, position_stride, 0u);
        }
        for (const NebulaLayoutInput& input : entry.inputs) {
            const bool zero = input.stream == NEBULA_STREAM_ZERO;
            const std::uint32_t slot = zero ? std::uint32_t{gpu::kStreamZero} : std::uint32_t{input.stream};
            if (slot >= gpu::kStreamCount) {
                continue;  // no such buffer slot: the input reads zero like one a program names without a list entry
            }
            add(input_location(input.semantic), input_format(input.format), slot, zero ? 0u : input.stride, input.offset);
        }
        for (std::uint32_t location = 0; location < reads.size(); ++location) {
            add(location, location_format(location), gpu::kStreamZero, 0u, 0u);
        }
        descriptor.vertexDescriptor = vertices;
        descriptor.inputPrimitiveTopology = MTLPrimitiveTopologyClassTriangle;
        descriptor.rasterSampleCount = 1u << samples_log2;

        const std::uint32_t state = desc.state;
        const bool target = (variant & kSceneVariantTarget) != 0u;
        Blend color;
        color.writes = ((state & NEBULA_STATE_COLOR_WRITE) != 0u
                            ? MTLColorWriteMaskRed | MTLColorWriteMaskGreen | MTLColorWriteMaskBlue
                            : MTLColorWriteMaskNone) |
                       ((state & (NEBULA_STATE_ALPHA_WRITE | NEBULA_STATE_ALPHA_PASS)) != 0u && target ? MTLColorWriteMaskAlpha
                                                                                                        : MTLColorWriteMaskNone);
        const std::uint32_t op = state & 3u;
        if (op != NEBULA_BLEND_REPLACE) {
            color.enabled = true;
            if (op == NEBULA_BLEND_SUBTRACT) {
                color.source = MTLBlendFactorOne;
                color.destination = MTLBlendFactorOne;
                color.operation = MTLBlendOperationReverseSubtract;
            } else {
                color.source = blend_factor((state >> 2u) & 15u);
                color.destination = blend_factor((state >> 6u) & 15u);
                color.operation = MTLBlendOperationAdd;
                if (!target) {
                    // No destination alpha in the scene: it reads as one.
                    color.source = no_destination_alpha(color.source);
                    color.destination = no_destination_alpha(color.destination);
                }
            }
            color.source_alpha = alpha_factor(color.source);
            color.destination_alpha = alpha_factor(color.destination);
            color.alpha_operation = color.operation;
        }
        const mtl::Format color_format = target                                  ? layout->target_color
                                         : (variant & kSceneVariantHdr) != 0u ? layout->wide_color
                                                                                : layout->scene_color;
        apply(descriptor.colorAttachments[0], color, static_cast<MTLPixelFormat>(color_format));
        // Aux 4: the motion pass's second target is there and left as it is.
        if (aux == 1u || aux == 4u) {
            Blend motion;
            motion.writes = aux == 1u ? MTLColorWriteMaskRed | MTLColorWriteMaskGreen : MTLColorWriteMaskNone;
            apply(descriptor.colorAttachments[1], motion, static_cast<MTLPixelFormat>(layout->motion));
        } else if (aux == 2u || aux == 3u) {
            Blend reactive;
            reactive.enabled = true;
            reactive.source = reactive.destination = MTLBlendFactorOne;
            reactive.operation = MTLBlendOperationMax;
            reactive.source_alpha = reactive.destination_alpha = MTLBlendFactorOne;
            reactive.alpha_operation = MTLBlendOperationMax;
            reactive.writes = MTLColorWriteMaskRed;
            apply(descriptor.colorAttachments[1], reactive, static_cast<MTLPixelFormat>(layout->reactive));
        }
        if (aux == 3u) {
            // The effects layer: render target 2 takes the colour as the scene target blends it, 3 the product of the
            // destination factors (nothing from the source: it keeps what the destination factor lets through). Colour only.
            constexpr MTLColorWriteMask kRgb = MTLColorWriteMaskRed | MTLColorWriteMaskGreen | MTLColorWriteMaskBlue;
            Blend layer = color;
            layer.writes = kRgb;
            apply(descriptor.colorAttachments[2], layer, static_cast<MTLPixelFormat>(layout->layer));
            Blend through = layer;
            through.source = MTLBlendFactorZero;
            through.operation = MTLBlendOperationAdd;
            through.destination = color.operation == MTLBlendOperationAdd ? color.destination : MTLBlendFactorOne;
            through.source_alpha = alpha_factor(through.source);
            through.destination_alpha = alpha_factor(through.destination);
            through.alpha_operation = MTLBlendOperationAdd;
            apply(descriptor.colorAttachments[3], through, static_cast<MTLPixelFormat>(layout->layer));
        }
        descriptor.depthAttachmentPixelFormat = static_cast<MTLPixelFormat>(layout->depth);
        if (layout->depth == mtl::Format::Depth32fStencil8) {
            descriptor.stencilAttachmentPixelFormat = static_cast<MTLPixelFormat>(layout->depth);
        }

        NSError* error = nil;
        const std::uint64_t began = now_us();
        id<MTLRenderPipelineState> const made = [metal_device() newRenderPipelineStateWithDescriptor:descriptor error:&error];
        gPipelineUs.fetch_add(now_us() - began, std::memory_order_relaxed);
        gPipelineCount.fetch_add(1u, std::memory_order_relaxed);
        if (made == nil) {
            std::fprintf(stderr, "[pipelines] create failed: %llx %llx state %08x variant %u aux %u: %s\n",
                         static_cast<unsigned long long>(desc.vertex), static_cast<unsigned long long>(desc.pixel), state, variant,
                         aux, error != nil ? error.localizedDescription.UTF8String : "no reason given");
            return nullptr;
        }
        auto* const pipeline = new gpu::Pipeline();
        pipeline->state = (__bridge_retained void*)made;
        pipeline->depth_stencil = depth_state_of(state);
        pipeline->primitive = static_cast<std::uint8_t>(MTLPrimitiveTypeTriangle);
        // Direct3D's winding, which the programs' clip space is written for: clockwise is front. Culling all has no state:
        // the walker draws nothing.
        pipeline->winding = static_cast<std::uint8_t>(MTLWindingClockwise);
        switch ((state >> 15u) & 3u) {
        case NEBULA_CULL_BACK: pipeline->cull = static_cast<std::uint8_t>(MTLCullModeBack); break;
        case NEBULA_CULL_FRONT: pipeline->cull = static_cast<std::uint8_t>(MTLCullModeFront); break;
        default: pipeline->cull = static_cast<std::uint8_t>(MTLCullModeNone); break;
        }
        pipeline->depth_clip = static_cast<std::uint8_t>(MTLDepthClipModeClip);
        pipeline->fill = static_cast<std::uint8_t>(MTLTriangleFillModeFill);
        const std::lock_guard<std::mutex> guard(lock_);
        owned_.push_back(pipeline);
        return pipeline;
    }
}

void Pipelines::destroy_all() noexcept {
    // The frames in flight keep the states they drew with: Metal's command buffers hold them.
    for (gpu::Pipeline* pipeline : owned_) {
        mtl::release(pipeline->state);
        delete pipeline;
    }
    owned_.clear();
    const std::lock_guard<std::mutex> guard(gPrograms.lock);
    for (const auto& [text, function] : gPrograms.functions) {
        mtl::release(function);
    }
    gPrograms.functions.clear();
    for (const auto& [key, state] : gPrograms.depth_states) {
        mtl::release(state);
    }
    gPrograms.depth_states.clear();
    gPrograms.failures_told.store(0u);
}

std::size_t Pipelines::save_cache() noexcept {
    // Nothing of the renderer's own to write: Metal keeps every program and pipeline it compiles in the system's cache
    // for this app, on disk, by program, state, GPU and system build, and a later start makes each from there (the whole
    // list in about a second on a 2017 MacBook Pro, against a minute of compiling the first time). An archive of the
    // app's own (MTLBinaryArchive) holds the same code under the same conditions and was tried in its place: on an AMD
    // GPU, pipelines that share programs were refused once the file was read back ("Failed to find linking data"), and
    // threads adding to it together lost entries.
    return 0u;
}

// No file of the renderer's own, so nothing says what the system's cache holds: every texture target form is prepared.
bool Pipelines::cache_file_state(std::filesystem::path&, std::uint64_t&) {
    return false;
}

std::uint64_t Pipelines::cache_file_hash() noexcept {
    return 0u;
}

}  // namespace galaxy::render::core
