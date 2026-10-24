// Uber pixel shader: the runtime form of ShaderGenerator::generate_ps. The
// PixelShaderKey of the draw is read from GxPsConstants (stage_config holds
// the key's packed stages, ind_cmd/ind_misc its indirect state and
// uber_ps_header its scalar fields) and interpreted with the same integer TEV
// arithmetic the specialized generator emits. The output signature is fixed
// per compile: UBER_DUAL_SRC, UBER_EXPORT_DEPTH and UBER_EARLY_DEPTH select
// one of the six variants the key flags can require.

#ifndef UBER_DUAL_SRC
#define UBER_DUAL_SRC 0
#endif
#ifndef UBER_EXPORT_DEPTH
#define UBER_EXPORT_DEPTH 0
#endif
#ifndef UBER_EARLY_DEPTH
#define UBER_EARLY_DEPTH 1
#endif

cbuffer GxPsConstants : register(b1) {
    uint4    stage_config[16];
    float4   tev_regs[4];
    float4   konst[4];
    float4   alpha_refs;
    float4   fog_params[2];
    float4   tex_dims[8];
    float4   ztex_params;
    uint4    ind_cmd[4];
    int4     ind_mtx[6];
    // x BP 0x27 (indirect order), y BP 0x25, z BP 0x26, w indirect stages.
    uint4    ind_misc;
    // x: tev stages | texgens << 8 | chans << 16 | fog type << 24
    // y: alpha comp0 | comp1 << 8 | logic << 16 | flags << 24
    // z: output flags | efb scale minus one << 16
    uint4    uber_ps_header;
};
Texture2D    tex[8] : register(t0);
SamplerState samp[8] : register(s0);

// TEV byte masks: one component at a time on Vulkan (Mali returns the constant vector of a vector AND,
// device_quirks_vk.cpp), the vector AND on D3D12.
#if defined(NEBULA_VULKAN)
int3 gx_u8(int3 v) { return int3(v.x & 255, v.y & 255, v.z & 255); }
int4 gx_u8(int4 v) { return int4(v.x & 255, v.y & 255, v.z & 255, v.w & 255); }
int2 gx_bit0(int2 v) { return int2(v.x & 1, v.y & 1); }
#else
int3 gx_u8(int3 v) { return v & int3(255,255,255); }
int4 gx_u8(int4 v) { return v & int4(255,255,255,255); }
int2 gx_bit0(int2 v) { return v & 1; }
#endif

struct PSIn {
    float4   pos      : SV_Position;
    float4   col0     : COLOR0;
    float4   col1     : COLOR1;
    float3   uv[8]    : TEXCOORD;
};

float4 sample_map(uint map, float2 uv) {
    float4 result = float4(0.0, 0.0, 0.0, 0.0);
    [branch] switch (map) {
    case 0u: result = tex[0].Sample(samp[0], uv); break;
    case 1u: result = tex[1].Sample(samp[1], uv); break;
    case 2u: result = tex[2].Sample(samp[2], uv); break;
    case 3u: result = tex[3].Sample(samp[3], uv); break;
    case 4u: result = tex[4].Sample(samp[4], uv); break;
    case 5u: result = tex[5].Sample(samp[5], uv); break;
    case 6u: result = tex[6].Sample(samp[6], uv); break;
    default: result = tex[7].Sample(samp[7], uv); break;
    }
    return result;
}

int4 swizzle(int4 v, uint codes) {
    return int4(v[codes & 3u], v[(codes >> 2u) & 3u],
                v[(codes >> 4u) & 3u], v[(codes >> 6u) & 3u]);
}

float4 swizzle_f(float4 v, uint codes) {
    return float4(v[codes & 3u], v[(codes >> 2u) & 3u],
                  v[(codes >> 4u) & 3u], v[(codes >> 6u) & 3u]);
}

int3 color_arg(uint sel, int4 prev, int4 c0, int4 c1, int4 c2,
               int4 texc, int4 rasc, int4 kon) {
    int3 r = int3(0, 0, 0);
    [branch] switch (sel) {
    case 0u:  r = prev.rgb; break;
    case 1u:  r = prev.aaa; break;
    case 2u:  r = c0.rgb; break;
    case 3u:  r = c0.aaa; break;
    case 4u:  r = c1.rgb; break;
    case 5u:  r = c1.aaa; break;
    case 6u:  r = c2.rgb; break;
    case 7u:  r = c2.aaa; break;
    case 8u:  r = texc.rgb; break;
    case 9u:  r = texc.aaa; break;
    case 10u: r = rasc.rgb; break;
    case 11u: r = rasc.aaa; break;
    case 12u: r = int3(255, 255, 255); break;
    case 13u: r = int3(128, 128, 128); break;
    case 14u: r = kon.rgb; break;
    default:  r = int3(0, 0, 0); break;
    }
    return r;
}

