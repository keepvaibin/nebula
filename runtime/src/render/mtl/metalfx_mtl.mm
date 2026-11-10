// SPDX-License-Identifier: GPL-3.0-only
// MetalFX on Metal: the spatial scaler (mtl/metalfx_mtl.h) and the temporal scaler (mtl/temporal_upscaler_mtl.h).
// The framework is linked weakly (it came with macOS 13 and iOS 16): every use is behind an availability check and
// the device's own word (DeviceCaps::metalfx_spatial, metalfx_temporal).
//
// The temporal scaler's inputs are the other temporal upscalers' (vk/temporal_upscaler_vk.h), which are what Apple's
// header asks for: motion in render pixels pointing to where each pixel was in the frame before
// (motionVectorScale 1), the jitter as the offset that returns to the unjittered frame (the projection's offset, as
// sgsr2.hlsl adds it), and a reversed depth.
#import <Metal/Metal.h>
#import <MetalFX/MetalFX.h>

#include "galaxy/render/halton.h"
#include "galaxy/render/mtl/metalfx_mtl.h"
#include "galaxy/render/mtl/temporal_upscaler_mtl.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <memory>

namespace galaxy::render::mtl {

namespace {

[[nodiscard]] id<MTLDevice> device() noexcept {
    return (__bridge id<MTLDevice>)Context::get().device();
}
[[nodiscard]] id<MTLTexture> metal(const Texture& texture) noexcept {
    return (__bridge id<MTLTexture>)texture.texture;
}

API_AVAILABLE(macos(13.0), ios(16.0))
class MetalFxSpatial final : public SpatialScaler {
public:
    bool create(unsigned input_width, unsigned input_height, unsigned output_width, unsigned output_height, Format color_format,
                Format output_format) {
        @autoreleasepool {
            MTLFXSpatialScalerDescriptor* const descriptor = [MTLFXSpatialScalerDescriptor new];
            descriptor.inputWidth = input_width;
            descriptor.inputHeight = input_height;
            descriptor.outputWidth = output_width;
            descriptor.outputHeight = output_height;
            descriptor.colorTextureFormat = static_cast<MTLPixelFormat>(color_format);
            descriptor.outputTextureFormat = static_cast<MTLPixelFormat>(output_format);
            // The picture is display values (gamma), as the window shows them.
            descriptor.colorProcessingMode = MTLFXSpatialScalerColorProcessingModePerceptual;
            scaler_ = [descriptor newSpatialScalerWithDevice:device()];
        }
        input_width_ = input_width;
        input_height_ = input_height;
        output_width_ = output_width;
        output_height_ = output_height;
        color_format_ = color_format;
        output_format_ = output_format;
        return scaler_ != nil;
    }
    bool matches(unsigned input_width, unsigned input_height, unsigned output_width, unsigned output_height, Format color_format,
                 Format output_format) const noexcept override {
        return input_width == input_width_ && input_height == input_height_ && output_width == output_width_ &&
               output_height == output_height_ && color_format == color_format_ && output_format == output_format_;
    }
    std::uint32_t output_use() const noexcept override { return static_cast<std::uint32_t>(scaler_.outputTextureUsage); }
    void encode(Object commands, const Texture& color, const Texture& output) override {
        scaler_.colorTexture = metal(color);
        scaler_.outputTexture = metal(output);
        scaler_.inputContentWidth = input_width_;
        scaler_.inputContentHeight = input_height_;
        [scaler_ encodeToCommandBuffer:(__bridge id<MTLCommandBuffer>)commands];
        scaler_.colorTexture = nil;
        scaler_.outputTexture = nil;
    }

private:
    id<MTLFXSpatialScaler> scaler_ = nil;
    unsigned input_width_ = 0u, input_height_ = 0u, output_width_ = 0u, output_height_ = 0u;
    Format color_format_ = Format::None, output_format_ = Format::None;
};

// The scene's depth as a texture of depth alone, where the scaler takes no depth with a stencil beside it: a
// full-target draw that writes each pixel's depth.
constexpr char kDepthCopy[] = R"MSL(
#include <metal_stdlib>
using namespace metal;
struct Corner { float4 position [[position]]; };
vertex Corner corner(uint id [[vertex_id]]) {
    Corner out;
    out.position = float4(id == 1u ? 3.0 : -1.0, id == 2u ? -3.0 : 1.0, 0.0, 1.0);
    return out;
}
struct Depth { float depth [[depth(any)]]; };
fragment Depth depth_of(Corner in, texture2d<float> scene [[texture(0)]]) {
    Depth out;
    out.depth = scene.read(uint2(in.position.xy)).r;
    return out;
}
)MSL";

API_AVAILABLE(macos(13.0), ios(16.0))
class MetalFxTemporal final : public TemporalUpscaler {
public:
    bool create(unsigned render_width, unsigned render_height, unsigned output_width, unsigned output_height, Format color_format,
                Format output_format) {
        render_width_ = render_width;
        render_height_ = render_height;
        output_width_ = output_width;
        output_height_ = output_height;
        @autoreleasepool {
            // The scene's own depth (with its stencil) first; a scaler that refuses it gets a copy of the depth alone.
            for (const bool direct_depth : {true, false}) {
                MTLFXTemporalScalerDescriptor* const descriptor = [MTLFXTemporalScalerDescriptor new];
                descriptor.inputWidth = render_width;
                descriptor.inputHeight = render_height;
                descriptor.outputWidth = output_width;
                descriptor.outputHeight = output_height;
                descriptor.colorTextureFormat = static_cast<MTLPixelFormat>(color_format);
                descriptor.depthTextureFormat = direct_depth ? MTLPixelFormatDepth32Float_Stencil8 : MTLPixelFormatDepth32Float;
                descriptor.motionTextureFormat = MTLPixelFormatRG16Float;
                descriptor.outputTextureFormat = static_cast<MTLPixelFormat>(output_format);
                // The scene is display values: nothing to expose.
                descriptor.autoExposureEnabled = NO;
                descriptor.inputContentPropertiesEnabled = NO;
                scaler_ = [descriptor newTemporalScalerWithDevice:device()];
                if (scaler_ != nil) {
                    direct_depth_ = direct_depth;
                    break;
                }
            }
            if (scaler_ == nil) {
                return false;
            }
            if (!direct_depth_ && !make_depth_copy()) {
                return false;
            }
        }
        return true;
    }

