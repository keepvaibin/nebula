#pragma once

#ifdef TARGET_PC
class J3DModelData;

// Nebula: a loaded model's triangles for the renderer's curved surfaces (Phong tessellation rounds the outlines a
// 456-line picture never showed: galaxy/gx/curved_surfaces.h). Its shapes' display lists give each triangle's
// position and normal indices; its position and normal arrays are decoded as GX reads them.
namespace NebulaCurvedSurfaces {
    void registerModel(J3DModelData* pModelData);
};  // namespace NebulaCurvedSurfaces
#endif
