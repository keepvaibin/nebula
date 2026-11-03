#include "JSystem/J3DGraphBase/J3DShapeDraw.hpp"
#include "JSystem/J3DGraphBase/J3DNativeShape.hpp"

J3DShapeDraw::J3DShapeDraw(J3DShapeMesh* pMesh) {
    mMesh = pMesh;
}

void J3DShapeDraw::draw() const {
    J3DNativeShape::draw(this);
}

J3DShapeDraw::~J3DShapeDraw() {
}
