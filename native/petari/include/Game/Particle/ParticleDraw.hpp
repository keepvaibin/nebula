#pragma once

#include <NebulaGpuTypes.h>

class ParticleCore;
struct EmitterWork;
struct SiteMaterial;

// An emitter's particles as draws (ParticleDraw.cpp). Quad shapes are instances (NebulaParticleInstance): a particle's
// matrix now and a game frame ago, its coordinates' rows and its colours; the vertex stage builds the corners from the
// shape's quad (runtime/src/render/materials/particle_instances.hlsl). Points, lines and stripes are corners in the world.
// A run ends at a new image, size or material.
namespace ParticleDraw {
    // The camera the emitters that follow are seen through.
    void camera(const f32 (*pMtx)[4]);
    // A new material: `extras`, its corners carry coordinates 1 and 2 (a second map, an indirect map, a projection).
    void begin(bool extras);
    void flush();
    // The name the drawn effect's items carry.
    void setSource(u32 source);
    // What the corners that follow are drawn as: a strip stands alone; others join the same kind.
    void primitive(u8 primitive);
    // The particle being drawn: where it was a frame ago comes from it.
    void particle(ParticleCore* pParticle);
    // Corner `index` (0-3) of the particle, given in the world.
    void corner(u32 index, f32 x, f32 y, f32 z, f32 s, f32 t);
    // The particle's matrix to eye space (its quad is in the shape's own units).
    void setMatrix(const f32 (*pMtx)[4]);
    // Its texture coordinates: the corner's own, two rows over them, or a projection of its place.
    void clearCoord();
    void setCoordRows(const f32 (*pMtx)[4]);
    void setProjection(const f32 (*pMtx)[4]);
    // The particle as an instance of the material's quad; `crossed`, the shape's second quad too.
    void quad(bool crossed);
    // The image in the shape's place: a new one ends the run.
    void image(EmitterWork* pWork, s32 image);
}  // namespace ParticleDraw

// The particle material (runtime/src/render/materials/particle.hlsl) and the colours the emitter and particle functions
// set: prm and env, which travel on the particles and carry over from one emitter to the next.
namespace ParticleMaterial {
    const SiteMaterial& material();
    // In lines of a 456-line picture; the texture spreads by a texel across the width.
    void lineWidth(f32 pixels);
    void pointSize(f32 pixels);
    // The texture in the particle material's slot: 0 the shape, 1 a child's, 2 the indirect map, 3 the second.
    void texture(u32 slot, const SiteTexture* pTexture);
    // The colours as R << 24 | G << 16 | B << 8 | A.
    u32 prmWord();
    u32 envWord();
    void prm(const NebulaColor8& rColor);
    void env(const NebulaColor8& rColor);
    void prmEnv(const NebulaColor8& rPrm, const NebulaColor8& rEnv);
}  // namespace ParticleMaterial
