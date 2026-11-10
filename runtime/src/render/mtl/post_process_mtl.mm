// SPDX-License-Identifier: GPL-3.0-only
// Presentation post-processing on Metal (mtl/post_process_mtl.h), pass for pass vk/post_process_vk.cpp's.
#import <Metal/Metal.h>

#include "galaxy/render/mtl/post_process_mtl.h"

#include "galaxy/render/mtl/utility_msl.h"

#include "AreaTex.h"
#include "SearchTex.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <iostream>
#include <string>
#include <utility>

namespace galaxy::render::mtl {

namespace {

// SMAA's blending weights: one in each channel, the alpha's as fine as the rest.
constexpr Format kWeightsFormat = Format::Rgba8;
constexpr Format kEdgesFormat = Format::Rg8;
constexpr std::uint8_t kRgb = MTLColorWriteMaskRed | MTLColorWriteMaskGreen | MTLColorWriteMaskBlue;
constexpr std::uint8_t kRgba = kRgb | MTLColorWriteMaskAlpha;
// post_process.hlsl's registers (vk/utility_spirv_sources.h kPostBindings): textures t0-t2 at 0-2, the linear and
// point samplers in set 1, the constants pushed.
constexpr NSUInteger kLinearSampler = kUtilitySetStride + 0u;
constexpr NSUInteger kPointSampler = kUtilitySetStride + 1u;

// Whether a program is compiled for each SMAA quality.
constexpr bool kFollowsQuality[] = {
#define POST(name, pass, library, quality, stage) quality,
#include "galaxy/render/vk/post_programs_vk.inc"
#undef POST
};

template <std::size_t N>
void set_reason(std::array<char, N>& out, const char* text) noexcept {
    std::snprintf(out.data(), out.size(), "%s", text);
}

[[nodiscard]] id<MTLDevice> device() noexcept {
    return (__bridge id<MTLDevice>)Context::get().device();
}
[[nodiscard]] id<MTLTexture> metal(const std::shared_ptr<Texture>& texture) noexcept {
    return texture ? (__bridge id<MTLTexture>)texture->texture : nil;
}
[[nodiscard]] Object own(id object) noexcept {
    return (__bridge_retained void*)object;
}

id<MTLSamplerState> make_sampler(MTLSamplerMinMagFilter filter) {
    MTLSamplerDescriptor* const descriptor = [MTLSamplerDescriptor new];
    descriptor.minFilter = descriptor.magFilter = filter;
    descriptor.mipFilter = MTLSamplerMipFilterNearest;
    descriptor.sAddressMode = descriptor.tAddressMode = descriptor.rAddressMode = MTLSamplerAddressModeClampToEdge;
    return [device() newSamplerStateWithDescriptor:descriptor];
}

// SMAA's edges pass (colour and stencil cleared, both kept) or weights pass (colour cleared, the stencil read).
id<MTLRenderCommandEncoder> begin_pass(Object commands, const std::shared_ptr<Texture>& color, const std::shared_ptr<Texture>& stencil,
                                       bool clear_color, bool clear_stencil, NSString* label) {
    MTLRenderPassDescriptor* const descriptor = [MTLRenderPassDescriptor renderPassDescriptor];
    descriptor.colorAttachments[0].texture = metal(color);
    descriptor.colorAttachments[0].loadAction = clear_color ? MTLLoadActionClear : MTLLoadActionDontCare;
    descriptor.colorAttachments[0].clearColor = MTLClearColorMake(0.0, 0.0, 0.0, 0.0);
    descriptor.colorAttachments[0].storeAction = MTLStoreActionStore;
    if (stencil) {
        descriptor.stencilAttachment.texture = metal(stencil);
        descriptor.stencilAttachment.loadAction = clear_stencil ? MTLLoadActionClear : MTLLoadActionLoad;
        descriptor.stencilAttachment.clearStencil = 0u;
        descriptor.stencilAttachment.storeAction = clear_stencil ? MTLStoreActionStore : MTLStoreActionDontCare;
    }
    id<MTLRenderCommandEncoder> const pass =
        [(__bridge id<MTLCommandBuffer>)commands renderCommandEncoderWithDescriptor:descriptor];
    pass.label = label;
    return pass;
}

}  // namespace

std::size_t PostProcessor::PipelineKeyHasher::operator()(const PipelineKey& key) const noexcept {
    std::uint64_t hash = 1469598103934665603ull;
    for (const std::uint64_t value : {std::uint64_t{key.vertex}, std::uint64_t{key.fragment}, static_cast<std::uint64_t>(key.color),
                                      static_cast<std::uint64_t>(key.depth), std::uint64_t{key.write_mask},
                                      std::uint64_t{key.samples}, std::uint64_t{key.quality}}) {
        hash ^= value;
        hash *= 1099511628211ull;
    }
    return static_cast<std::size_t>(hash);
}

void PostProcessor::initialize(const PostProcessSettings& settings, Format scene_format, Format depth_format) {
    shutdown();
    settings_ = settings;
    settings_.smaa_quality = std::min<std::uint8_t>(settings_.smaa_quality, 3u);
    status_ = PostProcessStatus{};
    status_.live_aa = static_cast<std::uint8_t>(settings_.aa);
    status_.live_quality = settings_.smaa_quality;
    set_reason(status_.smaa_reason, "STARTING");
    set_reason(status_.fxaa_reason, "STARTING");
    scene_format_ = scene_format;
    depth_format_ = depth_format;
    to_build_ = true;
    // Every program, at every SMAA quality: a settings change then makes pipelines only.
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
                for (unsigned quality = 0; quality < 4u; ++quality) {
                    if (quality != 0u && !kFollowsQuality[program]) {
                        made.at[program][quality] = retain(made.at[program][0]);
                        continue;
                    }
                    NSError* error = nil;
                    id<MTLLibrary> const library = [from newLibraryWithSource:@(post_msl(program, quality)) options:options error:&error];
                    id<MTLFunction> const function = library != nil ? [library newFunctionWithName:@"main0"] : nil;
                    if (function == nil) {
                        std::fprintf(stderr, "[post-process] program %u (quality %u) did not compile: %s\n", program, quality,
                                     error != nil ? error.localizedDescription.UTF8String : "no main0");
                        made.ok = false;
                        continue;
                    }
                    made.at[program][quality] = own(function);
                }
            }
        }
        return made;
    });
}

