#include "JSystem/J3DGraphBase/J3DNativeState.hpp"
#include "JSystem/J3DGraphBase/J3DNativeMaterial.hpp"
#include "JSystem/J3DGraphBase/J3DNativeDraw.hpp"
#include "JSystem/J3DGraphBase/J3DNativeShape.hpp"
#include "Game/Util/RenderSignals.hpp"
#include <string.h>

extern "C" const NebulaHostServices* nebula_host(void);

namespace {
    const u32 kMaxVertices = 16384;

    // The values of the primitive being sent.
    f32 sPosition[kMaxVertices * 3];
    f32 sNormal[kMaxVertices * 3];
    u32 sColor[2][kMaxVertices];
    f32 sTexCoord[8][kMaxVertices * 2];
    u8 sMatrix[kMaxVertices];

    u8 sPrimitive = 0;
    u32 sAttributes = 0;
    u32 sCount = 0;
    u32 sVertex = 0;
    // Finished quads waiting at the front of the arrays; the primitive being sent follows them.
    u32 sWaiting = 0;
    NebulaPassState sWaitingPass;
    // The vertex's next value: 0 matrix, 1 position, 2 normal, 3-4 colours, 5-12 texture coordinates, 13 done.
    u32 sNext = 0;

    bool wants(u32 step) {
        switch (step) {
        case 0:
            return (sAttributes & NEBULA_SHAPE_POSITION_MATRIX) != 0;
        case 1:
            return true;
        case 2:
            return (sAttributes & NEBULA_SHAPE_NORMAL) != 0;
        case 3:
            return (sAttributes & NEBULA_SHAPE_COLOR0) != 0;
        case 4:
            return (sAttributes & NEBULA_SHAPE_COLOR1) != 0;
        default:
            return step < 13 && (sAttributes & (NEBULA_SHAPE_TEXCOORD0 << (step - 5))) != 0;
        }
    }

    void fail(const char* pWhat) {
        NEBULA_PANIC("J3DNativeDraw: %s", pWhat);
    }

    void sendVertices(u32 count) {
        J3DNativeShape::flushState();
        NebulaImmediate draw = {};
        draw.primitive = sPrimitive;
        draw.attributes = sAttributes;
        draw.count = count;
        draw.position = sPosition;
        draw.normal = (sAttributes & NEBULA_SHAPE_NORMAL) != 0 ? sNormal : nullptr;
        draw.color[0] = (sAttributes & NEBULA_SHAPE_COLOR0) != 0 ? sColor[0] : nullptr;
        draw.color[1] = (sAttributes & NEBULA_SHAPE_COLOR1) != 0 ? sColor[1] : nullptr;
        for (u32 t = 0; t < 8; t++) {
            draw.texcoord[t] = (sAttributes & (NEBULA_SHAPE_TEXCOORD0 << t)) != 0 ? sTexCoord[t] : nullptr;
        }
        draw.position_matrix = (sAttributes & NEBULA_SHAPE_POSITION_MATRIX) != 0 ? sMatrix : nullptr;
        u32 places = 0;
        if (draw.position_matrix != nullptr) {
            for (u32 v = 0; v < count; v++) {
                places |= 1u << sMatrix[v];
            }
        }
        draw.matrix_count = J3DNativeShape::positionMatrices(places, &draw.matrices);
        draw.pass = *J3DNativeState::pass();
        const NebulaHostServices* pHost = nebula_host();
        pHost->immediate(pHost->user, &draw);
    }

    // Moves past values the vertex does not have; a complete vertex starts the next (the last sends the primitive).
    void advance() {
        while (sNext < 13 && !wants(sNext)) {
            sNext++;
        }
        if (sNext < 13) {
            return;
        }
        sNext = 0;
        if (++sVertex == sCount) {
            if (sPrimitive == NEBULA_PRIMITIVE_QUADS) {
                sWaiting += sCount;
                sWaitingPass = *J3DNativeState::pass();
                sVertex = 0;
            } else {
                sendVertices(sCount);
            }
            sCount = 0;
            return;
        }
        while (!wants(sNext)) {
            sNext++;
        }
    }

    // The value at `step` arrives: it must be the vertex's next.
    void arrive(u32 step) {
        if (sVertex >= sCount || sNext != step) {
            fail("a vertex value outside its primitive or out of order");
        }
        sNext++;
        advance();
    }
}  // namespace

