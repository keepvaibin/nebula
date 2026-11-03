#include "JSystem/JParticle/JPAExTexShape.hpp"
#include "JSystem/JParticle/JPAEmitter.hpp"
#include "JSystem/JParticle/JPAResourceManager.hpp"

// The indirect map (texture map 2) and the second texture (map 3). Their coordinates are the particle's own texture
// coordinates, which the native particle shader reads directly (runtime/src/render/materials/particle.hlsl: uv[1]).
void JPALoadExTex(JPAEmitterWorkData* work) {
    JPAExTexShape* ets = work->mpRes->getEts();

    if (ets->isUseIndirect()) {
        u8 texIdx = ets->getIndTexIdx();
        work->mpResMgr->use(work->mpRes->getTexIdx(texIdx), 2);
    }

    if (ets->isUseSecTex()) {
        u8 texIdx = ets->getSecTexIdx();
        work->mpResMgr->use(work->mpRes->getTexIdx(texIdx), 3);
    }
}

JPAExTexShape::JPAExTexShape(u8 const* data) {
    mpData = (const JPAExTexShapeData*)data;
}