void PostProcessor::release_pipelines() noexcept {
    for (auto& [key, state] : pipelines_) {
        release(state);
    }
    pipelines_.clear();
}

void PostProcessor::shutdown() noexcept {
    if (compiling_.valid()) {
        functions_ = compiling_.get();
    }
    release_pipelines();
    for (auto& program : functions_.at) {
        for (Object& function : program) {
            release(function);
            function = nullptr;
        }
    }
    functions_.ok = false;
    for (Object& state : stencil_states_) {
        release(state);
        state = nullptr;
    }
    for (Object& sampler : samplers_) {
        release(sampler);
        sampler = nullptr;
    }
    area_texture_.reset();
    search_texture_.reset();
    edges_ = weights_ = stencil_ = antialiased_ = scaled_ = spatial_output_ = {};
    spatial_.reset();
    ready_ = false;
    to_build_ = false;
}

Object PostProcessor::function(Program program) const {
    return functions_.at[program][kFollowsQuality[program] ? settings_.smaa_quality : 0u];
}

Object PostProcessor::pipeline(Program vertex, Program fragment, Format color, Format depth, std::uint8_t write_mask,
                               unsigned samples) {
    PipelineKey key;
    key.vertex = static_cast<std::uint8_t>(vertex);
    key.fragment = static_cast<std::uint8_t>(fragment);
    key.color = color;
    key.depth = depth;
    key.write_mask = write_mask;
    key.samples = static_cast<std::uint8_t>(samples);
    key.quality = kFollowsQuality[vertex] || kFollowsQuality[fragment] ? settings_.smaa_quality : 0u;
    if (const auto it = pipelines_.find(key); it != pipelines_.end()) {
        return it->second;
    }
    const Object vs = function(vertex);
    const Object ps = function(fragment);
    Object made = nullptr;
    if (vs != nullptr && ps != nullptr) {
        @autoreleasepool {
            MTLRenderPipelineDescriptor* const descriptor = [MTLRenderPipelineDescriptor new];
            descriptor.vertexFunction = (__bridge id<MTLFunction>)vs;
            descriptor.fragmentFunction = (__bridge id<MTLFunction>)ps;
            descriptor.colorAttachments[0].pixelFormat = static_cast<MTLPixelFormat>(color);
            descriptor.colorAttachments[0].writeMask = write_mask;
            if (depth == Format::Stencil8) {
                descriptor.stencilAttachmentPixelFormat = MTLPixelFormatStencil8;
            } else if (depth != Format::None) {
                descriptor.depthAttachmentPixelFormat = static_cast<MTLPixelFormat>(depth);
                descriptor.stencilAttachmentPixelFormat = static_cast<MTLPixelFormat>(depth);
            }
            descriptor.rasterSampleCount = samples;
            NSError* error = nil;
            id<MTLRenderPipelineState> const state = [device() newRenderPipelineStateWithDescriptor:descriptor error:&error];
            if (state == nil) {
                std::fprintf(stderr, "[post-process] a pipeline could not be made: %s\n",
                             error != nil ? error.localizedDescription.UTF8String : "");
            } else {
                made = own(state);
            }
        }
    }
    pipelines_.emplace(key, made);
    return made;
}

