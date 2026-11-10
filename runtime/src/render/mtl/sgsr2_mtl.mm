// SPDX-License-Identifier: GPL-3.0-only
// Snapdragon Game Super Resolution 2 on Metal (mtl/temporal_upscaler_mtl.h; the shaders: sgsr2.hlsl, turned to MSL
// at build). Two fragment passes, no compute: the depth separation at the render size, then the upsample at the
// output size, which writes the output and the next frame's history together. Tile-based GPUs keep both passes on
// chip. Pass for pass vk/sgsr2_vk.cpp's.
#import <Metal/Metal.h>

#include "galaxy/render/halton.h"
#include "galaxy/render/mtl/temporal_upscaler_mtl.h"
#include "galaxy/render/mtl/utility_msl.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <memory>

namespace galaxy::render::mtl {

namespace {

constexpr Format kDepthclipFormat = Format::R16f;
constexpr unsigned kPhases = 32u;  // SGSR's sample: the first 32 points of Halton(2, 3)
// sgsr2.hlsl's registers (vk/utility_spirv_sources.h kSgsrBindings): t0-t3 at textures 0-3, the sampler at 4, the
// constants pushed.
constexpr NSUInteger kSampler = 4u;

// sgsr2.hlsl's SgsrConstants.
struct Constants {
    float render_size[2];
    float render_rcp[2];
    float output_size[2];
    float output_rcp[2];
    float jitter[2];
    float scale_ratio[2];
    float input_rcp[2];
    float fov_hor;
    float min_lerp;
    float reset;
    float pad;
};
static_assert(sizeof(Constants) == 72u, "sgsr2.hlsl's constants");

[[nodiscard]] id<MTLDevice> device() noexcept {
    return (__bridge id<MTLDevice>)Context::get().device();
}
[[nodiscard]] id<MTLTexture> metal(const Texture& texture) noexcept {
    return (__bridge id<MTLTexture>)texture.texture;
}

id<MTLFunction> compile(UtilityShader shader) {
    MTLCompileOptions* const options = [MTLCompileOptions new];
    options.fastMathEnabled = NO;
    options.languageVersion = MTLLanguageVersion2_4;
    NSError* error = nil;
    id<MTLLibrary> const library = [device() newLibraryWithSource:@(utility_msl(shader)) options:options error:&error];
    id<MTLFunction> const made = library != nil ? [library newFunctionWithName:@"main0"] : nil;
    if (made == nil) {
        std::fprintf(stderr, "[sgsr2] a program did not compile: %s\n", error != nil ? error.localizedDescription.UTF8String : "no main0");
    }
    return made;
}

class Sgsr2Upscaler final : public TemporalUpscaler {
public:
    bool create(unsigned render_width, unsigned render_height, unsigned output_width, unsigned output_height, Format output_format) {
        render_width_ = render_width;
        render_height_ = render_height;
        output_width_ = output_width;
        output_height_ = output_height;
        output_format_ = output_format;
        @autoreleasepool {
            MTLSamplerDescriptor* const sampler = [MTLSamplerDescriptor new];
            sampler.sAddressMode = sampler.tAddressMode = sampler.rAddressMode = MTLSamplerAddressModeClampToEdge;
            sampler.mipFilter = MTLSamplerMipFilterNotMipmapped;
            // The depth is only gathered, which no filter changes.
            sampler.minFilter = sampler.magFilter = MTLSamplerMinMagFilterNearest;
            point_ = [device() newSamplerStateWithDescriptor:sampler];
            sampler.minFilter = sampler.magFilter = MTLSamplerMinMagFilterLinear;
            linear_ = [device() newSamplerStateWithDescriptor:sampler];
            id<MTLFunction> const vs = compile(UtilityShader::SgsrVs);
            id<MTLFunction> const convert = compile(UtilityShader::SgsrConvertPs);
            id<MTLFunction> const upscale = compile(UtilityShader::SgsrUpscalePs);
            if (vs == nil || convert == nil || upscale == nil) {
                return false;
            }
            NSError* error = nil;
            MTLRenderPipelineDescriptor* descriptor = [MTLRenderPipelineDescriptor new];
            descriptor.vertexFunction = vs;
            descriptor.fragmentFunction = convert;
            descriptor.colorAttachments[0].pixelFormat = static_cast<MTLPixelFormat>(kDepthclipFormat);
            convert_pipeline_ = [device() newRenderPipelineStateWithDescriptor:descriptor error:&error];
            descriptor = [MTLRenderPipelineDescriptor new];
            descriptor.vertexFunction = vs;
            descriptor.fragmentFunction = upscale;
            descriptor.colorAttachments[0].pixelFormat = static_cast<MTLPixelFormat>(output_format);
            descriptor.colorAttachments[1].pixelFormat = static_cast<MTLPixelFormat>(output_format);
            upscale_pipeline_ = [device() newRenderPipelineStateWithDescriptor:descriptor error:&error];
            if (convert_pipeline_ == nil || upscale_pipeline_ == nil) {
                std::fprintf(stderr, "[sgsr2] a pipeline could not be made: %s\n",
                             error != nil ? error.localizedDescription.UTF8String : "");
                return false;
            }
        }
        Context& context = Context::get();
        depthclip_ = context.create_texture(render_width, render_height, 1u, kDepthclipFormat, kUseTarget | kUseSample,
                                            Storage::Private, "sgsr2 depth clip");
        for (std::shared_ptr<Texture>& history : history_) {
            history = context.create_texture(output_width, output_height, 1u, output_format, kUseTarget | kUseSample,
                                             Storage::Private, "sgsr2 history");
        }
        return depthclip_ && history_[0] && history_[1];
    }

