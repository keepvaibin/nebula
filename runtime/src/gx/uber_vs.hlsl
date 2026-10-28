// Uber vertex shader: the runtime form of ShaderGenerator::generate_vs. Every
// decision the specialized generator bakes from VertexShaderKey is read here
// from the key copy at the tail of GxVsConstants (uber_vs_*), with the same
// arithmetic in the same order, so a draw renders the same while its
// specialized pipeline compiles in the background.

cbuffer GxVsConstants : register(b0) {
    float4x4 projection;
    uint4    texgen_config[4];
    float4   chan_ambient[2];
    float4   chan_material[2];
    uint     num_texgens;
    uint     num_chans;
    uint     flags;
    uint     mtx_palette_base;
    uint     matrix_index_a;
    uint     matrix_index_b;
    uint     pad0;
    uint     pad1;
    float4   inline_pos_matrix[3];
    float4   inline_tex_matrices[24];
    float4   inline_post_matrices[24];
    float4   line_point_raster;
    uint4    line_point_tex_offsets;
    // VertexShaderKey: x num_texgens, y num_chans, z flags, w texmtx_idx_mask.
    uint4    uber_vs_header;
    uint4    uber_texgen[2];
    uint4    uber_post_texgen[2];
    // color0, color1, alpha0, alpha1 channel controls.
    uint4    uber_channel;
    float4   uber_motion_skip_[7];
    // Physical ray tracing (flags bit 6): the model's baked light by facing (uber_constants.h).
    float4   debake_sh[7];
    float4   debake_peak;
};
StructuredBuffer<float4> mtx_palette : register(t0, space1);

struct VSIn {
    float3   pos      : POSITION;
    float3   nrm      : NORMAL;
    float3   tangent  : TANGENT;
    float3   binormal : BINORMAL;
    uint     color0   : COLOR0;
    uint     color1   : COLOR1;
    float2   uv[8]    : TEXCOORD;
    uint4    mtx_idx  : BLENDINDICES;
};
struct VSOut {
    float4   pos      : SV_Position;
    float4   col0     : COLOR0;
    float4   col1     : COLOR1;
    float3   uv[8]    : TEXCOORD;
};

float xfw(uint w) {
    float4 v = mtx_palette[mtx_palette_base + (w >> 2u)];
    return v[w & 3u];
}

uint key_texgen(uint i) {
    uint4 v = uber_texgen[i >> 2u];
    return v[i & 3u];
}

uint key_post_texgen(uint i) {
    uint4 v = uber_post_texgen[i >> 2u];
    return v[i & 3u];
}