bool PostProcessor::build_pipelines() {
    @autoreleasepool {
        samplers_[0] = own(make_sampler(MTLSamplerMinMagFilterLinear));
        samplers_[1] = own(make_sampler(MTLSamplerMinMagFilterNearest));
        MTLDepthStencilDescriptor* const descriptor = [MTLDepthStencilDescriptor new];
        descriptor.depthCompareFunction = MTLCompareFunctionAlways;
        descriptor.depthWriteEnabled = NO;
        stencil_states_[static_cast<std::size_t>(Stencil::None)] = own([device() newDepthStencilStateWithDescriptor:descriptor]);
        MTLStencilDescriptor* const stencil = [MTLStencilDescriptor new];
        stencil.stencilFailureOperation = MTLStencilOperationKeep;
        stencil.depthFailureOperation = MTLStencilOperationKeep;
        stencil.depthStencilPassOperation = MTLStencilOperationReplace;
        stencil.stencilCompareFunction = MTLCompareFunctionAlways;
        stencil.readMask = 0xFFu;
        stencil.writeMask = 0xFFu;
        descriptor.frontFaceStencil = stencil;
        descriptor.backFaceStencil = stencil;
        stencil_states_[static_cast<std::size_t>(Stencil::Mark)] = own([device() newDepthStencilStateWithDescriptor:descriptor]);
        stencil.depthStencilPassOperation = MTLStencilOperationKeep;
        stencil.stencilCompareFunction = MTLCompareFunctionEqual;
        descriptor.frontFaceStencil = stencil;
        descriptor.backFaceStencil = stencil;
        stencil_states_[static_cast<std::size_t>(Stencil::Test)] = own([device() newDepthStencilStateWithDescriptor:descriptor]);
    }
    // The scaling passes into a picture between passes are required; each AA pass is optional and only reported
    // when it cannot be built. The passes into the window are made for its format at the first present.
    if (pipeline(kFullscreenVs, kEasuPs, scene_format_, Format::None, kRgba) == nullptr ||
        pipeline(kFullscreenVs, kResamplePs, scene_format_, Format::None, kRgba) == nullptr) {
        return false;
    }
    const bool smaa = pipeline(kSmaaEdgesVs, kSmaaEdgesPs, kEdgesFormat, Format::Stencil8, kRgba) != nullptr &&
                      pipeline(kSmaaWeightsVs, kSmaaWeightsPs, kWeightsFormat, Format::Stencil8, kRgba) != nullptr &&
                      pipeline(kSmaaBlendVs, kSmaaBlendPs, scene_format_, Format::None, kRgba) != nullptr &&
                      pipeline(kSmaaBlendVs, kSmaaBlendKeepPs, scene_format_, depth_format_, kRgb) != nullptr;
    const bool fxaa = pipeline(kFullscreenVs, kFxaaPs, scene_format_, Format::None, kRgba) != nullptr &&
                      pipeline(kFullscreenVs, kFxaaPs, scene_format_, depth_format_, kRgb) != nullptr;
    status_.smaa_ok = smaa;
    status_.fxaa_ok = fxaa;
    set_reason(status_.smaa_reason, smaa ? "" : "PIPELINES");
    set_reason(status_.fxaa_reason, fxaa ? "" : "PIPELINES");
    return true;
}

bool PostProcessor::poll_ready() {
    if (!to_build_) {
        return ready_;
    }
    if (!compiling_.valid() || compiling_.wait_for(std::chrono::seconds(0)) != std::future_status::ready) {
        return false;
    }
    to_build_ = false;
    functions_ = compiling_.get();
    if (!functions_.ok || !build_pipelines()) {
        std::cerr << "[post-process] scaling pipelines failed; plain blit\n";
        set_reason(status_.skip_reason, "INIT FAILED");
        release_pipelines();
        return false;
    }
    self_test();
    status_.initialized = true;
    ready_ = true;
    std::cerr << "[post-process] self-test smaa=" << (status_.smaa_ok ? "pass" : "fail")
              << (status_.smaa_ok ? "" : " smaa-reason=") << status_.smaa_reason.data() << " fxaa="
              << (status_.fxaa_ok ? "pass" : "fail") << (status_.fxaa_ok ? "" : " fxaa-reason=") << status_.fxaa_reason.data()
              << '\n';
    return true;
}

void PostProcessor::apply_settings(const PostProcessSettings& settings) {
    PostProcessSettings next = settings;
    next.smaa_quality = std::min<std::uint8_t>(next.smaa_quality, 3u);
    if (ready_ && status_.smaa_ok && next.smaa_quality != settings_.smaa_quality) {
        // The quality's edges and weights pipelines, made here: a settings change, not a frame.
        const std::uint8_t before = settings_.smaa_quality;
        settings_.smaa_quality = next.smaa_quality;
        if (pipeline(kSmaaEdgesVs, kSmaaEdgesPs, kEdgesFormat, Format::Stencil8, kRgba) == nullptr ||
            pipeline(kSmaaWeightsVs, kSmaaWeightsPs, kWeightsFormat, Format::Stencil8, kRgba) == nullptr) {
            std::cerr << "[post-process] SMAA quality " << unsigned(next.smaa_quality) << " pipelines failed; keeping "
                      << unsigned(before) << '\n';
            next.smaa_quality = before;
        }
    }
    settings_ = next;
    status_.live_aa = static_cast<std::uint8_t>(settings_.aa);
    status_.live_quality = settings_.smaa_quality;
}