    bool evaluate(const TemporalUpscaleInputs& in) override {
        if (in.commands == nullptr || in.output == nullptr || in.color == nullptr || in.depth == nullptr || in.motion == nullptr ||
            in.render_width != render_width_ || in.render_height != render_height_ || in.output_width != output_width_ ||
            in.output_height != output_height_ || in.output->format != output_format_) {
            return false;
        }
        id<MTLCommandBuffer> const commands = (__bridge id<MTLCommandBuffer>)in.commands;
        const unsigned write = history_index_ ^ 1u;
        const bool reset = in.reset || !history_ready_;
        const Constants constants = constants_for(in, reset);
        @autoreleasepool {
            if (!history_ready_) {
                // Both histories start black; the reset ignores the one read this frame.
                MTLRenderPassDescriptor* const clear = [MTLRenderPassDescriptor renderPassDescriptor];
                for (unsigned i = 0; i < 2u; ++i) {
                    clear.colorAttachments[i].texture = metal(*history_[i]);
                    clear.colorAttachments[i].loadAction = MTLLoadActionClear;
                    clear.colorAttachments[i].clearColor = MTLClearColorMake(0.0, 0.0, 0.0, 0.0);
                    clear.colorAttachments[i].storeAction = MTLStoreActionStore;
                }
                [[commands renderCommandEncoderWithDescriptor:clear] endEncoding];
                history_ready_ = true;
            }
            // The depth clip: written whole, read by the upscale.
            MTLRenderPassDescriptor* const first = [MTLRenderPassDescriptor renderPassDescriptor];
            first.colorAttachments[0].texture = metal(*depthclip_);
            first.colorAttachments[0].loadAction = MTLLoadActionDontCare;
            first.colorAttachments[0].storeAction = MTLStoreActionStore;
            id<MTLRenderCommandEncoder> const convert = [commands renderCommandEncoderWithDescriptor:first];
            convert.label = @"SGSR 2 convert";
            [convert setFragmentTexture:metal(*in.depth) atIndex:0];
            [convert setFragmentSamplerState:point_ atIndex:kSampler];
            draw(convert, convert_pipeline_, render_width_, render_height_, constants);
            [convert endEncoding];
            // The output and the next history, both written whole.
            MTLRenderPassDescriptor* const second = [MTLRenderPassDescriptor renderPassDescriptor];
            second.colorAttachments[0].texture = metal(*in.output);
            second.colorAttachments[1].texture = metal(*history_[write]);
            for (unsigned i = 0; i < 2u; ++i) {
                second.colorAttachments[i].loadAction = MTLLoadActionDontCare;
                second.colorAttachments[i].storeAction = MTLStoreActionStore;
            }
            id<MTLRenderCommandEncoder> const upscale = [commands renderCommandEncoderWithDescriptor:second];
            upscale.label = @"SGSR 2 upscale";
            [upscale setFragmentTexture:metal(*history_[history_index_]) atIndex:0];
            [upscale setFragmentTexture:metal(*depthclip_) atIndex:1];
            [upscale setFragmentTexture:metal(*in.motion) atIndex:2];
            [upscale setFragmentTexture:metal(*in.color) atIndex:3];
            [upscale setFragmentSamplerState:linear_ atIndex:kSampler];
            draw(upscale, upscale_pipeline_, output_width_, output_height_, constants);
            [upscale endEncoding];
        }
        input_width_ = in.color->width;
        history_index_ = write;
        return true;
    }

