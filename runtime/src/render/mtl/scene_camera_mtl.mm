// SPDX-License-Identifier: GPL-3.0-only
// The wide scene's camera on Metal (mtl/scene_camera_mtl.h), pass for pass vk/scene_camera_vk.cpp's.
#import <Metal/Metal.h>

#include "galaxy/render/mtl/scene_camera_mtl.h"

#include "galaxy/render/mtl/utility_msl.h"
#include "galaxy/render/scene_camera_shape.h"

#include <algorithm>
#include <cstdio>
#include <iostream>

namespace galaxy::render::mtl {

namespace {

// scene_camera.hlsl's registers (vk/utility_spirv_sources.h kCameraBindings): t0 at texture 0, the exposure read at 1
// and written at 2, the depth at 3, the constants pushed.
constexpr UtilityShader kShaders[] = {UtilityShader::CameraMeterCs,   UtilityShader::CameraVs,        UtilityShader::CameraWidenPs,
                                      UtilityShader::CameraPs,        UtilityShader::CameraSamplesPs, UtilityShader::CameraResolvePs};
enum : unsigned { kMeterCs, kVs, kWidenPs, kPs, kSamplesPs, kResolvePs };

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

Object SceneCamera::function(unsigned index) {
    if (functions_[index] != nullptr) {
        return functions_[index];
    }
    // From utility.metallib (utility_msl.h renderer_function).
    functions_[index] = renderer_function(utility_msl(kShaders[index]));
    if (functions_[index] == nullptr) {
        std::fprintf(stderr, "[scene-camera] program %u could not be made\n", index);
    }
    return functions_[index];
}

bool SceneCamera::initialize(Format depth_format) {
    shutdown();
    depth_format_ = depth_format;
    // The meter reads the exposure it writes: a texture of one 32-bit channel read and written in one pass.
    if (device().readWriteTextureSupport == MTLReadWriteTextureTierNone) {
        return false;
    }
    @autoreleasepool {
        const Object compiled = function(kMeterCs);
        NSError* error = nil;
        id<MTLComputePipelineState> const meter =
            compiled != nullptr ? [device() newComputePipelineStateWithFunction:(__bridge id<MTLFunction>)compiled error:&error] : nil;
        if (meter == nil) {
            std::cerr << "[scene-camera] Metal meter failed\n";
            shutdown();
            return false;
        }
        meter_ = own(meter);
        MTLDepthStencilDescriptor* const depth = [MTLDepthStencilDescriptor new];
        depth.depthCompareFunction = MTLCompareFunctionAlways;
        depth.depthWriteEnabled = NO;
        no_depth_ = own([device() newDepthStencilStateWithDescriptor:depth]);
    }
    exposure_ = Context::get().create_texture(2u, 1u, 1u, Format::R32f, kUseWrite | kUseSample, Storage::Private,
                                              "scene camera exposure");
    if (!exposure_) {
        shutdown();
        return false;
    }
    exposure_ready_ = false;
    ready_ = true;
    return true;
}

void SceneCamera::shutdown() noexcept {
    ready_ = false;
    exposure_.reset();
    release(meter_);
    meter_ = nullptr;
    release(no_depth_);
    no_depth_ = nullptr;
    for (Made& made : pipelines_) {
        release(made.pipeline);
        made = Made{};
    }
    for (Object& compiled : functions_) {
        release(compiled);
        compiled = nullptr;
    }
}

void SceneCamera::prepare_pipelines(std::span<const ScenePassFormat> scene_passes, std::span<const ScenePassFormat> wide_passes) {
    if (!ready_) {
        return;
    }
    for (const ScenePassFormat& pass : wide_passes) {
        static_cast<void>(pipeline(kWiden, pass.color, pass.samples));
    }
    for (const ScenePassFormat& pass : scene_passes) {
        for (const Kind kind : {kCamera, kCameraSamples, kCameraResolve}) {
            static_cast<void>(pipeline(kind, pass.color, pass.samples));
        }
    }
}

Object SceneCamera::pipeline(Kind kind, Format color_format, unsigned samples) {
    Made* free_slot = nullptr;
    for (Made& made : pipelines_) {
        if (made.pipeline != nullptr && made.kind == kind && made.color == color_format && made.samples == samples) {
            return made.pipeline;
        }
        if (made.pipeline == nullptr && free_slot == nullptr) {
            free_slot = &made;
        }
    }
    const Object vs = function(kVs);
    const Object ps = function(kind == kWiden ? kWidenPs : kind == kCameraSamples ? kSamplesPs : kind == kCameraResolve ? kResolvePs : kPs);
    if (free_slot == nullptr || vs == nullptr || ps == nullptr) {
        return nullptr;
    }
    @autoreleasepool {
        MTLRenderPipelineDescriptor* const descriptor = [MTLRenderPipelineDescriptor new];
        descriptor.vertexFunction = (__bridge id<MTLFunction>)vs;
        descriptor.fragmentFunction = (__bridge id<MTLFunction>)ps;
        descriptor.colorAttachments[0].pixelFormat = static_cast<MTLPixelFormat>(color_format);
        descriptor.colorAttachments[0].writeMask = MTLColorWriteMaskAll;
        descriptor.depthAttachmentPixelFormat = static_cast<MTLPixelFormat>(depth_format_);
        descriptor.stencilAttachmentPixelFormat = static_cast<MTLPixelFormat>(depth_format_);
        // The per-sample camera's function reads its sample's number: Metal then runs it once a sample.
        descriptor.rasterSampleCount = samples;
        NSError* error = nil;
        id<MTLRenderPipelineState> const state = [device() newRenderPipelineStateWithDescriptor:descriptor error:&error];
        if (state == nil) {
            std::fprintf(stderr, "[scene-camera] pipeline failed for %u samples: %s\n", samples,
                         error != nil ? error.localizedDescription.UTF8String : "");
            return nullptr;
        }
        *free_slot = Made{kind, color_format, samples, own(state)};
    }
    return free_slot->pipeline;
}

void SceneCamera::full_screen(Object encoder, Object pipeline_state, const Texture& sampled, unsigned width, unsigned height) const {
    camera::Constants constants{};
    constants.width = static_cast<float>(width);
    constants.height = static_cast<float>(height);
    id<MTLRenderCommandEncoder> const pass = (__bridge id<MTLRenderCommandEncoder>)encoder;
    [pass setRenderPipelineState:(__bridge id<MTLRenderPipelineState>)pipeline_state];
    [pass setDepthStencilState:(__bridge id<MTLDepthStencilState>)no_depth_];
    [pass setCullMode:MTLCullModeNone];
    [pass setTriangleFillMode:MTLTriangleFillModeFill];
    [pass setDepthClipMode:MTLDepthClipModeClip];
    [pass setDepthBias:0.0f slopeScale:0.0f clamp:0.0f];
    // Only the meter reads the depth and writes the exposure; here the exposure is read alone.
    [pass setFragmentTexture:metal(sampled) atIndex:0];
    [pass setFragmentTexture:metal(*exposure_) atIndex:1];
    [pass setVertexBytes:&constants length:sizeof(constants) atIndex:kUtilityPushBuffer];
    [pass setFragmentBytes:&constants length:sizeof(constants) atIndex:kUtilityPushBuffer];
    [pass setViewport:MTLViewport{0.0, 0.0, static_cast<double>(width), static_cast<double>(height), 0.0, 1.0}];
    [pass setScissorRect:MTLScissorRect{0u, 0u, width, height}];
    [pass drawPrimitives:MTLPrimitiveTypeTriangle vertexStart:0 vertexCount:3];
}

void SceneCamera::widen(Object encoder, Format color_format, unsigned samples, const Texture& picture, unsigned width,
                        unsigned height) {
    if (const Object widen_pipeline = pipeline(kWiden, color_format, samples); widen_pipeline != nullptr) {
        full_screen(encoder, widen_pipeline, picture, std::min(width, picture.width), std::min(height, picture.height));
    }
}

void SceneCamera::meter(Object commands, const Texture& wide, const Texture& depth, unsigned width, unsigned height,
                        float seconds) {
    camera::Constants constants{};
    constants.width = static_cast<float>(std::min(width, wide.width));
    constants.height = static_cast<float>(std::min(height, wide.height));
    constants.seconds = exposure_ready_ ? seconds : -1.0f;
    exposure_ready_ = true;
    @autoreleasepool {
        id<MTLComputeCommandEncoder> const compute = [(__bridge id<MTLCommandBuffer>)commands computeCommandEncoder];
        compute.label = @"scene camera meter";
        [compute setComputePipelineState:(__bridge id<MTLComputePipelineState>)meter_];
        [compute setTexture:metal(wide) atIndex:0];
        [compute setTexture:metal(*exposure_) atIndex:2];
        [compute setTexture:metal(depth) atIndex:3];
        [compute setBytes:&constants length:sizeof(constants) atIndex:kUtilityPushBuffer];
        // One group of MeterCS's 8 x 9 threads (scene_camera.hlsl).
        [compute dispatchThreadgroups:MTLSizeMake(1, 1, 1) threadsPerThreadgroup:MTLSizeMake(8, 9, 1)];
        [compute endEncoding];
    }
}

void SceneCamera::draw(Object encoder, Format color_format, unsigned samples, const Texture& wide, unsigned width,
                       unsigned height, bool per_sample, bool resolve) {
    const Object camera_pipeline = pipeline(resolve ? kCameraResolve : per_sample ? kCameraSamples : kCamera, color_format, samples);
    if (camera_pipeline != nullptr) {
        full_screen(encoder, camera_pipeline, wide, std::min(width, wide.width), std::min(height, wide.height));
    }
}

}  // namespace galaxy::render::mtl
