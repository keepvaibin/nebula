// SPDX-License-Identifier: GPL-3.0-only
// The font material's vertex stage for instanced glyphs (NEBULA_ITEM_INSTANCES, CharWriter's runs): each instance
// is a glyph (NebulaGlyphInstance, three rows from the instance table's z), its six vertices the quad's two triangles
// (corners 0, 1, 2 and 0, 2, 3: top left, top right, bottom right, bottom left). A corner's place in the draw matrix's
// plane, its sheet coordinate and its colour come from the glyph's rows. The pixel stage is font_text.hlsl's.
#define NATIVE_OWN_VERTEX 1
#include "font_text.hlsl"

struct GlyphVertexIn {
    uint vertex : SV_VertexID;
    uint instance : SV_InstanceID;
};

NativeVaryings native_vs(GlyphVertexIn input) {
    const uint at = instances[draw_first_instance].z + input.instance * 3u;
    const uint corner = input.vertex < 3u ? input.vertex : (input.vertex == 3u ? 0u : input.vertex - 2u);
    const bool right = corner == 1u || corner == 2u;
    const bool bottom = corner >= 2u;
    const float4 box = rows[at];
    const float4 st = rows[at + 1u];
    const uint4 words = asuint(rows[at + 2u]);
    // Selects, not an indexed vector: some drivers put an indexed one in private memory.
    const uint word = corner == 0u ? words.x : (corner == 1u ? words.y : (corner == 2u ? words.z : words.w));
    const float3 pos = float3(right ? box.z : box.x, bottom ? box.w : box.y, 0.0);
    const Place place = native_place_moved(pos, pos, 0u, 0u);
    NativeVaryings o = (NativeVaryings)0;
    PLACE_OUT(o, place);
    o.color = float4(uint4(word >> 24u, word >> 16u, word >> 8u, word) & 255u) / 255.0;
    o.uv[0] = float4(right ? st.z : st.x, bottom ? st.w : st.y, 1.0, 0.0);
    return o;
}
