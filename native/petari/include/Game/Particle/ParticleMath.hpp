#pragma once

#include <nebula_geometry.hpp>

namespace ParticleMath {
    void dirMtx(const Vec3f& rDir, Mtx dst);
    void yzRotateMtx(s16 angleY, s16 angleZ, Mtx dst);
    void xyzRotateMtx(s16 x, s16 y, s16 z, Mtx dst);
    void splitR(const Mtx src, Mtx rotation);
    void splitRT(const Mtx src, Mtx rotation, Vec3f* pTranslation);
    // The keys as the resource has them.
    f32 keyValue(f32 time, u32 count, const f32* pKeys);
    void splitSRT(const Mtx src, Mtx rotation, Vec3f* pScale, Vec3f* pTranslation);
}  // namespace ParticleMath
