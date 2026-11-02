#pragma once

#include <NebulaGpu.h>

// What a Mario model's materials take for one draw: an opacity, its lighting or a blur ghost's colour.
struct MarioModelTint {
    enum Kind {
        Kind_None,
        Kind_Alpha,         // opacity in colour 2, blended, back faces culled
        Kind_ConstantAlpha, // the picture's alpha takes `alpha`
        Kind_ReflectAlpha,  // opacity in colour 2, the reflection's program
        Kind_Light,         // ambient and material colours, and fog when `hasFog`
        Kind_Ghost,         // the ghost's colour in colour 0
    };

    MarioModelTint() : kind(Kind_None), alpha(0), hasFog(false), fogStart(0.0f), fogEnd(0.0f), fogNear(0.0f), fogFar(0.0f) {
        color0.r = color0.g = color0.b = color0.a = 0;
        color1 = color0;
        fogColor = color0;
    }

    void send() const;

    u8 kind;
    u8 alpha;
    bool hasFog;
    NebulaColor8 color0;
    NebulaColor8 color1;
    NebulaColor8 fogColor;
    f32 fogStart;
    f32 fogEnd;
    f32 fogNear;
    f32 fogFar;
};
