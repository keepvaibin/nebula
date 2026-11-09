// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2024, Qualcomm Innovation Center, Inc. All rights reserved.
// Adapted for Nebula from Snapdragon Game Super Resolution 2, two-pass fragment variant
// (sgsr/v2/include/glsl_2_pass_fs, github.com/SnapdragonStudios/snapdragon-gsr d926f07); license in
// third_party/snapdragon-gsr/LICENSE.txt.
//
// The jittered 3D scene to the output's size, before the 2D layer (temporal_upscaler_vk.h's inputs):
// - ConvertPS, at the render size: how much each pixel's depth stands apart from its neighbours (depthclip). The scene's
//   motion is the renderer's own (render pixels, current -> previous, every pixel), so this pass reads no velocity and
//   reprojects no depth, and keeps only the depthclip (R16F) where SGSR keeps motion and depthclip (RGBA16F).
// - UpscalePS, at the output size: SGSR's five-tap Lanczos upsample, its colour box around the history and the blend;
//   writes the output and the next frame's history in one pass (two targets) where SGSR copies.
// The depth is reversed (1 near, 0 far): the nearest of a block is its maximum, and nothing is drawn at 0.
// The colour arithmetic runs at half precision (min16float, SPIR-V RelaxedPrecision) as SGSR's mediump; positions and
// coordinates stay full.

cbuffer SgsrConstants : register(b0) {
    float2 g_render_size;
    float2 g_render_rcp;
    float2 g_output_size;
    float2 g_output_rcp;
    float2 g_jitter;        // render pixels, the projection's offset (-0.5..0.5)
    float2 g_scale_ratio;   // x: output width / render width; y: min(20, (output area / render area)^3)
    float2 g_input_rcp;     // 1 / the scene images' size (the render region lies at their origin)
    float g_fov_hor;        // tan(vertical fov / 2) * render width / render height
    float g_min_lerp;       // 0.3 once the camera has held still for more than 5 frames, else 0
    float g_reset;          // 1: the history is dropped
    float g_pad;
};

struct SgsrVertex {
    float4 pos : SV_Position;
};

// One triangle over the target.
SgsrVertex SgsrVS(uint id : SV_VertexID) {
    SgsrVertex o;
    const float2 t = float2(id == 1u ? 2.0f : 0.0f, id == 2u ? 2.0f : 0.0f);
    o.pos = float4(t.x * 2.0f - 1.0f, t.y * 2.0f - 1.0f, 0.0f, 1.0f);
    return o;
}

#if defined(SGSR_CONVERT)
Texture2D<float> t_depth : register(t0);
SamplerState s_clamp : register(s0);

// A gather of the 2x2 texels whose shared corner is `corner` (render pixels), kept inside the render region.
float4 GatherDepth(float2 corner) {
    return t_depth.GatherRed(s_clamp, clamp(corner, 1.0f, g_render_size - 1.0f) * g_input_rcp);
}

float ConvertPS(SgsrVertex v) : SV_Target0 {
    // The 4x4 block from one texel up and left of this pixel, in four gathers (component x: column 0 row 1, y: 1 1,
    // z: 1 0, w: 0 0). The nearest is taken over the block's centre 2x2 (this pixel, right, down, down-right) as in
    // SGSR's compute variant; its fragment variant reads the block's outer rows there.
    const float2 corner = v.pos.xy - 0.5f;
    const float4 top_left = GatherDepth(corner);
    const float4 top_right = GatherDepth(corner + float2(2.0f, 0.0f));
    const float4 bottom_left = GatherDepth(corner + float2(0.0f, 2.0f));
    const float4 bottom_right = GatherDepth(corner + 2.0f);
    const float nearest = max(max(max(top_left.y, top_right.x), bottom_left.z), bottom_right.w);
    float depthclip = 0.0f;
    if (nearest > 1.0e-05f) {
        const float top_left4 = max(max(max(top_left.y, top_left.x), top_left.z), top_left.w);
        const float top_right4 = max(max(max(top_right.y, top_right.x), top_right.z), top_right.w);
        const float bottom_left4 = max(max(max(bottom_left.y, bottom_left.x), bottom_left.z), bottom_left.w);
        const float bottom_right4 = max(max(max(bottom_right.y, bottom_right.x), bottom_right.z), bottom_right.w);
        // SGSR's separation scales with the distance to the far plane, which reversed depth is itself.
        const float separation = 1.37e-05f * g_fov_hor * length(g_render_size) * nearest;
        const float epsilon = 1.19e-07f;
        float weight = 0.0f;
        weight += saturate(separation / (abs(nearest - top_left4) + epsilon));
        weight += saturate(separation / (abs(nearest - top_right4) + epsilon));
        weight += saturate(separation / (abs(nearest - bottom_left4) + epsilon));
        weight += saturate(separation / (abs(nearest - bottom_right4) + epsilon));
        depthclip = saturate(1.0f - weight * 0.25f);
    }
    return depthclip;
}
#endif