bool PostProcessor::ensure_target(Target& target, unsigned width, unsigned height, Format format) {
    target.used = clock_;
    if (target.texture && target.width == width && target.height == height) {
        return true;
    }
    target = {};
    target.texture = Context::get().create_texture(width, height, 1u, format,
                                                   format == Format::Stencil8 ? std::uint32_t{kUseTarget} : kUseTarget | kUseSample,
                                                   Storage::Private, "post-process target");
    if (!target.texture) {
        return false;
    }
    target.width = width;
    target.height = height;
    target.used = clock_;
    return true;
}

bool PostProcessor::ensure_smaa_targets(unsigned width, unsigned height) {
    smaa_used_ = clock_;
    if (stencil_.texture && stencil_.width == width && stencil_.height == height && edges_.texture && weights_.texture) {
        return true;
    }
    edges_ = weights_ = stencil_ = {};
    return ensure_target(stencil_, width, height, Format::Stencil8) && ensure_target(edges_, width, height, kEdgesFormat) &&
           ensure_target(weights_, width, height, kWeightsFormat);
}

// Hands back the targets no pass has asked for in 600 scene AA calls (SMAA's after a switch to FXAA or off, the
// scaling intermediate after a window size that needs none); the next pass that wants one makes it again.
void PostProcessor::trim_idle_targets() {
    constexpr std::uint64_t kTargetIdleFrames = 600u;
    if (stencil_.texture && clock_ > smaa_used_ + kTargetIdleFrames) {
        edges_ = weights_ = stencil_ = {};
    }
    for (Target* target : {&antialiased_, &scaled_, &spatial_output_}) {
        if (target->texture && clock_ > target->used + kTargetIdleFrames) {
            *target = {};
        }
    }
    if (!spatial_output_.texture) {
        spatial_.reset();
    }
}

void PostProcessor::draw(Object encoder, Object pipeline_state, Stencil stencil, const Object* inputs, unsigned input_count,
                         const gpu::Rect& viewport, const PostConstants& constants) const {
    id<MTLRenderCommandEncoder> const pass = (__bridge id<MTLRenderCommandEncoder>)encoder;
    [pass setRenderPipelineState:(__bridge id<MTLRenderPipelineState>)pipeline_state];
    [pass setDepthStencilState:(__bridge id<MTLDepthStencilState>)stencil_states_[static_cast<std::size_t>(stencil)]];
    [pass setStencilReferenceValue:1u];
    [pass setCullMode:MTLCullModeNone];
    [pass setTriangleFillMode:MTLTriangleFillModeFill];
    [pass setDepthClipMode:MTLDepthClipModeClip];
    [pass setDepthBias:0.0f slopeScale:0.0f clamp:0.0f];
    for (unsigned i = 0; i < input_count; ++i) {
        [pass setFragmentTexture:(__bridge id<MTLTexture>)inputs[i] atIndex:i];
    }
    [pass setFragmentSamplerState:(__bridge id<MTLSamplerState>)samplers_[0] atIndex:kLinearSampler];
    [pass setFragmentSamplerState:(__bridge id<MTLSamplerState>)samplers_[1] atIndex:kPointSampler];
    [pass setVertexBytes:&constants length:sizeof(constants) atIndex:kUtilityPushBuffer];
    [pass setFragmentBytes:&constants length:sizeof(constants) atIndex:kUtilityPushBuffer];
    const std::int32_t left = std::max(viewport.left, 0);
    const std::int32_t top = std::max(viewport.top, 0);
    [pass setViewport:MTLViewport{static_cast<double>(viewport.left), static_cast<double>(viewport.top),
                                  static_cast<double>(viewport.right - viewport.left),
                                  static_cast<double>(viewport.bottom - viewport.top), 0.0, 1.0}];
    if (viewport.right > left && viewport.bottom > top) {
        [pass setScissorRect:MTLScissorRect{static_cast<NSUInteger>(left), static_cast<NSUInteger>(top),
                                            static_cast<NSUInteger>(viewport.right - left),
                                            static_cast<NSUInteger>(viewport.bottom - top)}];
    }
    [pass drawPrimitives:MTLPrimitiveTypeTriangle vertexStart:0 vertexCount:3];
}

