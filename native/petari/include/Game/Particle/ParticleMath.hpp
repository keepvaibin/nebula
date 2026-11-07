#pragma once

#include <nebula_geometry.hpp>

namespace ParticleMath {
    void dirMtx(Vec3f const& param_0, f32 (*param_1)[4]);
    void yzRotateMtx(s16 angleY, s16 angleZ, f32 (*param_2)[4]);
    void xyzRotateMtx(s16 x, s16 y, s16 z, Mtx dst);
    void splitR(f32 const (*param_0)[4], f32 (*param_1)[4]);
    void splitRT(f32 const (*param_0)[4], f32 (*param_1)[4], Vec3f* param_2);
    // The keys as the resource has them.
    f32 keyValue(f32 param_0, u32 param_1, f32 const* param_2);
    void splitSRT(f32 const (*param_0)[4], f32 (*param_1)[4], Vec3f* param_2, Vec3f* param_3);
}  // namespace ParticleMath