#if defined(SGSR_UPSCALE)
Texture2D<float4> t_history : register(t0);   // the last frame's output, output size
Texture2D<float> t_depthclip : register(t1);   // ConvertPS's, render size
Texture2D<float2> t_motion : register(t2);     // render pixels, current -> previous
Texture2D<float4> t_color : register(t3);      // the jittered scene, render size
SamplerState s_linear : register(s0);

min16float FastLanczos(min16float base) {
    const min16float y = base - min16float(1.0f);
    const min16float y2 = y * y;
    return (min16float(0.75f) * y + y2) * y2;
}

struct SgsrOutput {
    float4 color : SV_Target0;
    float4 history : SV_Target1;
};

// One of the five taps: the Lanczos weight and SGSR's weighted box (centre, variance, bounds).
void Tap(min16float3 sample_color, float2 offset, min16float kernel_bias2, min16float curve_bias, inout min16float4 upsampled,
         inout min16float3 box_center, inout min16float3 box_var, inout min16float box_weight, inout min16float3 box_min,
         inout min16float3 box_max) {
    const min16float offset_dot = min16float(dot(offset, offset));
    const min16float weight = FastLanczos(saturate(offset_dot * kernel_bias2));
    upsampled += min16float4(sample_color * weight, weight);
    const min16float weight_box = exp(offset_dot * curve_bias);
    box_min = min(box_min, sample_color);
    box_max = max(box_max, sample_color);
    const min16float3 weighted = sample_color * weight_box;
    box_center += weighted;
    box_var += sample_color * weighted;
    box_weight += weight_box;
}