// One light's contribution (emit_light). attn/diffuse functions come from the
// channel control: bits 9-10 attenuation, bits 7-8 diffuse. With `finite` (key
// flag 0x20, kVsKeyFiniteLightAttn) the attenuation operands stay at most 1e18
// (squares 1e36), so a zero coefficient nulls an out-of-range cosine, such as
// one against a light direction the game never initialized.
void add_light(uint n, uint ctrl, bool alpha_part, bool finite, float3 wpos, float3 nrm,
               inout int3 lacc_i, inout int lacc_a_i) {
    const uint e = 0x180u + 4u * n;
    uint  lcw  = asuint(mtx_palette[mtx_palette_base + e].w);
    float4 le1 = mtx_palette[mtx_palette_base + e + 1u];
    float4 le2 = mtx_palette[mtx_palette_base + e + 2u];
    float4 le3 = mtx_palette[mtx_palette_base + e + 3u];
    float3 lpos  = float3(le2.z, le2.w, le3.x);
    float3 ldvec = float3(le3.y, le3.z, le3.w);
    float3 cosA  = le1.xyz;
    float3 distA = float3(le1.w, le2.x, le2.y);
    const uint atten_func = (ctrl >> 9u) & 3u;
    const uint diffuse_func = (ctrl >> 7u) & 3u;
    float3 ldir;
    float attn;
    if (atten_func == 1u) {
        float3 ldelta = lpos - wpos;
        float ldd = dot(ldelta, ldelta);
        ldir = (ldd > 0.0) ? ldelta * rsqrt(ldd) : nrm;
        attn = (dot(nrm, ldir) >= 0.0) ? max(0.0, dot(nrm, ldvec)) : 0.0;
        if (finite) {
            attn = min(attn, 1.0e18);
        }
        float3 distAttn = distA;
        if (diffuse_func != 0u) {
            float dad = dot(distA, distA);
            distAttn = (dad > 0.0) ? distA * rsqrt(dad) : distA;
        }
        float denom = dot(distAttn, float3(1.0, attn, attn*attn));
        attn = (denom != 0.0)
             ? max(0.0, dot(cosA, float3(1.0, attn, attn*attn))) / denom
             : 0.0;
    } else if (atten_func == 3u) {
        float3 ldelta = lpos - wpos;
        float dist2 = dot(ldelta, ldelta);
        float dist  = sqrt(dist2);
        ldir = (dist > 0.0) ? ldelta / dist : nrm;
        attn = max(0.0, dot(ldir, ldvec));
        if (finite) {
            attn = min(attn, 1.0e18);
            dist = min(dist, 1.0e18);
            dist2 = min(dist2, 1.0e36);
        }
        float denom = dot(distA, float3(1.0, dist, dist2));
        attn = (denom != 0.0)
             ? max(0.0, cosA.x + cosA.y*attn + cosA.z*attn*attn) / denom
             : 0.0;
    } else {
        float3 ldelta = lpos - wpos;
        float ldd = dot(ldelta, ldelta);
        ldir = (ldd > 0.0) ? ldelta * rsqrt(ldd) : nrm;
        attn = 1.0;
    }
    if (alpha_part) {
        float lcol = float(lcw & 255u);
        if (diffuse_func == 1u) {
            lacc_a_i += int(round(attn * dot(ldir, nrm) * lcol));
        } else if (diffuse_func == 2u) {
            lacc_a_i += int(round(attn * max(0.0, dot(ldir, nrm)) * lcol));
        } else {
            lacc_a_i += int(round(attn * lcol));
        }
    } else {
        float3 lcol = float3(
            float((lcw >> 24) & 255u),
            float((lcw >> 16) & 255u),
            float((lcw >>  8) & 255u));
        if (diffuse_func == 1u) {
            lacc_i += int3(round(attn * dot(ldir, nrm) * lcol));
        } else if (diffuse_func == 2u) {
            lacc_i += int3(round(attn * max(0.0, dot(ldir, nrm)) * lcol));
        } else {
            lacc_i += int3(round(attn * lcol));
        }
    }
}

// Interpolated redraw vertex blending (shader_gen.cpp kVertexBlendDef).
cbuffer GxVertexBlend : register(b2) {
    uint  vb_mode;   // 0: the draw's own vertices only
    uint  vb_frame;  // element of the presented frame's values (a zero element in game frames)
    uint  vb_slot;   // the pair's verdict
    uint  vb_last;   // the last vertex of the earlier draw
    uint  vb_base;   // its first vertex's byte offset in vb_prev (0 where the view starts there)
};
ByteAddressBuffer vb_prev : register(t1, space1);
StructuredBuffer<uint4> vb_verdict : register(t2, space1);
static float vb_t;
// Turned visible or invisible: the new colour; a channel that jumps by more than half: its new value.
uint vb_color(uint a, uint b) {
    if (((a & 0xFFu) == 0u) != ((b & 0xFFu) == 0u)) {
        return b;
    }
    uint c = 0u;
    [unroll] for (uint s = 0u; s < 32u; s += 8u) {
        const float x = float((a >> s) & 0xFFu);
        const float y = float((b >> s) & 0xFFu);
        c |= uint(abs(y - x) > 128.0 ? y : round(lerp(x, y, vb_t))) << s;
    }
    return c;
}
float vb_coord(float a, float b) {
    const float d = b - a;
    const float w = abs(d) > 0.5 ? a + round(d) : a;
    return abs(b - w) > 0.125 ? b : lerp(w, b, vb_t);
}
void vertex_blend(inout VSIn v, uint id) {
    // Root views have no bounds: every read stays inside its buffer even if
    // a compiler moves it above the checks.
    vb_t = asfloat(vb_verdict[min(vb_frame, 8193u)].x);
    if (vb_mode == 0u || vb_t >= 1.0) {
        return;
    }
    const uint4 verdict = vb_verdict[min(vb_slot, 8191u)];
    const float worst = asfloat(verdict.z);
    const float extent = asfloat(verdict.w);
    if (verdict.y != verdict.x && !(verdict.y == 0u && worst <= 0.25 * extent) &&
        !(worst <= 0.02 * extent)) {
        return;
    }
    const uint at = vb_base + min(id, vb_last) * 144u;
    v.pos = lerp(asfloat(vb_prev.Load3(at)), v.pos, vb_t);
    const float3 n = lerp(asfloat(vb_prev.Load3(at + 12u)), v.nrm, vb_t);
    const float nn = dot(n, n);
    if (nn > 0.0) {
        v.nrm = n * sqrt(dot(v.nrm, v.nrm) / nn);
    }
    const uint2 c = vb_prev.Load2(at + 48u);
    v.color0 = vb_color(c.x, v.color0);
    v.color1 = vb_color(c.y, v.color1);
    [unroll] for (uint i = 0u; i < 8u; ++i) {
        const float2 uv = asfloat(vb_prev.Load2(at + 56u + 8u * i));
        v.uv[i] = float2(vb_coord(uv.x, v.uv[i].x), vb_coord(uv.y, v.uv[i].y));
    }
}

