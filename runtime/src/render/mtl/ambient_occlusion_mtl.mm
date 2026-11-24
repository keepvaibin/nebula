// SPDX-License-Identifier: GPL-3.0-only
// Ambient occlusion on Metal (mtl/ambient_occlusion_mtl.h), pass for pass vk/ambient_occlusion_vk.cpp's.
#import <Metal/Metal.h>

#include "galaxy/render/mtl/ambient_occlusion_mtl.h"

#include "galaxy/render/ambient_occlusion_shape.h"
#include "galaxy/render/mtl/utility_msl.h"
#include "galaxy/render/idle_clock.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <iostream>

namespace galaxy::render::mtl {

namespace {

constexpr Format kDistanceFormat = Format::R32f;
// Occlusion and eye distance: the two channels the passes use (a shader write keeps x and y).
constexpr Format kOcclusionFormat = Format::Rg16f;
// The denoised occlusion at one occlusion pixel per scene target pixel: the multiply reads its occlusion alone.
constexpr Format kDenoisedFullFormat = Format::R16f;
// The multiplier: one channel (a shader write takes R of the value whatever the format).
constexpr Format kScaleFormat = Format::R8;
// ambient_occlusion.hlsl's registers (vk/utility_spirv_sources.h kAoBindings): t0-t2 at textures 0-2, u0 at 3, the
// models at buffer 4 and their tiles at 5, the constants pushed.
constexpr NSUInteger kObjectsBuffer = 4u;
constexpr NSUInteger kTilesBuffer = 5u;
constexpr UtilityShader kShaders[] = {UtilityShader::AoDistanceCs, UtilityShader::AoOcclusionCs, UtilityShader::AoDenoiseCs,
                                      UtilityShader::AoUpsampleCs, UtilityShader::AoSsaoCs,      UtilityShader::AoApplyVs,
                                      UtilityShader::AoApplyPs,    UtilityShader::AoApplyFusedPs};
// kShaders' graphics functions.
constexpr unsigned kApplyVs = 5u, kApplyPs = 6u, kApplyFusedPs = 7u;
static_assert(std::size(kShaders) == kApplyFusedPs + 1u);

[[nodiscard]] id<MTLDevice> device() noexcept {
    return (__bridge id<MTLDevice>)Context::get().device();
}
[[nodiscard]] id<MTLTexture> metal(const Texture& texture) noexcept {
    return (__bridge id<MTLTexture>)texture.texture;
}
[[nodiscard]] Object own(id object) noexcept {
    return (__bridge_retained void*)object;
}

}  // namespace

Object AmbientOcclusion::function(unsigned index) {
    if (functions_[index] != nullptr) {
        return functions_[index];
    }
    // From utility.metallib (utility_msl.h renderer_function).
    functions_[index] = renderer_function(utility_msl(kShaders[index]));
    if (functions_[index] == nullptr) {
        std::fprintf(stderr, "[ambient-occlusion] program %u could not be made\n", index);
    }
    return functions_[index];
}

bool AmbientOcclusion::initialize(Format depth_format) {
    shutdown();
    depth_format_ = depth_format;
    @autoreleasepool {
        static constexpr const char* kNames[kPassCount] = {"DepthCS", "AoCS", "DenoiseCS", "UpsampleCS", "SsaoCS"};
        for (unsigned i = 0; i < kPassCount; ++i) {
            const Object compiled = function(i);
            NSError* error = nil;
            id<MTLComputePipelineState> const state =
                compiled != nullptr ? [device() newComputePipelineStateWithFunction:(__bridge id<MTLFunction>)compiled error:&error] : nil;
            if (state == nil) {
                std::cerr << "[ambient-occlusion] Metal pass " << kNames[i] << " failed\n";
                shutdown();
                return false;
            }
            compute_[i] = own(state);
        }
        MTLDepthStencilDescriptor* const depth = [MTLDepthStencilDescriptor new];
        depth.depthCompareFunction = MTLCompareFunctionAlways;
        depth.depthWriteEnabled = NO;
        no_depth_ = own([device() newDepthStencilStateWithDescriptor:depth]);
    }
    ready_ = true;
    return true;
}

void AmbientOcclusion::shutdown() noexcept {
    ready_ = false;
    distance_.reset();
    raw_.reset();
    denoised_.reset();
    scale_.reset();
    for (Object& pipeline : compute_) {
        release(pipeline);
        pipeline = nullptr;
    }
    for (Apply& apply : apply_) {
        release(apply.pipeline);
        apply = Apply{};
    }
    for (Object& compiled : functions_) {
        release(compiled);
        compiled = nullptr;
    }
    release(no_depth_);
    no_depth_ = nullptr;
}

// Both forms of the multiply for each scene pass, made at the start so finish never makes one in a frame.
void AmbientOcclusion::prepare_pipelines(std::span<const ScenePassFormat> passes) {
    for (const ScenePassFormat& pass : passes) {
        for (const bool fused : {false, true}) {
            static_cast<void>(apply_pipeline(pass.color, pass.samples, fused));
        }
    }
}

// The multiply: the shader's output times the scene target's colour (RGB; the game's alpha stays), no depth.
Object AmbientOcclusion::apply_pipeline(Format color_format, unsigned samples, bool fused) {
    Apply* free_slot = nullptr;
    for (Apply& apply : apply_) {
        if (apply.pipeline != nullptr && apply.color == color_format && apply.samples == samples && apply.fused == fused) {
            return apply.pipeline;
        }
        if (apply.pipeline == nullptr && free_slot == nullptr) {
            free_slot = &apply;
        }
    }
    const Object vs = function(kApplyVs);
    const Object ps = function(fused ? kApplyFusedPs : kApplyPs);
    if (free_slot == nullptr || vs == nullptr || ps == nullptr) {
        return nullptr;
    }
    @autoreleasepool {
        MTLRenderPipelineDescriptor* const descriptor = [MTLRenderPipelineDescriptor new];
        descriptor.vertexFunction = (__bridge id<MTLFunction>)vs;
        descriptor.fragmentFunction = (__bridge id<MTLFunction>)ps;
        MTLRenderPipelineColorAttachmentDescriptor* const target = descriptor.colorAttachments[0];
        target.pixelFormat = static_cast<MTLPixelFormat>(color_format);
        target.blendingEnabled = YES;
        target.sourceRGBBlendFactor = MTLBlendFactorDestinationColor;
        target.destinationRGBBlendFactor = MTLBlendFactorZero;
        target.rgbBlendOperation = MTLBlendOperationAdd;
        target.sourceAlphaBlendFactor = MTLBlendFactorZero;
        target.destinationAlphaBlendFactor = MTLBlendFactorOne;
        target.alphaBlendOperation = MTLBlendOperationAdd;
        target.writeMask = MTLColorWriteMaskRed | MTLColorWriteMaskGreen | MTLColorWriteMaskBlue;
        descriptor.depthAttachmentPixelFormat = static_cast<MTLPixelFormat>(depth_format_);
        descriptor.stencilAttachmentPixelFormat = static_cast<MTLPixelFormat>(depth_format_);
        descriptor.rasterSampleCount = samples;
        NSError* error = nil;
        id<MTLRenderPipelineState> const state = [device() newRenderPipelineStateWithDescriptor:descriptor error:&error];
        if (state == nil) {
            std::fprintf(stderr, "[ambient-occlusion] the multiply's pipeline: %s\n",
                         error != nil ? error.localizedDescription.UTF8String : "");
            return nullptr;
        }
        *free_slot = Apply{color_format, samples, fused, own(state)};
    }
    return free_slot->pipeline;
}

bool AmbientOcclusion::ensure(std::shared_ptr<Texture>& target, unsigned width, unsigned height, Format format, const char* name) {
    if (target && target->width == width && target->height == height && target->format == format) {
        return true;
    }
    target = Context::get().create_texture(width, height, 1u, format, kUseWrite | kUseSample, Storage::Private, name);
    return target != nullptr;
}

void AmbientOcclusion::trim_idle() {
    if (idle_since(used_) && (distance_ || raw_ || denoised_ || scale_)) {
        distance_.reset();
        raw_.reset();
        denoised_.reset();
        scale_.reset();
    }
}

void AmbientOcclusion::dispatch(Object compute_encoder, Pass pass, const Texture& depth, const Texture& sampled1,
                                const Texture& sampled2, const Texture& storage, unsigned width, unsigned height) const {
    id<MTLComputeCommandEncoder> const compute = (__bridge id<MTLComputeCommandEncoder>)compute_encoder;
    [compute setComputePipelineState:(__bridge id<MTLComputePipelineState>)compute_[pass]];
    [compute setTexture:metal(depth) atIndex:0];
    [compute setTexture:metal(sampled1) atIndex:1];
    [compute setTexture:metal(sampled2) atIndex:2];
    [compute setTexture:metal(storage) atIndex:3];
    [compute setBuffer:(__bridge id<MTLBuffer>)objects_buffer_ offset:objects_offset_ atIndex:kObjectsBuffer];
    [compute setBuffer:(__bridge id<MTLBuffer>)tiles_buffer_ offset:tiles_offset_ atIndex:kTilesBuffer];
    [compute setBytes:&constants_ length:sizeof(constants_) atIndex:kUtilityPushBuffer];
    [compute dispatchThreadgroups:MTLSizeMake((width + 7u) / 8u, (height + 7u) / 8u, 1) threadsPerThreadgroup:MTLSizeMake(8, 8, 1)];
}

bool AmbientOcclusion::prepare(Object commands, const Texture& depth, unsigned part_width, unsigned part_height,
                               unsigned lowest_sixteenths, const SceneView& view, bool ssao,
                               std::span<const ao::ObjectOccluder> objects, UploadRing& ring, const StageMarker& mark) {
    if (!ready_) {
        return false;
    }
    used_ = idle_clock();
    const unsigned scene_width = std::min(part_width != 0u ? part_width : depth.width, depth.width);
    const unsigned scene_height = std::min(part_height != 0u ? part_height : depth.height, depth.height);
    const unsigned divisor = ao::occlusion_divisor(scene_height);
    const unsigned width = (scene_width + divisor - 1u) / divisor;
    const unsigned height = (scene_height + divisor - 1u) / divisor;
    fused_ = divisor <= 1u;
    target_width_ = scene_width;
    target_height_ = scene_height;
    // The targets at the field's largest size over every step the scene may take, so a step change makes none again.
    // Where every step's field is the scene's own pixels (the whole depth at one occlusion pixel per scene pixel) the
    // denoised field is one channel and no multiplier is made; else two channels (a divisor-1 step's multiply reads the
    // first alone) and the multiplier at the depth's size, kept through divisor-1 steps.
    const bool whole_fused = ao::occlusion_divisor(depth.height) <= 1u;
    const ao::Field field = ao::largest_field(depth.width, depth.height, lowest_sixteenths);
    if (whole_fused) {
        scale_.reset();  // a command buffer that names it keeps it
    }
    if (!ao::make_constants(view, width, height, divisor, ssao, constants_) ||
        !ensure(distance_, field.width, field.height, kDistanceFormat, "occlusion distance") ||
        !ensure(raw_, field.width, field.height, kOcclusionFormat, "occlusion") ||
        !ensure(denoised_, field.width, field.height, whole_fused ? kDenoisedFullFormat : kOcclusionFormat,
                "occlusion denoised") ||
        (!whole_fused && !ensure(scale_, depth.width, depth.height, kScaleFormat, "occlusion scale"))) {
        return false;
    }
    ao::bin_objects(objects, constants_, object_values_, object_tiles_, object_scratch_);
    const std::size_t object_bytes = object_values_.size() * sizeof(float);
    const std::size_t tile_bytes = object_tiles_.size() * sizeof(std::uint32_t);
    const UploadRing::Allocation objects_at = ring.allocate(object_bytes, 16u);
    const UploadRing::Allocation tiles_at = ring.allocate(tile_bytes, 16u);
    std::memcpy(objects_at.cpu, object_values_.data(), object_bytes);
    std::memcpy(tiles_at.cpu, object_tiles_.data(), tile_bytes);
    // Each in the buffer it lies in (a chunk when the ring's segment and spill were full).
    objects_buffer_ = ring.resource_of(objects_at)->buffer;
    objects_offset_ = objects_at.offset;
    tiles_buffer_ = ring.resource_of(tiles_at)->buffer;
    tiles_offset_ = tiles_at.offset;
    @autoreleasepool {
        id<MTLComputeCommandEncoder> const compute = [(__bridge id<MTLCommandBuffer>)commands computeCommandEncoder];
        compute.label = @"ambient occlusion";
        const Object encoder = (__bridge void*)compute;
        if (mark) mark(Stage::Distance);
        // The distance pass reads the depth alone; its other inputs are textures it does not write.
        dispatch(encoder, kDistance, depth, *denoised_, *raw_, *distance_, width, height);
        if (mark) mark(Stage::Occlusion);
        dispatch(encoder, ssao ? kSsao : kOcclusion, depth, *denoised_, *distance_, *raw_, width, height);
        if (mark) mark(Stage::Denoise);
        dispatch(encoder, kDenoise, depth, *raw_, *distance_, *denoised_, width, height);
        if (!fused_) {
            if (mark) mark(Stage::Upsample);
            // The scene's part of the multiplier (the multiply reads no further).
            dispatch(encoder, kUpsample, depth, *denoised_, *distance_, *scale_, scene_width, scene_height);
        }
        [compute endEncoding];
    }
    return true;
}

void AmbientOcclusion::finish(Object encoder, Format color_format, unsigned samples) {
    const Object pipeline = apply_pipeline(color_format, samples, fused_);  // made at the start (prepare_pipelines)
    const Texture* const read = fused_ ? denoised_.get() : scale_.get();
    if (pipeline == nullptr || read == nullptr) {
        return;
    }
    id<MTLRenderCommandEncoder> const pass = (__bridge id<MTLRenderCommandEncoder>)encoder;
    [pass setRenderPipelineState:(__bridge id<MTLRenderPipelineState>)pipeline];
    [pass setDepthStencilState:(__bridge id<MTLDepthStencilState>)no_depth_];
    [pass setCullMode:MTLCullModeNone];
    [pass setTriangleFillMode:MTLTriangleFillModeFill];
    [pass setDepthClipMode:MTLDepthClipModeClip];
    [pass setDepthBias:0.0f slopeScale:0.0f clamp:0.0f];
    // Only t1 is read: the multiplier, or with fused_ the denoised occlusion (the shader reads its depth slot only at more
    // than one scene target pixel per occlusion pixel). Every slot holds a texture.
    for (unsigned i = 0; i < 3u; ++i) {
        [pass setFragmentTexture:metal(*read) atIndex:i];
    }
    [pass setVertexBytes:&constants_ length:sizeof(constants_) atIndex:kUtilityPushBuffer];
    [pass setFragmentBytes:&constants_ length:sizeof(constants_) atIndex:kUtilityPushBuffer];
    if (objects_buffer_ != nullptr && tiles_buffer_ != nullptr) {
        [pass setFragmentBuffer:(__bridge id<MTLBuffer>)objects_buffer_ offset:objects_offset_ atIndex:kObjectsBuffer];
        [pass setFragmentBuffer:(__bridge id<MTLBuffer>)tiles_buffer_ offset:tiles_offset_ atIndex:kTilesBuffer];
    }
    [pass setViewport:MTLViewport{0.0, 0.0, static_cast<double>(target_width_), static_cast<double>(target_height_), 0.0, 1.0}];
    [pass setScissorRect:MTLScissorRect{0u, 0u, target_width_, target_height_}];
    [pass drawPrimitives:MTLPrimitiveTypeTriangle vertexStart:0 vertexCount:3];
}

}  // namespace galaxy::render::mtl
