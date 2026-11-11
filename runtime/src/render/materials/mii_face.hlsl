// SPDX-License-Identifier: GPL-3.0-only
// Mii heads (Game/Mii/MiiHead.cpp Head::drawOpa / drawXlu, and the stages the game adds for its Mii NPCs:
// Game/NPC/MiiFacePartsHolder.cpp draw). Stage 0 is the part's own form; the chain after it is Mii's
// lit stage or the NPC holder's two stages, in the same form. Texture 0 at the part's own coordinates.
//   form (params.x): 0 the constant colour (beard, nose, forehead, hair); 1 the cap, (K + K tex) / 2; 2 the face
//     texture over the faceline colour, lerp(K, tex.rab, tex.g) (swap table N + 1: R, A, B, G); 3 the mask, tex;
//     4 the nose line, black; 5 the glasses, K tex. Forms 0-2 are opaque (alpha 1: the constant's 8/8), 3-5 take the
//     texture's alpha.
//   chain (params.y): 0 none; 1 Mii's light, colour times the lit channel; 2 the holder's opaque stages; 3 the holder's
//     translucent stages.
//   The lit channel (chains 1-3): the material colour times the ambient plus lights (lights.x the colour channel's
//     mask, lights.y its diffuse function, lights.z its attenuation form; chain 2 also the alpha channel, light 2,
//     signed diffuse).
//   The alpha test (params.z): 0 above 0, 1 not 255.
struct MaterialConstants {
    float4 kcolor;     // the part's constant colour (rgba, 0-1)
    float4 params;     // x form, y chain, z the alpha test
    float4 mat_color;  // the lit channel's material colour
    float4 amb_color;  // and its ambient colour
    float4 lights;     // x the colour channel's light mask, y its diffuse function, z its attenuation form
};

#define NATIVE_COORDS 1
#define NATIVE_LIT 1
#include "native_material.hlsli"

// The lit channel's colour at a place: the lights the colour mask names over the ambient.
float3 mii_light(float3 eye, float3 nrm) {
    const uint mask = uint(material.lights.x);
    const uint diffuse = uint(material.lights.y);
    const uint atten = uint(material.lights.z);
    float3 sum = float3(0.0, 0.0, 0.0);
    for (uint n = 0u; n < min(native_light_count(), 24u); ++n) {
        if ((mask & (1u << n)) != 0u) {
            sum += channel_light(n, atten, diffuse, eye, nrm).rgb;
        }
    }
    return material.mat_color.rgb * saturate((AMBIENT_TERM(material.amb_color.rgb) * 255.0 + sum) / 255.0);
}

#if NATIVE_TRACED
bool material_relight(inout NativeVaryings i, float3 nrm) {
    if (uint(material.params.y) == 0u) {
        return false;  // no lit chain: the part's constant colour, painted
    }
    i.color.rgb = mii_light(i.lit_eye, nrm);
    return true;
}
#endif

void material_vertex(NativeVertex v, inout NativeVaryings o) {
    o.uv[0] = float4(v.uv[0], 1.0, 0.0);
    o.color = float4(1.0, 1.0, 1.0, 1.0);
    const uint chain = uint(material.params.y);
    if (chain == 0u) {
        return;
    }
    const float3 nrm = native_lit_normal(v);
    o.color.rgb = mii_light(v.eye, nrm);
    if (chain == 2u) {
        o.color.a = material.mat_color.a * saturate((material.amb_color.a * 255.0 + channel_light(2u, 2u, 1u, v.eye, nrm).a) / 255.0);
    }
}

float4 material_color(NativeVaryings i) {
    const uint form = uint(material.params.x);
    const float4 t = TEX(0).Sample(SAMP(0), i.uv[0].xy);
    const float3 k = material.kcolor.rgb;
    float3 c;
    float a;
    if (form == 0u) {
        c = k;
        a = 1.0;
    } else if (form == 1u) {
        c = saturate((k + k * t.rgb) * 0.5);
        a = 1.0;
    } else if (form == 2u) {
        c = saturate(lerp(k, float3(t.r, t.a, t.b), t.g));
        a = 1.0;
    } else if (form == 3u) {
        c = t.rgb;
        a = t.a;
    } else if (form == 4u) {
        c = float3(0.0, 0.0, 0.0);
        a = t.a;
    } else {
        c = saturate(k * t.rgb);
        a = t.a;
    }
    const uint chain = uint(material.params.y);
    if (chain == 1u) {
        c = saturate(c * i.color.rgb);
    } else if (chain == 2u) {
        // the game's stage 1: colour C0 + lerp(C1, PREV, RASC) with C0 and C1 black; alpha A0 + lerp(A1, 1/4, RASA) with
        // A0 -89 and A1 246. Stage 2: colour (PREV + RASC APREV) x2; alpha the constant 1/4 x2.
        c = saturate(c * i.color.rgb);
        const float a1 = saturate(-89.0 / 255.0 + lerp(246.0 / 255.0, 64.0 / 255.0, i.color.a));
        c = saturate((c + i.color.rgb * a1) * 2.0);
        a = 128.0 / 255.0;
    } else if (chain == 3u) {
        c = saturate(saturate(c * i.color.rgb) * 2.0);
    }
    const float a8 = round(a * 255.0);
    if (material.params.z != 0.0 ? a8 == 255.0 : a8 <= 0.0) {
        discard;
    }
    return float4(c, a);
}