// SMAA's edges and weights, or nothing for FXAA, from `source` (a copy of the picture).
bool PostProcessor::record_aa(Object commands, AaRun pass, Object source, unsigned width, unsigned height) {
    if (pass != AaRun::kSmaa) {
        return true;
    }
    if (!ensure_smaa_targets(width, height)) {
        return false;
    }
    const Object edges_pipeline = pipeline(kSmaaEdgesVs, kSmaaEdgesPs, kEdgesFormat, Format::Stencil8, kRgba);
    const Object weights_pipeline = pipeline(kSmaaWeightsVs, kSmaaWeightsPs, kWeightsFormat, Format::Stencil8, kRgba);
    if (edges_pipeline == nullptr || weights_pipeline == nullptr) {
        return false;
    }
    const gpu::Rect full = gpu::rect(0, 0, static_cast<std::int32_t>(width), static_cast<std::int32_t>(height));
    PostConstants c{};
    set_post_input_size(c, width, height);
    @autoreleasepool {
        id<MTLRenderCommandEncoder> const edges = begin_pass(commands, edges_.texture, stencil_.texture, true, true, @"SMAA edges");
        draw((__bridge void*)edges, edges_pipeline, Stencil::Mark, &source, 1u, full, c);
        [edges endEncoding];
        id<MTLRenderCommandEncoder> const weights =
            begin_pass(commands, weights_.texture, stencil_.texture, true, false, @"SMAA weights");
        const Object inputs[3] = {edges_.texture->texture, area_texture_->texture, search_texture_->texture};
        draw((__bridge void*)weights, weights_pipeline, Stencil::Test, inputs, 3u, full, c);
        [weights endEncoding];
    }
    return true;
}

AaRun PostProcessor::prepare_scene_aa(Object commands, const Texture& color, unsigned width, unsigned height,
                                      unsigned picture_width, unsigned picture_height, unsigned display_width,
                                      unsigned display_height) {
    if (!ready_ || width == 0u || height == 0u) {
        return AaRun::kNone;
    }
    ++clock_;
    trim_idle_targets();
    const PostProcessPlan plan = plan_post_process(picture_width, picture_height, display_width, display_height, settings_,
                                                   status_.smaa_ok, status_.fxaa_ok);
    AaRun pass = plan.smaa ? AaRun::kSmaa : plan.fxaa ? AaRun::kFxaa : AaRun::kNone;
    // The passes read a copy of the scene and write the scene target.
    if (pass != AaRun::kNone && !ensure_target(antialiased_, width, height, scene_format_)) {
        pass = AaRun::kNone;
    }
    if (pass != AaRun::kNone) {
        @autoreleasepool {
            id<MTLBlitCommandEncoder> const blit = [(__bridge id<MTLCommandBuffer>)commands blitCommandEncoder];
            blit.label = @"scene copy for AA";
            [blit copyFromTexture:(__bridge id<MTLTexture>)color.texture
                      sourceSlice:0
                      sourceLevel:0
                     sourceOrigin:MTLOriginMake(0, 0, 0)
                       sourceSize:MTLSizeMake(width, height, 1)
                        toTexture:metal(antialiased_.texture)
                 destinationSlice:0
                 destinationLevel:0
                destinationOrigin:MTLOriginMake(0, 0, 0)];
            [blit endEncoding];
        }
        if (!record_aa(commands, pass, antialiased_.texture->texture, width, height)) {
            pass = status_.fxaa_ok ? AaRun::kFxaa : AaRun::kNone;
        }
    }
    ++status_.frames;
    status_.last_run = pass;
    status_.last_wanted = plan.aa_wanted;
    status_.last_supersampled = plan.supersampled;
    status_.smaa_frames += pass == AaRun::kSmaa;
    status_.fxaa_frames += pass == AaRun::kFxaa;
    if (plan.aa_wanted) {
        ++status_.aa_wanted_frames;
        if (pass == AaRun::kNone) {
            ++status_.aa_skipped_frames;
            set_reason(status_.skip_reason, plan.smaa || plan.fxaa ? "NO MEMORY" : "FAILED");
        }
    }
    return pass;
}

// The last pass of the scene's AA, in the scene pass: over MSAA every sample takes the result.
void PostProcessor::finish_scene_aa(Object encoder, AaRun pass, unsigned width, unsigned height, unsigned samples) {
    if (pass == AaRun::kNone) {
        return;
    }
    const gpu::Rect full = gpu::rect(0, 0, static_cast<std::int32_t>(width), static_cast<std::int32_t>(height));
    PostConstants c{};
    set_post_input_size(c, width, height);
    if (pass == AaRun::kFxaa) {
        const Object state = pipeline(kFullscreenVs, kFxaaPs, scene_format_, depth_format_, kRgb, samples);
        const Object inputs[1] = {antialiased_.texture->texture};
        if (state != nullptr) {
            draw(encoder, state, Stencil::None, inputs, 1u, full, c);
        }
    } else {
        const Object state = pipeline(kSmaaBlendVs, kSmaaBlendKeepPs, scene_format_, depth_format_, kRgb, samples);
        const Object inputs[2] = {antialiased_.texture->texture, weights_.texture->texture};
        if (state != nullptr) {
            draw(encoder, state, Stencil::None, inputs, 2u, full, c);
        }
    }
}

