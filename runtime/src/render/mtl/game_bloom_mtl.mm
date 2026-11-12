// SPDX-License-Identifier: GPL-3.0-only
// The game's bloom on Metal (mtl/game_bloom_mtl.h): game_bloom.cpp's chain, pass for pass vk/game_bloom_vk.cpp's.
#import <Metal/Metal.h>

#include "galaxy/render/mtl/game_bloom_mtl.h"

#include "galaxy/render/game_bloom_shape.h"
#include "galaxy/render/mtl/utility_msl.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <iostream>
#include <utility>

namespace galaxy::render::mtl {

namespace {

constexpr Format kWorkFormat = Format::Rgba16f;
// game_bloom.hlsl's registers (vk/utility_spirv_sources.h kBloomBindings): t0-t2 at textures 0-2, u0-u2 at 3-5, the
// linear sampler in set 1, the constants pushed.
constexpr NSUInteger kLinearSampler = kUtilitySetStride + 0u;
constexpr UtilityShader kShaders[] = {UtilityShader::BloomDownCs, UtilityShader::BloomBoxCs, UtilityShader::BloomBlurCs,
                                      UtilityShader::BloomCompositeVs, UtilityShader::BloomCompositePs};

[[nodiscard]] id<MTLDevice> device() noexcept {
    return (__bridge id<MTLDevice>)Context::get().device();
}
[[nodiscard]] id<MTLTexture> metal(const Texture* texture) noexcept {
    return texture != nullptr ? (__bridge id<MTLTexture>)texture->texture : nil;
}
[[nodiscard]] Object own(id object) noexcept {
    return (__bridge_retained void*)object;
}

}  // namespace

void GameBloom::initialize(Format depth_format) {
    shutdown();
    depth_format_ = depth_format;
    to_build_ = true;
    const Object metal_device = Context::get().device();
    compiling_ = std::async(std::launch::async, [metal_device] {
        Functions made;
        made.ok = true;
        @autoreleasepool {
            id<MTLDevice> const from = (__bridge id<MTLDevice>)metal_device;
            MTLCompileOptions* const options = [MTLCompileOptions new];
            options.fastMathEnabled = NO;
            options.languageVersion = MTLLanguageVersion2_4;
            for (unsigned program = 0; program < kProgramCount; ++program) {
                NSError* error = nil;
                id<MTLLibrary> const library = [from newLibraryWithSource:@(utility_msl(kShaders[program])) options:options error:&error];
                id<MTLFunction> const function = library != nil ? [library newFunctionWithName:@"main0"] : nil;
                if (function == nil) {
                    std::fprintf(stderr, "[bloom] program %u did not compile: %s\n", program,
                                 error != nil ? error.localizedDescription.UTF8String : "no main0");
                    made.ok = false;
                    continue;
                }
                made.at[program] = own(function);
            }
        }
        return made;
    });
}

void GameBloom::destroy() noexcept {
    for (Object& pipeline : compute_) {
        release(pipeline);
        pipeline = nullptr;
    }
    for (Composite& composite : composites_) {
        release(composite.pipeline);
        composite = Composite{};
    }
    release(sampler_);
    sampler_ = nullptr;
    release(no_depth_);
    no_depth_ = nullptr;
}

void GameBloom::shutdown() noexcept {
    if (compiling_.valid()) {
        functions_ = compiling_.get();
    }
    to_build_ = false;
    destroy();
    for (Object& function : functions_.at) {
        release(function);
        function = nullptr;
    }
    functions_.ok = false;
    ready_ = false;
    kept_ = false;
    pending_ = false;
    for (std::shared_ptr<Texture>* t : {&dummy_, &dummy_out_, &keep_, &source_, &work_, &box_tmp_, &mask_, &level1_, &level2_in_, &level2_}) {
        t->reset();
    }
    for (auto& t : blur_h_) t.reset();
    for (auto& t : blur2_h_) t.reset();
}

bool GameBloom::poll_ready() {
    if (ready_) {
        return true;
    }
    if (!to_build_ || !compiling_.valid() || compiling_.wait_for(std::chrono::seconds(0)) != std::future_status::ready) {
        return false;
    }
    to_build_ = false;
    functions_ = compiling_.get();
    ready_ = functions_.ok && build();
    if (!ready_) {
        destroy();
        std::cerr << "[bloom] Metal passes unavailable: the game draws its own bloom\n";
    }
    return ready_;
}

bool GameBloom::build() {
    @autoreleasepool {
        MTLSamplerDescriptor* const sampler = [MTLSamplerDescriptor new];
        sampler.minFilter = sampler.magFilter = MTLSamplerMinMagFilterLinear;
        sampler.mipFilter = MTLSamplerMipFilterNotMipmapped;
        sampler.sAddressMode = sampler.tAddressMode = sampler.rAddressMode = MTLSamplerAddressModeClampToEdge;
        sampler_ = own([device() newSamplerStateWithDescriptor:sampler]);
        MTLDepthStencilDescriptor* const depth = [MTLDepthStencilDescriptor new];
        depth.depthCompareFunction = MTLCompareFunctionAlways;
        depth.depthWriteEnabled = NO;
        no_depth_ = own([device() newDepthStencilStateWithDescriptor:depth]);
        for (unsigned program = 0; program < 3u; ++program) {
            NSError* error = nil;
            id<MTLComputePipelineState> const state =
                [device() newComputePipelineStateWithFunction:(__bridge id<MTLFunction>)functions_.at[program] error:&error];
            if (state == nil) {
                std::fprintf(stderr, "[bloom] compute pipeline %u: %s\n", program,
                             error != nil ? error.localizedDescription.UTF8String : "");
                return false;
            }
            compute_[program] = own(state);
        }
    }
    return true;
}

Object GameBloom::composite_pipeline(Format color_format, unsigned samples) {
    Composite* free_slot = nullptr;
    for (Composite& composite : composites_) {
        if (composite.pipeline != nullptr && composite.color == color_format && composite.samples == samples) {
            return composite.pipeline;
        }
        if (composite.pipeline == nullptr && free_slot == nullptr) {
            free_slot = &composite;
        }
    }
    if (free_slot == nullptr) {
        return nullptr;
    }
    @autoreleasepool {
        MTLRenderPipelineDescriptor* const descriptor = [MTLRenderPipelineDescriptor new];
        descriptor.vertexFunction = (__bridge id<MTLFunction>)functions_.at[kCompositeVs];
        descriptor.fragmentFunction = (__bridge id<MTLFunction>)functions_.at[kCompositePs];
        descriptor.colorAttachments[0].pixelFormat = static_cast<MTLPixelFormat>(color_format);
        descriptor.colorAttachments[0].writeMask = MTLColorWriteMaskAll;
        descriptor.depthAttachmentPixelFormat = static_cast<MTLPixelFormat>(depth_format_);
        descriptor.stencilAttachmentPixelFormat = static_cast<MTLPixelFormat>(depth_format_);
        descriptor.rasterSampleCount = samples;
        NSError* error = nil;
        id<MTLRenderPipelineState> const state = [device() newRenderPipelineStateWithDescriptor:descriptor error:&error];
        if (state == nil) {
            std::fprintf(stderr, "[bloom] composite pipeline: %s\n", error != nil ? error.localizedDescription.UTF8String : "");
            return nullptr;
        }
        *free_slot = Composite{color_format, samples, own(state)};
    }
    return free_slot->pipeline;
}

bool GameBloom::ensure(std::shared_ptr<Texture>& target, unsigned width, unsigned height, Format format, bool work,
                       const char* name) {
    if (target && target->width == width && target->height == height && target->format == format) {
        return true;
    }
    target = Context::get().create_texture(width, height, 1u, format, work ? kUseWrite | kUseSample : std::uint32_t{kUseSample},
                                           Storage::Private, name);
    return target != nullptr;
}

bool GameBloom::copy_frame(Object commands, const Inputs& in, std::shared_ptr<Texture>& target, const char* name) {
    if (!ensure(target, in.frame_width, in.frame_height, in.color->format, false, name)) {
        return false;
    }
    @autoreleasepool {
        id<MTLBlitCommandEncoder> const blit = [(__bridge id<MTLCommandBuffer>)commands blitCommandEncoder];
        blit.label = @(name);
        [blit copyFromTexture:metal(in.color)
                  sourceSlice:0
                  sourceLevel:0
                 sourceOrigin:MTLOriginMake(0, 0, 0)
                   sourceSize:MTLSizeMake(in.frame_width, in.frame_height, 1)
                    toTexture:metal(target.get())
             destinationSlice:0
             destinationLevel:0
            destinationOrigin:MTLOriginMake(0, 0, 0)];
        [blit endEncoding];
    }
    return true;
}

std::shared_ptr<Texture> GameBloom::take_kept_image() {
    return std::move(keep_);
}

void GameBloom::keep_picture(std::shared_ptr<Texture> picture) {
    keep_ = std::move(picture);
    keep_used_ = clock_;
    kept_ = true;
}

std::shared_ptr<Texture> GameBloom::take_source_image() {
    return std::move(source_);
}

void GameBloom::give_source(std::shared_ptr<Texture> picture) {
    source_ = std::move(picture);
    simple_used_ = clock_;
    source_given_ = true;
}

void GameBloom::trim_idle() {
    constexpr std::uint64_t kIdleFrames = 600u;
    ++clock_;
    const auto idle = [&](std::uint64_t used) { return clock_ > used + kIdleFrames; };
    if (idle(keep_used_)) keep_.reset();
    if (idle(work_used_)) {
        for (std::shared_ptr<Texture>* t : {&work_, &box_tmp_, &mask_, &level1_, &blur_h_[0]}) t->reset();
    }
    if (idle(normal_used_)) {
        for (std::shared_ptr<Texture>* t :
             {&blur_h_[1], &blur_h_[2], &level2_in_, &level2_, &blur2_h_[0], &blur2_h_[1], &blur2_h_[2]}) {
            t->reset();
        }
    }
    if (idle(simple_used_)) source_.reset();
}

bool GameBloom::keep(Object commands, const Inputs& in) {
    kept_ = ready_ && in.color != nullptr && in.frame_width != 0u && in.frame_height != 0u &&
            copy_frame(commands, in, keep_, "bloom kept scene");
    keep_used_ = clock_;
    return kept_;
}

void GameBloom::dispatch(Object compute_encoder, Program program, const Constants& c, std::array<const Texture*, 3> sampled,
                         std::array<const Texture*, 3> storage, unsigned width, unsigned height) const {
    id<MTLComputeCommandEncoder> const compute = (__bridge id<MTLComputeCommandEncoder>)compute_encoder;
    [compute setComputePipelineState:(__bridge id<MTLComputePipelineState>)compute_[program]];
    for (unsigned i = 0; i < 3u; ++i) {
        [compute setTexture:metal(sampled[i] != nullptr ? sampled[i] : dummy_.get()) atIndex:i];
        [compute setTexture:metal(storage[i] != nullptr ? storage[i] : dummy_out_.get()) atIndex:3u + i];
    }
    [compute setSamplerState:(__bridge id<MTLSamplerState>)sampler_ atIndex:kLinearSampler];
    [compute setBytes:&c length:sizeof(c) atIndex:kUtilityPushBuffer];
    [compute dispatchThreadgroups:MTLSizeMake((width + 7u) / 8u, (height + 7u) / 8u, 1) threadsPerThreadgroup:MTLSizeMake(8, 8, 1)];
}

bool GameBloom::prepare(Object commands, const Inputs& in, const Params& p) {
    pending_ = false;
    const bool kept = std::exchange(kept_, false);
    const bool given = std::exchange(source_given_, false);
    const bool restore = p.kind == Kind::Restore;
    const bool normal = p.kind == Kind::Normal;
    if (!ready_ || in.color == nullptr || in.frame_width == 0u || in.frame_height == 0u || ((normal || restore) && !kept)) {
        return false;
    }
    keep_used_ = clock_;
    if (!restore) {
        work_used_ = clock_;
        (normal ? normal_used_ : simple_used_) = clock_;
    }
    composite_width_ = in.frame_width;
    composite_height_ = in.frame_height;
    composite_constants_ = {};
    if (restore) {
        // Kind 0: the scene alone, the levels unread.
        composite_inputs_[0] = composite_inputs_[1] = composite_inputs_[2] = keep_.get();
        pending_ = true;
        return true;
    }
    const bloom::Shape s = bloom::shape_of(in.frame_width, in.frame_height, in.scale);
    const unsigned blurs1 = normal ? 2u : 1u;
    bool made = ensure(dummy_, 1u, 1u, kWorkFormat, true, "bloom unused slot") &&
                ensure(dummy_out_, 1u, 1u, kWorkFormat, true, "bloom unused slot") &&
                ensure(work_, s.w1, s.h1, kWorkFormat, true, "bloom work") &&
                ensure(box_tmp_, s.w1, s.h1, kWorkFormat, true, "bloom box") &&
                ensure(mask_, s.w1, s.h1, kWorkFormat, true, "bloom mask") &&
                ensure(level1_, s.w1, s.h1, kWorkFormat, true, "bloom level 1");
    for (unsigned i = 0; made && i < blurs1; ++i) {
        made = ensure(blur_h_[i], s.w1, s.h1, kWorkFormat, true, "bloom level 1 blur");
    }
    if (made && normal) {
        made = ensure(level2_in_, s.w2, s.h2, kWorkFormat, true, "bloom level 2 source") &&
               ensure(level2_, s.w2, s.h2, kWorkFormat, true, "bloom level 2");
        for (unsigned i = 0; made && i < 3u; ++i) {
            made = ensure(blur2_h_[i], s.w2, s.h2, kWorkFormat, true, "bloom level 2 blur");
        }
    }
    if (!made || (!normal && !given && !copy_frame(commands, in, source_, "bloom source"))) {
        return false;
    }
    const bloom::Widths w = bloom::widths_of(normal, s, in.frame_width, in.frame_height, p.ring_x, p.ring_y);
    @autoreleasepool {
        id<MTLComputeCommandEncoder> const compute = [(__bridge id<MTLCommandBuffer>)commands computeCommandEncoder];
        compute.label = @"game bloom";
        const Object encoder = (__bridge void*)compute;
        // The selection's source: the glowing models drawn over black (normal: the scene target itself), the scene
        // (simple).
        {
            Constants c{};
            c.size[0] = s.w1;
            c.size[1] = s.h1;
            c.size[2] = s.f;
            c.uv[0] = static_cast<float>(in.frame_width);
            c.uv[1] = static_cast<float>(in.frame_height);
            dispatch(encoder, kDown, c, {normal ? in.color : source_.get(), nullptr, nullptr}, {work_.get(), nullptr, nullptr},
                     s.w1, s.h1);
        }
        // The game's 4 x 4 pixel footprint (across, then down with the selection).
        const bool across = s.box > 1.0f;
        if (across) {
            Constants c{};
            c.size[0] = s.w1;
            c.size[1] = s.h1;
            c.params[0] = s.box;
            c.params[1] = -1.0f;
            dispatch(encoder, kBox, c, {work_.get(), nullptr, nullptr}, {box_tmp_.get(), nullptr, nullptr}, s.w1, s.h1);
        }
        {
            Constants c{};
            c.size[0] = s.w1;
            c.size[1] = s.h1;
            c.size[3] = 1u;
            c.params[0] = s.box;
            c.params[1] = static_cast<float>(std::min(p.threshold, 255u));
            c.params[2] = static_cast<float>(normal ? 0u : std::min(p.channel, 3u));
            dispatch(encoder, kBox, c, {(across ? box_tmp_ : work_).get(), nullptr, nullptr}, {mask_.get(), nullptr, nullptr},
                     s.w1, s.h1);
        }
        // Level 1: the rings' Gaussians across, then down, summed at the taps' weight (normal: clamped, as the scene
        // target holds it).
        {
            Constants c{};
            c.size[0] = s.w1;
            c.size[1] = s.h1;
            c.size[2] = blurs1;
            for (unsigned i = 0; i < 3u; ++i) c.sigma[i] = w.x1[i];
            dispatch(encoder, kBlur, c, {mask_.get(), nullptr, nullptr},
                     {blur_h_[0].get(), blurs1 > 1u ? blur_h_[1].get() : nullptr, nullptr}, s.w1, s.h1);
            c.size[3] = 1u;
            for (unsigned i = 0; i < 3u; ++i) c.sigma[i] = w.y1[i];
            c.params[0] = (normal ? bloom::kLevel1Taps : bloom::kSimpleTaps) * static_cast<float>(std::min(p.tap1, 255u)) / 255.0f;
            c.params[1] = normal ? 1.0f : 0.0f;
            dispatch(encoder, kBlur, c, {blur_h_[0].get(), blurs1 > 1u ? blur_h_[1].get() : nullptr, nullptr},
                     {level1_.get(), nullptr, nullptr}, s.w1, s.h1);
        }
        // Level 2 (normal): level 1 in n x n boxes, its three rings' Gaussians.
        if (normal) {
            Constants c{};
            c.size[0] = s.w2;
            c.size[1] = s.h2;
            c.size[2] = s.n;
            c.uv[0] = static_cast<float>(s.w1);
            c.uv[1] = static_cast<float>(s.h1);
            dispatch(encoder, kDown, c, {level1_.get(), nullptr, nullptr}, {level2_in_.get(), nullptr, nullptr}, s.w2, s.h2);
            c = {};
            c.size[0] = s.w2;
            c.size[1] = s.h2;
            c.size[2] = 3u;
            for (unsigned i = 0; i < 3u; ++i) c.sigma[i] = w.x2[i];
            dispatch(encoder, kBlur, c, {level2_in_.get(), nullptr, nullptr},
                     {blur2_h_[0].get(), blur2_h_[1].get(), blur2_h_[2].get()}, s.w2, s.h2);
            c.size[3] = 1u;
            for (unsigned i = 0; i < 3u; ++i) c.sigma[i] = w.y2[i];
            c.params[0] = bloom::kLevel2Taps * static_cast<float>(std::min(p.tap2, 255u)) / 255.0f;
            c.params[1] = 1.0f;
            dispatch(encoder, kBlur, c, {blur2_h_[0].get(), blur2_h_[1].get(), blur2_h_[2].get()},
                     {level2_.get(), nullptr, nullptr}, s.w2, s.h2);
        }
        [compute endEncoding];
    }
    Constants& c = composite_constants_;
    c.params[0] = static_cast<float>(std::min(p.intensity, 255u)) / 255.0f;
    c.params[1] = std::max(p.strength, 0.0f);
    c.params[2] = normal ? 1.0f : 2.0f;
    c.uv[0] = 1.0f / static_cast<float>(s.f * s.w1);
    c.uv[1] = 1.0f / static_cast<float>(s.f * s.h1);
    c.uv[2] = 1.0f / static_cast<float>(s.f * s.n * std::max(s.w2, 1u));
    c.uv[3] = 1.0f / static_cast<float>(s.f * s.n * std::max(s.h2, 1u));
    composite_inputs_[0] = normal ? keep_.get() : source_.get();
    composite_inputs_[1] = level1_.get();
    composite_inputs_[2] = normal ? level2_.get() : level1_.get();
    pending_ = true;
    return true;
}

bool GameBloom::prepare_draw_back(Format color_format, unsigned samples) {
    return ready_ && composite_pipeline(color_format, samples) != nullptr;
}

void GameBloom::draw_back(Object encoder, Format color_format, unsigned samples, const Texture& picture, unsigned width,
                          unsigned height) {
    const Object pipeline = composite_pipeline(color_format, samples);  // made by prepare_draw_back
    const Constants constants{};  // kind 0: the picture alone
    id<MTLRenderCommandEncoder> const pass = (__bridge id<MTLRenderCommandEncoder>)encoder;
    [pass setRenderPipelineState:(__bridge id<MTLRenderPipelineState>)pipeline];
    [pass setDepthStencilState:(__bridge id<MTLDepthStencilState>)no_depth_];
    [pass setCullMode:MTLCullModeNone];
    [pass setTriangleFillMode:MTLTriangleFillModeFill];
    [pass setDepthClipMode:MTLDepthClipModeClip];
    [pass setDepthBias:0.0f slopeScale:0.0f clamp:0.0f];
    for (unsigned i = 0; i < 3u; ++i) {
        [pass setFragmentTexture:metal(&picture) atIndex:i];
    }
    [pass setFragmentSamplerState:(__bridge id<MTLSamplerState>)sampler_ atIndex:kLinearSampler];
    [pass setVertexBytes:&constants length:sizeof(Constants) atIndex:kUtilityPushBuffer];
    [pass setFragmentBytes:&constants length:sizeof(Constants) atIndex:kUtilityPushBuffer];
    [pass setViewport:MTLViewport{0.0, 0.0, static_cast<double>(width), static_cast<double>(height), 0.0, 1.0}];
    [pass setScissorRect:MTLScissorRect{0u, 0u, width, height}];
    [pass drawPrimitives:MTLPrimitiveTypeTriangle vertexStart:0 vertexCount:3];
}

void GameBloom::finish(Object encoder, Format color_format, unsigned samples) {
    if (!std::exchange(pending_, false)) {
        return;
    }
    const Object pipeline = composite_pipeline(color_format, samples);
    if (pipeline == nullptr) {
        return;
    }
    id<MTLRenderCommandEncoder> const pass = (__bridge id<MTLRenderCommandEncoder>)encoder;
    [pass setRenderPipelineState:(__bridge id<MTLRenderPipelineState>)pipeline];
    [pass setDepthStencilState:(__bridge id<MTLDepthStencilState>)no_depth_];
    [pass setCullMode:MTLCullModeNone];
    [pass setTriangleFillMode:MTLTriangleFillModeFill];
    [pass setDepthClipMode:MTLDepthClipModeClip];
    [pass setDepthBias:0.0f slopeScale:0.0f clamp:0.0f];
    for (unsigned i = 0; i < 3u; ++i) {
        [pass setFragmentTexture:metal(composite_inputs_[i]) atIndex:i];
    }
    [pass setFragmentSamplerState:(__bridge id<MTLSamplerState>)sampler_ atIndex:kLinearSampler];
    [pass setVertexBytes:&composite_constants_ length:sizeof(Constants) atIndex:kUtilityPushBuffer];
    [pass setFragmentBytes:&composite_constants_ length:sizeof(Constants) atIndex:kUtilityPushBuffer];
    [pass setViewport:MTLViewport{0.0, 0.0, static_cast<double>(composite_width_), static_cast<double>(composite_height_), 0.0, 1.0}];
    [pass setScissorRect:MTLScissorRect{0u, 0u, composite_width_, composite_height_}];
    [pass drawPrimitives:MTLPrimitiveTypeTriangle vertexStart:0 vertexCount:3];
}

}  // namespace galaxy::render::mtl