namespace J3DNativeDraw {
    void drawArrays(u8 primitive, u32 attributes, u32 count, const f32* pPositions, const u32* const pColors[2],
                    const f32* const pTexCoords[8]) {
        J3DNativeShape::flushState();
        NebulaImmediate draw = {};
        draw.primitive = primitive;
        draw.attributes = attributes;
        draw.count = count;
        draw.position = pPositions;
        draw.color[0] = pColors[0];
        draw.color[1] = pColors[1];
        for (u32 t = 0; t < 8; t++) {
            draw.texcoord[t] = pTexCoords[t];
        }
        draw.matrix_count = J3DNativeShape::positionMatrices(0, &draw.matrices);
        draw.pass = *J3DNativeState::pass();
        const NebulaHostServices* pHost = nebula_host();
        pHost->immediate(pHost->user, &draw);
    }

    void flush() {
        if (sWaiting != 0) {
            const u32 count = sWaiting;
            sWaiting = 0;
            sendVertices(count);
        }
    }

    // Waiting quads end at a primitive of another kind or other values.
    bool primitive_changed(u32 attributes) {
        return sPrimitive != NEBULA_PRIMITIVE_QUADS || attributes != sAttributes;
    }

    void begin(NebulaPrimitive primitive, u16 count, u32 attributes) {
        if (sVertex < sCount) {
            fail("a primitive begun before the last one's vertices were all given");
        }
        if (count > kMaxVertices) {
            fail("more vertices in one primitive than kMaxVertices");
        }
        sPrimitive = primitive;
        if (sWaiting != 0 && (primitive_changed(attributes) || sWaiting + count > kMaxVertices ||
                              memcmp(&sWaitingPass, J3DNativeState::pass(), sizeof(sWaitingPass)) != 0)) {
            const u8 next = sPrimitive;
            sPrimitive = NEBULA_PRIMITIVE_QUADS;
            flush();
            sPrimitive = next;
        }
        sAttributes = attributes;
        sCount = count;
        sVertex = 0;
        sNext = 0;
        if (count == 0) {
            return;
        }
        while (!wants(sNext)) {
            sNext++;
        }
    }

    void position(f32 x, f32 y, f32 z) {
        f32* p = sPosition + 3 * (sWaiting + sVertex);
        p[0] = x;
        p[1] = y;
        p[2] = z;
        arrive(1);
    }

    void normal(f32 x, f32 y, f32 z) {
        f32* p = sNormal + 3 * (sWaiting + sVertex);
        p[0] = x;
        p[1] = y;
        p[2] = z;
        arrive(2);
    }

    void color(u32 rgba) {
        // Colour 0 then colour 1, as the vertex has them.
        const u32 step = sNext;
        if (step != 3 && step != 4) {
            fail("a colour where the vertex has none next");
            return;
        }
        sColor[step - 3][sWaiting + sVertex] = rgba;
        arrive(step);
    }

    void color(u8 r, u8 g, u8 b, u8 a) {
        color(static_cast< u32 >(r) << 24 | static_cast< u32 >(g) << 16 | static_cast< u32 >(b) << 8 | a);
    }

    void texCoord(f32 s, f32 t) {
        const u32 step = sNext;
        if (step < 5 || step > 12) {
            fail("a texture coordinate where the vertex has none next");
            return;
        }
        f32* p = sTexCoord[step - 5] + 2 * (sWaiting + sVertex);
        p[0] = s;
        p[1] = t;
        arrive(step);
    }
};  // namespace J3DNativeDraw

void J3DNativeDrawBegin(NebulaPrimitive primitive, u16 count, u32 attributes) {
    J3DNativeDraw::begin(primitive, count, attributes);
}

void J3DNativeDrawPosition(f32 x, f32 y, f32 z) {
    J3DNativeDraw::position(x, y, z);
}

void J3DNativeDrawNormal(f32 x, f32 y, f32 z) {
    J3DNativeDraw::normal(x, y, z);
}

void J3DNativeDrawColor(u32 rgba) {
    J3DNativeDraw::color(rgba);
}

void J3DNativeDrawTexCoord(f32 s, f32 t) {
    J3DNativeDraw::texCoord(s, t);
}