int alpha_arg(uint sel, int4 prev, int4 c0, int4 c1, int4 c2,
              int4 texc, int4 rasc, int4 kon) {
    int r = 0;
    [branch] switch (sel) {
    case 0u: r = prev.a; break;
    case 1u: r = c0.a; break;
    case 2u: r = c1.a; break;
    case 3u: r = c2.a; break;
    case 4u: r = texc.a; break;
    case 5u: r = rasc.a; break;
    case 6u: r = kon.a; break;
    default: r = 0; break;
    }
    return r;
}

int fixed_konst(uint k) {
    return k < 4u ? int(255u - 32u * k) : int(256u - 32u * k);
}

int3 konst_rgb(uint kc) {
    if (kc <= 7u) {
        int f = fixed_konst(kc);
        return int3(f, f, f);
    }
    if (kc <= 11u) {
        return int3(0, 0, 0);
    }
    if (kc <= 15u) {
        return int3(round(saturate(konst[kc - 12u].rgb) * 255.0));
    }
    const uint group = (kc - 16u) / 4u;
    const uint index = (kc - 16u) % 4u;
    float4 k = konst[index];
    float c = k[group];
    return int3(round(saturate(float3(c, c, c)) * 255.0));
}

int konst_a(uint ka) {
    int result = 0;
    if (ka <= 7u) {
        result = fixed_konst(ka);
    } else if (ka >= 16u) {
        const uint group = (ka - 16u) / 4u;
        const uint index = (ka - 16u) % 4u;
        float4 k = konst[index];
        result = int(round(saturate(k[group]) * 255.0));
    }
    return result;
}

int3 tev_bias_i3(uint bias) {
    return bias == 1u ? int3(128, 128, 128) : bias == 2u ? int3(-128, -128, -128) : int3(0, 0, 0);
}

int tev_bias_i(uint bias) {
    return bias == 1u ? 128 : bias == 2u ? -128 : 0;
}

bool alpha_compare(uint comp, uint sample, uint ref_val) {
    bool r = false;
    [branch] switch (comp) {
    case 0u: r = false; break;
    case 1u: r = sample < ref_val; break;
    case 2u: r = sample == ref_val; break;
    case 3u: r = sample <= ref_val; break;
    case 4u: r = sample > ref_val; break;
    case 5u: r = sample != ref_val; break;
    case 6u: r = sample >= ref_val; break;
    default: r = true; break;
    }
    return r;
}

uint indirect_wrap_mask(uint wrap) {
    uint m = 0u;
    [branch] switch (wrap) {
    case 1u: m = (256u << 7u) - 1u; break;
    case 2u: m = (128u << 7u) - 1u; break;
    case 3u: m = (64u << 7u) - 1u; break;
    case 4u: m = (32u << 7u) - 1u; break;
    case 5u: m = (16u << 7u) - 1u; break;
    default: m = 0u; break;
    }
    return m;
}

uint stage_ind_cmd(uint stage) {
    uint4 v = ind_cmd[stage >> 2u];
    return v[stage & 3u];
}

#if UBER_EXPORT_DEPTH && UBER_DUAL_SRC
struct PSOut { float4 color : SV_Target0; float4 color1 : SV_Target1; float depth : SV_Depth; };
#elif UBER_EXPORT_DEPTH
struct PSOut { float4 color : SV_Target0; float depth : SV_Depth; };
#elif UBER_DUAL_SRC
struct PSOut { float4 color : SV_Target0; float4 color1 : SV_Target1; };
#else
struct PSOut { float4 color : SV_Target0; };
#endif