    unsigned jitter_phase_count(unsigned, unsigned) const override { return kPhases; }
    void jitter_offset(unsigned index, unsigned phase_count, float& x, float& y) const override {
        const unsigned i = phase_count != 0u ? index % phase_count + 1u : index + 1u;
        x = halton(i, 2u) - 0.5f;
        y = halton(i, 3u) - 0.5f;
    }
    const char* name() const noexcept override { return "SGSR 2"; }
    bool writes_output() const noexcept override { return false; }

private:
    Constants constants_for(const TemporalUpscaleInputs& in, bool reset) {
        Constants c{};
        const float rw = static_cast<float>(render_width_);
        const float rh = static_cast<float>(render_height_);
        const float ow = static_cast<float>(output_width_);
        const float oh = static_cast<float>(output_height_);
        c.render_size[0] = rw;
        c.render_size[1] = rh;
        c.render_rcp[0] = 1.0f / rw;
        c.render_rcp[1] = 1.0f / rh;
        c.output_size[0] = ow;
        c.output_size[1] = oh;
        c.output_rcp[0] = 1.0f / ow;
        c.output_rcp[1] = 1.0f / oh;
        c.jitter[0] = in.jitter_x;
        c.jitter[1] = in.jitter_y;
        const float area = (ow * oh) / (rw * rh);
        c.scale_ratio[0] = ow / rw;
        c.scale_ratio[1] = std::min(20.0f, area * area * area);
        c.input_rcp[0] = 1.0f / static_cast<float>(std::max(in.color->width, 1u));
        c.input_rcp[1] = 1.0f / static_cast<float>(std::max(in.color->height, 1u));
        c.fov_hor = std::tan(in.fov_y * 0.5f) * rw / rh;
        // SGSR's still camera: the view and the lens unchanged for more than five frames lets the history hold a
        // little where the box would reject it (minLerpContribution).
        bool still = false;
        if (in.view_known && view_known_ && !reset) {
            float change = std::fabs(in.fov_y - fov_y_);
            for (std::size_t i = 0; i < in.view.size(); ++i) {
                change += std::fabs(in.view[i] - view_[i]);
            }
            still = change < 1.0e-05f;
        }
        still_frames_ = still ? still_frames_ + 1u : 0u;
        view_ = in.view;
        view_known_ = in.view_known;
        fov_y_ = in.fov_y;
        c.min_lerp = still_frames_ > 5u ? 0.3f : 0.0f;
        c.reset = reset ? 1.0f : 0.0f;
        return c;
    }

    static void draw(id<MTLRenderCommandEncoder> pass, id<MTLRenderPipelineState> pipeline, unsigned width, unsigned height,
                     const Constants& constants) {
        // The constants as the shaders' block: a multiple of sixteen bytes.
        std::uint8_t block[80]{};
        std::memcpy(block, &constants, sizeof(constants));
        [pass setRenderPipelineState:pipeline];
        [pass setCullMode:MTLCullModeNone];
        [pass setViewport:MTLViewport{0.0, 0.0, static_cast<double>(width), static_cast<double>(height), 0.0, 1.0}];
        [pass setScissorRect:MTLScissorRect{0u, 0u, width, height}];
        [pass setVertexBytes:block length:sizeof(block) atIndex:kUtilityPushBuffer];
        [pass setFragmentBytes:block length:sizeof(block) atIndex:kUtilityPushBuffer];
        [pass drawPrimitives:MTLPrimitiveTypeTriangle vertexStart:0 vertexCount:3];
    }

    unsigned render_width_ = 0u, render_height_ = 0u, output_width_ = 0u, output_height_ = 0u;
    unsigned input_width_ = 0u;
    Format output_format_ = Format::None;
    id<MTLSamplerState> point_ = nil;
    id<MTLSamplerState> linear_ = nil;
    id<MTLRenderPipelineState> convert_pipeline_ = nil;
    id<MTLRenderPipelineState> upscale_pipeline_ = nil;
    std::shared_ptr<Texture> depthclip_;
    std::array<std::shared_ptr<Texture>, 2> history_;
    unsigned history_index_ = 0u;
    bool history_ready_ = false;
    std::array<float, 12> view_{};
    bool view_known_ = false;
    float fov_y_ = 0.0f;
    unsigned still_frames_ = 0u;
};

}  // namespace

std::unique_ptr<TemporalUpscaler> create_sgsr2_upscaler(unsigned render_width, unsigned render_height, unsigned output_width,
                                                        unsigned output_height, Format output_format) {
    auto upscaler = std::make_unique<Sgsr2Upscaler>();
    if (!upscaler->create(render_width, render_height, output_width, output_height, output_format)) {
        std::fprintf(stderr, "[sgsr2] unavailable at %ux%u -> %ux%u\n", render_width, render_height, output_width, output_height);
        return nullptr;
    }
    return upscaler;
}

}  // namespace galaxy::render::mtl