PostProcessor::PresentStep PostProcessor::prepare_present(Object commands, const Texture& source, unsigned source_width,
                                                          unsigned source_height, unsigned target_width,
                                                          unsigned target_height, Format target_format, bool hdr) {
    // The scene was antialiased before the 2D layer (prepare_scene_aa).
    PostProcessSettings scaling = settings_;
    scaling.aa = PostProcessSettings::Aa::kOff;
    PostProcessPlan plan = plan_post_process(source_width, source_height, target_width, target_height, scaling, status_.smaa_ok,
                                             status_.fxaa_ok);
    PresentStep step{};
    step.ten_bit = target_format == Format::Bgr10a2 || target_format == Format::Rgb10a2;
    if (step.ten_bit && plan.resample_to_target && source_width == target_width && source_height == target_height &&
        !hdr) {
        // The picture at the target's own size, into a target that keeps its ten bits: every pixel is its source's,
        // unchanged. The renderer's plain copy draws that (no pipeline here), without the box filter's arithmetic for
        // each pixel of the display.
        return step;
    }
    set_post_input_size(step.constants, source_width, source_height);
    const Object input = source.texture;
    step.input = input;
    // Enlarging on both axes with sharpening: one pass straight into the target.
    if (plan.resample && plan.rcas && target_width >= source_width && target_height >= source_height) {
        if (const Object last = pipeline(kFullscreenVs, kRcasScaledPs, target_format, Format::None, kRgba); last != nullptr) {
            set_fsr_rcas_constants(step.constants, settings_.sharpness);
            step.constants.params[0] = static_cast<float>(source_width);
            step.constants.params[1] = static_cast<float>(source_height);
            step.constants.params[2] = static_cast<float>(target_width);
            step.constants.params[3] = static_cast<float>(target_height);
            step.pipeline = last;
            return step;
        }
    }
    // MetalFX's spatial scaler in EASU's place: the enlarged picture, then RCAS of it or its plain copy into the target.
    if (plan.easu && settings_.upscaler == PostProcessSettings::Upscaler::kMetalFx &&
        record_spatial(commands, source, source_width, source_height, target_width, target_height)) {
        step.input = spatial_output_.texture->texture;
        step.constants = {};
        set_post_input_size(step.constants, target_width, target_height);
        step.constants.params[0] = static_cast<float>(target_width);
        step.constants.params[1] = static_cast<float>(target_height);
        if (plan.rcas) {
            set_fsr_rcas_constants(step.constants, settings_.sharpness);
            step.pipeline = pipeline(kFullscreenVs, kRcasPs, target_format, Format::None, kRgba);
        } else {
            step.constants.params[2] = static_cast<float>(target_width);
            step.constants.params[3] = static_cast<float>(target_height);
            step.pipeline = pipeline(kFullscreenVs, kResamplePs, target_format, Format::None, kRgba);
        }
        return step;
    }
    // A window-sized intermediate for a scaling step that RCAS then reads.
    if ((plan.easu || plan.resample) && plan.rcas && !ensure_target(scaled_, target_width, target_height, scene_format_)) {
        plan.rcas = false;
        plan.resample = !plan.easu;
        plan.resample_to_target = plan.resample;
    }
    const gpu::Rect window = gpu::rect(0, 0, static_cast<std::int32_t>(target_width), static_cast<std::int32_t>(target_height));
    if (plan.easu) {
        set_fsr_easu_constants(step.constants, static_cast<float>(source_width), static_cast<float>(source_height),
                               static_cast<float>(target_width), static_cast<float>(target_height));
        if (!plan.rcas) {
            step.pipeline = pipeline(kFullscreenVs, kEasuPs, target_format, Format::None, kRgba);
            return step;
        }
        @autoreleasepool {
            id<MTLRenderCommandEncoder> const pass = begin_pass(commands, scaled_.texture, nullptr, false, false, @"FSR 1 EASU");
            draw((__bridge void*)pass, pipeline(kFullscreenVs, kEasuPs, scene_format_, Format::None, kRgba), Stencil::None, &input,
                 1u, window, step.constants);
            [pass endEncoding];
        }
    } else if (plan.resample) {
        step.constants.params[0] = static_cast<float>(source_width);
        step.constants.params[1] = static_cast<float>(source_height);
        step.constants.params[2] = static_cast<float>(target_width);
        step.constants.params[3] = static_cast<float>(target_height);
        if (plan.resample_to_target) {
            step.pipeline = pipeline(kFullscreenVs, kResamplePs, target_format, Format::None, kRgba);
            return step;
        }
        @autoreleasepool {
            id<MTLRenderCommandEncoder> const pass = begin_pass(commands, scaled_.texture, nullptr, false, false, @"resample");
            draw((__bridge void*)pass, pipeline(kFullscreenVs, kResamplePs, scene_format_, Format::None, kRgba), Stencil::None,
                 &input, 1u, window, step.constants);
            [pass endEncoding];
        }
    }
    // RCAS of the scaled picture (or of the source at its own size).
    if (plan.easu || plan.resample) {
        step.input = scaled_.texture->texture;
    }
    step.constants = {};
    set_post_input_size(step.constants, target_width, target_height);
    set_fsr_rcas_constants(step.constants, settings_.sharpness);
    step.constants.params[0] = static_cast<float>(target_width);
    step.constants.params[1] = static_cast<float>(target_height);
    step.pipeline = pipeline(kFullscreenVs, kRcasPs, target_format, Format::None, kRgba);
    return step;
}