SgsrOutput UpscalePS(SgsrVertex v) {
    const min16float bias_max = min16float(g_scale_ratio.x);
    const min16float scale_factor = min16float(g_scale_ratio.y);
    const float2 hr_uv = v.pos.xy * g_output_rcp;
    const float2 jitter_uv = saturate(hr_uv + g_jitter * g_render_rcp);
    const int2 last = int2(g_render_size) - 1;
    const int2 input_pos = min(int2(jitter_uv * g_render_size), last);

    // SGSR's motion is clip space, current minus previous (twice the uv step); the renderer's is the uv step in pixels.
    const float2 motion_px = t_motion.SampleLevel(s_linear, jitter_uv * g_render_size * g_input_rcp, 0.0f);
    const float2 motion = -2.0f * motion_px * g_render_rcp;
    const float2 prev_uv = saturate(hr_uv + motion_px * g_render_rcp);
    const min16float depth_factor = min16float(t_depthclip.SampleLevel(s_linear, jitter_uv, 0.0f));
    min16float3 history = min16float3(t_history.SampleLevel(s_linear, prev_uv, 0.0f).rgb);

    const min16float bias_min = max(min16float(1.0f), min16float(0.3f) + min16float(0.3f) * bias_max);
    const min16float bias_factor = min16float(0.25f) * depth_factor;
    const min16float kernel_bias = lerp(bias_max, bias_min, bias_factor) * min16float(0.5f);
    const min16float kernel_bias2 = kernel_bias * kernel_bias;
    const float motion_len = length(motion * g_output_size);
    const min16float curve_bias = min16float(lerp(-2.0f, -3.0f, saturate(motion_len * 0.02f)));

    const float2 src_pos = float2(input_pos) + 0.5f - g_jitter;
    const float2 src_offset = src_pos - hr_uv * g_render_size;

    // The taps are clamped to the render region (SGSR fetches past its edge).
    const float4 center = t_color.Load(int3(input_pos, 0));
    const min16float3 top_mid = min16float3(t_color.Load(int3(min(input_pos + int2(0, 1), last), 0)).rgb);
    min16float4 upsampled = min16float4(0.0f, 0.0f, 0.0f, 0.0f);
    min16float3 box_center = min16float3(0.0f, 0.0f, 0.0f);
    min16float3 box_var = min16float3(0.0f, 0.0f, 0.0f);
    min16float box_weight = min16float(0.0f);
    min16float3 box_min = top_mid;
    min16float3 box_max = top_mid;
    Tap(top_mid, src_offset + float2(0.0f, 1.0f), kernel_bias2, curve_bias, upsampled, box_center, box_var, box_weight,
        box_min, box_max);
    Tap(min16float3(t_color.Load(int3(min(input_pos + int2(1, 0), last), 0)).rgb), src_offset + float2(1.0f, 0.0f), kernel_bias2,
        curve_bias, upsampled, box_center, box_var, box_weight, box_min, box_max);
    Tap(min16float3(t_color.Load(int3(max(input_pos - int2(1, 0), 0), 0)).rgb), src_offset + float2(-1.0f, 0.0f), kernel_bias2,
        curve_bias, upsampled, box_center, box_var, box_weight, box_min, box_max);
    Tap(min16float3(center.rgb), src_offset, kernel_bias2, curve_bias, upsampled, box_center, box_var, box_weight, box_min,
        box_max);
    Tap(min16float3(t_color.Load(int3(max(input_pos - int2(0, 1), 0), 0)).rgb), src_offset + float2(0.0f, -1.0f), kernel_bias2,
        curve_bias, upsampled, box_center, box_var, box_weight, box_min, box_max);

    const min16float box_rcp = min16float(1.0f) / box_weight;
    box_center *= box_rcp;
    box_var *= box_rcp;
    box_var = sqrt(abs(box_var - box_center * box_center));

    // All five Lanczos weights can reach zero (a wide kernel bias with the output pixel at a render pixel's corner): the
    // centre colour stands in then, where the division would be 0 / 0.
    const min16float3 average = upsampled.w > min16float(1.0e-5f) ? upsampled.rgb / upsampled.w : min16float3(center.rgb);
    upsampled.rgb = clamp(average, box_min - min16float(0.075f), box_max + min16float(0.075f));
    upsampled.w = upsampled.w * min16float(1.0f / 3.0f);

    min16float base_update = min16float(1.0f) - depth_factor;
    base_update = min(base_update, lerp(base_update, upsampled.w * min16float(10.0f), min16float(saturate(10.0f * motion_len))));
    base_update = min(base_update, lerp(base_update, upsampled.w, min16float(saturate(motion_len * 0.05f))));
    min16float base_alpha = base_update;

    const min16float box_scale = max(depth_factor, min16float(saturate(motion_len * 0.05f)));
    const min16float box_size = lerp(scale_factor, min16float(1.0f), box_scale);
    const min16float3 scaled_var = box_var * box_size;
    box_max = min(box_max, box_center + scaled_var);
    box_min = max(box_min, box_center - scaled_var);

    const min16float3 clamped = clamp(history, box_min, box_max);
    const min16float start_lerp = abs(motion.x) + abs(motion.y) > 0.000001f ? min16float(0.0f) : min16float(g_min_lerp);
    const bool outside = any(box_min > history) || any(history > box_max);
    const min16float lerp_contribution = saturate(outside ? start_lerp : min16float(1.0f));
    history = lerp(clamped, history, lerp_contribution);
    base_alpha = lerp(min(base_alpha, min16float(0.1f)), base_alpha, lerp_contribution);

    const float alpha_sum = max(1.192e-07f, float(base_alpha + upsampled.w));
    const min16float alpha = min16float(saturate(float(upsampled.w) / alpha_sum + g_reset));
    const float3 color = float3(lerp(history, upsampled.rgb, alpha));

    SgsrOutput o;
    // The game's alpha (its masks) from the render pixel the output pixel lies in.
    o.color = float4(color, center.a);
    o.history = o.color;
    return o;
}
#endif
