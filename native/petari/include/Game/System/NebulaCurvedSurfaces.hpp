#pragma once

class J3DModelData;

// Nebula: a loaded model's triangles for the renderer's curved surfaces (Phong tessellation rounds the outlines a
// older picture never showed: curved_surfaces.h). Its shapes give each triangle's
// position and normal indices; its position and normal arrays are decoded.
namespace NebulaCurvedSurfaces {
    void registerModel(J3DModelData* pModelData);
};  // namespace NebulaCurvedSurfaces