bool PostProcessor::record_spatial(Object commands, const Texture& source, unsigned source_width, unsigned source_height,
                                   unsigned target_width, unsigned target_height) {
    // The scaler reads the whole texture: a picture that is part of a larger one goes the other way.
    if (source.width != source_width || source.height != source_height) {
        return false;
    }
    const unsigned sizes[4] = {source_width, source_height, target_width, target_height};
    if (!spatial_ || !spatial_->matches(source_width, source_height, target_width, target_height, source.format, scene_format_)) {
        if (std::equal(std::begin(sizes), std::end(sizes), std::begin(spatial_refused_))) {
            return false;
        }
        spatial_ = create_spatial_scaler(source_width, source_height, target_width, target_height, source.format, scene_format_);
        spatial_output_ = {};
        if (!spatial_) {
            std::copy(std::begin(sizes), std::end(sizes), std::begin(spatial_refused_));
            return false;
        }
        std::fprintf(stderr, "[post-process] MetalFX spatial %ux%u -> %ux%u\n", source_width, source_height, target_width,
                     target_height);
    }
    spatial_output_.used = clock_;
    if (!spatial_output_.texture || spatial_output_.width != target_width || spatial_output_.height != target_height) {
        spatial_output_ = {};
        spatial_output_.texture = Context::get().create_texture(target_width, target_height, 1u, scene_format_,
                                                                spatial_->output_use() | kUseSample, Storage::Private,
                                                                "MetalFX spatial output");
        if (!spatial_output_.texture) {
            return false;
        }
        spatial_output_.width = target_width;
        spatial_output_.height = target_height;
        spatial_output_.used = clock_;
    }
    spatial_->encode(commands, source, *spatial_output_.texture);
    return true;
}

void PostProcessor::finish_present(Object encoder, const PresentStep& step, const gpu::Rect& viewport, const PostOutput& output) {
    PostConstants constants = step.constants;
    constants.origin[0] = viewport.left;
    constants.origin[1] = viewport.top;
    set_post_output(constants, output);
    if (step.ten_bit && !output.hdr) {
        // A target with the scene's ten bits takes the picture as it is: nothing to round, so nothing to dither.
        constants.origin[2] = 0;
    }
    draw(encoder, step.pipeline, Stencil::None, &step.input, 1u, viewport, constants);
}

bool PostProcessor::create_lookup_textures(Object commands, std::vector<std::shared_ptr<Buffer>>& staging) {
    Context& context = Context::get();
    id<MTLBlitCommandEncoder> const blit = [(__bridge id<MTLCommandBuffer>)commands blitCommandEncoder];
    blit.label = @"SMAA lookups";
    const auto upload = [&](std::shared_ptr<Texture>& texture, const unsigned char* data, unsigned width, unsigned height,
                            unsigned pitch, Format format) {
        texture = context.create_texture(width, height, 1u, format, kUseSample, Storage::Private, "SMAA lookup");
        std::shared_ptr<Buffer> buffer =
            context.create_buffer(static_cast<std::uint64_t>(pitch) * height, Storage::Shared, "SMAA lookup upload");
        if (!texture || !buffer) {
            return false;
        }
        std::memcpy(buffer->mapped, data, static_cast<std::size_t>(pitch) * height);
        [blit copyFromBuffer:(__bridge id<MTLBuffer>)buffer->buffer
                       sourceOffset:0
                  sourceBytesPerRow:pitch
                sourceBytesPerImage:NSUInteger{pitch} * height
                         sourceSize:MTLSizeMake(width, height, 1)
                          toTexture:metal(texture)
                   destinationSlice:0
                   destinationLevel:0
                  destinationOrigin:MTLOriginMake(0, 0, 0)];
        staging.push_back(std::move(buffer));
        return true;
    };
    const bool made = upload(area_texture_, areaTexBytes, AREATEX_WIDTH, AREATEX_HEIGHT, AREATEX_PITCH, Format::Rg8) &&
                      upload(search_texture_, searchTexBytes, SEARCHTEX_WIDTH, SEARCHTEX_HEIGHT, SEARCHTEX_PITCH, Format::R8);
    [blit endEncoding];
    return made;
}

