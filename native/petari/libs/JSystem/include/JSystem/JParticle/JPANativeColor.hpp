#pragma once

#include <revolution/gx/GXStruct.h>

// The particle material's colours (runtime/src/render/materials/particle.hlsl: prm in its register 0 slot, env in its
// register 1 slot). The emitter and per-particle colour functions set them; the material in use takes them at once
// (a constants-only resend) and the next emitter's material starts from them, as the colours carried over on GX.
namespace JPANativeColor {
    void prm(const GXColor& rColor);
    void env(const GXColor& rColor);
    void prmEnv(const GXColor& rPrm, const GXColor& rEnv);
}  // namespace JPANativeColor