    bool evaluate(const TemporalUpscaleInputs& in) override {
        if (in.commands == nullptr || in.color == nullptr || in.depth == nullptr || in.motion == nullptr || in.output == nullptr ||
            in.render_width != render_width_ || in.render_height != render_height_ || in.output_width != output_width_ ||
            in.output_height != output_height_) {
            return false;
        }
        id<MTLCommandBuffer> const commands = (__bridge id<MTLCommandBuffer>)in.commands;
        @autoreleasepool {
            id<MTLTexture> depth = metal(*in.depth);
            if (!direct_depth_) {
                MTLRenderPassDescriptor* const pass = [MTLRenderPassDescriptor renderPassDescriptor];
                pass.depthAttachment.texture = metal(*depth_copy_);
                pass.depthAttachment.loadAction = MTLLoadActionDontCare;
                pass.depthAttachment.storeAction = MTLStoreActionStore;
                id<MTLRenderCommandEncoder> const copy = [commands renderCommandEncoderWithDescriptor:pass];
                copy.label = @"depth for MetalFX";
                [copy setRenderPipelineState:depth_pipeline_];
                [copy setDepthStencilState:depth_write_];
                [copy setFragmentTexture:depth atIndex:0];
                [copy drawPrimitives:MTLPrimitiveTypeTriangle vertexStart:0 vertexCount:3];
                [copy endEncoding];
                depth = metal(*depth_copy_);
            }
            scaler_.colorTexture = metal(*in.color);
            scaler_.depthTexture = depth;
            scaler_.motionTexture = metal(*in.motion);
            scaler_.outputTexture = metal(*in.output);
            scaler_.inputContentWidth = render_width_;
            scaler_.inputContentHeight = render_height_;
            scaler_.jitterOffsetX = in.jitter_x;
            scaler_.jitterOffsetY = in.jitter_y;
            scaler_.motionVectorScaleX = 1.0f;
            scaler_.motionVectorScaleY = 1.0f;
            scaler_.depthReversed = YES;
            scaler_.reset = in.reset ? YES : NO;
            [scaler_ encodeToCommandBuffer:commands];
            scaler_.colorTexture = nil;
            scaler_.depthTexture = nil;
            scaler_.motionTexture = nil;
            scaler_.outputTexture = nil;
        }
        return true;
    }

