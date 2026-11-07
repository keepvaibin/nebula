#include "Game/Particle/ExTexShape.hpp"
#include "Game/Particle/EmitterCore.hpp"
#include "Game/Particle/ParticleResources.hpp"

// The indirect map (texture map 2) and the second texture (map 3). Their coordinates are the particle's own texture
// coordinates, which the native particle shader reads directly (runtime/src/render/materials/particle.hlsl: uv[1]).
void ParticleLoadExTex(EmitterWork* work) {
    ExTexShape* ets = work->mpRes->getEts();

    if (ets->isUseIndirect()) {
        u8 texIdx = ets->getIndTexIdx();
        work->mpResMgr->use(work->mpRes->getTexIdx(texIdx), 2);
    }

    if (ets->isUseSecTex()) {
        u8 texIdx = ets->getSecTexIdx();
        work->mpResMgr->use(work->mpRes->getTexIdx(texIdx), 3);
    }
}
