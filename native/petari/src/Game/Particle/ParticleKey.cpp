#include "Game/Particle/ParticleKey.hpp"
#include "Game/Particle/ParticleMath.hpp"

f32 ParticleKey::calc(f32 frame) const {
    const u32 count = mpData->mKeyCount;
    const f32* pKeys = &mpData->mKeys[0][0];
    if (mpData->mLoop != 0) {
        int length = (int)pKeys[(count - 1) * 4] + 1;
        int loops = length != 0 ? (int)frame / length : (int)frame < 0 ? -1 : 0;
        frame = frame - (loops * length);
    }
    return ParticleMath::keyValue(frame, count, pKeys);
}
