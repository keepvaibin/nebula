#pragma once

#ifdef TARGET_PC
class J3DModelData;

// Nebula: a model's shape lists for the renderer to build ahead of their first draw (galaxy/gx/native_shapes.h), once
// the model is final (ResourceHolder, after ModelUtil's edits). Each list goes with its handle, its shape's vertex
// description list, the arrays J3DShape::loadVtxArray sets before each draw and the shape's current matrix.
namespace NebulaNativeShapes {
    void registerModel(J3DModelData* pModelData);
};  // namespace NebulaNativeShapes
#endif
