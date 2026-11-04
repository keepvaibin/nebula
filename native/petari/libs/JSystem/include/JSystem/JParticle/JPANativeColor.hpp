#pragma once

#include <NebulaGpuTypes.h>

// The particle material's colours (runtime/src/render/materials/particle.hlsl: prm in its register 0 slot, env in its
// register 1 slot). The emitter and per-particle colour functions set them; the material in use takes them at once
// (a constants-only resend) and the next emitter's material starts from them, as colours carry over.
// An emitter's particles as one draw (JPABaseShape.cpp): begin at its material, flush when it ends.
class JPABaseParticle;

namespace JPAParticleBatch {
    // The camera the emitters that follow are seen through.
    void camera(const f32 (*pMtx)[4]);
    // `extras`: the shape reads coordinates 1 and 2 (a second map, an indirect map, a projection).
    void begin(bool extras);
    void flush();
    // What the vertices that follow are drawn as: a strip stands alone; others join the same kind.
    void primitive(u8 primitive);
    // The particle being drawn: where it was a frame ago comes from it.
    void particle(JPABaseParticle* pParticle);
    // Corner `index` (0-3) of the particle, given in the world.
    void corner(u32 index, f32 x, f32 y, f32 z, f32 s, f32 t);
    // The particle's matrix to eye space (its corners are in the shape's own units), and its coordinates' projection.
    void setMatrix(const f32 (*pMtx)[4]);
    void setProjection(const f32 (*pMtx)[4]);
}  // namespace JPAParticleBatch

namespace JPANativeColor {
    // The texture in the particle material's slot: 0 the shape, 1 a child's, 2 the indirect map, 3 the second.
    void texture(u32 slot, const J3DNativeTexObj* pTexture);
    // The colours as R << 24 | G << 16 | B << 8 | A: they travel on the particle's vertices.
    u32 prmWord();
    u32 envWord();
    void prm(const NebulaColor8& rColor);
    void env(const NebulaColor8& rColor);
    void prmEnv(const NebulaColor8& rPrm, const NebulaColor8& rEnv);
}  // namespace JPANativeColor