    // As FSR's sequence: eight phases at native size, more the larger the enlargement.
    unsigned jitter_phase_count(unsigned render_width, unsigned output_width) const override {
        const float ratio = render_width != 0u ? static_cast<float>(output_width) / static_cast<float>(render_width) : 1.0f;
        return static_cast<unsigned>(std::clamp(std::lround(8.0f * ratio * ratio), 8l, 64l));
    }
    void jitter_offset(unsigned index, unsigned phase_count, float& x, float& y) const override {
        const unsigned i = phase_count != 0u ? index % phase_count + 1u : index + 1u;
        x = halton(i, 2u) - 0.5f;
        y = halton(i, 3u) - 0.5f;
    }
    const char* name() const noexcept override { return "MetalFX temporal"; }
    bool writes_output() const noexcept override { return true; }
    [[nodiscard]] std::uint32_t output_use() const noexcept { return static_cast<std::uint32_t>(scaler_.outputTextureUsage); }

private:
    bool make_depth_copy() {
        depth_copy_ = Context::get().create_texture(render_width_, render_height_, 1u, Format::Depth32f, kUseTarget | kUseSample,
                                                    Storage::Private, "depth for MetalFX");
        NSError* error = nil;
        id<MTLLibrary> const library = [device() newLibraryWithSource:@(kDepthCopy) options:nil error:&error];
        if (!depth_copy_ || library == nil) {
            std::fprintf(stderr, "[metalfx] the depth copy: %s\n", error != nil ? error.localizedDescription.UTF8String : "no texture");
            return false;
        }
        MTLRenderPipelineDescriptor* const descriptor = [MTLRenderPipelineDescriptor new];
        descriptor.vertexFunction = [library newFunctionWithName:@"corner"];
        descriptor.fragmentFunction = [library newFunctionWithName:@"depth_of"];
        descriptor.depthAttachmentPixelFormat = MTLPixelFormatDepth32Float;
        depth_pipeline_ = [device() newRenderPipelineStateWithDescriptor:descriptor error:&error];
        MTLDepthStencilDescriptor* const depth = [MTLDepthStencilDescriptor new];
        depth.depthCompareFunction = MTLCompareFunctionAlways;
        depth.depthWriteEnabled = YES;
        depth_write_ = [device() newDepthStencilStateWithDescriptor:depth];
        return depth_pipeline_ != nil && depth_write_ != nil;
    }

    id<MTLFXTemporalScaler> scaler_ = nil;
    bool direct_depth_ = true;
    std::shared_ptr<Texture> depth_copy_;
    id<MTLRenderPipelineState> depth_pipeline_ = nil;
    id<MTLDepthStencilState> depth_write_ = nil;
    unsigned render_width_ = 0u, render_height_ = 0u, output_width_ = 0u, output_height_ = 0u;
};

}  // namespace

std::unique_ptr<SpatialScaler> create_spatial_scaler(unsigned input_width, unsigned input_height, unsigned output_width,
                                                     unsigned output_height, Format color_format, Format output_format) {
    if (!Context::get().caps().metalfx_spatial) {
        return nullptr;
    }
    if (@available(macOS 13.0, iOS 16.0, *)) {
        auto scaler = std::make_unique<MetalFxSpatial>();
        if (scaler->create(input_width, input_height, output_width, output_height, color_format, output_format)) {
            return scaler;
        }
        std::fprintf(stderr, "[metalfx] no spatial scaler for %ux%u -> %ux%u\n", input_width, input_height, output_width,
                     output_height);
    }
    return nullptr;
}

std::unique_ptr<TemporalUpscaler> create_metalfx_upscaler(unsigned render_width, unsigned render_height, unsigned output_width,
                                                          unsigned output_height, Format color_format, Format output_format) {
    if (!Context::get().caps().metalfx_temporal) {
        return nullptr;
    }
    if (@available(macOS 13.0, iOS 16.0, *)) {
        auto scaler = std::make_unique<MetalFxTemporal>();
        if (scaler->create(render_width, render_height, output_width, output_height, color_format, output_format)) {
            return scaler;
        }
        std::fprintf(stderr, "[metalfx] no temporal scaler for %ux%u -> %ux%u\n", render_width, render_height, output_width,
                     output_height);
    }
    return nullptr;
}

}  // namespace galaxy::render::mtl