// Physical ray tracing (flags bit 6): the gain of a de-baked vertex colour, the model's baked light at its most lit
// facing (debake_peak, its light at the player's up; GxBackend::flush_draw_state) over the light at the object-space
// normal `nrm` (nine spherical harmonics per channel in debake_sh; GxBackend::fit_debake_light), clamped to 1/4..4.
// Without a normal the gain is 1.
float3 nebula_debake_gain(float3 nrm) {
    const float len2 = dot(nrm, nrm);
    if (!(len2 > 0.25)) {
        return float3(1.0, 1.0, 1.0);
    }
    const float3 n = nrm * rsqrt(len2);
    const float y[9] = {0.282095, 0.488603 * n.y, 0.488603 * n.z, 0.488603 * n.x, 1.092548 * n.x * n.y,
                        1.092548 * n.y * n.z, 0.315392 * (3.0 * n.z * n.z - 1.0), 1.092548 * n.x * n.z,
                        0.546274 * (n.x * n.x - n.y * n.y)};
    float3 light = float3(0.0, 0.0, 0.0);
    [unroll] for (uint i = 0u; i < 9u; ++i) {
        light.r += debake_sh[i >> 2u][i & 3u] * y[i];
        light.g += debake_sh[(9u + i) >> 2u][(9u + i) & 3u] * y[i];
        light.b += debake_sh[(18u + i) >> 2u][(18u + i) & 3u] * y[i];
    }
    const float3 peak = max(debake_peak.rgb, 1e-3);
    return clamp(peak / max(light, peak * 0.25), 0.25, 4.0);
}

// A vertex colour with its baked light removed: its hue at full brightness (black stays black).
float3 nebula_debake(float3 c) {
    const float m = max(c.r, max(c.g, c.b));
    return m > (1.0 / 255.0) ? c / m : c;
}

