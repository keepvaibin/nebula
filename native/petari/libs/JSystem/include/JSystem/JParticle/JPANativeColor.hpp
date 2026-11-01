#pragma once

#include <revolution/gx/GXStruct.h>

// The particle material's colours (runtime/src/render/materials/particle.hlsl: prm in its register 0 slot, env in its
// register 1 slot). The emitter and per-particle colour functions set them; the material in use takes them at once
// (a constants-only resend) and the next emitter's material starts from them, as the colours carried over on GX.
namespace JPANativeColor {
    void prm(const NebulaColor8& rColor);
    void env(const NebulaColor8& rColor);
    void prmEnv(const NebulaColor8& rPrm, const NebulaColor8& rEnv);
}  // namespace JPANativeColor