void PostProcessor::self_test() {
    const auto fail = [&](AaRun pass, const char* why) {
        if (pass != AaRun::kFxaa && status_.smaa_ok) {
            status_.smaa_ok = false;
            set_reason(status_.smaa_reason, why);
        }
        if (pass != AaRun::kSmaa && status_.fxaa_ok) {
            status_.fxaa_ok = false;
            set_reason(status_.fxaa_reason, why);
        }
    };
    if (!status_.smaa_ok && !status_.fxaa_ok) {
        return;
    }
    Context& context = Context::get();
    constexpr unsigned kSize = kPostSelfTestSize;
    constexpr unsigned kPitch = kSize * 4u;
    std::array<std::uint8_t, kPitch * kSize> pattern{};
    write_post_self_test_pattern(pattern.data(), kPitch);
    const AaRun passes[2] = {AaRun::kSmaa, AaRun::kFxaa};
    bool recorded[2] = {false, false};
    std::vector<std::shared_ptr<Buffer>> staging;
    std::shared_ptr<Buffer> readback;
    std::uint64_t fence = 0u;
    @autoreleasepool {
        id<MTLCommandBuffer> const commands = [(__bridge id<MTLCommandQueue>)context.queue() commandBuffer];
        commands.label = @"post-process self-test";
        const Object list = (__bridge void*)commands;
        if (status_.smaa_ok && !create_lookup_textures(list, staging)) {
            fail(AaRun::kSmaa, "LOOKUP");
        }
        const std::shared_ptr<Buffer> upload = context.create_buffer(pattern.size(), Storage::Shared, "post-process self-test pattern");
        const std::shared_ptr<Texture> source =
            context.create_texture(kSize, kSize, 1u, scene_format_, kUseSample, Storage::Private, "post-process self-test source");
        readback = context.create_buffer(2u * pattern.size(), Storage::Shared, "post-process self-test");
        if (!upload || !source || !readback) {
            fail(AaRun::kNone, "TEST SETUP");
            return;
        }
        staging.push_back(upload);
        rgba8_to_ten_bit(pattern.data(), reinterpret_cast<std::uint8_t*>(upload->mapped), kSize, kSize, kPitch);
        {
            id<MTLBlitCommandEncoder> const blit = [commands blitCommandEncoder];
            [blit copyFromBuffer:(__bridge id<MTLBuffer>)upload->buffer
                           sourceOffset:0
                      sourceBytesPerRow:kPitch
                    sourceBytesPerImage:pattern.size()
                             sourceSize:MTLSizeMake(kSize, kSize, 1)
                              toTexture:metal(source)
                       destinationSlice:0
                       destinationLevel:0
                      destinationOrigin:MTLOriginMake(0, 0, 0)];
            [blit endEncoding];
        }
        for (unsigned i = 0; i < 2u; ++i) {
            const bool ok = passes[i] == AaRun::kSmaa ? status_.smaa_ok : status_.fxaa_ok;
            if (!ok) continue;
            if (!ensure_target(antialiased_, kSize, kSize, scene_format_) || !record_aa(list, passes[i], source->texture, kSize, kSize)) {
                fail(passes[i], "NO MEMORY");
                continue;
            }
            const gpu::Rect full = gpu::rect(0, 0, static_cast<std::int32_t>(kSize), static_cast<std::int32_t>(kSize));
            PostConstants c{};
            set_post_input_size(c, kSize, kSize);
            id<MTLRenderCommandEncoder> const pass = begin_pass(list, antialiased_.texture, nullptr, false, false, @"AA self-test");
            if (passes[i] == AaRun::kSmaa) {
                const Object inputs[2] = {source->texture, weights_.texture->texture};
                draw((__bridge void*)pass, pipeline(kSmaaBlendVs, kSmaaBlendPs, scene_format_, Format::None, kRgba), Stencil::None,
                     inputs, 2u, full, c);
            } else {
                const Object inputs[1] = {source->texture};
                draw((__bridge void*)pass, pipeline(kFullscreenVs, kFxaaPs, scene_format_, Format::None, kRgba), Stencil::None,
                     inputs, 1u, full, c);
            }
            [pass endEncoding];
            id<MTLBlitCommandEncoder> const out = [commands blitCommandEncoder];
            [out copyFromTexture:metal(antialiased_.texture)
                             sourceSlice:0
                             sourceLevel:0
                            sourceOrigin:MTLOriginMake(0, 0, 0)
                              sourceSize:MTLSizeMake(kSize, kSize, 1)
                                toBuffer:(__bridge id<MTLBuffer>)readback->buffer
                       destinationOffset:i * pattern.size()
                  destinationBytesPerRow:kPitch
                destinationBytesPerImage:pattern.size()];
            [out endEncoding];
            recorded[i] = true;
        }
        fence = context.commit(list);
    }
    if (!context.wait(fence, 5'000'000'000ull)) {
        fail(AaRun::kNone, "GPU TIMEOUT");
        return;
    }
    for (unsigned i = 0; i < 2u; ++i) {
        if (!recorded[i]) continue;
        std::array<std::uint8_t, kPitch * kSize> result_rgba{};
        ten_bit_to_rgba8(reinterpret_cast<const std::uint8_t*>(readback->mapped) + i * pattern.size(), result_rgba.data(), kSize,
                         kSize, kPitch, kPitch);
        const PostSelfTestResult result = check_post_self_test(pattern.data(), result_rgba.data(), kPitch);
        std::cerr << "[post-process] self-test " << (passes[i] == AaRun::kSmaa ? "smaa" : "fxaa") << " changed=" << result.changed
                  << " stray=" << result.stray << '\n';
        if (!result.passed) fail(passes[i], "SELF-TEST");
    }
    // The 32 x 32 targets go now instead of at the first frame.
    edges_ = weights_ = stencil_ = antialiased_ = {};
}

}  // namespace galaxy::render::mtl