#if UBER_EARLY_DEPTH && !UBER_EXPORT_DEPTH
[earlydepthstencil]
#endif
PSOut main(PSIn pin) {
    const uint n_stages = uber_ps_header.x & 0xFFu;
    const uint key_texgens = (uber_ps_header.x >> 8u) & 0xFFu;
    const uint fog_type = (uber_ps_header.x >> 24u) & 0xFFu;
    const uint comp0 = uber_ps_header.y & 0xFFu;
    const uint comp1 = (uber_ps_header.y >> 8u) & 0xFFu;
    const uint logic = (uber_ps_header.y >> 16u) & 0xFFu;
    const uint key_flags = (uber_ps_header.y >> 24u) & 0xFFu;
    const uint output_flags = uber_ps_header.z & 0xFFu;
    const uint efb_scale_minus_one = (uber_ps_header.z >> 16u) & 0xFFu;
    const uint num_ind = ind_misc.w;
    const uint ind_ref = ind_misc.x;

    float3 uvs[8];
    [unroll] for (uint u = 0u; u < 8u; ++u) {
        uvs[u] = pin.uv[u];
    }

    int4 tev_prev_i = int4(round(tev_regs[0] * 255.0));
    int4 tev_c_i[3];
    tev_c_i[0] = int4(round(tev_regs[1] * 255.0));
    tev_c_i[1] = int4(round(tev_regs[2] * 255.0));
    tev_c_i[2] = int4(round(tev_regs[3] * 255.0));
    int4 tex_color_i = int4(0,0,0,0);
    int4 last_raw_tex_i = int4(0,0,0,0);
    int4 ras_color_i = int4(0,0,0,0);
    int4 konst_i = int4(0,0,0,0);
    int alphabump_i = 0;
    int2 tevcoord_i = int2(0,0);
    int3 ind_tex_i[4] = {int3(0,0,0), int3(0,0,0), int3(0,0,0), int3(0,0,0)};

    [loop] for (uint ind = 0u; ind < num_ind && ind < 4u; ++ind) {
        const uint texmap = (ind_ref >> (ind * 6u)) & 0x7u;
        const uint texcoord_raw = (ind_ref >> (ind * 6u + 3u)) & 0x7u;
        const uint texcoord = texcoord_raw < key_texgens ? texcoord_raw : 0u;
        const uint scale_raw = ind < 2u ? ind_misc.y : ind_misc.z;
        const uint pair_shift = (ind & 1u) != 0u ? 8u : 0u;
        const uint scale_s = (scale_raw >> pair_shift) & 0xFu;
        const uint scale_t = (scale_raw >> (pair_shift + 4u)) & 0xFu;
        if (key_texgens != 0u) {
            float3 ind_tq = uvs[texcoord];
            float2 ind_uv = (ind_tq.z == 0.0) ? ind_tq.xy : ind_tq.xy / ind_tq.z;
            int2 ind_fix = int2(round(ind_uv * tex_dims[texcoord].zw * 128.0));
            ind_fix = int2(ind_fix.x >> scale_s, ind_fix.y >> scale_t);
            float2 ind_tuv = (float2(ind_fix) / 128.0) / tex_dims[texmap].xy;
            float4 ind_sample = sample_map(texmap, ind_tuv);
            ind_tex_i[ind] = int3(round(saturate(ind_sample.abg) * 255.0));
        } else {
            float4 ind_sample = sample_map(texmap, float2(0.0, 0.0));
            ind_tex_i[ind] = int3(round(saturate(ind_sample.abg) * 255.0));
        }
    }

    // Stages whose indirect command adds the previous coordinate; a stage
    // must keep its coordinate state current when a later one reads it.
    uint addprev_mask = 0u;
    if (num_ind != 0u) {
        [loop] for (uint ap = 0u; ap < n_stages && ap < 16u; ++ap) {
            const uint raw = stage_ind_cmd(ap);
            if (raw != 0u && ((raw >> 20u) & 1u) != 0u) {
                addprev_mask |= 1u << ap;
            }
        }
    }

    [loop] for (uint st = 0u; st < n_stages && st < 16u; ++st) {
        const uint4 packed = stage_config[st];
        const uint color_env = packed.x;
        const uint alpha_env = packed.y;
        const uint order = packed.z;
        const uint ksel = packed.w;

        const uint color_d = color_env & 0xFu;
        const uint color_c = (color_env >> 4u) & 0xFu;
        const uint color_b = (color_env >> 8u) & 0xFu;
        const uint color_a = (color_env >> 12u) & 0xFu;
        const uint color_bias = (color_env >> 16u) & 0x3u;
        const bool color_sub = ((color_env >> 18u) & 1u) != 0u;
        const bool color_clamp = ((color_env >> 19u) & 1u) != 0u;
        const uint color_scale = (color_env >> 20u) & 0x3u;
        const uint color_dest = (color_env >> 22u) & 0x3u;

        const uint alpha_d = (alpha_env >> 4u) & 0x7u;
        const uint alpha_c = (alpha_env >> 7u) & 0x7u;
        const uint alpha_b = (alpha_env >> 10u) & 0x7u;
        const uint alpha_a = (alpha_env >> 13u) & 0x7u;
        const uint alpha_bias = (alpha_env >> 16u) & 0x3u;
        const bool alpha_sub = ((alpha_env >> 18u) & 1u) != 0u;
        const bool alpha_clamp = ((alpha_env >> 19u) & 1u) != 0u;
        const uint alpha_scale = (alpha_env >> 20u) & 0x3u;
        const uint alpha_dest = (alpha_env >> 22u) & 0x3u;

        const uint texmap = order & 0x7u;
        const uint texcoord_raw = (order >> 3u) & 0x7u;
        const uint texcoord = (key_texgens != 0u && texcoord_raw < key_texgens) ? texcoord_raw : 0u;
        const bool tex_en = ((order >> 6u) & 1u) != 0u;
        const uint ras_chan = (order >> 7u) & 0x7u;

        const uint kcsel = ksel & 0x1Fu;
        const uint kasel = (ksel >> 5u) & 0x1Fu;
        const uint ras_swz = (ksel >> 10u) & 0xFFu;
        const uint tex_swz = (ksel >> 18u) & 0xFFu;

        const uint ind_raw = stage_ind_cmd(st);
        const bool has_indirect_cmd = num_ind != 0u && ind_raw != 0u;
        const uint ind_bt = ind_raw & 0x3u;
        const uint ind_fmt = (ind_raw >> 2u) & 0x3u;
        const uint ind_bias = (ind_raw >> 4u) & 0x7u;
        const uint ind_bs = (ind_raw >> 7u) & 0x3u;
        const uint ind_mtx_sel = (ind_raw >> 9u) & 0x3u;
        const uint ind_mtx_id = (ind_raw >> 11u) & 0x3u;
        const uint ind_wrap_s = (ind_raw >> 13u) & 0x7u;
        const uint ind_wrap_t = (ind_raw >> 16u) & 0x7u;
        const bool ind_add_prev = ((ind_raw >> 20u) & 1u) != 0u;
        const bool has_indirect_sample = has_indirect_cmd && ind_bt < num_ind && ind_bt < 4u;
        const bool later_indirect_addprev = (addprev_mask >> (st + 1u)) != 0u;
        const bool indirect_coord_needed = has_indirect_cmd || later_indirect_addprev;

        if (has_indirect_sample && ind_bs != 0u) {
            const uint alpha_shift = ind_fmt == 0u ? 0u : ind_fmt == 1u ? 5u : ind_fmt == 2u ? 4u : 3u;
            int3 ind_value = ind_tex_i[ind_bt];
            const int component = ind_bs == 1u ? ind_value.x : ind_bs == 2u ? ind_value.y : ind_value.z;
            alphabump_i = (component << alpha_shift) & 248;
        }

        if (indirect_coord_needed) {
            const uint fmt_shift = ind_fmt == 0u ? 0u : ind_fmt == 1u ? 3u : ind_fmt == 2u ? 4u : 5u;
            const int bias_add = ind_fmt == 0u ? -128 : 1;
            int2 stage_tevcoord = int2(0, 0);
            if (key_texgens != 0u) {
                float3 tq = uvs[texcoord];
                float2 tuv = (tq.z == 0.0) ? tq.xy : tq.xy / tq.z;
                stage_tevcoord = int2(round(tuv * tex_dims[texcoord].zw * 128.0));
            }
            int2 wrappedcoord = stage_tevcoord;
            int2 indtevtrans = int2(0, 0);
            if (has_indirect_sample) {
                int3 iindtevcrd = ind_tex_i[ind_bt] >> fmt_shift;
                if ((ind_bias & 0x1u) != 0u) iindtevcrd.x += bias_add;
                if ((ind_bias & 0x2u) != 0u) iindtevcrd.y += bias_add;
                if ((ind_bias & 0x4u) != 0u) iindtevcrd.z += bias_add;
                if (ind_mtx_sel != 0u && ind_mtx_sel <= 3u) {
                    const uint row = (ind_mtx_sel - 1u) * 2u;
                    if (ind_mtx_id == 0u) {
                        indtevtrans = int2(
                            (ind_mtx[row].x * iindtevcrd.x + ind_mtx[row].y * iindtevcrd.y + ind_mtx[row].z * iindtevcrd.z) >> 3,
                            (ind_mtx[row + 1u].x * iindtevcrd.x + ind_mtx[row + 1u].y * iindtevcrd.y + ind_mtx[row + 1u].z * iindtevcrd.z) >> 3);
                    } else if (ind_mtx_id == 1u) {
                        indtevtrans = (stage_tevcoord * iindtevcrd.xx) >> 8;
                    } else if (ind_mtx_id == 2u) {
                        indtevtrans = (stage_tevcoord * iindtevcrd.yy) >> 8;
                    } else {
                        indtevtrans = int2(0, 0);
                    }
                    if (ind_mtx[row].w >= 0) {
                        indtevtrans = indtevtrans >> ind_mtx[row].w;
                    } else {
                        indtevtrans = indtevtrans << (-ind_mtx[row].w);
                    }
                }
            }
            if (ind_wrap_s == 0u) {
                wrappedcoord.x = stage_tevcoord.x;
            } else if (ind_wrap_s >= 6u) {
                wrappedcoord.x = 0;
            } else {
                wrappedcoord.x = stage_tevcoord.x & int(indirect_wrap_mask(ind_wrap_s));
            }
            if (ind_wrap_t == 0u) {
                wrappedcoord.y = stage_tevcoord.y;
            } else if (ind_wrap_t >= 6u) {
                wrappedcoord.y = 0;
            } else {
                wrappedcoord.y = stage_tevcoord.y & int(indirect_wrap_mask(ind_wrap_t));
            }
            if (ind_add_prev) {
                tevcoord_i += wrappedcoord + indtevtrans;
            } else {
                tevcoord_i = wrappedcoord + indtevtrans;
            }
            tevcoord_i = (tevcoord_i << 8) >> 8;
        }

        if (tex_en && key_texgens != 0u) {
            float2 sample_uv;
            if (has_indirect_cmd) {
                sample_uv = (float2(tevcoord_i) / 128.0) / tex_dims[texmap].xy;
            } else {
                float3 tq = uvs[texcoord];
                float2 tuv = (tq.z == 0.0) ? tq.xy : tq.xy / tq.z;
                int2 tex_fix = int2(round(tuv * tex_dims[texcoord].zw * 128.0));
                sample_uv = (float2(tex_fix) / 128.0) / tex_dims[texmap].xy;
            }
            float4 tex_sample = sample_map(texmap, sample_uv);
            last_raw_tex_i = int4(round(saturate(tex_sample) * 255.0));
            tex_color_i = swizzle(last_raw_tex_i, tex_swz);
        } else if (key_texgens != 0u) {
            tex_color_i = int4(255,255,255,255);
        } else {
            tex_color_i = int4(0,0,0,0);
        }

        if (ras_chan == 0u) {
            ras_color_i = int4(round(saturate(swizzle_f(pin.col0, ras_swz)) * 255.0));
        } else if (ras_chan == 1u) {
            ras_color_i = int4(round(saturate(swizzle_f(pin.col1, ras_swz)) * 255.0));
        } else if (ras_chan == 5u) {
            ras_color_i = swizzle(int4(alphabump_i, alphabump_i, alphabump_i, alphabump_i), ras_swz);
        } else if (ras_chan == 6u) {
            int abn = alphabump_i | (alphabump_i >> 5);
            ras_color_i = swizzle(int4(abn, abn, abn, abn), ras_swz);
        } else {
            ras_color_i = int4(0,0,0,0);
        }

        konst_i = int4(konst_rgb(kcsel), konst_a(kasel));

        // Color combiner. Both combiners observe the stage-entry registers;
        // the alpha R8/GR16/BGR24 compares reuse the COLOR A/B selections.
        const int3 ca = color_arg(color_a, tev_prev_i, tev_c_i[0], tev_c_i[1], tev_c_i[2], tex_color_i, ras_color_i, konst_i);
        const int3 cb = color_arg(color_b, tev_prev_i, tev_c_i[0], tev_c_i[1], tev_c_i[2], tex_color_i, ras_color_i, konst_i);
        const int3 cc = color_arg(color_c, tev_prev_i, tev_c_i[0], tev_c_i[1], tev_c_i[2], tex_color_i, ras_color_i, konst_i);
        const int3 cd = color_arg(color_d, tev_prev_i, tev_c_i[0], tev_c_i[1], tev_c_i[2], tex_color_i, ras_color_i, konst_i);
        const int3 alpha_compare_a = gx_u8(ca);
        const int3 alpha_compare_b = gx_u8(cb);
        int3 color_out;
        if (color_bias == 3u) {
            int3 ca_i = gx_u8(ca);
            int3 cb_i = gx_u8(cb);
            int3 cc_i = gx_u8(cc);
            int3 cd_i = cd;
            int3 cmp_i;
            if (color_scale == 0u) {
                cmp_i = (color_sub ? (ca_i.x == cb_i.x) : (ca_i.x > cb_i.x)) ? cc_i : int3(0,0,0);
            } else if (color_scale == 1u) {
                int av = ca_i.x + (ca_i.y << 8);
                int bv = cb_i.x + (cb_i.y << 8);
                cmp_i = (color_sub ? (av == bv) : (av > bv)) ? cc_i : int3(0,0,0);
            } else if (color_scale == 2u) {
                int av = ca_i.x + (ca_i.y << 8) + (ca_i.z << 16);
                int bv = cb_i.x + (cb_i.y << 8) + (cb_i.z << 16);
                cmp_i = (color_sub ? (av == bv) : (av > bv)) ? cc_i : int3(0,0,0);
            } else {
                if (color_sub) {
                    cmp_i = (int3(1,1,1) - sign(abs(ca_i - cb_i))) * cc_i;
                } else {
                    cmp_i = max(sign(ca_i - cb_i), int3(0,0,0)) * cc_i;
                }
            }
            color_out = cd_i + cmp_i;
        } else {
            int3 ai = gx_u8(ca);
            int3 bi = gx_u8(cb);
            int3 ci = gx_u8(cc);
            int3 di = cd;
            const uint left = color_scale == 1u ? 1u : color_scale == 2u ? 2u : 0u;
            int3 lhs = (di + tev_bias_i3(color_bias)) << left;
            int3 mixv = (ai << 8) + (bi - ai) * (ci + (ci >> 7));
            mixv = mixv << left;
            if (color_scale != 3u) {
                mixv = (mixv + (color_sub ? int3(127,127,127) : int3(128,128,128))) >> 8;
            } else {
                mixv = mixv >> 8;
            }
            color_out = color_sub ? (lhs - mixv) : (lhs + mixv);
            if (color_scale == 3u) {
                color_out = color_out >> 1;
            }
        }
        if (color_clamp) {
            color_out = clamp(color_out, int3(0,0,0), int3(255,255,255));
        } else {
            color_out = clamp(color_out, int3(-1024,-1024,-1024), int3(1023,1023,1023));
        }

        // Alpha combiner reads the stage-entry alpha registers; the color
        // write above only changed rgb.
        const int aa = alpha_arg(alpha_a, tev_prev_i, tev_c_i[0], tev_c_i[1], tev_c_i[2], tex_color_i, ras_color_i, konst_i);
        const int ab = alpha_arg(alpha_b, tev_prev_i, tev_c_i[0], tev_c_i[1], tev_c_i[2], tex_color_i, ras_color_i, konst_i);
        const int ac = alpha_arg(alpha_c, tev_prev_i, tev_c_i[0], tev_c_i[1], tev_c_i[2], tex_color_i, ras_color_i, konst_i);
        const int ad = alpha_arg(alpha_d, tev_prev_i, tev_c_i[0], tev_c_i[1], tev_c_i[2], tex_color_i, ras_color_i, konst_i);
        int alpha_out;
        if (alpha_bias == 3u) {
            bool acmp;
            if (alpha_scale == 3u) {
                int aa_i = aa & 255;
                int ab_i = ab & 255;
                acmp = alpha_sub ? (aa_i == ab_i) : (aa_i > ab_i);
            } else if (alpha_scale == 0u) {
                acmp = alpha_sub ? (alpha_compare_a.x == alpha_compare_b.x)
                                 : (alpha_compare_a.x > alpha_compare_b.x);
            } else if (alpha_scale == 1u) {
                int av = alpha_compare_a.x + (alpha_compare_a.y << 8);
                int bv = alpha_compare_b.x + (alpha_compare_b.y << 8);
                acmp = alpha_sub ? (av == bv) : (av > bv);
            } else {
                int av = alpha_compare_a.x + (alpha_compare_a.y << 8) + (alpha_compare_a.z << 16);
                int bv = alpha_compare_b.x + (alpha_compare_b.y << 8) + (alpha_compare_b.z << 16);
                acmp = alpha_sub ? (av == bv) : (av > bv);
            }
            int ac_i = ac & 255;
            alpha_out = ad + (acmp ? ac_i : 0);
        } else {
            int ai = aa & 255;
            int bi = ab & 255;
            int ci = ac & 255;
            int di = ad;
            const uint left = alpha_scale == 1u ? 1u : alpha_scale == 2u ? 2u : 0u;
            int lhs = (di + tev_bias_i(alpha_bias)) << left;
            int mixv = (ai << 8) + (bi - ai) * (ci + (ci >> 7));
            mixv = mixv << left;
            if (alpha_scale != 3u) {
                mixv = (mixv + (alpha_sub ? 127 : 128)) >> 8;
            } else {
                mixv = mixv >> 8;
            }
            alpha_out = alpha_sub ? (lhs - mixv) : (lhs + mixv);
            if (alpha_scale == 3u) {
                alpha_out = alpha_out >> 1;
            }
        }
        if (alpha_clamp) {
            alpha_out = clamp(alpha_out, 0, 255);
        } else {
            alpha_out = clamp(alpha_out, -1024, 1023);
        }

        [branch] switch (color_dest) {
        case 0u: tev_prev_i.rgb = color_out; break;
        case 1u: tev_c_i[0].rgb = color_out; break;
        case 2u: tev_c_i[1].rgb = color_out; break;
        default: tev_c_i[2].rgb = color_out; break;
        }
        [branch] switch (alpha_dest) {
        case 0u: tev_prev_i.a = alpha_out; break;
        case 1u: tev_c_i[0].a = alpha_out; break;
        case 2u: tev_c_i[1].a = alpha_out; break;
        default: tev_c_i[2].a = alpha_out; break;
        }
    }

    // The last stage's destination reaches the PE.
    if (n_stages != 0u) {
        const uint4 last = stage_config[min(n_stages, 16u) - 1u];
        const uint last_color_dest = (last.x >> 22u) & 0x3u;
        const uint last_alpha_dest = (last.y >> 22u) & 0x3u;
        if (last_color_dest != 0u) tev_prev_i.rgb = tev_c_i[last_color_dest - 1u].rgb;
        if (last_alpha_dest != 0u) tev_prev_i.a = tev_c_i[last_alpha_dest - 1u].a;
    }

    int4 frag_i = gx_u8(tev_prev_i);
    float4 frag = float4(frag_i) / 255.0;

    {
        uint a_sample = uint(frag_i.a);
        uint a_ref0 = uint(round(saturate(alpha_refs.x) * 255.0));
        uint a_ref1 = uint(round(saturate(alpha_refs.y) * 255.0));
        bool test0 = alpha_compare(comp0, a_sample, a_ref0);
        bool test1 = alpha_compare(comp1, a_sample, a_ref1);
        bool alpha_pass;
        if (logic == 0u) alpha_pass = test0 && test1;
        else if (logic == 1u) alpha_pass = test0 || test1;
        else if (logic == 2u) alpha_pass = test0 != test1;
        else alpha_pass = test0 == test1;
        if (!alpha_pass) discard;
    }

    uint tev_alpha_i = uint(frag_i.a);
    if (tev_alpha_i == 1u) frag.a = 0.0;

    // RGBA6 colour precision; bit 4 keeps 8-bit colour (no dither).
    const bool rgba6 = (output_flags & 0x7u) == 1u && (output_flags & 0x10u) == 0u;
    if (rgba6 && (output_flags & 0x08u) != 0u) {
        int3 dither_rgb = int3(round(saturate(frag.rgb) * 255.0));
        int2 dither_xy;
        if (efb_scale_minus_one == 0xFFu) {
            dither_xy = gx_bit0(int2(pin.pos.xy * ztex_params.y));
        } else if (efb_scale_minus_one == 0u) {
            dither_xy = gx_bit0(int2(pin.pos.xy));
        } else {
            // Integer EFB scale: min(scale - 1, kMaxEfbScale - 1) + 1.
            dither_xy = gx_bit0(int2(pin.pos.xy) / int(min(efb_scale_minus_one, 15u) + 1u));
        }
        dither_rgb = (dither_rgb - (dither_rgb >> 6)) + (dither_xy.x ^ dither_xy.y) * 2 + dither_xy.y;
        frag.rgb = float3(dither_rgb) / 255.0;
    }

    const bool ztex_on = (key_flags & 0x02u) != 0u;
    uint zcoord_i = uint(clamp((1.0 - pin.pos.z) * 16777216.0, 0.0, 16777215.0));
    if (ztex_on) {
        const uint ztex_op = (key_flags >> 3u) & 0x3u;
        const uint ztex_fmt = (key_flags >> 5u) & 0x3u;
        uint ztex_i;
        if (ztex_fmt == 0u) {
            ztex_i = uint(last_raw_tex_i.a & 255);
        } else if (ztex_fmt == 1u) {
            ztex_i = uint((last_raw_tex_i.a & 255) * 256 + (last_raw_tex_i.r & 255));
        } else {
            ztex_i = uint((last_raw_tex_i.r & 255) * 65536 + (last_raw_tex_i.g & 255) * 256 + (last_raw_tex_i.b & 255));
        }
        uint zbias_i = uint(round(ztex_params.x * 16777216.0));
        if (ztex_op == 1u) {
            zcoord_i = (zcoord_i + ztex_i + zbias_i) & 0xFFFFFFu;
        } else {
            zcoord_i = (ztex_i + zbias_i) & 0xFFFFFFu;
        }
    }

    if (fog_type != 0u) {
        uint fog_zc = zcoord_i;
        float fog_ze;
        if (fog_params[1].w == 0.0) {
            fog_ze = (fog_params[0].x * 16777216.0)
                   / float(max(1u, uint(fog_params[0].y) - (fog_zc >> uint(fog_params[0].z))));
        } else {
            fog_ze = fog_params[0].x * float(fog_zc) / 16777216.0;
        }
        float fog_f = saturate(fog_ze - fog_params[0].w);
        if (fog_type == 4u) {
            fog_f = 1.0 - exp2(-8.0 * fog_f);
        } else if (fog_type == 5u) {
            fog_f = 1.0 - exp2(-8.0 * fog_f * fog_f);
        } else if (fog_type == 6u) {
            fog_f = exp2(-8.0 * (1.0 - fog_f));
        } else if (fog_type == 7u) {
            fog_f = 1.0 - fog_f;
            fog_f = exp2(-8.0 * fog_f * fog_f);
        }
        int fog_i = int(round(fog_f * 256.0));
        int3 fog_rgb_i = int3(round(frag.rgb * 255.0));
        int3 fog_color_i = int3(round(saturate(fog_params[1].rgb) * 255.0));
        frag.rgb = float3((fog_rgb_i * (256 - fog_i) + fog_color_i * fog_i) >> 8) / 255.0;
    }

    float tev_alpha = saturate(frag.a);
    uint4 efb_color = uint4(round(saturate(frag) * 255.0));
    if (rgba6) {
        frag.rgb = float3(efb_color.rgb >> 2) / 63.0;
    } else {
        frag.rgb = float3(efb_color.rgb) / 255.0;
    }
    // Under ray-traced light (the HDR scene EFB, sRGB-encoded past 1), in linear light after the TEV's 8-bit result: a
    // light source's glow (additive, output_flags bit 5) at its radiance times the camera's exposure (ztex_params.z);
    // a lit translucent (bit 6) under the light of the surface behind it (texture 7, exposed), unclamped, unless its
    // scale (ztex_params.w) is 0 (light sources, clear surfaces) or negative (a light source in the table's colour);
    // an opaque draw after the trace (bit 7) takes no emitter's light.
    if ((output_flags & 0x60u) != 0u) {
        float3 lin = frag.rgb <= 0.04045 ? frag.rgb / 12.92 : pow((frag.rgb + 0.055) / 1.055, 2.4);
        if ((output_flags & 0x20u) != 0u) {
            lin *= ztex_params.z;
        }
        if ((output_flags & 0x40u) != 0u && ztex_params.w > 0.0) {
            uint lit_w, lit_h;
            tex[7].GetDimensions(lit_w, lit_h);
            const int2 lit_at = min(int2(pin.pos.xy * ztex_params.w), int2(lit_w, lit_h) - 1);
            const float3 light = max(tex[7].Load(int3(lit_at, 0)).rgb, 0.0);
            lin *= (output_flags & 0x80u) != 0u ? min(light, 1.15) : light;
        } else if (ztex_params.w < 0.0) {
            // A light source the table shows in its own colour (`recolour`: RGB8 + 1, negated) keeps its peak, not its
            // luminance: a blue glow's luminance is a fifth of its peak.
            const uint code = uint(-ztex_params.w) - 1u;
            const float3 c = float3(code & 255u, (code >> 8u) & 255u, (code >> 16u) & 255u) / 255.0;
            const float3 hue = c <= 0.04045 ? c / 12.92 : pow((c + 0.055) / 1.055, 2.4);
            const float peak = max(hue.r, max(hue.g, hue.b));
            lin = peak > 1e-4 ? max(lin.r, max(lin.g, lin.b)) * hue / peak : lin;
        }
        frag.rgb = lin <= 0.0031308 ? lin * 12.92 : 1.055 * pow(lin, 1.0 / 2.4) - 0.055;
    }
    if ((key_flags & 0x04u) != 0u) {
        uint dst_alpha6 = uint(round(saturate(alpha_refs.w) * 255.0)) >> 2;
        frag.a = float(dst_alpha6) / 63.0;
    } else {
        frag.a = float(efb_color.a >> 2) / 63.0;
    }

    PSOut o;
    o.color = frag;
#if UBER_DUAL_SRC
    o.color1 = float4(0.0, 0.0, 0.0, tev_alpha);
#endif
#if UBER_EXPORT_DEPTH
    o.depth = 1.0 - float(zcoord_i) / 16777216.0;
#endif
    return o;
}