VSOut main(VSIn v, uint vertex_id : SV_VertexID) {
    vertex_blend(v, vertex_id);
    const uint key_texgens = uber_vs_header.x;
    const uint key_chans = uber_vs_header.y;
    const uint key_flags = uber_vs_header.z;
    const uint key_texmtx_mask = uber_vs_header.w;
    const bool finite_attn = (key_flags & 0x20u) != 0u;

    // The vertex's PNMATIDX where the stream carries one, else the draw's CP MatrixIndexA.
    uint pn_row = min(((key_flags & 0x01u) != 0u ? v.mtx_idx.x : matrix_index_a) & 0x3Fu, 61u);
    float3 pos_in = v.pos;
    float4 wpos;
    if ((key_flags & 0x01u) != 0u) {
        uint pnmtx_row = pn_row + mtx_palette_base;
        wpos = float4(
            dot(mtx_palette[pnmtx_row+0u], float4(pos_in, 1.0)),
            dot(mtx_palette[pnmtx_row+1u], float4(pos_in, 1.0)),
            dot(mtx_palette[pnmtx_row+2u], float4(pos_in, 1.0)),
            1.0);
    } else {
        wpos = float4(
            dot(inline_pos_matrix[0], float4(pos_in, 1.0)),
            dot(inline_pos_matrix[1], float4(pos_in, 1.0)),
            dot(inline_pos_matrix[2], float4(pos_in, 1.0)),
            1.0);
    }

    VSOut o = (VSOut)0;
    o.pos = mul(projection, wpos);

    float4 vtx_col0 = float4(
        float((v.color0 >> 24u) & 0xFFu) / 255.0,
        float((v.color0 >> 16u) & 0xFFu) / 255.0,
        float((v.color0 >>  8u) & 0xFFu) / 255.0,
        float((v.color0       ) & 0xFFu) / 255.0);
    float4 vtx_col1 = float4(
        float((v.color1 >> 24u) & 0xFFu) / 255.0,
        float((v.color1 >> 16u) & 0xFFu) / 255.0,
        float((v.color1 >>  8u) & 0xFFu) / 255.0,
        float((v.color1       ) & 0xFFu) / 255.0);
    // Physical ray tracing (flags bits 2-3): an unlit channel's vertex colour is the art's baked light.
    if ((flags & 64u) != 0u) {
        const float3 debake_gain = nebula_debake_gain(v.nrm);
        if ((flags & 4u) != 0u) vtx_col0.rgb = saturate(vtx_col0.rgb * debake_gain);
        if ((flags & 8u) != 0u) vtx_col1.rgb = saturate(vtx_col1.rgb * debake_gain);
    } else {
        if ((flags & 4u) != 0u) vtx_col0.rgb = nebula_debake(vtx_col0.rgb) * (float((flags >> 8u) & 0xFFu) / 255.0);
        if ((flags & 8u) != 0u) vtx_col1.rgb = nebula_debake(vtx_col1.rgb) * (float((flags >> 16u) & 0xFFu) / 255.0);
    }
    // Lit channels (flags bits 4-5) keep their vertices' colours: the game lights them, so those are the material.

    bool lighting_used = false;
    [unroll] for (uint lc = 0u; lc < 2u; ++lc) {
        if (lc < key_chans &&
            (((uber_channel[lc] >> 1u) & 1u) != 0u ||
             ((uber_channel[2u + lc] >> 1u) & 1u) != 0u)) {
            lighting_used = true;
        }
    }
    float3 nrm = float3(0.0, 0.0, 0.0);
    if (lighting_used) {
        uint nrm_base = 0x400u + 3u * (pn_row & 31u);
        float3 nrm_t = float3(
            dot(float3(xfw(nrm_base+0u), xfw(nrm_base+1u), xfw(nrm_base+2u)), v.nrm),
            dot(float3(xfw(nrm_base+3u), xfw(nrm_base+4u), xfw(nrm_base+5u)), v.nrm),
            dot(float3(xfw(nrm_base+6u), xfw(nrm_base+7u), xfw(nrm_base+8u)), v.nrm));
        nrm = (dot(nrm_t, nrm_t) > 0.0) ? normalize(nrm_t) : nrm_t;
    }

    float4 chan_out[2];
    chan_out[0] = vtx_col0;
    chan_out[1] = vtx_col1;
    [unroll] for (uint ch = 0u; ch < 2u; ++ch) {
        if (ch >= key_chans) {
            continue;
        }
        const float4 vtx = ch == 0u ? vtx_col0 : vtx_col1;
        const uint cc = uber_channel[ch];
        const uint ca = uber_channel[2u + ch];
        float4 result;

        int3 mat_c_i = int3(round(((cc & 1u) != 0u ? vtx : chan_material[ch]).rgb * 255.0));
        if (((cc >> 1u) & 1u) != 0u) {
            int3 lacc_i = int3(round((((cc >> 6u) & 1u) != 0u ? vtx : chan_ambient[ch]).rgb * 255.0));
            int unused_a = 0;
            const uint mask = ((cc >> 2u) & 0xFu) | (((cc >> 11u) & 0xFu) << 4u);
            [loop] for (uint n = 0u; n < 8u; ++n) {
                if (((mask >> n) & 1u) != 0u) {
                    add_light(n, cc, false, finite_attn, wpos.xyz, nrm, lacc_i, unused_a);
                }
            }
            lacc_i = clamp(lacc_i, int3(0,0,0), int3(255,255,255));
            result.rgb = float3((mat_c_i * (lacc_i + (lacc_i >> 7))) >> 8) / 255.0;
        } else {
            result.rgb = float3(mat_c_i) / 255.0;
        }

        int mat_a_i = int(round(((ca & 1u) != 0u ? vtx.a : chan_material[ch].a) * 255.0));
        if (((ca >> 1u) & 1u) != 0u) {
            int lacc_a_i = int(round((((ca >> 6u) & 1u) != 0u ? vtx.a : chan_ambient[ch].a) * 255.0));
            int3 unused_c = int3(0,0,0);
            const uint mask = ((ca >> 2u) & 0xFu) | (((ca >> 11u) & 0xFu) << 4u);
            [loop] for (uint n = 0u; n < 8u; ++n) {
                if (((mask >> n) & 1u) != 0u) {
                    add_light(n, ca, true, finite_attn, wpos.xyz, nrm, unused_c, lacc_a_i);
                }
            }
            lacc_a_i = clamp(lacc_a_i, 0, 255);
            result.a = float((mat_a_i * (lacc_a_i + (lacc_a_i >> 7))) >> 8) / 255.0;
        } else {
            result.a = float(mat_a_i) / 255.0;
        }
        chan_out[ch] = result;
    }
    o.col0 = chan_out[0];
    o.col1 = chan_out[1];

    // The texture coordinates are read and written by switch, never by a dynamic array index, as in Dolphin's uber
    // vertex shader: the Adreno 840's compiler (512.842) fails on dynamically indexed coordinate arrays here.
    float3 uv0 = float3(0.0, 0.0, 0.0);
    float3 uv1 = float3(0.0, 0.0, 0.0);
    float3 uv2 = float3(0.0, 0.0, 0.0);
    float3 uv3 = float3(0.0, 0.0, 0.0);
    float3 uv4 = float3(0.0, 0.0, 0.0);
    float3 uv5 = float3(0.0, 0.0, 0.0);
    float3 uv6 = float3(0.0, 0.0, 0.0);
    float3 uv7 = float3(0.0, 0.0, 0.0);

    [loop] for (uint i = 0u; i < key_texgens && i < 8u; ++i) {
        const uint tg = key_texgen(i);
        const uint type = (tg >> 4u) & 7u;
        float3 res;
        if (type == 2u) {
            res = float3(o.col0.rg, 1.0);
        } else if (type == 3u) {
            res = float3(o.col1.rg, 1.0);
        } else if (type == 1u) {
            uint emboss_base = 0x400u + 3u * (pn_row & 31u);
            float3x3 emboss_matrix = float3x3(
                xfw(emboss_base), xfw(emboss_base+1u), xfw(emboss_base+2u),
                xfw(emboss_base+3u), xfw(emboss_base+4u), xfw(emboss_base+5u),
                xfw(emboss_base+6u), xfw(emboss_base+7u), xfw(emboss_base+8u));
            float3 emboss_tangent = mul(emboss_matrix, v.tangent);
            float3 emboss_binormal = mul(emboss_matrix, v.binormal);
            const uint light = 0x180u + 4u * ((tg >> 15u) & 7u);
            float4 emboss_l2 = mtx_palette[mtx_palette_base + light + 2u];
            float4 emboss_l3 = mtx_palette[mtx_palette_base + light + 3u];
            float3 emboss_delta = float3(emboss_l2.zw, emboss_l3.x) - wpos.xyz;
            float emboss_dist2 = dot(emboss_delta, emboss_delta);
            float3 emboss_ldir = (emboss_dist2 > 0.0) ? emboss_delta * rsqrt(emboss_dist2) : float3(0,0,0);
            float3 emboss_source;
            [branch] switch ((tg >> 12u) & 7u) {
            case 0u: emboss_source = uv0; break;
            case 1u: emboss_source = uv1; break;
            case 2u: emboss_source = uv2; break;
            case 3u: emboss_source = uv3; break;
            case 4u: emboss_source = uv4; break;
            case 5u: emboss_source = uv5; break;
            case 6u: emboss_source = uv6; break;
            default: emboss_source = uv7; break;
            }
            res = emboss_source + float3(dot(emboss_ldir, emboss_tangent), dot(emboss_ldir, emboss_binormal), 0.0);
        } else {
            const uint source_row = (tg >> 7u) & 31u;
            float4 coord = float4(0.0, 0.0, 1.0, 1.0);
            if (source_row == 0u) {
                coord.xyz = v.pos;
            } else if (source_row == 1u) {
                coord.xyz = v.nrm;
            } else if (source_row == 3u) {
                coord.xyz = v.tangent;
            } else if (source_row == 4u) {
                coord.xyz = v.binormal;
            } else if (source_row >= 5u && source_row <= 12u) {
                float2 raw;
                [branch] switch (source_row - 5u) {
                case 0u: raw = v.uv[0]; break;
                case 1u: raw = v.uv[1]; break;
                case 2u: raw = v.uv[2]; break;
                case 3u: raw = v.uv[3]; break;
                case 4u: raw = v.uv[4]; break;
                case 5u: raw = v.uv[5]; break;
                case 6u: raw = v.uv[6]; break;
                default: raw = v.uv[7]; break;
                }
                coord = float4(raw, 1.0, 1.0);
            }
            if (((tg >> 2u) & 1u) == 0u) {
                coord.z = 1.0;
            }
            const bool stq = ((tg >> 1u) & 1u) != 0u;
            if (((key_texmtx_mask >> i) & 1u) != 0u) {
                uint packed;
                uint shift;
                if (i < 3u) {
                    packed = v.mtx_idx.x;
                    shift = 8u + 8u * i;
                } else if (i < 7u) {
                    packed = v.mtx_idx.y;
                    shift = 8u * (i - 3u);
                } else {
                    packed = v.mtx_idx.z;
                    shift = 0u;
                }
                uint tm = min((packed >> shift) & 0xFFu, 61u) + mtx_palette_base;
                res = float3(
                    dot(coord, mtx_palette[tm+0u]),
                    dot(coord, mtx_palette[tm+1u]),
                    stq ? dot(coord, mtx_palette[tm+2u]) : 1.0);
            } else {
                const uint inline_base = i * 3u;
                res = float3(
                    dot(coord, inline_tex_matrices[inline_base]),
                    dot(coord, inline_tex_matrices[inline_base + 1u]),
                    stq ? dot(coord, inline_tex_matrices[inline_base + 2u]) : 1.0);
            }
            if ((key_flags & 0x04u) != 0u) {
                const uint pt = key_post_texgen(i);
                if (((pt >> 8u) & 1u) != 0u) {
                    res = (dot(res, res) > 0.0) ? normalize(res) : res;
                }
                const uint post_base = i * 3u;
                float4 P0 = inline_post_matrices[post_base];
                float4 P1 = inline_post_matrices[post_base + 1u];
                float4 P2 = inline_post_matrices[post_base + 2u];
                res = float3(dot(P0.xyz, res) + P0.w,
                             dot(P1.xyz, res) + P1.w,
                             dot(P2.xyz, res) + P2.w);
            }
            if (res.z == 0.0) res.xy = clamp(res.xy * 0.5, -1.0, 1.0);
        }
        [branch] switch (i) {
        case 0u: uv0 = res; break;
        case 1u: uv1 = res; break;
        case 2u: uv2 = res; break;
        case 3u: uv3 = res; break;
        case 4u: uv4 = res; break;
        case 5u: uv5 = res; break;
        case 6u: uv6 = res; break;
        default: uv7 = res; break;
        }
    }
    o.uv[0] = uv0;
    o.uv[1] = uv1;
    o.uv[2] = uv2;
    o.uv[3] = uv3;
    o.uv[4] = uv4;
    o.uv[5] = uv5;
    o.uv[6] = uv6;
    o.uv[7] = uv7;
    return o;
}
